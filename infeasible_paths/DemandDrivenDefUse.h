#ifndef DEMANDDRIVENDEFUSE_H_
#define DEMANDDRIVENDEFUSE_H_

#include "InfeasiblePathDetector.h"

using namespace llvm;
using namespace std;

namespace{

  class DemandDrivenDefUse { 
  private:

  public:
		InfeasiblePathResult result;
		InfeasiblePathDetector detector;

    DemandDrivenDefUse() {}

		void startBlockAnalysis(BasicBlock& B, map<string, set<pair<BasicBlock*, BasicBlock*>>>& def_use){		
			
      detector.detectPaths(B, result);

			set<Value*> local_def; 

			// Iterate over used variables, call demand driven analysis on each 
			for(BasicBlock::iterator ins = B.begin(); ins != B.end(); ++ins)
					if ((*ins).getOpcode() == Instruction::Store){
                Value* op = ins->getOperand(1);
								if(op->hasName())
									local_def.insert(op);
					}		
					else if ((*ins).getOpcode() == Instruction::Load){
                Value* op = ins->getOperand(0);	
								if(op->hasName()){
									// Is it locally defined?
									if(local_def.find(op) != local_def.end())
										def_use[op->getName()].insert(make_pair(&B, &B));
									else
										demandDrivenDefUseAnalysis(def_use, *op, B);
								}
					}								

		}


		void demandDrivenDefUseAnalysis(map<string, set<pair<BasicBlock*, BasicBlock*>>>& def_use, Value& v, BasicBlock& u){
			// Initialize Q map 
			// Initialize worklist 
		  queue<pair<BasicBlock*, set<pair<Query, QueryResolution>>>> worklist;
			map<BasicBlock*, set<pair<Query, QueryResolution>>> Q;

			// Initial q 
			set<pair<Query, QueryResolution>> ipp;
			
			// Iterate predecessor edgges .. raise q 
      for (BasicBlock* pred : predecessors(&u))
				raise_query(def_use, v, make_pair(pred, &u), ipp, Q, worklist, u);
			

			// Iterate worklist 
			while(!worklist.empty()) {
				pair<BasicBlock*, set<pair<Query, QueryResolution>>> workItem = worklist.front();
        worklist.pop();

				for (BasicBlock* pred : predecessors(workItem.first))
					raise_query(def_use, v, make_pair(pred, workItem.first), workItem.second, Q, worklist, u);
			}
		}


		void raise_query(map<string, set<pair<BasicBlock*, BasicBlock*>>>& def_use, Value& v,
										 pair<BasicBlock*, BasicBlock*> e, set<pair<Query, QueryResolution>>& ipp,
										 map<BasicBlock*, set<pair<Query, QueryResolution>>>& Q, 
										 queue<pair<BasicBlock*, set<pair<Query, QueryResolution>>>>& worklist, 
										 BasicBlock& u){

			set<pair<Query, QueryResolution>> q_;

			// Do we need to propagate? 
			if(resolve(def_use, v, e, ipp, Q, u, q_)){

				
				if(Q.count(e.first) == 0){

					Q[e.first] = ipp;
					worklist.push(make_pair(e.first, Q.at(e.first)));

				}else{

					set<pair<Query, QueryResolution>> temp = Q.at(e.first);
					Q[e.first] = intersection_(Q.at(e.first), ipp);

					if(temp != Q.at(e.first))
						worklist.push(make_pair(e.first, Q.at(e.first)));

				}

			}
		}


		bool resolve(map<string, set<pair<BasicBlock*, BasicBlock*>>>& def_use, Value& v,
								 pair<BasicBlock*, BasicBlock*> e, set<pair<Query, QueryResolution>>& ipp,
								 map<BasicBlock*, set<pair<Query, QueryResolution>>> &Q,
								 BasicBlock& u, set<pair<Query, QueryResolution>> &q_){
			
			// Did we follow an infeasible path? 
			if(intersection_(ipp, result.startSet[e]).size() != 0)
				return false;

			// Remove paths in progress that are no longer followed
			ipp = intersection_(ipp, result.presentSet[e]);

			// Add paths in progress that are started at edge e
			ipp = union_(ipp, result.endSet[e]);

			// Rename 
			for (pair<Query, QueryResolution> p : ipp) {
				ipp.erase(p);
				ipp.insert(make_pair(detector.substitute((*e.first), p.first), p.second));
			}

			// Add to def-use and terminate if we found a def 
			for(BasicBlock::iterator i = e.first->begin(); i != e.first->end(); ++i)
					if (i->getOpcode() == Instruction::Store)
							if(i->getOperand(1)->getName() == v.getName()){
								def_use[v.getName()].insert(make_pair(e.first, &u));
								return false;
							}
						
					
						
			q_ = ipp;
			return true;
		}
	
		// Returns the intersection of two sets
		set<pair<Query, QueryResolution>> intersection_(set<pair<Query, QueryResolution>>& s1,
																									  set<pair<Query, QueryResolution>>& s2){
			set<pair<Query, QueryResolution>> intersect; 
			set_intersection(s1.begin(),s1.end(),s2.begin(),s2.end(),
                  		inserter(intersect,intersect.begin()));
			return intersect;
		}

		// Retruns the union of two sets 
		set<pair<Query, QueryResolution>> union_(set<pair<Query, QueryResolution>>& s1,
																						 set<pair<Query, QueryResolution>>& s2){
			set<pair<Query, QueryResolution>> union_set; 
			set_union(s1.begin(),s1.end(),s2.begin(),s2.end(),
                  		inserter(union_set,union_set.begin()));
			return union_set;
		}
	};

}


#endif
