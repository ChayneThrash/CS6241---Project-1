#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/LoopInfo.h"

#include "DemandDrivenDefUse.h"

using namespace llvm;

namespace {

  class DemandDrivenDefUseRun: public FunctionPass {
  private:

  public:
    static char ID;

    DemandDrivenDefUseRun() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
			errs() << "Performing def-use analysis on " << F.getName() << "\n";

			for(BasicBlock& B : F){
				DemandDrivenDefUse defUseAnalysis; 
				defUseAnalysis.startBlockAnalysis(B);
			}
      return false;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }

  };
}

char DemandDrivenDefUseRun::ID = 0;
static RegisterPass<DemandDrivenDefUseRun> X("DemandDrivenDefUseRun", "Computes def-use pairs utilizing the infeasible paths information", true, true);

