#include "Debug/Logger.hpp"
#include "TDAInfo/TBAAParser.hpp"
#include "TypeDeductionAnalysis.hpp"

#include <llvm/IR/IntrinsicInst.h>

#define DEBUG_TYPE "tda"

using namespace llvm;
using namespace tda;

static bool isMetadata(const Value* value) {
  return value && (value->getType()->isMetadataTy() || isa<MetadataAsValue>(value));
}

static bool isToSkip(const Value* value) {
  if (isMetadata(value))
    return true;

  const auto* inst = dyn_cast<Instruction>(value);
  if (!inst)
    return false;

  if (isa<DbgInfoIntrinsic>(inst))
    return true;
  if (auto* call = dyn_cast<CallBase>(inst)) {
    if (const Function* calledFun = call->getCalledFunction()) {
      if (calledFun->isIntrinsic()) {
        switch (calledFun->getIntrinsicID()) {
        case Intrinsic::dbg_declare:
        case Intrinsic::dbg_value:
        case Intrinsic::dbg_label:
        case Intrinsic::var_annotation:
        case Intrinsic::ptr_annotation:
        case Intrinsic::lifetime_start:
        case Intrinsic::lifetime_end:   return true;
        default:                        break;
        }
      }
    }
  }
  return false;
}

static std::unordered_set<Intrinsic::ID> supportedIntrinsics = {
  Intrinsic::memcpy,
  Intrinsic::memmove,
  Intrinsic::memset,
};

void TypeDeductionAnalysis::deduceFromValue(Value* value) {
  if (isToSkip(value))
    return;

  currDeductionValue = value;

  if (auto* global = dyn_cast<GlobalVariable>(value))
    deduceFromGlobalVariable(global);
  else if (auto* alloca = dyn_cast<AllocaInst>(value))
    deduceFromAlloca(alloca);
  else if (auto* load = dyn_cast<LoadInst>(value))
    deduceFromLoadStore(load);
  else if (auto* store = dyn_cast<StoreInst>(value))
    deduceFromLoadStore(store);
  else if (auto* gep = dyn_cast<GetElementPtrInst>(value))
    deduceFromGep(gep);
  else if (auto* call = dyn_cast<CallBase>(value))
    deduceFromCall(call);
  else if (auto* fun = dyn_cast<Function>(value))
    deduceFromFunction(fun);
  else if (deducedTypes[value].empty())
    updateDeducedTypes(value, TransparentTypeFactory::createFromType(value->getType()));

  currDeductionValue = nullptr;
}

void TypeDeductionAnalysis::deduceFromGlobalVariable(GlobalVariable* globalVar) {
  std::unique_ptr<TransparentType> valueType = TransparentTypeFactory::createFromType(globalVar->getValueType(), 0);
  const TypeAliasSet globalVarTypes = getOrCreateDeducedTypes(globalVar);
  TypeAliasSet initializerTypes;

  Value* initializer = nullptr;
  if (globalVar->hasInitializer()) {
    initializer = globalVar->getInitializer();
    initializerTypes = getOrCreateDeducedTypes(initializer);
  }

  auto deduce = [&](const TransparentType* globalVarType, const TransparentType* initializerType) {
    if (initializer && !isa<Function>(initializer)) {
      updateDeducedTypes(initializer, valueType->clone());

      const TransparentType* globalVarPointedType = globalVarType->getPointedType();
      if (initializerType->isStructurallyEquivalent(globalVarPointedType))
        updateDeducedTypes(initializer, globalVarPointedType->clone());

      updateDeducedTypes(globalVar, valueType->getPointerToType());
      updateDeducedTypes(globalVar, initializerType->getPointerToType());
    }
    else
      updateDeducedTypes(globalVar, valueType->getPointerToType());
  };

  for (const auto& globalVarType : globalVarTypes) {
    if (!initializerTypes.empty())
      for (const auto& initializerType : initializerTypes)
        deduce(globalVarType.get(), initializerType.get());
    else
      deduce(globalVarType.get(), nullptr);
  }
}

void TypeDeductionAnalysis::deduceFromAlloca(AllocaInst* alloca) {
  std::unique_ptr<TransparentType> allocatedType =
    TransparentTypeFactory::createFromType(alloca->getAllocatedType(), 0);
  updateDeducedTypes(alloca, allocatedType->getPointerToType());
}

void TypeDeductionAnalysis::deduceFromLoadStore(Instruction* inst) {
  assert(isa<LoadInst>(inst) || isa<StoreInst>(inst));
  Value* valueOperand = nullptr;
  Value* ptrOperand = nullptr;
  if (auto* load = dyn_cast<LoadInst>(inst)) {
    valueOperand = load;
    ptrOperand = load->getPointerOperand();
  }
  else if (auto* store = dyn_cast<StoreInst>(inst)) {
    valueOperand = store->getValueOperand();
    ptrOperand = store->getPointerOperand();
  }

  const TypeAliasSet valueOperandTypes = getOrCreateDeducedTypes(valueOperand);
  const TypeAliasSet ptrOperandTypes = getOrCreateDeducedTypes(ptrOperand);

  auto iter = std::ranges::find_if(ptrOperandTypes, [](const auto& type) -> bool {
    const TransparentType* pointedType = type->getPointedType();
    return pointedType ? pointedType->isUnion() : false;
  });
  if (iter != ptrOperandTypes.end())
    // ptrOperand is a union
    return;

  if (isa<Function>(ptrOperand))
    return;

  auto deduce = [&](const TransparentType* valueOperandType, const TransparentType* ptrOperandType) {
    // Deduce from tbaa metadata, if present (only once)
    if (!tbaaUsedForInstruction.contains(inst)) {
      auto [ptrOperandTypeTbaa, valueOperandTypeTbaa] = TBAAParser::getLoadStoreTypesFromTbaa(inst);
      updateDeducedTypes(valueOperand, std::move(valueOperandTypeTbaa));
      updateDeducedTypes(ptrOperand, std::move(ptrOperandTypeTbaa));
      tbaaUsedForInstruction.insert(inst);
    }

    std::unique_ptr<TransparentType> indexedType = ptrOperandType->getIndexedType(valueOperandType);
    std::unique_ptr<TransparentType> mergedPtrOperandType = nullptr;
    if (indexedType)
      mergedPtrOperandType = ptrOperandType->cloneAndSetIndexedType(valueOperandType, valueOperandType);

    updateDeducedTypes(valueOperand, std::move(indexedType));
    updateDeducedTypes(ptrOperand, mergedPtrOperandType ? std::move(mergedPtrOperandType) : valueOperandType->getPointerToType());
  };

  for (const auto& valueOperandType : valueOperandTypes)
    for (const auto& ptrOperandType : ptrOperandTypes)
      deduce(valueOperandType.get(), ptrOperandType.get());
}

void TypeDeductionAnalysis::deduceFromGep(GetElementPtrInst* gep) {
  Value* ptrOperand = gep->getPointerOperand();
  const TypeAliasSet gepTypes = getOrCreateDeducedTypes(gep);
  const TypeAliasSet ptrOperandTypes = getOrCreateDeducedTypes(ptrOperand);

  auto iter = std::ranges::find_if(ptrOperandTypes, [](const auto& type) -> bool {
    const TransparentType* pointedType = type->getPointedType();
    return pointedType ? pointedType->isUnion() : false;
  });
  if (iter != ptrOperandTypes.end())
    // ptrOperand is a union
    return;

  std::unique_ptr<TransparentType> srcElType = TransparentTypeFactory::createFromType(gep->getSourceElementType());

  if (srcElType->isUnion()) {
    updateDeducedTypes(ptrOperand, std::move(srcElType));
    return;
  }

  std::unique_ptr<TransparentType> srcElPtrType = srcElType->getPointerToType();
  std::unique_ptr<TransparentType> indexedFromSrcElType = srcElPtrType->getIndexedType(srcElType.get(), gep->indices());
  if (indexedFromSrcElType)
    updateDeducedTypes(gep, indexedFromSrcElType->getPointerToType());

  auto deduce = [&](const TransparentType* gepType, const TransparentType* ptrOperandType) {
    std::unique_ptr<TransparentType> indexedFromPtrOpType =
      ptrOperandType->getIndexedType(srcElType.get(), gep->indices());
    if (indexedFromPtrOpType)
      updateDeducedTypes(gep, indexedFromPtrOpType->getPointerToType());

    std::unique_ptr<TransparentType> ptrOpTypeMergedWithGep =
      ptrOperandType->cloneAndSetIndexedType(gepType->getPointedType(), srcElType.get(), gep->indices());
    updateDeducedTypes(ptrOperand, std::move(ptrOpTypeMergedWithGep));

    updateDeducedTypes(ptrOperand, std::move(srcElPtrType));
  };

  for (const auto& gepType : gepTypes)
    for (const auto& ptrOperandType : ptrOperandTypes)
      deduce(gepType.get(), ptrOperandType.get());
}

void TypeDeductionAnalysis::deduceFromCall(CallBase* call) {
  Function* calledFun = call->getCalledFunction();
  if (!calledFun)
    return;

  if (calledFun->isIntrinsic() && supportedIntrinsics.contains(calledFun->getIntrinsicID())) {
    deduceFromSupportedIntrinsicCall(call);
    return;
  }

  if (calledFun->isDeclaration())
    return;

  mergeTypeAliasSets(call, calledFun);

  for (auto&& [callArg, funArg] : zip(call->args(), calledFun->args())) {
    if (isa<Function>(callArg))
      // FIXME: the deduced type of functions is their return type (different LLVM-14 type), causing incompatible merges
      continue;
    mergeTypeAliasSets(callArg, &funArg);
  }
}

void TypeDeductionAnalysis::deduceFromFunction(Function* function) {
  for (BasicBlock& bb : *function) {
    Instruction* termInst = bb.getTerminator();
    if (auto* returnInst = dyn_cast<ReturnInst>(termInst))
      if (Value* retValue = returnInst->getReturnValue())
        mergeTypeAliasSets(function, retValue);
  }
}

void TypeDeductionAnalysis::deduceFromSupportedIntrinsicCall(CallBase* call) {
  Function* intrinsicFun = call->getCalledFunction();
  assert(intrinsicFun && intrinsicFun->isIntrinsic());
  Intrinsic::ID id = intrinsicFun->getIntrinsicID();
  if (id == Intrinsic::memcpy || id == Intrinsic::memmove)
    mergeTypeAliasSets(call->getArgOperand(0), call->getArgOperand(1));
}

void TypeDeductionAnalysis::mergeTypeAliasSets(Value* value1, Value* value2) {
  const TypeAliasSet types1 = getOrCreateDeducedTypes(value1);
  const TypeAliasSet types2 = getOrCreateDeducedTypes(value2);

  auto mergeAliases = [&](const TransparentType* type1, const TransparentType* type2) {
    updateDeducedTypes(value1, type2->clone());
    updateDeducedTypes(value2, type1->clone());
  };

  for (const auto& type1 : types1)
    for (const auto& type2 : types2)
      mergeAliases(type1.get(), type2.get());
}
