
#include "DemandDrivenDefUse.h"

using namespace llvm;

namespace {

  class DemandDrivenDefUseRun: public FunctionPass {
  private:

  public:
    static char ID;

    DemandDrivenDefUseRun() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
			errs() << "[*] Performing def-use analysis on " << F.getName() << "\n";

			map<string, set<pair<BasicBlock*, BasicBlock*>>>  def_use;
			DemandDrivenDefUse defUseAnalysis; 

			for(BasicBlock& B : F)
				defUseAnalysis.startBlockAnalysis(B, def_use);

			map<string, set<pair<BasicBlock*, BasicBlock*>>>::iterator it;
			for (it = def_use.begin(); it != def_use.end(); ++it){
					errs() << "\t[$] Def-Use(" << it->first << "): ";

					for(pair<BasicBlock*, BasicBlock*> p : it->second)
						errs() << "(" << p.first->getName() << ", " << p.second->getName() << ") ";

					errs() << "\n"; 
			}

			errs() << "\n";

      return false;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }

  };
}

char DemandDrivenDefUseRun::ID = 0;
static RegisterPass<DemandDrivenDefUseRun> X("DemandDrivenDefUseRun", "Computes def-use pairs utilizing the infeasible paths information", true, true);

