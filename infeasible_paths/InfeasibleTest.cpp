//#include "InfeasiblePathDetector.h"
#include "InterproceduralInfeasiblePathDetector.h"
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
        Node initialNode(&b, nullptr);
        detector.detectPaths(initialNode, result);

        errs()<< "BasicBlock: " << b.getName();
        errs()<< " Start set: ";
        for(std::pair< std::pair<Node*, Node*>, std::set<std::pair<Query, QueryResolution>>> startingPoints : result.startSet) {
          for (std::pair<Query, QueryResolution> startValue : startingPoints.second) {
            if (startValue.second == QueryUndefined) {
              errs() << "wtf?";
            }
            errs()<<"{e: " << startingPoints.first.first->basicBlock->getName() << "," << startingPoints.first.second->basicBlock->getName() << " R: ";
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
        for(std::pair< std::pair<Node*, Node*>, std::set<std::pair<Query, QueryResolution>>> presentPoints : result.presentSet) {
          for (std::pair<Query, QueryResolution> presentValue : presentPoints.second) {
            if (presentValue.second == QueryUndefined) {
              errs() << "wtf?";
            }
            errs()<<"{e: " << presentPoints.first.first->basicBlock->getName() << "," << presentPoints.first.second->basicBlock->getName() << " R: ";
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
        for(std::pair< std::pair<Node*, Node*>, std::set<std::pair<Query, QueryResolution>>> endPoints : result.endSet) {
          for (std::pair<Query, QueryResolution> endValue : endPoints.second) {
            if (endValue.second == QueryUndefined) {
              errs() << "wtf?";
            }
            errs()<<"{e: " << endPoints.first.first->basicBlock->getName() << "," << endPoints.first.second->basicBlock->getName() << " R: ";
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

