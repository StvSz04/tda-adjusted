#pragma once

#include "StructPaddingInfo.hpp"
#include "TransparentType.hpp"

#include <llvm/IR/IntrinsicInst.h>

namespace tda {

class TBAAParser {
public:
  static std::pair<std::unique_ptr<TransparentType>, std::unique_ptr<TransparentType>>
  getLoadStoreTypesFromTbaa(const llvm::Instruction* inst);

  static std::unordered_map<llvm::StructType*, StructPaddingInfo> getStructPaddingInfo(llvm::Module& module);

private:
  static bool isStructTypeDescriptor(const llvm::MDNode* mdNode);

  static std::pair<std::unique_ptr<TransparentType>, std::unique_ptr<TransparentType>>
  getPlaceholderStructTypes(const llvm::MDNode* structTypeMd, const llvm::MDNode* accessTypeMd, unsigned accessOffset);
};

} // namespace tda
