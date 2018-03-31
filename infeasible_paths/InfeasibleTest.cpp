//#include "InfeasiblePathDetector.h"
#include "InterproceduralInfeasiblePathDetector.h"
using namespace llvm;

namespace {

  void printCallStack(std::stack<Node*> callStack) {
    errs() << "(";
    while(callStack.size() > 0) {
      Node* n = callStack.top();
      callStack.pop();

      if (n == nullptr) {
        errs() << "null";
      }
      else {
        errs()<< n->basicBlock->getName() << ", ";
      }
    }

    errs() << ")";
  }

  class InfeasibleTest : public FunctionPass {
  private:

  public:
    static char ID;

    InfeasibleTest() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {

      if (F.getName() != "main") {
        return false;
      }
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
        for(std::pair< std::tuple<Node*, Node*, std::stack<Node*>>, std::set<std::pair<Query, QueryResolution>>> startingPoints : result.startSet) {
          for (std::pair<Query, QueryResolution> startValue : startingPoints.second) {
            if (startValue.second == QueryUndefined) {
              errs() << "wtf?";
            }
            BasicBlock* bb1 = std::get<0>(startingPoints.first)->basicBlock;
            BasicBlock* bb2 = std::get<1>(startingPoints.first)->basicBlock;
            errs()<<"{e: " << bb1->getParent()->getName() << "." << bb1->getName() << ", " << bb2->getParent()->getName() << "." << bb2->getName() << "CS: ";
            printCallStack(std::get<2>(startingPoints.first));
            errs() << " R: ";
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
        for(std::pair< std::tuple<Node*, Node*, std::stack<Node*>>, std::set<std::pair<Query, QueryResolution>>> presentPoints : result.presentSet) {
          for (std::pair<Query, QueryResolution> presentValue : presentPoints.second) {
            if (presentValue.second == QueryUndefined) {
              errs() << "wtf?";
            }
            BasicBlock* bb1 = std::get<0>(presentPoints.first)->basicBlock;
            BasicBlock* bb2 = std::get<1>(presentPoints.first)->basicBlock;
            errs()<<"{e: " << bb1->getParent()->getName() << "." << bb1->getName() << ", " << bb2->getParent()->getName() << "." << bb2->getName() << "CS: ";
            printCallStack(std::get<2>(presentPoints.first));
            errs() << " R: ";
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
        for(std::pair< std::tuple<Node*, Node*, std::stack<Node*>>, std::set<std::pair<Query, QueryResolution>>> endPoints : result.endSet) {
          for (std::pair<Query, QueryResolution> endValue : endPoints.second) {
            if (endValue.second == QueryUndefined) {
              errs() << "wtf?";
            }
            BasicBlock* bb1 = std::get<0>(endPoints.first)->basicBlock;
            BasicBlock* bb2 = std::get<1>(endPoints.first)->basicBlock;
            errs()<<"{e: " << bb1->getParent()->getName() << "." << bb1->getName() << ", " << bb2->getParent()->getName() << "." << bb2->getName() << "CS: ";
            printCallStack(std::get<2>(endPoints.first));
            errs() << " R: ";
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

