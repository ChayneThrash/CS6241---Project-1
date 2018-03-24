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

        errs()<< "Start set: ";
        for(std::pair< std::pair<BasicBlock*, BasicBlock*>, std::pair<Value*, QueryResolution>> startingPoints : result.startSet) {
          if (startingPoints.second.second == QueryUndefined) {
            errs() << "wtf?";
          }
          errs()<<"{e: " << startingPoints.first.first->getName() << "," << startingPoints.first.second->getName() << "R: ";
          if (startingPoints.second.second == QueryTrue) {
            errs() << "T}";
          }
          else {
            errs() << "F}";
          }
        }
        errs()<< "\n";

        errs()<< "Present set: ";
        for(std::pair< std::pair<BasicBlock*, BasicBlock*>, std::pair<Value*, QueryResolution>> presentPoints : result.presentSet) {
          if (presentPoints.second.second == QueryUndefined) {
            errs() << "wtf?";
          }
          errs()<<"{e: " << presentPoints.first.first->getName() << "," << presentPoints.first.second->getName() << "R: ";
          if (presentPoints.second.second == QueryTrue) {
            errs() << "T}";
          }
          else {
            errs() << "F}";
          }
        }
        errs()<< "\n";

        errs()<< "End set: ";
        for(std::pair< std::pair<BasicBlock*, BasicBlock*>, std::pair<Value*, QueryResolution>> endPoints : result.endSet) {
          if (endPoints.second.second == QueryUndefined) {
            errs() << "wtf?";
          }
          errs()<<"{e: " << endPoints.first.first->getName() << "," << endPoints.first.second->getName() << "R: ";
          if (endPoints.second.second == QueryTrue) {
            errs() << "T}";
          }
          else {
            errs() << "F}";
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

