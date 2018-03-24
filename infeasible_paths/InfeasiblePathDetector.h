#ifndef INFEASIBLEPATHDETECTOR_H_
#define INFEASIBLEPATHDETECTOR_H_

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

#include <set>
#include <queue>
#include <map>
#include <algorithm>
using namespace llvm;

namespace {


  enum QueryOperator { IsTrue, AreEqual, AreNotEqual };

  enum QueryResolution { QueryTrue, QueryFalse, QueryUndefined };

  struct Query
  {
    Value* lhs;
    QueryOperator queryOperator;
    ConstantInt* rhs;

    bool operator==(const Query& other) {
      return this->lhs == other.lhs && this->rhs == other.rhs && this->queryOperator == other.queryOperator;
    }

    bool operator<(const Query& other) {
      return (this->lhs < other.lhs) || (this->rhs == other.lhs && (this->queryOperator < other.queryOperator || (this->queryOperator == other.queryOperator && this->rhs < other.rhs)));
    }
  };

  struct  InfeasiblePathResult {
    std::map<std::pair<BasicBlock*, BasicBlock*>, std::pair<Value*, QueryResolution>> startSet;
    std::map<std::pair<BasicBlock*, BasicBlock*>, std::pair<Value*, QueryResolution>> presentSet;
    std::map<std::pair<BasicBlock*, BasicBlock*>, std::pair<Value*, QueryResolution>> endSet;
  };

  class InfeasiblePathDetector {
  private:

  public:
    InfeasiblePathDetector() {}

    void detectPaths(BasicBlock& basicBlock, InfeasiblePathResult& result) {
      const TerminatorInst* terminator = basicBlock.getTerminator();
      if (terminator->getNumSuccessors() == 1 || terminator->getOpcode() != Instruction::Br) {
        return;
      }

      std::queue<std::pair<BasicBlock*, Query>> worklist;
      std::map<BasicBlock*, std::vector<Query>> visited;

      Query initialQuery;
      initialQuery.lhs = terminator->getOperand(0);
      initialQuery.rhs = nullptr;
      initialQuery.queryOperator = IsTrue;

      worklist.push(std::make_pair(&basicBlock, initialQuery));
      visited[&basicBlock].push_back(initialQuery);
      
      BasicBlock* trueDestination = dyn_cast<BasicBlock>(terminator->getOperand(2));
      BasicBlock* falseDestination = dyn_cast<BasicBlock>(terminator->getOperand(1));

      std::map<std::pair<Query, BasicBlock*>, std::set<QueryResolution>> queryResolutions;
      std::set<std::pair<Query, BasicBlock*>> queriesResolvedInNode;

      QueryResolution resolution;

      // Step 1
      while(worklist.size() != 0) {
        std::pair<BasicBlock*, Query> workItem = worklist.front();
        worklist.pop();

        BasicBlock* b = workItem.first;
        Query currentValue = workItem.second;

        if(!resolve(*b, currentValue, resolution)) {
          if (b == &(b->getParent()->getEntryBlock())) {
            queriesResolvedInNode.insert(std::make_pair(currentValue, b));
            queryResolutions[std::make_pair(currentValue, b)].insert(QueryUndefined);
          }

          currentValue = substitute(*b, currentValue);
          for(BasicBlock* pred : predecessors(b)) {
            if (std::find(visited[pred].begin(), visited[pred].end(), currentValue) == visited[pred].end()) {
              visited[pred].push_back(currentValue);
              worklist.push(std::make_pair(pred, currentValue));
            }
          }
        }
        else {
          queriesResolvedInNode.insert(std::make_pair(currentValue, b));
          queryResolutions[std::make_pair(currentValue, b)].insert(resolution);
        }
      }

      std::set<BasicBlock*> step2WorkList;
      for (std::pair<const std::pair<Query, BasicBlock*>, std::set<QueryResolution>> resolvedNode : queryResolutions) {
        BasicBlock* b = resolvedNode.first.second;
        for (BasicBlock* succ : successors(b)) {
          step2WorkList.insert(succ);
        }
      }

      // Step 2
      while (step2WorkList.size() != 0) {
        std::set<BasicBlock*>::iterator bIter = step2WorkList.begin();
        BasicBlock* b = *bIter;
        step2WorkList.erase(bIter);

        for(Query query : visited[b]) {

          std::pair<Query, BasicBlock*> currentBlockAndQuery = std::make_pair(query, b);
          
          if (queriesResolvedInNode.count(currentBlockAndQuery) != 0) {
            continue;
          }

          for (BasicBlock* pred : predecessors(b)) {
            size_t currentNumberResultsForBlock = queryResolutions[currentBlockAndQuery].size();
            for(QueryResolution qr : queryResolutions[std::make_pair(substitute(*b, query), pred)]) {
              queryResolutions[currentBlockAndQuery].insert(qr);
            }
            if (queryResolutions[currentBlockAndQuery].size() > currentNumberResultsForBlock) {
              for (BasicBlock* succ : successors(b)) {
                step2WorkList.insert(succ);
              }   
            }
          }
        }
      }

      // Step 3
      if (queryResolutions[std::make_pair(initialQuery, &basicBlock)].count(QueryTrue) > 0) {
        result.endSet[std::make_pair(&basicBlock, trueDestination)] = std::make_pair(initialQuery, QueryTrue);
      }

      if (queryResolutions[std::make_pair(initialQuery, &basicBlock)].count(QueryFalse) > 0) {
        result.endSet[std::make_pair(&basicBlock, falseDestination)] = std::make_pair(initialQuery, QueryFalse);
      }

      for (std::pair<BasicBlock*, std::vector<Query>> visitedNode : visited) {
        BasicBlock* b = visitedNode.first;
        for (Query query : visitedNode.second) {
          Query substitutedQuery = substitute(*b, query);
          for (BasicBlock* pred : predecessors(b)) {
            if (queryResolutions[std::make_pair(substitutedQuery, pred)].count(QueryTrue) > 0) {
              result.presentSet[std::make_pair(pred, b)] = std::make_pair(substitutedQuery, QueryTrue);
            }

            if (queryResolutions[std::make_pair(substitutedQuery, pred)].count(QueryFalse) > 0) {
              result.presentSet[std::make_pair(pred, b)] = std::make_pair(substitutedQuery, QueryFalse);
            }

            if (
                  queryResolutions[std::make_pair(substitutedQuery, pred)].count(QueryTrue) > 0 
                && queryResolutions[std::make_pair(substitutedQuery, pred)].size() == 1
                && queryResolutions[std::make_pair(query, b)].size() > 1
              ) {
              result.startSet[std::make_pair(pred, b)] = std::make_pair(substitutedQuery, QueryTrue);
            }
            else if (
                  queryResolutions[std::make_pair(substitutedQuery, pred)].count(QueryFalse) > 0 
                && queryResolutions[std::make_pair(substitutedQuery, pred)].size() == 1
                && queryResolutions[std::make_pair(query, b)].size() > 1
              ) {
              result.startSet[std::make_pair(pred, b)] = std::make_pair(substitutedQuery, QueryFalse);
            }
          }
        }
      }

    }

    Query substitute(BasicBlock& basicBlock, Query q) {
      for (BasicBlock::reverse_iterator iIter = basicBlock.rbegin(); iIter != basicBlock.rend(); ++iIter) {
        Instruction& i = *iIter;
        if (i.getOpcode() == Instruction::Store && i.getOperand(1) == q.lhs) {
          if (!isa<ConstantInt>(i.getOperand(0))) {
            q.lhs = i.getOperand(0);
          }
        }
        else if (q.lhs == &i) {
          if (i.getOpcode() == Instruction::Load) {
            q.lhs = i.getOperand(0);
          }
          else if (q.queryOperator == IsTrue) {
            if (i.getOpcode() == Instruction::Trunc) {
              TruncInst *truncInstruction = dyn_cast<TruncInst>(&i);
              if (truncInstruction->getSrcTy()->isIntegerTy() && truncInstruction->getDestTy()->isIntegerTy()) {
                IntegerType* integerType = dyn_cast<IntegerType>(truncInstruction->getDestTy());
                if (integerType->getBitWidth() == 1) {
                  q.lhs = i.getOperand(0);
                }
              }
            }
            else if (i.getOpcode() == Instruction::ICmp) {
              ICmpInst *cmpInstruction = dyn_cast<ICmpInst>(&i);

              if (isa<ConstantInt>(i.getOperand(0))) {
                if (!isa<ConstantInt>(i.getOperand(1))) {
                  q.lhs = i.getOperand(1);
                  q.rhs = dyn_cast<ConstantInt>(i.getOperand(0));
                  q.queryOperator = cmpInstruction->isEquality() ? AreEqual : AreNotEqual;
                }
              }
              else if (isa<ConstantInt>(i.getOperand(1))){
                if (!isa<ConstantInt>(i.getOperand(0))) {
                  q.lhs = i.getOperand(0);
                  q.rhs = dyn_cast<ConstantInt>(i.getOperand(1));
                  q.queryOperator = cmpInstruction->isEquality() ? AreEqual : AreNotEqual;
                }
              }
            }
          }
        }
      }
      return q;
    }


    bool resolve(BasicBlock& basicBlock, Query q, QueryResolution& resolution) {
      for (BasicBlock::reverse_iterator iIter = basicBlock.rbegin(); iIter != basicBlock.rend(); ++iIter) {
        Instruction& i = *iIter;
        if (i.getOpcode() == Instruction::Store && i.getOperand(1) == q.lhs) {

          if (isa<ConstantInt>(i.getOperand(0))) {
            auto *constantValue = dyn_cast<ConstantInt>(i.getOperand(0));

            if (q.queryOperator == isTrue) {
              if (constantValue->isZero()) {
                resolution = QueryFalse;
              }
              else {
                resolution = QueryTrue;
              }
              return true;
            }
            else if (q.queryOperator == AreEqual) {
              if (q.rhs == constantValue) {
                resolution = queryTrue;
              }
              else {
                resolution = QueryFalse;
              }
              return true;
            }
            else if (q.queryOperator == AreNotEqual) {
              if (q.rhs != constantValue) {
                resolution = queryTrue;
              }
              else {
                resolution = QueryFalse;
              }
              return true;
            }
            break;
          }
          else {
            q.lhs = i.getOperand(0);
          }
        }
        else if (q.lhs == &i) {
          if (i.getOpcode() == Instruction::Load) {
            q.lhs = i.getOperand(0);
          }
          else if (q.queryOperator == IsTrue) {
            if (i.getOpcode() == Instruction::Trunc) {
              TruncInst *truncInstruction = dyn_cast<TruncInst>(&i);
              if (truncInstruction->getSrcTy()->isIntegerTy() && truncInstruction->getDestTy()->isIntegerTy()) {
                IntegerType* integerType = dyn_cast<IntegerType>(truncInstruction->getDestTy());
                if (integerType->getBitWidth() == 1) {
                  q.lhs = i.getOperand(0);
                }
                else {
                  resolution = QueryUndefined;
                  return true;
                }
              }
            }
            else if (i.getOpcode() == Instruction::ICmp) {
              ICmpInst *cmpInstruction = dyn_cast<ICmpInst>(&i);

              if (isa<ConstantInt>(i.getOperand(0))) {
                if (isa<ConstantInt>(i.getOperand(1))) {
                  if (cmpInstruction->isEquality()) {
                    resolution = (i.getOperand(0) == i.getOperand(1)) ? QueryTrue : QueryFalse;
                  }
                  else {
                    resolution = (i.getOperand(0) != i.getOperand(1)) ? QueryTrue : QueryFalse; 
                  }
                  return true;
                }
                else {
                  q.lhs = i.getOperand(1);
                  q.rhs = dyn_cast<ConstantInt>(i.getOperand(0));
                  q.queryOperator = cmpInstruction->isEquality() ? AreEqual : AreNotEqual;
                }
              }
              else if (isa<ConstantInt>(i.getOperand(1))){
                if (isa<ConstantInt>(i.getOperand(0))) {
                  if (cmpInstruction->isEquality()) {
                    resolution = (i.getOperand(0) == i.getOperand(1)) ? QueryTrue : QueryFalse;
                  }
                  else {
                    resolution = (i.getOperand(0) != i.getOperand(1)) ? QueryTrue : QueryFalse; 
                  }
                  return true;
                }
                else {
                  q.lhs = i.getOperand(0);
                  q.rhs = dyn_cast<ConstantInt>(i.getOperand(1));
                  q.queryOperator = cmpInstruction->isEquality() ? AreEqual : AreNotEqual;
                }
              }
              else {
                resolution = QueryUndefined;
                return true;
              }
            }
          }
          else {
            resolution = QueryUndefined;
            return true;
          }
        }
      }
      return false;
    }

  };
}

#endif