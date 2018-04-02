
#include "InterproceduralDemandDrivenDefUse.h"

using namespace llvm;

namespace {

  class InterproceduralDemandDrivenDefUseRun: public FunctionPass {
  private:

  public:
    static char ID;
    Module* m;

    InterproceduralDemandDrivenDefUseRun() : FunctionPass(ID) {}

    bool doInitialization(Module &M) override {
      m = &M;
      return false;
    }

    bool runOnFunction(Function &F) override {
			errs() << "[*] Performing def-use analysis on " << F.getName() << "\n";


			map<string, set<pair<BasicBlock*, BasicBlock*>>>  def_use;

			for(BasicBlock& B : F)
				InterproceduralDemandDrivenDefUse().startBlockAnalysis(B, *m, def_use);


      return false;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }

  };
}

char InterproceduralDemandDrivenDefUseRun::ID = 0;
static RegisterPass<InterproceduralDemandDrivenDefUseRun> X("InterproceduralDemandDrivenDefUseRun", "Computes def-use pairs utilizing the infeasible paths information across procedures", true, true);

