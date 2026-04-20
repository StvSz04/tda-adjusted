#pragma once

#include "../Utils/PrintUtils.hpp"

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/DebugInfoMetadata.h>

namespace tda {

/**
 *  Compact description of the padding of a structure.
 *  All ranges are expressed in bytes and written as half-open intervals [begin, end).
 */
class StructPaddingInfo final : public Printable {
public:
  using ByteRange = std::pair<unsigned, unsigned>;

  StructPaddingInfo() = default;
  StructPaddingInfo(llvm::ArrayRef<ByteRange> ranges);
  StructPaddingInfo(const llvm::DICompositeType* diCompositeType);

  llvm::ArrayRef<ByteRange> getPaddingRanges() const { return paddingByteRanges; }

  std::string toString() const override;

private:
  llvm::SmallVector<ByteRange, 4> paddingByteRanges;
};

} // namespace tda
