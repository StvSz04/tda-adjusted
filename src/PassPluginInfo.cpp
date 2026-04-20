#include "TypeDeductionAnalysis.hpp"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Instructions.h" 

using namespace llvm;

// Tiny driver transform pass that just runs the analysis once
struct TDARunnerPass : PassInfoMixin<TDARunnerPass> {
  PreservedAnalyses run(Module& M, ModuleAnalysisManager& MAM) {
    (void) MAM.getResult<tda::TypeDeductionAnalysis>(M);
    return PreservedAnalyses::all();
  }
};

struct restoreTypes : PassInfoMixin<restoreTypes> {
   PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
      auto &res = MAM.getResult<tda::TypeDeductionAnalysis>(M);
      bool Changed = false;
      LLVMContext &Ctx = M.getContext();

      for (Function &F : M) {
          // --- 1. Tag Function Arguments (for 'define' and 'declare' lines) ---
          for (Argument &Arg : F.args()) {
              if (res.transparentTypes.count(&Arg)) {
                  const auto &typeSet = res.transparentTypes.at(&Arg);
                  if (!typeSet.empty()) {
                      std::string typeName = (*typeSet.begin())->toString();
                      std::string key = "arg_type_" + std::to_string(Arg.getArgNo());
                      MDString *MDS = MDString::get(Ctx, "TYPE_TOKEN:" + typeName);
                      
                      // Attach metadata to the first instruction of the function 
                      // as a "header" tag for the Python script
                      if (!F.isDeclaration()) {
                          F.getEntryBlock().front().setMetadata(key, MDNode::get(Ctx, {MDS}));
                      }
                  }
              }
          }

          for (BasicBlock &BB : F) {
              for (Instruction &I : BB) {
                  // --- 2. Existing Logic: Direct Lookup ---
                  const auto *foundSet = res.transparentTypes.count(&I) ? &res.transparentTypes.at(&I) : nullptr;

                  // --- 3. Fallback: Pointer Operands (Load/Store/GEP) ---
                  if (!foundSet || foundSet->empty()) {
                      Value *ptrOp = nullptr;
                      if (auto *SI = dyn_cast<StoreInst>(&I)) ptrOp = SI->getPointerOperand();
                      else if (auto *LI = dyn_cast<LoadInst>(&I)) ptrOp = LI->getPointerOperand();
                      else if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) ptrOp = GEP->getPointerOperand();

                      if (ptrOp && res.transparentTypes.count(ptrOp)) {
                          foundSet = &res.transparentTypes.at(ptrOp);
                      }
                  }

                  // --- 4. Special Case: Call Site Arguments ---
                  if (auto *CI = dyn_cast<CallInst>(&I)) {
                      for (unsigned i = 0; i < CI->arg_size(); ++i) {
                          Value *argVal = CI->getArgOperand(i);
                          if (res.transparentTypes.count(argVal)) {
                              const auto &argTypeSet = res.transparentTypes.at(argVal);
                              if (!argTypeSet.empty()) {
                                  std::string typeName = (*argTypeSet.begin())->toString();
                                  std::string key = "call_arg_type_" + std::to_string(i);
                                  MDString *MDS = MDString::get(Ctx, "TYPE_TOKEN:" + typeName);
                                  I.setMetadata(key, MDNode::get(Ctx, {MDS}));
                                  Changed = true;
                              }
                          }
                      }
                  }

                  // --- 5. Apply "restored_type" Metadata ---
                  if (foundSet && !foundSet->empty()) {
                      auto &firstType = **foundSet->begin();
                      std::string typeName = firstType.toString();

                      std::string token = "TYPE_TOKEN:" + typeName;
                      MDString *MDS = MDString::get(Ctx, token);
                      I.setMetadata("restored_type", MDNode::get(Ctx, {MDS}));
                      
                      Changed = true;
                  }
              }
          }
      }
      return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "TypeDeductionAnalysis", "1.0", [](PassBuilder& passBuilder) {
    passBuilder.registerAnalysisRegistrationCallback([](ModuleAnalysisManager& moduleAnalysisManager) {
      moduleAnalysisManager.registerPass([&] { return tda::TypeDeductionAnalysis(); });
    });

    passBuilder.registerPipelineParsingCallback(
      [](StringRef name, ModulePassManager& modulePassManager, ArrayRef<PassBuilder::PipelineElement>) {
        if (name == "tda") {
          modulePassManager.addPass(TDARunnerPass());
          return true;
        }
        else if (name == "restore-types") { 
          modulePassManager.addPass(restoreTypes());
          return true;
        }
        return false;
      });
  }};
}