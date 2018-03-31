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
#include <stack>
#include <tuple>
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
      originalQuery = nullptr;
    }

    ~Query() {
      if (originalQuery != nullptr) {
        delete originalQuery;
      }
    }

    Value* lhs;
    QueryOperator queryOperator;
    ConstantInt* rhs;
    bool isSummaryNodeQuery;
    Query* originalQuery;

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
    std::map<std::tuple<Node*, Node*, std::stack<Node*>>, std::set<std::pair<Query, QueryResolution>>> startSet;
    std::map<std::tuple<Node*, Node*, std::stack<Node*>>, std::set<std::pair<Query, QueryResolution>>> presentSet;
    std::map<std::tuple<Node*, Node*, std::stack<Node*>>, std::set<std::pair<Query, QueryResolution>>> endSet;
  };

  class InfeasiblePathDetector {
  private:
    std::map<std::pair<Query, Node*>, std::set<std::pair<QueryResolution, std::stack<Node*>>>> queryResolutions;
    std::set<std::pair<Query, Node*>> queriesResolvedInNode;
    Node* trueDestinationNode;
    Node* falseDestinationNode;

  public:
    InfeasiblePathDetector() {}

    void detectPaths(Node& initialNode, InfeasiblePathResult& result) {
      if (initialNode.endsWithConditionalBranch()) {
        return;
      }
      queryResolutions.clear();
      queriesResolvedInNode.clear();

      // Work list contains two nodes since whenever a query gets propagated up, it should continue to the proper call site so we save
      // the call site with it.
      std::queue<std::pair<Node*, Query>> worklist;
      std::map<Node*, std::vector<Query>> visited;

      Query initialQuery;
      initialQuery.lhs = initialNode.getBranchCondition();
      initialQuery.rhs = nullptr;
      initialQuery.isSummaryNodeQuery = false;
      initialQuery.queryOperator = IsTrue;


      worklist.push(std::make_pair(&initialNode, initialQuery));
      visited[&initialNode].push_back(initialQuery);

      trueDestinationNode = initialNode.getTrueEdge();
      falseDestinationNode = initialNode.getFalseEdge();

      

      std::map<std::pair<Function*, Query>, std::set<Query>> functionQueryCache;

      executeStepOne(worklist, visited, initialNode, initialQuery, result, functionQueryCache);
      
      // Step 2
      std::set<Node*> step2WorkList;
      for (std::pair<const std::pair<Query, Node*>, std::set<std::pair<QueryResolution, std::stack<Node*>>>> resolvedNode : queryResolutions) {
        Node* n = resolvedNode.first.second;
        for (Node* succ : n->getSuccessors()) {
          step2WorkList.insert(succ);
        }
      }

      while (step2WorkList.size() != 0) {
        std::set<Node*>::iterator nIter = step2WorkList.begin();
        Node* n = *nIter;
        step2WorkList.erase(nIter);

        for(Query query : visited[n]) {

          std::pair<Query, Node*> currentBlockAndQuery = std::make_pair(query, n);
          
          if (queriesResolvedInNode.count(currentBlockAndQuery) != 0) {
            continue;
          }

          std::map<Node*, Query> substituteMap;
          substitute(*n, query, substituteMap);
          for (Node* pred : n->getPredecessors()) {
            size_t currentNumberResultsForBlock = queryResolutions[currentBlockAndQuery].size();
            for(std::pair<QueryResolution, std::stack<Node*>> qr : queryResolutions[std::make_pair(substituteMap[pred], pred)]) {

              std::stack<Node*> stackCopy = qr.second;

              // prevent propagated queries from other call sites to this return point.
              if (pred->isExitOfFunction) {
                Node* callSiteOfExitedFunction = n->getPredecessorBypassingFunctionCall();
                if (qr.second.top() != nullptr && qr.second.top() != callSiteOfExitedFunction) {
                  continue;
                }
                if (qr.second.top() == callSiteOfExitedFunction) {
                  stackCopy.pop();
                }
              }

              // Make sure queries propagated to function calls are associated with the proper calling context.
              if (n->isEntryOfFunction) {
                Node* callSite = pred;
                stackCopy.push(callSite);
              }

              // make sure we don't have the same resolution twice in the same block. It's OK if the same resolution is there for different calling points
              // but the nullptr ensures that the results looked at are only those shared between all call sites.
              std::stack<Node*> emptyCallStack;
              emptyCallStack.push(nullptr);
              if (queryResolutions[currentBlockAndQuery].count(std::make_pair(qr.first, emptyCallStack)) == 0) {
                queryResolutions[currentBlockAndQuery].insert(std::make_pair(qr.first, stackCopy));
              }
            }
            if (queryResolutions[currentBlockAndQuery].size() > currentNumberResultsForBlock) {
              for (Node* succ : n->getSuccessors()) {
                step2WorkList.insert(succ);
              }   
            }
          }
        }
      }

      // Step 3
      std::stack<Node*> emptyCallStack;
      emptyCallStack.push(nullptr);
      if (queryResolutions[std::make_pair(initialQuery, &initialNode)].count(std::make_pair(QueryTrue, emptyCallStack)) > 0) {
        result.endSet[std::make_tuple(&initialNode, trueDestinationNode, emptyCallStack)].insert(std::make_pair(initialQuery, QueryTrue));
      }

      if (queryResolutions[std::make_pair(initialQuery, &initialNode)].count(std::make_pair(QueryFalse, emptyCallStack)) > 0) {
        result.endSet[std::make_tuple(&initialNode, falseDestinationNode, emptyCallStack)].insert(std::make_pair(initialQuery, QueryFalse));
      }

      for (std::pair<Node*, std::vector<Query>> visitedNode : visited) {
        Node* n = visitedNode.first;
        for (Query query : visitedNode.second) {

          std::map<Node*, Query> substituteMap;
          substitute(*n, query, substituteMap);
          for (Node* pred : n->getPredecessors()) {
            Query substitutedQuery = substituteMap[pred];

            std::set<std::stack<Node*>> uniqueCallStacks;
            for(std::pair<QueryResolution, std::stack<Node*>> qr : queryResolutions[std::make_pair(substitutedQuery, pred)]) {
              if (qr.first != QueryTrue && qr.first != QueryFalse) {
                continue;
              }
              result.presentSet[std::make_tuple(pred, n, qr.second)].insert(std::make_pair(substitutedQuery, qr.first));
              uniqueCallStacks.insert(qr.second);
            }

            for (std::stack<Node*> callStack : uniqueCallStacks) {
              if (callStack == emptyCallStack) {
                auto countNotTruePredicate = [](std::pair<QueryResolution, std::stack<Node*>> p) { return p.first != QueryTrue; };
                auto countNotFalsePredicate = [](std::pair<QueryResolution, std::stack<Node*>> p) { return p.first != QueryFalse; };
                if (
                    queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryTrue, callStack)) > 0 
                    && std::count_if(queryResolutions[std::make_pair(substitutedQuery, pred)].begin(), queryResolutions[std::make_pair(substitutedQuery, pred)].end(), countNotTruePredicate) == 0
                    && queryResolutions[std::make_pair(query, n)].size() > 1
                  ) {
                  result.startSet[std::make_tuple(pred, n, callStack)].insert(std::make_pair(substitutedQuery, QueryTrue));
                }
                else if (
                    queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryFalse, callStack)) > 0 
                    && std::count_if(queryResolutions[std::make_pair(substitutedQuery, pred)].begin(), queryResolutions[std::make_pair(substitutedQuery, pred)].end(), countNotFalsePredicate) == 0
                    && queryResolutions[std::make_pair(query, n)].size() > 1
                  ) {
                  result.startSet[std::make_tuple(pred, n, callStack)].insert(std::make_pair(substitutedQuery, QueryFalse));
                }
              }
              else {
                if (
                    queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryTrue, callStack)) > 0 
                    && queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryFalse, callStack)) == 0
                    && queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryUndefined, callStack)) ==  0
                    && queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryFalse, emptyCallStack)) == 0
                    && queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryUndefined, emptyCallStack)) ==  0
                    && queryResolutions[std::make_pair(query, n)].size() > 1
                  ) {
                  result.startSet[std::make_tuple(pred, n, callStack)].insert(std::make_pair(substitutedQuery, QueryTrue));
                }
                else if (
                    queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryFalse, callStack)) > 0 
                    && queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryTrue, callStack)) == 0
                    && queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryUndefined, callStack)) ==  0
                    && queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryTrue, emptyCallStack)) == 0
                    && queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryUndefined, emptyCallStack)) ==  0
                    && queryResolutions[std::make_pair(query, n)].size() > 1
                  ) {
                  result.startSet[std::make_tuple(pred, n, callStack)].insert(std::make_pair(substitutedQuery, QueryFalse));
                }
              }
            }
          }
        }
      }

    }

    void executeStepOne(std::queue<std::pair<Node*, Query>>& worklist, std::map<Node*, std::vector<Query>>& visited, Node& initialNode, 
                        Query initialQuery, InfeasiblePathResult& result, std::map<std::pair<Function*, Query>, std::set<Query>>& functionQueryCache) {
      while(worklist.size() != 0) {
        std::pair<Node*, Query> workItem = worklist.front();
        worklist.pop();

        Node* n = workItem.first;
        Query currentValue = workItem.second;

        QueryResolution resolution;
        if(!resolve(*n, currentValue, resolution)) {
          if (n == initialNode.getFunctionEntryNode()) {
            queriesResolvedInNode.insert(std::make_pair(currentValue, n));
            std::stack<Node*> callStack;
            callStack.push(nullptr);
            queryResolutions[std::make_pair(currentValue, n)].insert(std::make_pair(QueryUndefined, callStack));
          }

          std::map<Node*, Query> substituteMap;
          currentValue = substitute(*n, currentValue, substituteMap);
          

          if (n->isEntryOfFunction) {
            functionQueryCache[std::make_pair(n->basicBlock->getParent(), initialQuery)].insert(currentValue);
          }
          else {
            std::set<Node*> preds = n->getPredecessors();
            if (preds.size() > 0) {
              Node* p = *(preds.begin());
              if (p->isExitOfFunction) {
                std::queue<std::pair<Node*, Query>> worklistForFunction;
                for(Node* pred : preds) {
                  if (std::find(visited[pred].begin(), visited[pred].end(), substituteMap[pred]) == visited[pred].end()) {
                    visited[pred].push_back(substituteMap[pred]);
                    worklistForFunction.push(std::make_pair(pred, substituteMap[pred]));
                  }
                }
                if (worklistForFunction.size() > 0) {
                  executeStepOne(worklistForFunction, visited, initialNode, currentValue, result, functionQueryCache);
                }

                Function* functionCalled = p->basicBlock->getParent();
                Node* predecessor = n->getPredecessorBypassingFunctionCall();
                for(Query q : functionQueryCache[std::make_pair(functionCalled, currentValue)]) {
                  if (std::find(visited[predecessor].begin(), visited[predecessor].end(), q) == visited[predecessor].end()) {
                    visited[predecessor].push_back(q);
                    worklist.push(std::make_pair(predecessor, q));
                  }
                }

              }
              else {
                for(Node* pred : n->getPredecessors()) {
                  if (std::find(visited[pred].begin(), visited[pred].end(), substituteMap[pred]) == visited[pred].end()) {
                    visited[pred].push_back(substituteMap[pred]);
                    worklist.push(std::make_pair(pred, substituteMap[pred]));
                  }
                }
              }
            }
          }
        }
        else {
          queriesResolvedInNode.insert(std::make_pair(currentValue, n));
          std::stack<Node*> callStack;
          callStack.push(nullptr);
          queryResolutions[std::make_pair(currentValue, n)].insert(std::make_pair(resolution, callStack));

          // There is an edge case where the query may becomes resolved instantly. If this is case, just add the branch exit edges to all of the output sets.
          if (n == &initialNode && currentValue == initialQuery) {
            if (resolution == QueryTrue) {
              result.startSet[std::make_tuple(n, trueDestinationNode, callStack)].insert( std::make_pair(initialQuery, QueryTrue));
              result.presentSet[std::make_tuple(n, trueDestinationNode, callStack)].insert(std::make_pair(initialQuery, QueryTrue));
              result.endSet[std::make_tuple(n, trueDestinationNode, callStack)].insert(std::make_pair(initialQuery, QueryTrue));
            }
            else if (resolution == QueryFalse) {
              result.startSet[std::make_tuple(n, falseDestinationNode, callStack)].insert(std::make_pair(initialQuery, QueryFalse));
              result.presentSet[std::make_tuple(n, falseDestinationNode, callStack)].insert(std::make_pair(initialQuery, QueryFalse));
              result.endSet[std::make_tuple(n, falseDestinationNode, callStack)].insert(std::make_pair(initialQuery, QueryFalse));
            }
            break;
          }
        }
      }
    }

    Query substitute(Node& basicBlock, Query q, std::map<Node*, Query>& querySubstitutedToPreds) {
      for (Instruction* iIter : basicBlock.getReversedInstructions()) {
        Instruction& i = *iIter;
        if (i.getOpcode() == Instruction::Store && i.getOperand(1) == q.lhs) {
          if (!isa<ConstantInt>(i.getOperand(0))) {
            q.lhs = i.getOperand(0);
          }
        }
        else if (i.getOpcode() == Instruction::Call) {
          CallInst* callInst = dyn_cast<CallInst>(&i);
          Function* f = callInst->getCalledFunction();
          if (f != nullptr) {
            for(Node* n : basicBlock.getPredecessors()) {
              Query summaryQuery = q;
              summaryQuery.isSummaryNodeQuery = true;
              if (&i == q.lhs) {
                ReturnInst* returnInst = dyn_cast<ReturnInst>(n->getReversedInstructions().front());
                summaryQuery.lhs = returnInst->getReturnValue();
              }
              querySubstitutedToPreds[n] = summaryQuery;
              return q;
            }
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
      for (Node* n : basicBlock.getPredecessors()) {
        querySubstitutedToPreds[n] = q;
      }
      return q;
    }

    bool resolve(Node& basicBlock, Query q, QueryResolution& resolution) {
      for (Instruction* iIter : basicBlock.getReversedInstructions()) {
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
        else if (i.getOpcode() == Instruction::Call) {
          continue;
        }
        else if (i.getOpcode() == Instruction::Ret) {
          ReturnInst* returnInst = dyn_cast<ReturnInst>(&i);
          if (q.lhs == returnInst->getReturnValue() && isa<ConstantInt>(q.lhs)) {
            resolution = resolveConstantAssignment(dyn_cast<ConstantInt>(q.lhs), q);
            return true;
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