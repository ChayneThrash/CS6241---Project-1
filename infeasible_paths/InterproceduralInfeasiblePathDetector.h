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

  bool checkIfStackIsSubset(std::stack<Node*> potentialSuper, std::stack<Node*> potentialSub) {
    if (potentialSub.size() > potentialSuper.size()) {
      return false;
    }

    for(int i = 0; i < potentialSub.size(); ++i) {
      if (potentialSuper.top() != potentialSub.top()) {
        return false;
      }
      potentialSuper.pop();
      potentialSub.pop();
    }
    return true;
  }

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
    std::stack<std::pair<unsigned, ConstantInt*>> intermediateOperations;

    bool operator==(const Query& other) const {
      return this->lhs == other.lhs && this->rhs == other.rhs && this->queryOperator == other.queryOperator && this->isSummaryNodeQuery == other.isSummaryNodeQuery && this->intermediateOperations == other.intermediateOperations;
    }

    bool operator<(const Query& other) const {
      if (this->lhs == other.lhs) {
        if (this->queryOperator == other.queryOperator) {
          if (this->rhs == other.rhs) {
            if (this->isSummaryNodeQuery == other.isSummaryNodeQuery) {
              if (this->intermediateOperations == other.intermediateOperations) {
                return false;
              }
              return this->intermediateOperations < other.intermediateOperations;
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
    std::map<std::pair<Node*, Node*>, std::set<std::tuple<Query, QueryResolution, std::stack<Node*>>>> startSet;
    std::map<std::pair<Node*, Node*>, std::set<std::tuple<Query, QueryResolution, std::stack<Node*>>>> presentSet;
    std::map<std::pair<Node*, Node*>, std::set<std::tuple<Query, QueryResolution, std::stack<Node*>>>> endSet;

    std::set<std::pair<Query, QueryResolution>> getStartSetFor(std::tuple<Node*, Node*, std::stack<Node*>> key) {
      return querySet(key, startSet);
    }

    std::set<std::pair<Query, QueryResolution>> getPresentSetFor(std::tuple<Node*, Node*, std::stack<Node*>> key) {
      return querySet(key, presentSet);
    }

    std::set<std::pair<Query, QueryResolution>> getEndSetFor(std::tuple<Node*, Node*, std::stack<Node*>> key) {
      return querySet(key, endSet);
    }

  private:
    std::set<std::pair<Query, QueryResolution>> querySet(std::tuple<Node*, Node*, std::stack<Node*>>& key, std::map<std::pair<Node*, Node*>, std::set<std::tuple<Query, QueryResolution, std::stack<Node*>>>>& s) {
      auto matchesKey = [&key](std::tuple<Query, QueryResolution, std::stack<Node*>> t) { return checkIfStackIsSubset(std::get<2>(key), std::get<2>(t)); };

      const auto& resultsToCheck = s[std::make_pair(std::get<0>(key), std::get<1>(key))];

      std::set<std::tuple<Query, QueryResolution, std::stack<Node*>>>::iterator result = std::find_if(resultsToCheck.begin(), resultsToCheck.end(), matchesKey);

      std::set<std::pair<Query, QueryResolution>> resultSet;
      while(result != resultsToCheck.end()) {
        auto r = *result;
        resultSet.insert(std::make_pair(std::get<0>(r), std::get<1>(r)));
        result = std::find_if(++result, resultsToCheck.end(), matchesKey);
      }
      return resultSet;
    }

  };

  class InfeasiblePathDetector {
  private:
    std::map<std::pair<Query, Node*>, std::set<std::pair<QueryResolution, std::stack<Node*>>>> queryResolutions;
    std::set<std::pair<Query, Node*>> queriesResolvedInNode;
    Node* trueDestinationNode;
    Node* falseDestinationNode;
    BasicBlock* topMostBasicBlock;
    Node* initialNode;
    std::set<Query> queriesPropagatedToCallers;

  public:
    InfeasiblePathDetector() {}

    void detectPaths(Node& incomingNode, InfeasiblePathResult& result, Module& m) {
      initialNode = &incomingNode;
      if (!initialNode->endsWithConditionalBranch()) {
        return;
      }
      Function* main = m.getFunction("main");
      if (main == nullptr) {
        errs() << "ERROR: could not get main...\n";
      }
      topMostBasicBlock = &(main->getEntryBlock());

      queryResolutions.clear();
      queriesResolvedInNode.clear();
      queriesPropagatedToCallers.clear();

      // Work list contains two nodes since whenever a query gets propagated up, it should continue to the proper call site so we save
      // the call site with it.
      std::queue<std::tuple<Node*, Query, std::stack<Node*>>> worklist;
      std::map<Node*, std::vector<Query>> visited;

      Query initialQuery;
      initialQuery.lhs = initialNode->getBranchCondition();
      initialQuery.rhs = nullptr;
      initialQuery.isSummaryNodeQuery = false;
      initialQuery.queryOperator = IsTrue;


      worklist.push(std::make_tuple(initialNode, initialQuery, std::stack<Node*>()));
      visited[initialNode].push_back(initialQuery);

      trueDestinationNode = initialNode->getTrueEdge();
      falseDestinationNode = initialNode->getFalseEdge();

      std::map<std::pair<Function*, Query>, std::set<Query>> functionQueryCache;

      executeStepOne(worklist, visited, initialQuery, result, functionQueryCache);
      
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
                if (qr.second.size() != 0 && qr.second.top() != callSiteOfExitedFunction) {
                  continue;
                }
                if (qr.second.size() != 0 && qr.second.top() == callSiteOfExitedFunction) {
                  stackCopy.pop();
                }
              }

              // Make sure queries propagated to function calls are associated with the proper calling context.
              if (n->isEntryOfFunction) {
                if (n->basicBlock->getParent() != initialNode->basicBlock->getParent() || queriesPropagatedToCallers.count(substituteMap[pred]) == 0) {
                  Node* callSite = pred;
                  stackCopy.push(callSite);
                }
              }

              // make sure we don't have the same resolution twice in the same block. It's OK if the same resolution is there for different calling points
              // but the nullptr ensures that the results looked at are only those shared between all call sites.
              std::stack<Node*> emptyCallStack;
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
      if (queryResolutions[std::make_pair(initialQuery, initialNode)].count(std::make_pair(QueryTrue, emptyCallStack)) > 0) {
        result.endSet[std::make_pair(initialNode, trueDestinationNode)].insert(std::make_tuple(initialQuery, QueryTrue, emptyCallStack));
        visited[trueDestinationNode].push_back(initialQuery);
      }

      if (queryResolutions[std::make_pair(initialQuery, initialNode)].count(std::make_pair(QueryFalse, emptyCallStack)) > 0) {
        result.endSet[std::make_pair(initialNode, falseDestinationNode)].insert(std::make_tuple(initialQuery, QueryFalse, emptyCallStack));
        visited[falseDestinationNode].push_back(initialQuery);
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
              result.presentSet[std::make_pair(pred, n)].insert(std::make_tuple(substitutedQuery, qr.first, qr.second));
              uniqueCallStacks.insert(qr.second);
            }

            for (std::stack<Node*> callStack : uniqueCallStacks) {
              if (callStack == emptyCallStack) {
                auto countNotTruePredicate = [](std::pair<QueryResolution, std::stack<Node*>> p) { return p.first != QueryTrue; };
                auto countNotFalsePredicate = [](std::pair<QueryResolution, std::stack<Node*>> p) { return p.first != QueryFalse; };
                if (
                    queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryTrue, callStack)) > 0 
                    && std::count_if(queryResolutions[std::make_pair(substitutedQuery, pred)].begin(), queryResolutions[std::make_pair(substitutedQuery, pred)].end(), countNotTruePredicate) == 0
                    && (queryResolutions[std::make_pair(query, n)].size() > 1 || n == trueDestinationNode)
                  ) {
                  result.startSet[std::make_pair(pred, n)].insert(std::make_tuple(substitutedQuery, QueryTrue, callStack));
                }
                else if (
                    queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryFalse, callStack)) > 0 
                    && std::count_if(queryResolutions[std::make_pair(substitutedQuery, pred)].begin(), queryResolutions[std::make_pair(substitutedQuery, pred)].end(), countNotFalsePredicate) == 0
                    && (queryResolutions[std::make_pair(query, n)].size() > 1 || n == falseDestinationNode)
                  ) {
                  result.startSet[std::make_pair(pred, n)].insert(std::make_tuple(substitutedQuery, QueryFalse, callStack));
                }
              }
              else {
                auto countNotTruePredicate = [&callStack](std::pair<QueryResolution, std::stack<Node*>> p) { return p.first != QueryTrue && checkIfStackIsSubset(callStack, p.second); };
                auto countNotFalsePredicate = [&callStack](std::pair<QueryResolution, std::stack<Node*>> p) { return p.first != QueryFalse && checkIfStackIsSubset(callStack, p.second); };
                if (
                    queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryTrue, callStack)) > 0 
                    && std::count_if(queryResolutions[std::make_pair(substitutedQuery, pred)].begin(), queryResolutions[std::make_pair(substitutedQuery, pred)].end(), countNotTruePredicate) == 0
                    && queryResolutions[std::make_pair(query, n)].size() > 1
                  ) {
                  result.startSet[std::make_pair(pred, n)].insert(std::make_tuple(substitutedQuery, QueryTrue, callStack));
                }
                else if (
                    queryResolutions[std::make_pair(substitutedQuery, pred)].count(std::make_pair(QueryFalse, callStack)) > 0 
                    && std::count_if(queryResolutions[std::make_pair(substitutedQuery, pred)].begin(), queryResolutions[std::make_pair(substitutedQuery, pred)].end(), countNotFalsePredicate) == 0
                    && queryResolutions[std::make_pair(query, n)].size() > 1
                  ) {
                  result.startSet[std::make_pair(pred, n)].insert(std::make_tuple(substitutedQuery, QueryFalse, callStack));
                }
              }
            }
          }
        }
      }

    }

    void executeStepOne(std::queue<std::tuple<Node*, Query, std::stack<Node*>>>& worklist, std::map<Node*, std::vector<Query>>& visited, 
                        Query initialQuery, InfeasiblePathResult& result, std::map<std::pair<Function*, Query>, std::set<Query>>& functionQueryCache) {
      while(worklist.size() != 0) {
        std::tuple<Node*, Query, std::stack<Node*>> workItem = worklist.front();
        worklist.pop();

        Node* n = std::get<0>(workItem);
        Query currentValue = std::get<1>(workItem);
        std::stack<Node*> callStack = std::get<2>(workItem);

        QueryResolution resolution;
        if(!resolve(*n, currentValue, resolution)) {
          if (n->basicBlock == topMostBasicBlock && n->programPointInBlock == findFunctionCallTopDown(topMostBasicBlock)) {
            queriesResolvedInNode.insert(std::make_pair(currentValue, n));
            std::stack<Node*> emptyCallStack;
            queryResolutions[std::make_pair(currentValue, n)].insert(std::make_pair(QueryUndefined, emptyCallStack));
          }

          std::map<Node*, Query> substituteMap;
          currentValue = substitute(*n, currentValue, substituteMap);
          

          if (n->isEntryOfFunction) {
            // Reached the starting point of the function under analysis.
            if (callStack.size() == 0) {
              for(Node* pred : n->getPredecessors()) {
                queriesPropagatedToCallers.insert(substituteMap[pred]);
                if (std::find(visited[pred].begin(), visited[pred].end(), substituteMap[pred]) == visited[pred].end()) {
                  visited[pred].push_back(substituteMap[pred]);
                  worklist.push(std::make_tuple(pred, substituteMap[pred], callStack));
                }
              }
            }
            else {
              functionQueryCache[std::make_pair(n->basicBlock->getParent(), initialQuery)].insert(currentValue);
            }
          }
          else {
            std::set<Node*> preds = n->getPredecessors();
            if (preds.size() > 0) {
              Node* p = *(preds.begin());
              if (p->isExitOfFunction) {
                std::queue<std::tuple<Node*, Query, std::stack<Node*>>> worklistForFunction;
                callStack.push(n->getPredecessorBypassingFunctionCall());
                for(Node* pred : preds) {
                  if (std::find(visited[pred].begin(), visited[pred].end(), substituteMap[pred]) == visited[pred].end()) {
                    visited[pred].push_back(substituteMap[pred]);
                    worklistForFunction.push(std::make_tuple(pred, substituteMap[pred], callStack));
                  }
                }
                if (worklistForFunction.size() > 0) {
                  executeStepOne(worklistForFunction, visited, currentValue, result, functionQueryCache);
                }

                Function* functionCalled = p->basicBlock->getParent();
                Node* predecessor = n->getPredecessorBypassingFunctionCall();
                callStack.pop();
                for(Query q : functionQueryCache[std::make_pair(functionCalled, currentValue)]) {
                  if (std::find(visited[predecessor].begin(), visited[predecessor].end(), q) == visited[predecessor].end()) {
                    visited[predecessor].push_back(q);
                    worklist.push(std::make_tuple(predecessor, q, callStack));
                  }
                }

              }
              else {
                for(Node* pred : n->getPredecessors()) {
                  if (std::find(visited[pred].begin(), visited[pred].end(), substituteMap[pred]) == visited[pred].end()) {
                    visited[pred].push_back(substituteMap[pred]);
                    worklist.push(std::make_tuple(pred, substituteMap[pred], callStack));
                  }
                }
              }
            }
          }
        }
        else {
          queriesResolvedInNode.insert(std::make_pair(currentValue, n));
          std::stack<Node*> emptyCallStack;
          queryResolutions[std::make_pair(currentValue, n)].insert(std::make_pair(resolution, emptyCallStack));

          // There is an edge case where the query may becomes resolved instantly. If this is case, just add the branch exit edges to all of the output sets.
          if (n == initialNode && currentValue == initialQuery) {
            if (resolution == QueryTrue) {
              result.startSet[std::make_pair(n, trueDestinationNode)].insert( std::make_tuple(initialQuery, QueryTrue, emptyCallStack));
              result.presentSet[std::make_pair(n, trueDestinationNode)].insert(std::make_tuple(initialQuery, QueryTrue, emptyCallStack));
              result.endSet[std::make_pair(n, trueDestinationNode)].insert(std::make_tuple(initialQuery, QueryTrue, emptyCallStack));
            }
            else if (resolution == QueryFalse) {
              result.startSet[std::make_pair(n, falseDestinationNode)].insert(std::make_tuple(initialQuery, QueryFalse, emptyCallStack));
              result.presentSet[std::make_pair(n, falseDestinationNode)].insert(std::make_tuple(initialQuery, QueryFalse, emptyCallStack));
              result.endSet[std::make_pair(n, falseDestinationNode)].insert(std::make_tuple(initialQuery, QueryFalse, emptyCallStack));
            }
            break;
          }
        }
      }
    }

    Query substitute(Node& basicBlock, Query q, std::map<Node*, Query>& querySubstitutedToPreds) {
      return getSubstitutedQueries(basicBlock, q, querySubstitutedToPreds).back();
    }

    std::vector<Query> getSubstitutedQueries(Node& basicBlock, Query q, std::map<Node*, Query>& querySubstitutedToPreds) {
      std::vector<Query> substituedQueries;
      substituedQueries.push_back(q);
      for (Instruction* iIter : basicBlock.getReversedInstructions()) {
        Instruction& i = *iIter;
        if (i.getOpcode() == Instruction::Store && i.getOperand(1) == q.lhs) {
          if (!isa<ConstantInt>(i.getOperand(0))) {
            q.lhs = i.getOperand(0);
            substituedQueries.push_back(q);
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
              return substituedQueries;
            }
          }
        }
        else if (q.lhs == &i) {
          if (i.getOpcode() == Instruction::Load) {
            q.lhs = i.getOperand(0);
            substituedQueries.push_back(q);
          }
          else if (q.queryOperator == IsTrue) {
            if (i.getOpcode() == Instruction::Trunc) {
              TruncInst *truncInstruction = dyn_cast<TruncInst>(&i);
              if (truncInstruction->getSrcTy()->isIntegerTy() && truncInstruction->getDestTy()->isIntegerTy()) {
                IntegerType* integerType = dyn_cast<IntegerType>(truncInstruction->getDestTy());
                if (integerType->getBitWidth() == 1) {
                  q.lhs = i.getOperand(0);
                  substituedQueries.push_back(q);
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
                  substituedQueries.push_back(q);
                }
              }
              else if (isa<ConstantInt>(i.getOperand(1))){
                if (!isa<ConstantInt>(i.getOperand(0))) {
                  q.lhs = i.getOperand(0);
                  q.rhs = dyn_cast<ConstantInt>(i.getOperand(1));
                  q.queryOperator = getQueryOperatorForPredicate(cmpInstruction->getPredicate());
                  substituedQueries.push_back(q);
                }
              }
            }
          }
          else if (i.getOpcode() == Instruction::Add) {

            Value* op1 = i.getOperand(0);
            Value* op2 = i.getOperand(1);

            if (isa<ConstantInt>(op1)) {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op1)));
              q.lhs = op2;
            }
            else {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op2)));
              q.lhs = op1;
            }
          }
          else if (i.getOpcode() == Instruction::Sub) {

            Value* op1 = i.getOperand(0);
            Value* op2 = i.getOperand(1);

            if (isa<ConstantInt>(op1)) {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op1)));
              q.lhs = op2;
            }
            else {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op2)));
              q.lhs = op1;
            }
          }
          else if (i.getOpcode() == Instruction::Mul) {

            Value* op1 = i.getOperand(0);
            Value* op2 = i.getOperand(1);

            if (isa<ConstantInt>(op1)) {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op1)));
              q.lhs = op2;
            }
            else {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op2)));
              q.lhs = op1;
            }
          }
          else if (i.getOpcode() == Instruction::UDiv) {

            Value* op1 = i.getOperand(0);
            Value* op2 = i.getOperand(1);

            if (isa<ConstantInt>(op1)) {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op1)));
              q.lhs = op2;
            }
            else {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op2)));
              q.lhs = op1;
            }
          }
          else if (i.getOpcode() == Instruction::SDiv) {

            Value* op1 = i.getOperand(0);
            Value* op2 = i.getOperand(1);

            if (isa<ConstantInt>(op1)) {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op1)));
              q.lhs = op2;
            }
            else {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op2)));
              q.lhs = op1;
            }
          }
        }
      }
      for (Node* n : basicBlock.getPredecessors()) {
        querySubstitutedToPreds[n] = q;
      }
      return substituedQueries;
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
        else if (isDereferenceOf(q.lhs, &i)) {
          if (q.queryOperator == IsTrue) {
            resolution = QueryFalse;
            return true;
          }
        }
        else if (i.getOpcode() == Instruction::Call) {
          CallInst* callInst = dyn_cast<CallInst>(&i);
          Function* f = callInst->getCalledFunction();
          if ((f == nullptr || f->isDeclaration()) && isa<GlobalVariable>(q.lhs)) {
            resolution = QueryUndefined;
            return true;
          }
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
          else if (i.getOpcode() == Instruction::Add) {

            Value* op1 = i.getOperand(0);
            Value* op2 = i.getOperand(1);

            if (!isa<ConstantInt>(op1) && !isa<ConstantInt>(op2)) {
              resolution = QueryUndefined;
              return true;
            }

            if (isa<ConstantInt>(op1) && isa<ConstantInt>(op1)) {
              ConstantInt* c1 = dyn_cast<ConstantInt>(op1);
              ConstantInt* c2 = dyn_cast<ConstantInt>(op2);

              APInt addResult = c1->getValue() + c2->getValue();
              resolution = resolveConstantAssignment(addResult, q);
              return true;
            }

            if (isa<ConstantInt>(op1)) {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op1)));
              q.lhs = op2;
            }
            else {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op2)));
              q.lhs = op1;
            }
          }
          else if (i.getOpcode() == Instruction::Sub) {

            Value* op1 = i.getOperand(0);
            Value* op2 = i.getOperand(1);

            if (!isa<ConstantInt>(op1) && !isa<ConstantInt>(op2)) {
              resolution = QueryUndefined;
              return true;
            }

            if (isa<ConstantInt>(op1) && isa<ConstantInt>(op1)) {
              ConstantInt* c1 = dyn_cast<ConstantInt>(op1);
              ConstantInt* c2 = dyn_cast<ConstantInt>(op2);

              APInt addResult = c1->getValue() - c2->getValue();
              resolution = resolveConstantAssignment(addResult, q);
              return true;
            }

            if (isa<ConstantInt>(op1)) {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op1)));
              q.lhs = op2;
            }
            else {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op2)));
              q.lhs = op1;
            }
          }
          else if (i.getOpcode() == Instruction::Mul) {

            Value* op1 = i.getOperand(0);
            Value* op2 = i.getOperand(1);

            if (!isa<ConstantInt>(op1) && !isa<ConstantInt>(op2)) {
              resolution = QueryUndefined;
              return true;
            }

            if (isa<ConstantInt>(op1) && isa<ConstantInt>(op1)) {
              ConstantInt* c1 = dyn_cast<ConstantInt>(op1);
              ConstantInt* c2 = dyn_cast<ConstantInt>(op2);

              APInt multResult = c1->getValue() * c2->getValue();
              resolution = resolveConstantAssignment(multResult, q);
              return true;
            }

            if (isa<ConstantInt>(op1)) {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op1)));
              q.lhs = op2;
            }
            else {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op2)));
              q.lhs = op1;
            }
          }
          else if (i.getOpcode() == Instruction::UDiv) {

            Value* op1 = i.getOperand(0);
            Value* op2 = i.getOperand(1);

            if (!isa<ConstantInt>(op1) && !isa<ConstantInt>(op2)) {
              resolution = QueryUndefined;
              return true;
            }

            if (isa<ConstantInt>(op1) && isa<ConstantInt>(op1)) {
              ConstantInt* c1 = dyn_cast<ConstantInt>(op1);
              ConstantInt* c2 = dyn_cast<ConstantInt>(op2);

              APInt multResult = c1->getValue().udiv(c2->getValue());
              resolution = resolveConstantAssignment(multResult, q);
              return true;
            }

            if (isa<ConstantInt>(op1)) {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op1)));
              q.lhs = op2;
            }
            else {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op2)));
              q.lhs = op1;
            }
          }
          else if (i.getOpcode() == Instruction::SDiv) {

            Value* op1 = i.getOperand(0);
            Value* op2 = i.getOperand(1);

            if (!isa<ConstantInt>(op1) && !isa<ConstantInt>(op2)) {
              resolution = QueryUndefined;
              return true;
            }

            if (isa<ConstantInt>(op1) && isa<ConstantInt>(op1)) {
              ConstantInt* c1 = dyn_cast<ConstantInt>(op1);
              ConstantInt* c2 = dyn_cast<ConstantInt>(op2);

              APInt multResult = c1->getValue().sdiv(c2->getValue());
              resolution = resolveConstantAssignment(multResult, q);
              return true;
            }

            if (isa<ConstantInt>(op1)) {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op1)));
              q.lhs = op2;
            }
            else {
              q.intermediateOperations.push(std::make_pair(i.getOpcode(), dyn_cast<ConstantInt>(op2)));
              q.lhs = op1;
            }
          }
          else {
            resolution = QueryUndefined;
            return true;
          }
        }
      }
      if (basicBlock.basicBlock == topMostBasicBlock && basicBlock.programPointInBlock == findFunctionCallTopDown(topMostBasicBlock)) {
        if (isa<GlobalVariable>(q.lhs)) {
          GlobalVariable* global = dyn_cast<GlobalVariable>(q.lhs);
          if (isa<ConstantInt>(global->getInitializer())) {
            resolution = resolveConstantAssignment(dyn_cast<ConstantInt>(global->getInitializer()), q);
            return true;
          }
        }
      }

      if (basicBlock.getPredecessors().size() == 1) {
        if ((*(basicBlock.getPredecessors().begin()))->endsWithConditionalBranch()) {
          if (priorConditionGuaranteesCurrent(&basicBlock, (*(basicBlock.getPredecessors().begin())), q)) {
            resolution = QueryFalse;
            return true;
          }
        }
      }

      return false;
    }

    QueryResolution resolveConstantAssignment(ConstantInt* constant, Query& q) {
      return resolveConstantAssignment(constant->getValue(), q);
    }

    QueryResolution resolveConstantAssignment(const APInt& constant, Query& q) {
      std::stack<std::pair<unsigned, ConstantInt*>> intermediateOperations = q.intermediateOperations;
      APInt value = constant;
      while (intermediateOperations.size() > 0) {
        std::pair<unsigned, ConstantInt*> operation = intermediateOperations.top();
        intermediateOperations.pop();
        switch(operation.first)
        {
          case Instruction::Add: value = value + operation.second->getValue(); break;
          case Instruction::Sub: value = value - operation.second->getValue(); break;
          case Instruction::Mul: value = value * operation.second->getValue(); break;
          case Instruction::UDiv: value = value.udiv(operation.second->getValue()); break;
          case Instruction::SDiv: value = value.sdiv(operation.second->getValue()); break;
          default: break;
        }
      }
      switch(q.queryOperator)
      {
        case IsTrue: return (value.getBoolValue()) ? QueryFalse : QueryTrue;
        case AreEqual: return (q.rhs->getValue() == value) ? QueryFalse : QueryTrue;
        case AreNotEqual: return (q.rhs->getValue() != value) ? QueryFalse : QueryTrue;
        default: return getQueryResolutionForConstantComparison(value, q.rhs->getValue(), q.queryOperator);
      }
    }

    QueryResolution getQueryResolutionForConstantComparison(ICmpInst& i) {
      ConstantInt* c1 = dyn_cast<ConstantInt>(i.getOperand(0));
      ConstantInt* c2 = dyn_cast<ConstantInt>(i.getOperand(1));
      return getQueryResolutionForConstantComparison(c1, c2, getQueryOperatorForPredicate(i.getPredicate()));
    }

    QueryResolution getQueryResolutionForConstantComparison(ConstantInt* c1, ConstantInt* c2, QueryOperator qOp) {
      return getQueryResolutionForConstantComparison(c1->getValue(), c2->getValue(), qOp);
    }

    QueryResolution getQueryResolutionForConstantComparison(const APInt& c1, const APInt c2, QueryOperator qOp) {
      switch(qOp)
      {
        case AreEqual: return (c1 == c2) ? QueryFalse : QueryTrue;
        case AreNotEqual: return (c1 != c2) ? QueryFalse : QueryTrue; 
        case IsSignedGreaterThan: return (c1.sgt(c2)) ? QueryFalse : QueryTrue;
        case IsUnsignedGreaterThan: return (c1.ugt(c2)) ? QueryFalse : QueryTrue;
        case IsSignedGreaterThanOrEqual: return (c1.sge(c2)) ? QueryFalse : QueryTrue;
        case IsUnsignedGreaterThanOrEqual: return (c1.uge(c2)) ? QueryFalse : QueryTrue;
        case IsSignedLessThan: return (c1.slt(c2)) ? QueryFalse : QueryTrue;
        case IsUnsignedLessThan: return (c1.ult(c2)) ? QueryFalse : QueryTrue;
        case IsSignedLessThanOrEqual: return (c1.sle(c2)) ? QueryFalse : QueryTrue;
        case IsUnsignedLessThanOrEqual: return (c1.ule(c2)) ? QueryFalse : QueryTrue;
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

    bool isDereferenceOf(Value* value, Instruction* i) {
      if (i->getOpcode() == Instruction::GetElementPtr) {
        GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(i);
        if (gepInst->getPointerOperand() == value) {
          return true;
        }
      }
      return false;
    }

    bool priorConditionGuaranteesCurrent(Node* currentNode, Node* n, Query current) {

      Query q;
      q.lhs = n->getBranchCondition();
      q.queryOperator = IsTrue;
      q.rhs = nullptr;
      std::map<Node*, Query> temp;

      std::stack<std::pair<unsigned, ConstantInt*>> intermediateOperations = q.intermediateOperations;
      APInt value = current.rhs->getValue();
      while (intermediateOperations.size() > 0) {
        std::pair<unsigned, ConstantInt*> operation = intermediateOperations.top();
        intermediateOperations.pop();
        switch(operation.first)
        {
          case Instruction::Add: value = value + operation.second->getValue(); break;
          case Instruction::Sub: value = value - operation.second->getValue(); break;
          case Instruction::Mul: value = value * operation.second->getValue(); break;
          case Instruction::UDiv: value = value.udiv(operation.second->getValue()); break;
          case Instruction::SDiv: value = value.sdiv(operation.second->getValue()); break;
          default: break;
        }
      }

      bool isTrueBranch = currentNode == n->getTrueEdge();
      for (Query queryToCheck : getSubstitutedQueries(*n, q, temp)) {
        if (!isTrueBranch) {
          queryToCheck.queryOperator = reverseComparison(queryToCheck.queryOperator);
        }
        if (queryToCheck.lhs == current.lhs) {
          if (current.queryOperator == queryToCheck.queryOperator) {
            switch(current.queryOperator)
            {
              case IsTrue: return true;
              case AreEqual: 
              case AreNotEqual: return (value == queryToCheck.rhs->getValue());
              case IsSignedGreaterThan: return (queryToCheck.rhs->getValue().sge(value));
              case IsUnsignedGreaterThan: return (queryToCheck.rhs->getValue().uge(value));
              case IsSignedGreaterThanOrEqual: return (queryToCheck.rhs->getValue().sge(value));
              case IsUnsignedGreaterThanOrEqual: return (queryToCheck.rhs->getValue().uge(value));
              case IsSignedLessThan: return (queryToCheck.rhs->getValue().sle(value));
              case IsUnsignedLessThan: return (queryToCheck.rhs->getValue().ule(value));
              case IsSignedLessThanOrEqual: return (queryToCheck.rhs->getValue().sle(value));
              case IsUnsignedLessThanOrEqual: return (queryToCheck.rhs->getValue().ule(value));
            }
          }
        }
      }
      return false;
    }

  };
}

#endif