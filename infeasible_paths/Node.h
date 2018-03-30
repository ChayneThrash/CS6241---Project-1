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


/// Conclusions: function exit nodes need to know where all of the caller locations are that were originally called from. Basically, some exit node manager that can keep track of all of the predecessors of 
/// the exit nodes. Nodes inside of the original calling context will not propogate their results into the function (maybe? need to think on this some more...) 


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

  Node(BasicBlock* bb, Instruction* programPoint, std::map<std::pair<BasicBlock*, Instruction*>, Node*>* allNodes, Node* callSite) : Node(bb, programPoint, false, allNodes) {
    predecessors.push_back(callSite);
    predecessorsInitialized = true;
    isEntryOfFunction = true;
  }

  Node(BasicBlock* bb, std::map<std::pair<BasicBlock*, Instruction*>, Node*>* allNodes, Node* returnPoint) : Node(bb, nullptr, false, allNodes) {
    successors.push_back(returnPoint);
    successorsInitialized = true;
    isExitOfFunction = true;
  }

  Node(BasicBlock* bb, Instruction* programPoint, bool isStartingNode, std::map<std::pair<BasicBlock*, Instruction*>, Node*>* allNodes) : 
        basicBlock(bb), programPointInBlock(programPoint), successors(), predecessors(),
        instructionsReversed(), successorsInitialized(false), predecessorsInitialized(false),
        isExitOfFunction(false), isEntryOfFunction(true) {
    populateInstructionList();
    this->isStartingNode = isStartingNode;
    if (isStartingNode) {
      this->allNodes = new std::map<std::pair<BasicBlock*, Instruction*>, Node*>();
      (*(this->allNodes))[std::make_pair(bb, programPoint)] = this;
    }
    else {
      this->allNodes = allNodes;
    }
  }

  Node(BasicBlock* bb, Instruction* programPoint) : Node(bb, programPoint, true, nullptr)
  {}

  ~Node() {
    if (isStartingNode) {
      for(std::pair<std::pair<BasicBlock*, Instruction*>, Node*> node : *allNodes) {
        if (node.second != this) {
          delete node.second;
        }
      }
    }
  }

  BasicBlock* basicBlock;
  Instruction* programPointInBlock;

  Node* getFunctionEntryNode() {
    BasicBlock* entryBlock = &(basicBlock->getParent()->getEntryBlock());
    return getOrCreateNode(entryBlock, findFunctionCallTopDown(entryBlock));
  }

  const std::vector<Node*>& getSuccessors() {
    if (!successorsInitialized) {
      populateSuccessors();
      successorsInitialized = true;
    }
    return successors;
  }

  const std::vector<Node*>& getPredecessors() {
    if (!predecessorsInitialized) {
      populatePredecessors();
      predecessorsInitialized = true;
    }
    return predecessors;
  }

  const std::vector<Instruction*>& getReversedInstructions() {
    return instructionsReversed;
  }

  Value* getBranchCondition() {
    const TerminatorInst* terminator = basicBlock->getTerminator();
    return terminator->getOperand(0);
  }

  bool endsWithConditionalBranch() const {
    const TerminatorInst* terminator = basicBlock->getTerminator();
    if (terminator->getNumSuccessors() == 1 || terminator->getOpcode() != Instruction::Br) {
      return true;
    }
    else {
      return false;
    }
  }

  Node* getTrueEdge() {
    const TerminatorInst* terminator = basicBlock->getTerminator();
    BasicBlock* trueDestination = dyn_cast<BasicBlock>(terminator->getOperand(2));
    Instruction* i = findFunctionCallTopDown(trueDestination);

    if (!successorsInitialized) {
      populateSuccessors();
      successorsInitialized = true;
    }
    return (*allNodes)[std::make_pair(trueDestination, i)];
  }

  Node* getFalseEdge() {
    const TerminatorInst* terminator = basicBlock->getTerminator();
    BasicBlock* falseDestination = dyn_cast<BasicBlock>(terminator->getOperand(1));
    Instruction* i = findFunctionCallTopDown(falseDestination);

    if (!successorsInitialized) {
      populateSuccessors();
      successorsInitialized = true;
    }
    return (*allNodes)[std::make_pair(falseDestination, i)];
  }

  void addPredecessor(Node* node) {
    predecessors.push_back(node);
  }

  void addSuccessor(Node* node) {
    successors.push_back(node);
  }

private:
  std::vector<Node*> successors;
  std::vector<Node*> predecessors;
  std::vector<Instruction*> instructionsReversed;
  bool successorsInitialized;
  bool predecessorsInitialized;
  bool isStartingNode;
  std::map<std::pair<BasicBlock*, Instruction*>, Node*>* allNodes;
  bool isExitOfFunction;
  bool isEntryOfFunction;


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
            Node* callSite = getOrCreateNode(basicBlock, &i);
            createFunctionEntryNode(f, callSite);
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
            Node* callSite = getOrCreateNode(basicBlock, &i);
            createFunctionEntryNode(f, callSite);
            break;
          }
        }
      }
    }

    if (!functionFound) {
      for(BasicBlock* pred : llvm::predecessors(basicBlock)) {
        addPredecessor(pred, nullptr);
      }
    }
  }

  void populateSuccessors() {
    
    if (programPointInBlock == nullptr) {
      for(BasicBlock* succ : llvm::successors(basicBlock)) {
        addSuccessor(succ, findFunctionCallTopDown(succ));
      }
    }
    else {
      for (Instruction& i : *basicBlock) {
        if (&i == programPointInBlock) {
          CallInst* callInst = dyn_cast<CallInst>(&i);
          Function* f = callInst->getCalledFunction();

          Node* entryNode = createFunctionEntryNode(f, this);
          addSuccessor(entryNode);

          Instruction* nextFunctionCall = findNextFunctionCallAfter(basicBlock, programPointInBlock);
          Node* returnToPoint = getOrCreateNode(basicBlock, nextFunctionCall);
          createFunctionExitNodes(returnToPoint, f);
          break;
        }
      }
    }
  }

  void populateInstructionList() {
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

  void addPredecessor(BasicBlock* bb, Instruction* i) {
    predecessors.push_back(getOrCreateNode(bb, i));
  }

  void addSuccessor(BasicBlock* bb, Instruction* i) {
    successors.push_back(getOrCreateNode(bb, i));
  }

  Node* getOrCreateNode(BasicBlock* bb, Instruction* i) {
    Node* node;
    if (allNodes->count(std::make_pair(bb, i)) > 0) {
      node = (*allNodes)[std::make_pair(bb, i)];
    }
    else {
      node = new Node(bb, i, false, allNodes);
      (*allNodes)[std::make_pair(bb, i)] = node;
    }
    return node;
  }

  void addFunctionExitBlocksToPredecessors(Function& f, Instruction* callInstruction) {
    for(BasicBlock& b : f) {
      if (b.getTerminator()->getNumSuccessors() == 0) {
        Node* n = createFunctionExitNode(&b, this);
        addPredecessor(n);
      }
    }
  }

  void createNodesForFunctionCall(Node* callSite, Node* returnToPoint, Function* f) {
    createFunctionExitNodes(returnToPoint, f);
    createFunctionEntryNode(f, callSite);
  }

  void createFunctionExitNodes(Node* returnToPoint, Function* f) {
    for(BasicBlock& b : *f) {
      if (b.getTerminator()->getNumSuccessors() == 0) {
        createFunctionExitNode(&b, returnToPoint);
      }
    }
  }

  Node* createFunctionExitNode(BasicBlock* bb, Node* returnToPoint) {
    Node* node;
    if (allNodes->count(std::make_pair(bb, nullptr)) > 0) {
      node = (*allNodes)[std::make_pair(bb, nullptr)];
      node->addSuccessor(returnToPoint);
    }
    else {
      node = new Node(bb, allNodes, returnToPoint);
      (*allNodes)[std::make_pair(bb, nullptr)] = node;
    }
    return node;
  }

  Node* createFunctionEntryNode(Function* f, Node* callSite) {
    Node* node;
    BasicBlock* bb = &(f->getEntryBlock());
    Instruction* i = findFunctionCallTopDown(bb);
    if (allNodes->count(std::make_pair(bb, i)) > 0) {
      node = (*allNodes)[std::make_pair(bb, i)];
      node->addPredecessor(callSite);
    }
    else {
      node = new Node(bb, i, allNodes, callSite);
      (*allNodes)[std::make_pair(bb, nullptr)] = node;
    }
    return node;
  }

  Instruction* findNextFunctionCallAfter(BasicBlock* bb, Instruction* toFind) {
    bool programPointFound = toFind == nullptr;
    for(Instruction& i : *bb) {
      if (!programPointFound) {
        if (&i == toFind) {
          programPointFound = true;
        }
        continue;
      }

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

};


#endif