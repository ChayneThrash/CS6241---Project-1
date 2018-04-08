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
    Module* m;

    InfeasibleTest() : FunctionPass(ID) {}


    bool doInitialization(Module &M) override {
      m = &M;
      return false;
    }
    bool runOnFunction(Function &F) override {

      for(BasicBlock& b : F) {

        const TerminatorInst* terminator = b.getTerminator();
        if (terminator->getNumSuccessors() == 1 || terminator->getOpcode() != Instruction::Br) {
          continue;
        }
        
        errs()<< "BasicBlock: " << F.getName() << "." << b.getName();
        InfeasiblePathResult result;
        InfeasiblePathDetector detector;
        Node initialNode(&b, nullptr);
        detector.detectPaths(initialNode, result, *m);

        errs()<< " Start set: ";
        for(std::pair< std::pair<Node*, Node*>, std::set<std::tuple<Query, QueryResolution, std::stack<Node*>>>> startingPoints : result.startSet) {
          for (std::tuple<Query, QueryResolution, std::stack<Node*>> startValue : startingPoints.second) {
            if (std::get<1>(startValue) == QueryUndefined) {
              errs() << "wtf?";
            }
            BasicBlock* bb1 = startingPoints.first.first->basicBlock;
            BasicBlock* bb2 = startingPoints.first.second->basicBlock;
            errs()<<"{e: " << bb1->getParent()->getName() << "." << bb1->getName() << ", " << bb2->getParent()->getName() << "." << bb2->getName() << " CS: ";
            printCallStack(std::get<2>(startValue));
            errs() << " R: ";
            if (std::get<1>(startValue) == QueryTrue) {
              errs() << "T}";
            }
            else {
              errs() << "F}";
            }
          }
          
        }
        errs()<< "\n";

        errs()<< "Present set: ";
        for(std::pair< std::pair<Node*, Node*>, std::set<std::tuple<Query, QueryResolution, std::stack<Node*>>>> startingPoints : result.presentSet) {
          for (std::tuple<Query, QueryResolution, std::stack<Node*>> startValue : startingPoints.second) {
            if (std::get<1>(startValue) == QueryUndefined) {
              errs() << "wtf?";
            }
            BasicBlock* bb1 = startingPoints.first.first->basicBlock;
            BasicBlock* bb2 = startingPoints.first.second->basicBlock;
            errs()<<"{e: " << bb1->getParent()->getName() << "." << bb1->getName() << ", " << bb2->getParent()->getName() << "." << bb2->getName() << " CS: ";
            printCallStack(std::get<2>(startValue));
            errs() << " R: ";
            if (std::get<1>(startValue) == QueryTrue) {
              errs() << "T}";
            }
            else {
              errs() << "F}";
            }
          }
          
        }
        errs()<< "\n";

        errs()<< "End set: ";
        for(std::pair< std::pair<Node*, Node*>, std::set<std::tuple<Query, QueryResolution, std::stack<Node*>>>> startingPoints : result.endSet) {
          for (std::tuple<Query, QueryResolution, std::stack<Node*>> startValue : startingPoints.second) {
            if (std::get<1>(startValue) == QueryUndefined) {
              errs() << "wtf?";
            }
            BasicBlock* bb1 = startingPoints.first.first->basicBlock;
            BasicBlock* bb2 = startingPoints.first.second->basicBlock;
            errs()<<"{e: " << bb1->getParent()->getName() << "." << bb1->getName() << ", " << bb2->getParent()->getName() << "." << bb2->getName() << " CS: ";
            printCallStack(std::get<2>(startValue));
            errs() << " R: ";
            if (std::get<1>(startValue) == QueryTrue) {
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

