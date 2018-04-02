#ifndef INTERPROCEDURALDEMANDDRIVENDEFUSE_H_
#define INTERPROCEDURALDEMANDDRIVENDEFUSE_H_

#include "InterproceduralInfeasiblePathDetector.h"

using namespace llvm;
using namespace std;

namespace{

  class InterproceduralDemandDrivenDefUse { 
  private:

  public:

		InfeasiblePathResult result;
		InfeasiblePathDetector detector;

		// Pointer to the def-use map we are adding to
		map<string, set<pair<BasicBlock*, BasicBlock*>>> *def_use;

		// IPP
		typedef set<tuple<Query, QueryResolution, stack<Node*>>> IPP;

		// Summery node has: 
		// 1) def-uses 2) is it transp? 3)ipp_
		typedef tuple<map<string, set<pair<BasicBlock*, BasicBlock*>>>, bool, IPP> SN;
		
		// Def-Use query type
		// Last element: True = summery node query 
		typedef tuple<IPP, SN, bool> DUQuery; 

		// Summery nodes map
		map<tuple<Node*, Value*, IPP>, SN> SNMap;

		// Queue of workitems 
	  queue<pair<Node*, DUQuery>> worklist;

		// Node-DefUseQuery Map
		map<Node*, DUQuery> Q;
		
		// Keeping a stack of entered call sites 
		stack<Node*> key; 

		// Module 
		Module *m; 

    InterproceduralDemandDrivenDefUse() {}

		void startBlockAnalysis(BasicBlock& B, Module &m, map<string, set<pair<BasicBlock*, BasicBlock*>>>& def_use){		
			this->def_use = &def_use; 
			this->m = &m;

			// Iterate over used variables, call demand driven analysis on each 
			for(BasicBlock::iterator ins = B.begin(); ins != B.end(); ++ins)
					if ((*ins).getOpcode() == Instruction::Load){
                Value* op = ins->getOperand(0);	
								if(op->hasName())
										demandDrivenDefUseAnalysis(*op, Node(&B, &(*ins)));
								
					}		
			

    }

		void demandDrivenDefUseAnalysis(Value& v, Node u){
			
		  worklist = queue<pair<Node*, DUQuery>>();
			Q = map<Node*, DUQuery>(); 
			SNMap = map<tuple<Node*, Value*, IPP>, SN>() ;

			// Initial q 
			DUQuery initial_query; 
			
			// Iterate predecessor edgges .. raise q 
      for (Node* pred : u.getPredecessors())
				raise_query(v, make_pair(pred, &u), initial_query, u);
			

			// Iterate worklist 
			while(!worklist.empty()) {
				pair<Node*, DUQuery> workItem = worklist.front();
        worklist.pop();

				////////////////////////////THIS NEED REVIEW ///////////////////////////////////
				
				// Case n is call site node
				if(Function *f = isFunctionCall(workItem.first->getReversedInstructions().back())){
					// Keeping track of call sites
					key.push(workItem.first); 
					
					for(Node* x : getFunctionExitNodes(*f)){
					
						if(SNMap.count(make_tuple(x, v, get<1>(workItem.second))) == 0){
							SN s = make_tuple(map<string, set<pair<BasicBlock*, BasicBlock*>>>() , false, IPP());
							raise_query(v, make_pair(x, workItem.first), DUQuery(get<1>(workItem.second), s, true));
						}else{

							add_to_def_use(get<1>(SNMap.at(make_tuple(x, v, get<1>(workItem.second)))), v.getName());

							if(get<2>(SNMap.at(make_tuple(x, v, workItem.second.first))))
								for (Node* pred : workItem.first.getPredecessors())
									raise_query(v, make_pair(pred, workItem.first), DUQuery(get<3>(SNMap.at(make_tuple(x, v, get<1>(workItem.second)))), get<2>(workItem.second), true), u);
						}
					}

				}else if(workItem.first->isEntryOfFunction){
					key.pop(); 

					if (get<2>(workItem.second)){
						get<2>(get<2>(workItem.second)) = true;
						get<3>(get<2>(workItem.second)) = get<1>(workItem.second); 
					}
					
					for(Function &func : m)
						for(BaiscBlock &b : func)
							for(Instruction &i: b)
								if(Function *f = isFunctionCall(workItem.first->getReversedInstructions().back()) == workItem.first.basicBlock->getParent())
									raise_query(v, make_pair(Node(b, i), workItem.first), workItem.second, u);
				}else{
					for (Node* pred : predecessors(workItem.first))
						raise_query(v, make_pair(pred, workItem.first), workItem.second, u);
				}
			}
		}


		void raise_query(Value& v, pair<Node*, Node*> e, DUQuery &q, Node& u){

			// Do we need to propagate? 
			if(resolve(v, e, q, u)){

				
				if(Q.count(e.first) == 0){

					Q[e.first] = q;
					worklist.push(make_pair(e.first, Q.at(e.first)));

				}else{

					DUQuery temp = Q.at(e.first);
					Q[e.first].first = intersection_(Q.at(e.first).first, q.first);

					if(temp.first != Q.at(e.first).first)
						worklist.push(make_pair(e.first, Q.at(e.first)));

				}

			}
		}


		bool resolve(Value& v, pair<Node*, Node*> e, DUQuery &q, Node& u){

			// Did we follow an infeasible path? 
			if(intersection_(q.first, result.getStartSetFor(make_tuple(e.first, e.second, key))).size() != 0)
				return false;

			// Remove paths in progress that are no longer followed
			q.first = intersection_(q.first, result.getPresentSetFor(make_tuple(e.first, e.second, key)));

			// Add paths in progress that are started at edge e
			q.first = union_(q.first, result.getEndSetFor(make_tuple(e.first, e.second, key)));

			// Rename 
			for (tuple<Query, QueryResolution, std::stack<Node*>> t : q.first) {
				q.first.erase(t);
				///////////////////////////// USAGE OF NEW SUBSTITUTE /////////////////////////
				q.first.insert(make_pair(detector.substitute((*e.first), p.first), p.second));
			}

			// Add to def-use and terminate if we found a def 
			for(vector<Instruction*>::reverse_iterator i = e.first->getReversedInstructions()->rbegin(); 
					i != e.first->getReversedInstructions()->rend(); ++i)
					if (i->getOpcode() == Instruction::Store)
							if(i->getOperand(1)->getName() == v.getName()){
								def_use[v.getName()].insert(make_pair(e.first->basicBlock, u->basicBlock));
								return false;
							}

			return true;
		}

		// From InterproceduralInfeasiblePathDetector with modification .. for now just copy
		Function* isFunctionCall(Instruction* i) {
		  if (i == nullptr || i->getOpcode() != Instruction::Call) {
		    return nullptr;
		  }
		  CallInst* callInst = dyn_cast<CallInst>(i);
		  Function* f = callInst->getCalledFunction();

		  return f;
		}	


		// Returns the intersection of two sets
		IPP intersection_(IPP& s1, IPP& s2){
			IPP intersect; 
			set_intersection(s1.begin(),s1.end(),s2.begin(),s2.end(),
                  		inserter(intersect,intersect.begin()));
			return intersect;
		}

		// Retruns the union of two sets 
		IPP union_(IPP& s1, IPP& s2){
			IPP union_set; 
			set_union(s1.begin(),s1.end(),s2.begin(),s2.end(),
                  		inserter(union_set,union_set.begin()));
			return union_set;
		}

		void add_to_def_use(map<string, set<pair<BasicBlock*, BasicBlock*>>> defs, string varName){
			for(pair<BasicBlock*, BasicBlock*>> p : defs[varName])
				def_use[varName].insert(p);
		}

		set<Node*> getFunctionExitNodes(Function &F){
			set<Node*> exitNodes;

		  for(BasicBlock& b : *f) {
		    if (b.getTerminator()->getNumSuccessors() == 0) {
		      exitNodes.insert(Node(b, b.getTerminator()));
		    }
		  }

			return exitNodes;
		}

	};

}


#endif
