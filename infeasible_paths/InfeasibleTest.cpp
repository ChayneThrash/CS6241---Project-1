#include "InfeasiblePathDetector.h"
using namespace llvm;

namespace {

  class InfeasibleTest : public FunctionPass {
  private:

  public:
    static char ID;

    InfeasibleTest() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {

      for(BasicBlock& b : F) {
        const TerminatorInst* terminator = b.getTerminator();
        if (terminator->getNumSuccessors() == 1 || terminator->getOpcode() != Instruction::Br) {
          continue;
        }

        InfeasiblePathResult result;
        InfeasiblePathDetector detector;
        detector.detectPaths(b, result);

        errs()<< "BasicBlock: " << b.getName();
        errs()<< " Start set: ";
        for(std::pair< std::pair<BasicBlock*, BasicBlock*>, std::set<std::pair<Query, QueryResolution>>> startingPoints : result.startSet) {
          for (std::pair<Query, QueryResolution> startValue : startingPoints.second) {
            if (startValue.second == QueryUndefined) {
              errs() << "wtf?";
            }
            errs()<<"{e: " << startingPoints.first.first->getName() << "," << startingPoints.first.second->getName() << " R: ";
            if (startValue.second == QueryTrue) {
              errs() << "T}";
            }
            else {
              errs() << "F}";
            }
          }
          
        }
        errs()<< "\n";

        errs()<< "Present set: ";
        for(std::pair< std::pair<BasicBlock*, BasicBlock*>, std::set<std::pair<Query, QueryResolution>>> presentPoints : result.presentSet) {
          for (std::pair<Query, QueryResolution> presentValue : presentPoints.second) {
            if (presentValue.second == QueryUndefined) {
              errs() << "wtf?";
            }
            errs()<<"{e: " << presentPoints.first.first->getName() << "," << presentPoints.first.second->getName() << " R: ";
            if (presentValue.second == QueryTrue) {
              errs() << "T}";
            }
            else {
              errs() << "F}";
            }
          }
        }
        errs()<< "\n";

        errs()<< "End set: ";
        for(std::pair< std::pair<BasicBlock*, BasicBlock*>, std::set<std::pair<Query, QueryResolution>>> endPoints : result.endSet) {
          for (std::pair<Query, QueryResolution> endValue : endPoints.second) {
            if (endValue.second == QueryUndefined) {
              errs() << "wtf?";
            }
            errs()<<"{e: " << endPoints.first.first->getName() << "," << endPoints.first.second->getName() << " R: ";
            if (endValue.second == QueryTrue) {
              errs() << "T}";
            }
            else {
              errs() << "F}";
            }
          }
        }
        errs()<< "\n";

      }

      return false;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }

  };
}

char InfeasibleTest::ID = 0;
static RegisterPass<InfeasibleTest> X("InfeasibleTest", "Computes stats for each function", true, true);

