#ifndef NODE_H_
#define NODE_H_
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

#include <vector>

using namespace llvm;

Instruction* findFunctionCallTopDown(BasicBlock* b) {
  for(Instruction& i : *b) {
    if (i.getOpcode() == Instruction::Call) {
      CallInst* callInst = dyn_cast<CallInst>(&i);
      Function* f = callInst->getCalledFunction();
      if (f != nullptr) {
        return &i;
      }
    }
  }
  return nullptr;
}

struct Node {

  Node(BasicBlock* bb, Instruction* programPoint) : basicBlock(bb), programPointInBlock(programPoint), parentNode(nullptr), successors(), predecessors(),
                                                    instructionsReversed(), successorsInitialized(false), predecessorsInitialized(false)
  {
    if (programPointInBlock == nullptr) {
      for (BasicBlock::reverse_iterator iIter = basicBlock->rbegin(); iIter != basicBlock->rend(); ++iIter) {
        Instruction& i = *iIter;
        instructionsReversed.push_back(&i);
        if (i.getOpcode() == Instruction::Call) {
          CallInst* callInst = dyn_cast<CallInst>(&i);
          Function* f = callInst->getCalledFunction();
          if (f != nullptr) {
            break;
          }
        }
      }
    }
    else {
      bool programPointReached = false;
      for (BasicBlock::reverse_iterator iIter = basicBlock->rbegin(); iIter != basicBlock->rend(); ++iIter) {
        Instruction& i = *iIter;
        if (!programPointReached) {
          if (&i == programPointInBlock) {
            programPointReached = true;  
            instructionsReversed.push_back(&i);
          }
          continue;
        }

        instructionsReversed.push_back(&i);
        if (i.getOpcode() == Instruction::Call) {
          CallInst* callInst = dyn_cast<CallInst>(&i);
          Function* f = callInst->getCalledFunction();
          if (f != nullptr) {
            break;
          }
        }
      }
    }
  }

  ~Node() {
    if (parentNode != nullptr) {
      delete parentNode;
      parentNode = nullptr;
    }
  }

  BasicBlock* basicBlock;
  Instruction* programPointInBlock;
  Node* parentNode;

  const std::vector<Node>& getSuccessors() {
    if (!successorsInitialized) {
      populateSuccessors();
      successorsInitialized = true;
    }
    return successors;
  }

  const std::vector<Node>& getPredecessors() {
    if (!predecessorsInitialized) {
      populatePredecessors();
      predecessorsInitialized = true;
    }
    return predecessors;
  }

  const std::vector<Instruction*>& getReversedInstructions() {
    return instructionsReversed;
  }

  bool operator==(const Node& other) const {
    return this->basicBlock == other.basicBlock && this->programPointInBlock == other.programPointInBlock;
  }

  bool operator<(const Node& other) const {
    if (this->basicBlock == other.basicBlock) {
      if (this->programPointInBlock == other.programPointInBlock) {
      }
      return this->programPointInBlock < other.programPointInBlock;
    }
    return this->basicBlock < other.basicBlock;
  }



private:
  std::vector<Node> successors;
  std::vector<Node> predecessors;
  std::vector<Instruction*> instructionsReversed;
  bool successorsInitialized;
  bool predecessorsInitialized;

  void populatePredecessors() {
    bool functionFound = false;
    if (programPointInBlock == nullptr) {
      for (BasicBlock::reverse_iterator iIter = basicBlock->rbegin(); iIter != basicBlock->rend(); ++iIter) {
        Instruction& i = *iIter;
        if (i.getOpcode() == Instruction::Call) {
          CallInst* callInst = dyn_cast<CallInst>(&i);
          Function* f = callInst->getCalledFunction();
          if (f != nullptr) {
            functionFound = true;
            addFunctionExitBlocksToPredecessors(*f, callInst);
            break;
          }
        }
      }
    }
    else {
      bool programPointReached = false;
      for (BasicBlock::reverse_iterator iIter = basicBlock->rbegin(); iIter != basicBlock->rend(); ++iIter) {
        Instruction& i = *iIter;
        if (!programPointReached) {
          if (&i == programPointInBlock) {
            programPointReached = true;  
          }
          continue;
        }

        if (i.getOpcode() == Instruction::Call) {
          CallInst* callInst = dyn_cast<CallInst>(&i);
          Function* f = callInst->getCalledFunction();
          if (f != nullptr) {
            functionFound = true;
            addFunctionExitBlocksToPredecessors(*f, callInst);
            break;
          }
        }
      }
    }

    if (!functionFound) {
      if (basicBlock == &(basicBlock->getParent()->getEntryBlock()) && parentNode != nullptr) {
        predecessors.push_back(*parentNode);
      }
      else {
        for(BasicBlock* pred : llvm::predecessors(basicBlock)) {
          predecessors.push_back(Node(pred, nullptr));
        }
      }
    }
  }

  void populateSuccessors() {
    
    if (programPointInBlock == nullptr) {
      for(BasicBlock* succ : llvm::successors(basicBlock)) {
        Instruction* functionCall = findFunctionCallTopDown(succ);
        successors.push_back(Node(succ, functionCall));
      }
    }
    else {
      bool functionFound = false;
      bool programPointReached = false;
      for (Instruction& i : *basicBlock) {
        if (!programPointReached) {
          if (&i == programPointInBlock) {
            programPointReached = true;  
          }
          continue;
        }

        if (i.getOpcode() == Instruction::Call) {
          CallInst* callInst = dyn_cast<CallInst>(&i);
          Function* f = callInst->getCalledFunction();
          if (f != nullptr) {
            Instruction* functionCall = findFunctionCallTopDown(&(f->getEntryBlock()));
            successors.push_back(Node(&(f->getEntryBlock()), functionCall));
            functionFound = true;
            break;
          }
        }
      }
      if (!functionFound) {
        for(BasicBlock* succ : llvm::successors(basicBlock)) {
          successors.push_back(Node(succ, findFunctionCallTopDown(succ)));
        }
      }
    }

    
  }

  void addFunctionExitBlocksToPredecessors(Function& f, Instruction* callInstruction) {
    for(BasicBlock& b : f) {
      if (b.getTerminator()->getNumSuccessors() == 0) {
        Node n(&b, nullptr);
        n.parentNode = new Node(basicBlock, callInstruction);
        predecessors.push_back(Node(&b, nullptr));
      }
    }
  }

};


#endif