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

#include "Node.h"

using namespace llvm;

namespace {


  enum QueryOperator { 
    IsTrue, 
    AreEqual, 
    AreNotEqual, 
    IsSignedGreaterThan, 
    IsUnsignedGreaterThan, 
    IsSignedLessThan, 
    IsUnsignedLessThan, 
    IsSignedGreaterThanOrEqual, 
    IsUnsignedGreaterThanOrEqual,
    IsSignedLessThanOrEqual,
    IsUnsignedLessThanOrEqual
  };

  enum QueryResolution { QueryTrue, QueryFalse, QueryUndefined };

  struct Query
  {
    Query() 
    {
      lhs = nullptr;
      rhs = nullptr;
      queryOperator = IsTrue;
      isSummaryNodeQuery = false;
    }

    Value* lhs;
    QueryOperator queryOperator;
    ConstantInt* rhs;
    bool isSummaryNodeQuery;

    bool operator==(const Query& other) const {
      return this->lhs == other.lhs && this->rhs == other.rhs && this->queryOperator == other.queryOperator && this->isSummaryNodeQuery == other.isSummaryNodeQuery;
    }

    bool operator<(const Query& other) const {
      if (this->lhs == other.lhs) {
        if (this->queryOperator == other.queryOperator) {
          if (this->rhs == other.rhs) {
            if (this->isSummaryNodeQuery == other.isSummaryNodeQuery) {
              return false;
            }
            return this->isSummaryNodeQuery < other.isSummaryNodeQuery;
          }
          return this->rhs < other.rhs;
        }
        return this->queryOperator < other.queryOperator;
      }
      return this->lhs < other.lhs;
    }
  };

  struct  InfeasiblePathResult {
    std::map<std::pair<Node, Node>, std::set<std::pair<Query, QueryResolution>>> startSet;
    std::map<std::pair<Node, Node>, std::set<std::pair<Query, QueryResolution>>> presentSet;
    std::map<std::pair<Node, Node>, std::set<std::pair<Query, QueryResolution>>> endSet;
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

      std::queue<std::pair<Node, Query>> worklist;
      std::map<Node, std::vector<Query>> visited;

      Query initialQuery;
      initialQuery.lhs = terminator->getOperand(0);
      initialQuery.rhs = nullptr;
      initialQuery.isSummaryNodeQuery = false;
      initialQuery.queryOperator = IsTrue;

      Node initialNode(&basicBlock, findFunctionCallTopDown(&basicBlock));

      worklist.push(std::make_pair(initialNode, initialQuery));
      visited[initialNode].push_back(initialQuery);
      
      BasicBlock* trueDestination = dyn_cast<BasicBlock>(terminator->getOperand(2));
      BasicBlock* falseDestination = dyn_cast<BasicBlock>(terminator->getOperand(1));

      Node trueDestinationNode(trueDestination, findFunctionCallTopDown(trueDestination));
      Node falseDestinationNode(falseDestination, findFunctionCallTopDown(falseDestination));

      std::map<std::pair<Query, Node>, std::set<QueryResolution>> queryResolutions;
      std::set<std::pair<Query, Node>> queriesResolvedInNode;

      QueryResolution resolution;

      // Step 1
      while(worklist.size() != 0) {
        std::pair<Node, Query> workItem = worklist.front();
        worklist.pop();

        Node n = workItem.first;
        BasicBlock* b = n.basicBlock;
        Query currentValue = workItem.second;

        if(!resolve(*b, currentValue, resolution)) {
          if (b == &(b->getParent()->getEntryBlock())) {
            queriesResolvedInNode.insert(std::make_pair(currentValue, n));
            queryResolutions[std::make_pair(currentValue, n)].insert(QueryUndefined);
          }

          currentValue = substitute(*b, currentValue);
          for(Node pred : n.getPredecessors()) {
            if (std::find(visited[pred].begin(), visited[pred].end(), currentValue) == visited[pred].end()) {
              visited[pred].push_back(currentValue);
              worklist.push(std::make_pair(pred, currentValue));
            }
          }
        }
        else {
          queriesResolvedInNode.insert(std::make_pair(currentValue, n));
          queryResolutions[std::make_pair(currentValue, n)].insert(resolution);

          // There is an edge case where the query may becomes resolved instantly. If this is case, just add the branch exit edges to all of the output sets.
          if (b == &basicBlock && currentValue == initialQuery) {
            if (resolution == QueryTrue) {
              result.startSet[std::make_pair(n, trueDestinationNode)].insert( std::make_pair(initialQuery, QueryTrue));
              result.presentSet[std::make_pair(n, trueDestinationNode)].insert(std::make_pair(initialQuery, QueryTrue));
              result.endSet[std::make_pair(n, trueDestinationNode)].insert(std::make_pair(initialQuery, QueryTrue));
            }
            else if (resolution == QueryFalse) {
              result.startSet[std::make_pair(n, falseDestinationNode)].insert(std::make_pair(initialQuery, QueryFalse));
              result.presentSet[std::make_pair(n, falseDestinationNode)].insert(std::make_pair(initialQuery, QueryFalse));
              result.endSet[std::make_pair(n, falseDestinationNode)].insert(std::make_pair(initialQuery, QueryFalse));
            }
            return;
          }
        }
      }

      std::set<Node> step2WorkList;
      for (std::pair<const std::pair<Query, Node>, std::set<QueryResolution>> resolvedNode : queryResolutions) {
        Node n = resolvedNode.first.second;
        for (Node succ : n.getSuccessors()) {
          step2WorkList.insert(succ);
        }
      }

      // Step 2
      while (step2WorkList.size() != 0) {
        std::set<Node>::iterator nIter = step2WorkList.begin();
        Node n = *nIter;
        step2WorkList.erase(nIter);

        for(Query query : visited[n]) {

          std::pair<Query, Node> currentBlockAndQuery = std::make_pair(query, n);
          
          if (queriesResolvedInNode.count(currentBlockAndQuery) != 0) {
            continue;
          }

          for (Node pred : n.getPredecessors()) {
            size_t currentNumberResultsForBlock = queryResolutions[currentBlockAndQuery].size();
            for(QueryResolution qr : queryResolutions[std::make_pair(substitute(*(n.basicBlock), query), pred)]) {
              queryResolutions[currentBlockAndQuery].insert(qr);
            }
            if (queryResolutions[currentBlockAndQuery].size() > currentNumberResultsForBlock) {
              for (Node succ : n.getSuccessors()) {
                step2WorkList.insert(succ);
              }   
            }
          }
        }
      }

      // Step 3
      if (queryResolutions[std::make_pair(initialQuery, initialNode)].count(QueryTrue) > 0) {
        result.endSet[std::make_pair(initialNode, trueDestinationNode)].insert(std::make_pair(initialQuery, QueryTrue));
      }

      if (queryResolutions[std::make_pair(initialQuery, initialNode)].count(QueryFalse) > 0) {
        result.endSet[std::make_pair(initialNode, falseDestinationNode)].insert(std::make_pair(initialQuery, QueryFalse));
      }

      for (std::pair<Node, std::vector<Query>> visitedNode : visited) {
        Node n = visitedNode.first;
        for (Query query : visitedNode.second) {
          Query substitutedQuery = substitute(*(n.basicBlock), query);
          for (Node pred : n.getPredecessors()) {
            if (queryResolutions[std::make_pair(substitutedQuery, pred)].count(QueryTrue) > 0) {
              result.presentSet[std::make_pair(pred, n)].insert(std::make_pair(substitutedQuery, QueryTrue));
            }

            if (queryResolutions[std::make_pair(substitutedQuery, pred)].count(QueryFalse) > 0) {
              result.presentSet[std::make_pair(pred, n)].insert(std::make_pair(substitutedQuery, QueryFalse));
            }

            if (
                  queryResolutions[std::make_pair(substitutedQuery, pred)].count(QueryTrue) > 0 
                && queryResolutions[std::make_pair(substitutedQuery, pred)].size() == 1
                && queryResolutions[std::make_pair(query, n)].size() > 1
              ) {
              result.startSet[std::make_pair(pred, n)].insert(std::make_pair(substitutedQuery, QueryTrue));
            }
            else if (
                  queryResolutions[std::make_pair(substitutedQuery, pred)].count(QueryFalse) > 0 
                && queryResolutions[std::make_pair(substitutedQuery, pred)].size() == 1
                && queryResolutions[std::make_pair(query, n)].size() > 1
              ) {
              result.startSet[std::make_pair(pred, n)].insert(std::make_pair(substitutedQuery, QueryFalse));
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
                  q.queryOperator = reverseComparison(getQueryOperatorForPredicate(cmpInstruction->getPredicate()));
                }
              }
              else if (isa<ConstantInt>(i.getOperand(1))){
                if (!isa<ConstantInt>(i.getOperand(0))) {
                  q.lhs = i.getOperand(0);
                  q.rhs = dyn_cast<ConstantInt>(i.getOperand(1));
                  q.queryOperator = getQueryOperatorForPredicate(cmpInstruction->getPredicate());
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
            resolution = resolveConstantAssignment(constantValue, q);
            return true;
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
                  resolution = getQueryResolutionForConstantComparison(*cmpInstruction);
                  return true;
                }
                else {
                  q.lhs = i.getOperand(1);
                  q.rhs = dyn_cast<ConstantInt>(i.getOperand(0));
                  q.queryOperator = reverseComparison(getQueryOperatorForPredicate(cmpInstruction->getPredicate()));
                }
              }
              else if (isa<ConstantInt>(i.getOperand(1))){
                if (isa<ConstantInt>(i.getOperand(0))) {
                  resolution = getQueryResolutionForConstantComparison(*cmpInstruction);
                  return true;
                }
                else {
                  q.lhs = i.getOperand(0);
                  q.rhs = dyn_cast<ConstantInt>(i.getOperand(1));
                  q.queryOperator = getQueryOperatorForPredicate(cmpInstruction->getPredicate());
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

    QueryResolution resolveConstantAssignment(ConstantInt* constant, Query& q) {
      switch(q.queryOperator)
      {
        case IsTrue: return (constant->isZero()) ? QueryTrue : QueryFalse;
        case AreEqual: return (q.rhs->getValue() == constant->getValue()) ? QueryFalse : QueryTrue;
        case AreNotEqual: return (q.rhs->getValue() != constant->getValue()) ? QueryFalse : QueryTrue;
        default: return getQueryResolutionForConstantComparison(constant, q.rhs, q.queryOperator);
      }
    }

    QueryResolution getQueryResolutionForConstantComparison(ICmpInst& i) {
      ConstantInt* c1 = dyn_cast<ConstantInt>(i.getOperand(0));
      ConstantInt* c2 = dyn_cast<ConstantInt>(i.getOperand(1));
      return getQueryResolutionForConstantComparison(c1, c2, getQueryOperatorForPredicate(i.getPredicate()));
    }

    QueryResolution getQueryResolutionForConstantComparison(ConstantInt* c1, ConstantInt* c2, QueryOperator qOp) {
      switch(qOp)
      {
        case AreEqual: return (c1->getValue() == c2->getValue()) ? QueryFalse : QueryTrue;
        case AreNotEqual: return (c1->getValue() != c2->getValue()) ? QueryFalse : QueryTrue; 
        case IsSignedGreaterThan: return (c1->getValue().sgt(c2->getValue())) ? QueryFalse : QueryTrue;
        case IsUnsignedGreaterThan: return (c1->getValue().ugt(c2->getValue())) ? QueryFalse : QueryTrue;
        case IsSignedGreaterThanOrEqual: return (c1->getValue().sge(c2->getValue())) ? QueryFalse : QueryTrue;
        case IsUnsignedGreaterThanOrEqual: return (c1->getValue().uge(c2->getValue())) ? QueryFalse : QueryTrue;
        case IsSignedLessThan: return (c1->getValue().slt(c2->getValue())) ? QueryFalse : QueryTrue;
        case IsUnsignedLessThan: return (c1->getValue().ult(c2->getValue())) ? QueryFalse : QueryTrue;
        case IsSignedLessThanOrEqual: return (c1->getValue().sle(c2->getValue())) ? QueryFalse : QueryTrue;
        case IsUnsignedLessThanOrEqual: return (c1->getValue().ule(c2->getValue())) ? QueryFalse : QueryTrue;
        default: return QueryUndefined;
      }
    }

    QueryOperator getQueryOperatorForPredicate(ICmpInst::Predicate p) {
      switch(p) {
        case ICmpInst::ICMP_EQ: return AreEqual;
        case ICmpInst::ICMP_NE: return AreNotEqual;
        case ICmpInst::ICMP_SGT: return IsSignedGreaterThan;
        case ICmpInst::ICMP_UGT: return IsUnsignedGreaterThan;
        case ICmpInst::ICMP_SGE: return IsSignedGreaterThanOrEqual;
        case ICmpInst::ICMP_UGE: return IsUnsignedGreaterThanOrEqual;
        case ICmpInst::ICMP_SLT: return IsSignedLessThan;
        case ICmpInst::ICMP_ULT: return IsUnsignedLessThan;
        case ICmpInst::ICMP_SLE: return IsSignedLessThanOrEqual;
        case ICmpInst::ICMP_ULE: return IsUnsignedLessThanOrEqual;
        default: return IsTrue;
      }
    }

    QueryOperator reverseComparison(QueryOperator qOp) {
      switch(qOp)
      {
        case AreEqual: return AreEqual;
        case AreNotEqual: return AreNotEqual;
        case IsSignedGreaterThan: return IsSignedLessThanOrEqual;
        case IsUnsignedGreaterThan: return IsUnsignedLessThanOrEqual;
        case IsSignedGreaterThanOrEqual: return IsSignedLessThan;
        case IsUnsignedGreaterThanOrEqual: return IsUnsignedLessThan;
        case IsSignedLessThan: return IsUnsignedGreaterThanOrEqual;
        case IsUnsignedLessThan: return IsUnsignedGreaterThanOrEqual;
        case IsSignedLessThanOrEqual: return IsSignedGreaterThan;
        case IsUnsignedLessThanOrEqual: return IsUnsignedGreaterThan;
        default: return IsTrue;
      }
    }

  };
}

#endif