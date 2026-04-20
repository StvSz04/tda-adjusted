#include "Debug/Logger.hpp"
#include "TBAAParser.hpp"

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>

#include <algorithm>

using namespace llvm;
using namespace tda;

std::pair<std::unique_ptr<TransparentType>, std::unique_ptr<TransparentType>>
TBAAParser::getLoadStoreTypesFromTbaa(const Instruction* inst) {
  assert(isa<LoadInst>(inst) || isa<StoreInst>(inst));
  const MDNode* mdNode = inst->getMetadata(LLVMContext::MD_tbaa);
  if (!mdNode)
    return {nullptr, nullptr};
  auto* baseTypeMd = cast<MDNode>(mdNode->getOperand(0));
  auto* accessTypeMd = cast<MDNode>(mdNode->getOperand(1));
  unsigned accessOffset = mdconst::extract_or_null<ConstantInt>(mdNode->getOperand(2))->getZExtValue();
  if (!isStructTypeDescriptor(baseTypeMd))
    return {nullptr, nullptr};
  return getPlaceholderStructTypes(baseTypeMd, accessTypeMd, accessOffset);
}

std::pair<std::unique_ptr<TransparentType>, std::unique_ptr<TransparentType>>
TBAAParser::getPlaceholderStructTypes(const MDNode* structTypeMd, const MDNode* accessTypeMd, unsigned accessOffset) {
  unsigned numFields = structTypeMd->getNumOperands() / 3 - 1;
  std::unique_ptr<TransparentType> accessedType = nullptr;
  SmallVector<std::unique_ptr<TransparentType>> fieldTypes;
  SmallVector<unsigned> fieldOffsets;
  SmallVector<unsigned> fieldSizes;
  fieldTypes.reserve(numFields);
  bool foundAccess = false;
  for (unsigned i = 0; i < numFields; i++) {
    auto* fieldTypeMd = cast<MDNode>(structTypeMd->getOperand(3 + i * 3));
    unsigned fieldOffset = mdconst::extract<ConstantInt>(structTypeMd->getOperand(3 + i * 3 + 1))->getZExtValue();
    unsigned fieldSize = mdconst::extract<ConstantInt>(structTypeMd->getOperand(3 + i * 3 + 2))->getZExtValue();

    fieldOffsets.push_back(fieldOffset);
    fieldSizes.push_back(fieldSize);

    unsigned nextFieldOffset = 0;
    bool isLastField = i + 1 == numFields;
    if (!isLastField)
      nextFieldOffset = mdconst::extract<ConstantInt>(structTypeMd->getOperand(3 + (i + 1) * 3 + 1))->getZExtValue();
    foundAccess = (!foundAccess && isLastField) || (accessOffset >= fieldOffset && accessOffset < nextFieldOffset);

    if (isStructTypeDescriptor(fieldTypeMd)) {
      auto [fieldType, accessedTypeInField] =
        getPlaceholderStructTypes(fieldTypeMd, accessTypeMd, accessOffset - fieldOffset);
      if (foundAccess)
        accessedType = accessTypeMd == fieldTypeMd ? fieldType->clone() : std::move(accessedTypeInField);
      fieldTypes.push_back(std::move(fieldType));
    }
    else
      fieldTypes.push_back(TransparentTypeFactory::createFromType(nullptr, 0));
  }
  return {TransparentTypeFactory::createFromFields(fieldTypes, fieldOffsets, fieldSizes, 1), std::move(accessedType)};
}

std::unordered_map<StructType*, StructPaddingInfo> TBAAParser::getStructPaddingInfo(Module& module) {
  std::unordered_map<StructType*, StructPaddingInfo> structPaddingInfo;
  const DataLayout& dataLayout = module.getDataLayout();
  // TODO
  return structPaddingInfo;
}

bool TBAAParser::isStructTypeDescriptor(const MDNode* mdNode) { return mdNode->getNumOperands() >= 6; }
