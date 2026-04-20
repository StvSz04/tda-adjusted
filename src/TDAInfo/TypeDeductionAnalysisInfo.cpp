#include "DebugInfoParser.hpp"
#include "TBAAParser.hpp"
#include "TypeDeductionAnalysisInfo.hpp"

using namespace llvm;
using namespace tda;

TypeDeductionAnalysisInfo& TypeDeductionAnalysisInfo::getInstance() {
  static TypeDeductionAnalysisInfo instance;
  return instance;
}

void TypeDeductionAnalysisInfo::initialize(Module& m) {
  structPaddingInfo = getStructPaddingInfo(m);
  dataLayout = &m.getDataLayout();
}

std::optional<StructPaddingInfo> TypeDeductionAnalysisInfo::getStructPaddingInfo(StructType* t) const {
  auto iter = structPaddingInfo.find(t);
  return iter != structPaddingInfo.end() ? std::optional(iter->second) : std::nullopt;
}

std::unordered_map<StructType*, StructPaddingInfo> TypeDeductionAnalysisInfo::getStructPaddingInfo(Module& m) {
  return DebugInfoParser::getStructPaddingInfo(m);
  //return TBAAParser::getStructPaddingInfo(m);
}
