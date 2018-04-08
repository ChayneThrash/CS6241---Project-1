#include "InterproceduralDemandDrivenDefUse.h"

using namespace llvm;

namespace {

  class InterproceduralDemandDrivenDefUseRun: public ModulePass {
  private:

  public:
    static char ID;

    InterproceduralDemandDrivenDefUseRun() : ModulePass(ID) {}

    bool runOnModule(Module &M) override {
			int numberOfPairs = 0;

			for(Module::iterator f = M.begin(); f != M.end(); ++f){
				Function &F = *f;
				errs() << "[*] Performing def-use analysis on " << F.getName() << "\n";


				map<string, set<pair<BasicBlock*, BasicBlock*>>>  def_use;
				set<string> localVar; 
				for(BasicBlock& B : F)
					InterproceduralDemandDrivenDefUse().startBlockAnalysis(B, M, def_use, localVar);

				map<string, set<pair<BasicBlock*, BasicBlock*>>>::iterator it;
				for (it = def_use.begin(); it != def_use.end(); ++it){
						errs() << "\t[$] Def-Use(" << it->first << "): ";

						for(pair<BasicBlock*, BasicBlock*> p : it->second){
							numberOfPairs++;
							errs() << "(" << p.first->getParent()->getName() << ":" << p.first->getName() << ", " << p.second->getParent()->getName() << ":" << p.second->getName() << ") ";
						}
						errs() << "\n"; 
				}
			}

			errs() <<"[*] Total of " << numberOfPairs << " pairs were found. \n";

      return false;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }

  };
}

char InterproceduralDemandDrivenDefUseRun::ID = 0;
static RegisterPass<InterproceduralDemandDrivenDefUseRun> X("InterproceduralDemandDrivenDefUseRun", "Computes def-use pairs utilizing the infeasible paths information across procedures", true, true);

