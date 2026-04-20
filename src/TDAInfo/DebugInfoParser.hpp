#pragma once

#include "StructPaddingInfo.hpp"

#include <llvm/IR/DebugInfoMetadata.h>

namespace tda {

class DebugInfoParser {
public:
  static std::unordered_map<llvm::StructType*, StructPaddingInfo> getStructPaddingInfo(const llvm::Module& module);

private:
  static void insertPaddingInfo(llvm::DICompositeType* diCompositeType,
                                std::unordered_map<llvm::StructType*, StructPaddingInfo>& paddingInfoMap,
                                llvm::SmallDenseSet<llvm::DICompositeType*>& visited,
                                llvm::LLVMContext& ctx,
                                const llvm::DataLayout& dataLayout,
                                llvm::StructType* structType = nullptr);
};

} // namespace tda
