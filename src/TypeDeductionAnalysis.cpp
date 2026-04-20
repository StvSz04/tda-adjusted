#include "Debug/Logger.hpp"
#include "TDAInfo/TypeDeductionAnalysisInfo.hpp"
#include "TransparentType.hpp"
#include "TypeDeductionAnalysis.hpp"

#include <llvm/ADT/Statistic.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>

// Added a string buffer to read the result(densemap) and return a string verion line 138
# include <string>
# include "llvm/Support/raw_ostream.h"
# include "llvm/Support/FileSystem.h"  // for raw_fd_ostream

#define DEBUG_TYPE "tda"
// #define LLVM_DEBUG(x) x

STATISTIC(stat0ptrTypes, "Total pointer types to deduce");
STATISTIC(stat1ptrAliases, "Total pointer alias types deduced");
STATISTIC(stat2multipleAliases, "Pointers with multiple aliases");
STATISTIC(stat3transparentAliases, "Transparent pointer aliases");
STATISTIC(stat4partiallyTransparentAliases, "Partially transparent pointer aliases");
STATISTIC(stat5opaqueAliases, "Opaque aliases (not deduced)");

using namespace llvm;
using namespace tda;



AnalysisKey TypeDeductionAnalysis::Key;

// Serializes the Result's DenseMap to a JSON-like string.
// "value_name": ["type1", "type2", ...]
// The full output is wrapped in { }.

TypeDeductionAnalysis::Result TypeDeductionAnalysis::run(Module& m, ModuleAnalysisManager&) {
  LLVM_DEBUG(log().logln("[TypeDeductionAnalysis]", Logger::Magenta));
  Result result;

  TypeDeductionAnalysisInfo::getInstance().initialize(m);

  // Build initial deduction queue
  for (Function& f : m) {
    if (f.isDeclaration()) {
      // Cannot deduce from a declaration: just create its transparent type (could be opaque)
      updateDeducedTypes(&f, TransparentTypeFactory::createFromValue(&f));
      continue;
    }
    for (Instruction& inst : instructions(f)) {
      deductionQueue.push_back(&inst);
      // Cannot deduce from a constant: just create its transparent type
      for (Use& operand : inst.operands())
        if (auto* constant = dyn_cast<Constant>(operand.get()))
          updateDeducedTypes(constant, TransparentTypeFactory::createFromValue(constant));
    }
    deductionQueue.push_back(&f);
  }
  for (GlobalValue& globalValue : m.globals())
    deductionQueue.push_back(&globalValue);

  // Continue deducing until a fix point is reached
  unsigned iterations = 0;
  while (changed) {
    LLVM_DEBUG(log() << Logger::Blue << "[Deduction iteration " << iterations << "]\n"
                     << Logger::Reset);
    iterations++;
    changed = false;
    for (Value* value : deductionQueue)
      deduceFromValue(value);
  }
  LLVM_DEBUG(
    Logger& logger = log();
    logger.logln("[Deduction completed]", Logger::Blue);
    logDeducedTypes();
    logger.logln("[Opaque pointers and pointers with multiple alias types]", Logger::Yellow);
    bool any = false;
    bool printedValue = false;
    auto logAliasSet = [&](const Value* value, const TypeAliasSet& typeAliasSet) {
      logger.log("[Value] ", Logger::Bold).logValueln(value);
      auto indenter = logger.getIndenter();
      indenter.increaseIndent();
      logger.logln(typeAliasSet, Logger::Yellow);
      printedValue = true;
      any = true;
    };
    for (auto& [value, typeAliasSet] : deducedTypes) {
      printedValue = false;
      for (const auto& type : typeAliasSet)
        if (type->containsOpaquePtr())
          if (!printedValue) {
            logAliasSet(value, typeAliasSet);
            break;
          }
      if (!printedValue && typeAliasSet.size() > 1)
        logAliasSet(value, typeAliasSet);
    }
    if (!any)
      logger.logln("None", Logger::Green););

  stat0ptrTypes = 0;
  stat1ptrAliases = 0;
  stat2multipleAliases = 0;
  stat3transparentAliases = 0;
  stat4partiallyTransparentAliases = 0;
  stat5opaqueAliases = 0;
  // Save deduced transparent types and compute statistics
  for (auto& [value, typeAliasSet] : deducedTypes) {
    // Statistics
    if (value->getType()->isPointerTy()) {
      stat0ptrTypes++;
      if (!typeAliasSet.empty()) {
        for (const auto& type : typeAliasSet) {
          stat1ptrAliases++;
          if (!type->containsOpaquePtr())
            stat3transparentAliases++;
          else if (type->isPointerTT() && !type->getPointedType())
            stat5opaqueAliases++;
          else
            stat4partiallyTransparentAliases++;
        }
        if (typeAliasSet.size() > 1)
          stat2multipleAliases++;
      }
      else
        stat5opaqueAliases++;
    }
    // Move into result
    if (value->getType()->isPointerTy())
      result.transparentTypes[value] = std::move(typeAliasSet);
    // if (!typeAliasSet.empty())
    //   result.transparentTypes[value] = std::move(typeAliasSet);
  }

  LLVM_DEBUG(log().logln("[End of TypeDeductionAnalysis]", Logger::Magenta));

  if (stat0ptrTypes > 0) {
      float recoveryRate = (float)stat3transparentAliases / (float)stat1ptrAliases * 100.0f;
      Logger& logger = log();
      logger.logln("--- Research Statistics ---", Logger::Cyan);
      logger.log("Total Pointer Types: ").logln(std::to_string(stat0ptrTypes));
      logger.log("Transparent Aliases: ").logln(std::to_string(stat3transparentAliases), Logger::Green);
      logger.log("Opaque Aliases:      ").logln(std::to_string(stat5opaqueAliases), Logger::Red);
      logger.log("Recovery Rate:       ").logln(std::to_string(recoveryRate) + "%", Logger::Bold);
  }

  

  return result;
}

const TypeAliasSet& TypeDeductionAnalysis::updateDeducedTypes(Value* value,
                                                              std::unique_ptr<TransparentType> deducedType) {
  TypeAliasSet& typeAliasSet = deducedTypes[value];
  if (!deducedType)
    return typeAliasSet;

  Logger& logger = log();
  auto indenter = logger.getIndenter();
  auto logDeductionValue = [&] {
    if (!currDeductionValue)
      return;
    LLVM_DEBUG(logger.log("[Deducing from] ", Logger::Bold).logValueln(currDeductionValue));
    indenter.increaseIndent();
  };

  bool typeSetChanged = false;
  const auto iter = std::ranges::find_if(
    typeAliasSet, [&deducedType](const auto& type) -> bool { return type->isCompatibleWith(deducedType.get()); });
  if (iter != typeAliasSet.end()) {
    std::unique_ptr<TransparentType> merged = (*iter)->mergeWith(deducedType.get());
    if (*merged != **iter) {
      LLVM_DEBUG(
        logDeductionValue();
        logger.log("Changed (merge) type alias set of: ").logValueln(value);
        indenter.increaseIndent();
        logger.log("from: ").logln(typeAliasSet, Logger::Cyan););
      typeAliasSet.erase(iter);
      typeAliasSet.insert(std::move(merged));
      typeSetChanged = true;
    }
  }
  else {
    assert(typeAliasSet.empty()                                                      // First alias
           || ((*typeAliasSet.begin())->isPointerTT() && deducedType->isPointerTT()) // All pointers
           || (*typeAliasSet.begin())->isStructurallyEquivalent(deducedType.get())); // All structurally equivalent
    LLVM_DEBUG(
      logDeductionValue();
      logger.log("Changed (insert) type alias set of: ").logValueln(value);
      indenter.increaseIndent();
      logger.log("from: ").logln(typeAliasSet, Logger::Cyan););
    typeAliasSet.insert(std::move(deducedType));
    typeSetChanged = true;
  }

  if (typeSetChanged) {
    changed = true;
    LLVM_DEBUG(logger.log("to:   ").logln(typeAliasSet, Logger::Cyan););
  }
  return typeAliasSet;
}

TypeAliasSet TypeDeductionAnalysis::getOrCreateDeducedTypes(Value* value) {
  auto copyTypeAliasSet = [](const TypeAliasSet& typeAliasSet) {
    TypeAliasSet typeAliasSetCopy;
    for (const auto& type : typeAliasSet)
      typeAliasSetCopy.insert(type->clone());
    return typeAliasSetCopy;
  };

  auto iter = deducedTypes.find(value);
  if (iter != deducedTypes.end() && !iter->second.empty())
    return copyTypeAliasSet(iter->second);
  LLVM_DEBUG(log().logln("Creating initial type:", Logger::Cyan););
  const TypeAliasSet& typeAliasSet = updateDeducedTypes(value, TransparentTypeFactory::createFromValue(value));
  return copyTypeAliasSet(typeAliasSet);
}

void TypeDeductionAnalysis::logDeducedTypes() {
  Logger& logger = log();
  logger.logln("[Results]", Logger::Green);
  for (const auto& [value, typeAliasSet] : deducedTypes) {
    logger.log("[Value] ", Logger::Bold).logValueln(value);
    auto indenter = logger.getIndenter();
    indenter.increaseIndent();
    if (!typeAliasSet.empty()) {
      logger.log("deduced alias types: ");
      logger.log("{ ", Logger::Bold);
      bool first = true;
      for (const auto& type : typeAliasSet) {
        auto color = type->containsOpaquePtr() ? Logger::Yellow : Logger::Green;
        if (!first)
          logger.log(", ", Logger::Bold);
        else
          first = false;
        logger.log(type, color);
      }
      logger.logln(" }", Logger::Bold);
    }
    else
      logger.logln("No types deduced", Logger::Yellow);
  }
}
