#ifndef DEMANDDRIVENDEFUSE_H_
#define DEMANDDRIVENDEFUSE_H_

#include "InfeasiblePathDetector.h"
#include <set>
#include <map>
#include <queue>

using namespace llvm;
using namespace std;

namespace{

  class DemandDrivenDefUse { 
  private:

  public:
    DemandDrivenDefUse() {}

		map<string, set<pair<BasicBlock*, BasicBlock*>>> startBlockAnalysis(BasicBlock& B){
			errs() << "Performing def-use analysis on " << B.getName() <<"\n";
			
			map<string, set<pair<BasicBlock*, BasicBlock*>>>  def_use;

			// Is it a branch block? Do infeasible paths analysis if it is.. 
			const TerminatorInst* terminator = B.getTerminator();
			InfeasiblePathResult result;
			InfeasiblePathDetector detector;
      if (terminator->getNumSuccessors() > 1 || terminator->getOpcode() == Instruction::Br)
        detector.detectPaths(B, result);

			// Iterate over used variables, call demand driven analysis on each 
			for(BasicBlock::iterator ins = B.begin(); ins != B.end(); ++ins){
				unsigned int numOp = ins->getNumOperands();
        //for(unsigned int i=0; i < numOp-1; ++i){
							
                Value* op = ins->getOperand(numOp-1);
								if(op->hasName()){
                  errs()<< "Calling demand driven analysis on  " << op->getName() <<"\n";
									demandDrivenDefUseAnalysis(&def_use, *op, B, result);
								}
        //}
			}

			map<string, set<pair<BasicBlock*, BasicBlock*>>>::iterator it;
		
			errs() << "Current: " << def_use.size() << "\n";
			for ( it = def_use.begin(); it != def_use.end(); it++ )
			{
					errs() << it->first << ':'; 
					for(pair<BasicBlock*, BasicBlock*> p : it->second){
						errs() << "(" << p.first->getName() << ", " << p.second->getName() << "), ";
						
					}

					errs()<<"\n";
			}
			
			return def_use;
		}


		void demandDrivenDefUseAnalysis(map<string, set<pair<BasicBlock*, BasicBlock*>>>* def_use, Value& v, BasicBlock& u, InfeasiblePathResult result){
			// Initialize Q map 
			// Initialize worklist 
		  queue<pair<BasicBlock*, set<pair<Query, QueryResolution>>>> worklist;
			map<BasicBlock*, set<pair<Query, QueryResolution>>> Q;

			// Initial q 
			set<pair<Query, QueryResolution>> ipp;
			
			// Iterate predecessor edgges .. raise q 
      for (BasicBlock* pred : predecessors(&u))
				raise_query(def_use, v, make_pair(pred, &u), ipp, Q, worklist, result);
			

			// Iterate worklist 
			while(!worklist.empty()) {
				pair<BasicBlock*, set<pair<Query, QueryResolution>>> workItem = worklist.front();
        worklist.pop();

				for (BasicBlock* pred : predecessors(workItem.first))
					raise_query(def_use, v, make_pair(pred, workItem.first), workItem.second, Q, worklist, result);
			}
		}


		void raise_query(map<string, set<pair<BasicBlock*, BasicBlock*>>>* def_use, Value& v, pair<BasicBlock*, BasicBlock*> e, set<pair<Query, QueryResolution>> ipp,
										 map<BasicBlock*, set<pair<Query, QueryResolution>>> Q, 
										 queue<pair<BasicBlock*, set<pair<Query, QueryResolution>>>> worklist,
										 InfeasiblePathResult result){
	
			set<pair<Query, QueryResolution>>* q_ = resolve(def_use, v, e, ipp, Q, result);

			if(q_ != nullptr){
				set<pair<Query, QueryResolution>> temp = Q[e.first];
				if(Q.find(e.first) == Q.end())
					Q[e.first] = ipp;
				else
					Q[e.first] = intersection_(Q[e.first], ipp);

				if(temp != Q[e.first])
					worklist.push(make_pair(e.first, Q[e.first]));
			}
		}


		set<pair<Query, QueryResolution>>* resolve(map<string, set<pair<BasicBlock*, BasicBlock*>>>* def_use, Value& v, pair<BasicBlock*, BasicBlock*> e, set<pair<Query, QueryResolution>> ipp,
										 													map<BasicBlock*, set<pair<Query, QueryResolution>>> Q,
																							InfeasiblePathResult result){
			
		
			if(intersection_(ipp, result.startSet[e]).size() != 0)
				return nullptr;

			errs() << "probagating " << v.getName() << "\n";

			ipp = intersection_(ipp, result.presentSet[e]);
			ipp = union_(ipp, result.endSet[e]);

			for (pair<Query, QueryResolution> p : ipp) {
				ipp.erase(p);
				ipp.insert(make_pair(substitute((*e.first), p.first), p.second));
			}

			// Could make it better by performing this once for each block?
			for(BasicBlock::iterator i = e.first->begin(); i != e.first->end(); ++i)
					if (StoreInst * storeInst = static_cast<StoreInst*>(&*i)){
						if (storeInst->getPointerOperand()->hasName()){
							if(storeInst->getPointerOperand()->getName() == v.getName()){
								errs() << v.getName() << ": " << e.first->getName() << ", " << e.second->getName() << "\n";
								
								(*def_use)[v.getName()].insert(make_pair(e.first, e.second));
								return nullptr;
							}
						}
					}
						

			return &ipp;
		}
		

		// Thses functions from InfeasiblePathDetector, for now just copy.. 
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



		set<pair<Query, QueryResolution>> intersection_(set<pair<Query, QueryResolution>> s1, set<pair<Query, QueryResolution>> s2){
			set<pair<Query, QueryResolution>> intersect; 
			set_intersection(s1.begin(),s1.end(),s2.begin(),s2.end(),
                  		inserter(intersect,intersect.begin()));
			return intersect;
		}

		set<pair<Query, QueryResolution>> union_(set<pair<Query, QueryResolution>> s1, set<pair<Query, QueryResolution>> s2){
			set<pair<Query, QueryResolution>> union_set; 
			set_intersection(s1.begin(),s1.end(),s2.begin(),s2.end(),
                  		inserter(union_set,union_set.begin()));
			return union_set;
		}
	};

}


#endif
