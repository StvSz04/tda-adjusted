#pragma once

#include "StructPaddingInfo.hpp"

#include <llvm/IR/Module.h>

namespace tda {

class TypeDeductionAnalysisInfo {
public:
  TypeDeductionAnalysisInfo(const TypeDeductionAnalysisInfo&) = delete;
  TypeDeductionAnalysisInfo& operator=(const TypeDeductionAnalysisInfo&) = delete;

  static TypeDeductionAnalysisInfo& getInstance();

  void initialize(llvm::Module& m);

  std::optional<StructPaddingInfo> getStructPaddingInfo(llvm::StructType* t) const;
  const llvm::DataLayout* getDataLayout() const { return dataLayout; }

private:
  std::unordered_map<llvm::StructType*, StructPaddingInfo> structPaddingInfo;
  const llvm::DataLayout* dataLayout;

  TypeDeductionAnalysisInfo()
  : dataLayout(nullptr) {}

  static std::unordered_map<llvm::StructType*, StructPaddingInfo> getStructPaddingInfo(llvm::Module& m);
};

} // namespace tda
