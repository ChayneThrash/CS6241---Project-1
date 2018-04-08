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
		typedef set<pair<Query, QueryResolution>> IPP;

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

		void startBlockAnalysis(BasicBlock& B, Module &m, map<string, set<pair<BasicBlock*, BasicBlock*>>>& def_use, set<string>& localVar){		
			this->def_use = &def_use; 
			this->m = &m;

			set<Value*> local_def; 
			if (B.getInstList().size() == 0) {
				return;
			}

			Node leaderNode(&B, nullptr);
			// Iterate over used variables, call demand driven analysis on each 
			for(BasicBlock::iterator ins = B.begin(); ins != B.end(); ++ins){
					// Did we find an allocate instruction? Means that the scope of this 
					// variable is limited to the function only... Do intraprocedural 
					// def-use analysis only. 
					if ((*ins).getOpcode() == Instruction::Alloca)
						localVar.insert(ins->getName());
					if ((*ins).getOpcode() == Instruction::Store){
            Value* op = ins->getOperand(1);
						if(op->hasName())
							local_def.insert(op);
					}		
					else if ((*ins).getOpcode() == Instruction::Load){
            Value* op = ins->getOperand(0);
						bool isLocal = localVar.find(op->getName()) != localVar.end();
						if(op->hasName()){
							if(local_def.find(op) != local_def.end() && isLocal)
								def_use[op->getName()].insert(make_pair(&B, &B));
							else
								demandDrivenDefUseAnalysis(*op, *(leaderNode.getNodeFor(&B, &(*ins))), isLocal);
						}
					}		
			
			}
    }

		void demandDrivenDefUseAnalysis(Value& v, Node& u, bool isLocal){
			detector.detectPaths(u, result, *m);

		  worklist = queue<pair<Node*, DUQuery>>();
			Q = map<Node*, DUQuery>(); 
			SNMap = map<tuple<Node*, Value*, IPP>, SN>() ;

			// Initial q 
			DUQuery initial_query; 
			
			// Iterate predecessor edgges .. raise q 
      for (Node* pred : u.getPredecessors())
				raise_query(v, make_pair(pred, &u), initial_query, u, isLocal);
			

			// Iterate worklist 
			while(!worklist.empty()) {
				pair<Node*, DUQuery> workItem = worklist.front();
        worklist.pop();
				
				// Case n is call site node
				if(isCallSite(workItem.first) && !isLocal){
					// Keeping track of call sites
					key.push(workItem.first); 
					
					for(Node* x : workItem.first->getPredecessors()){
					
						if(SNMap.count(make_tuple(x, &v, get<0>(workItem.second))) == 0){
							SN s = make_tuple(map<string, set<pair<BasicBlock*, BasicBlock*>>>() , false, IPP());
							DUQuery q_ = make_tuple(get<0>(workItem.second), s, true);
							raise_query(v, make_pair(x, workItem.first), q_, u, isLocal);
						}else{

							add_to_def_use(get<0>(SNMap.at(make_tuple(x, &v, get<0>(workItem.second)))), v.getName());

							if(get<1>(SNMap.at(make_tuple(x, &v, get<0>(workItem.second))))){
								for (Node* pred : workItem.first->getPredecessors()){
									DUQuery q_ = make_tuple(get<2>(SNMap.at(make_tuple(x, &v, get<0>(workItem.second)))), get<1>(workItem.second), true);
									raise_query(v, make_pair(pred, workItem.first), q_, u, isLocal);
								}
							}
						}
					}

				}else if(workItem.first->isEntryOfFunction  && !isLocal){
					if(!key.empty())
						key.pop(); 
					
					if (get<2>(workItem.second)){
						get<1>(get<1>(workItem.second)) = true;
						get<2>(get<1>(workItem.second)) = get<0>(workItem.second); 
					}

					for (Node* pred : workItem.first->getPredecessors())
						raise_query(v, make_pair(pred, workItem.first), workItem.second, u, isLocal);
				}else{
					for (Node* pred : workItem.first->getPredecessors())
						raise_query(v, make_pair(pred, workItem.first), workItem.second, u, isLocal);
				}
			}
		}


		void raise_query(Value& v, pair<Node*, Node*> e, DUQuery &q, Node& u, bool isLocal){

			// Do we need to propagate? 
			if(resolve(v, e, q, u, isLocal)){

				
				if(Q.count(e.first) == 0){

					Q[e.first] = q;
					worklist.push(make_pair(e.first, Q.at(e.first)));

				}else{

					DUQuery temp = Q.at(e.first);
					get<0>(Q[e.first]) = intersection_(get<0>(Q.at(e.first)), get<0>(q));

					if(get<0>(temp) != get<0>(Q.at(e.first)))
						worklist.push(make_pair(e.first, Q.at(e.first)));

				}

			}
		}


		bool resolve(Value& v, pair<Node*, Node*> e, DUQuery &q, Node& u, bool isLocal){

			// Did we follow an infeasible path? 
			tuple<Node*, Node*, stack<Node*>> resultKey = make_tuple(e.first, e.second, key);
			IPP startSet = result.getStartSetFor(resultKey);
			if(intersection_(get<0>(q), startSet).size() != 0) 
				return false;

			// Remove paths in progress that are no longer followed
				IPP presentSet = result.getPresentSetFor(resultKey);
			get<0>(q) = intersection_(get<0>(q), presentSet);

			// Add paths in progress that are started at edge e
			IPP endSet = result.getEndSetFor(resultKey);
			get<0>(q) = union_(get<0>(q), endSet);


			// Rename 
			for (pair<Query, QueryResolution> p : get<0>(q)) {
				get<0>(q).erase(p);
				std::map<Node*, Query> dummy; 
				Query newQuery = detector.substitute(*e.first, p.first, dummy);
				get<0>(q).insert(make_pair(newQuery, p.second));
			}

			// Add to def-use and terminate if we found a def 
			const vector<Instruction*>& instructions =  e.first->getReversedInstructions();
			for(vector<Instruction*>::const_reverse_iterator i = instructions.rbegin(); i != instructions.rend(); ++i) {
				if ((*i)->getOpcode() == Instruction::Store) {
					if((*i)->getOperand(1)->getName() == v.getName()){
						if(isLocal && (e.first->basicBlock->getParent() != u.basicBlock->getParent()))
							continue;
						pair<BasicBlock*, BasicBlock*> defUse = make_pair(e.first->basicBlock, u.basicBlock);
						(*def_use)[v.getName()].insert(defUse);
						
						// Is it a summery node query?									
						if(get<2>(q) && !isLocal){
							SN s = get<1>(q);
							get<0>(s)[v.getName()].insert(defUse);
						}

						return false;
					}
				}
			}
					

			return true;
		}

		bool isCallSite(Node* &u){
				return u->getSuccessors().size() == 1 && (*(u->getSuccessors().begin()))->isEntryOfFunction;
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
			for(pair<BasicBlock*, BasicBlock*> p : defs[varName])
				(*def_use)[varName].insert(p);
		}


	};

}


#endif
