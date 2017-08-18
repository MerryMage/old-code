#include "pgraph.h"

bool matcher_is_terminal(const string& matcher){
  return matcher[0] != '$' || (matcher[0] == '$' && matcher[1] == '$');
}

// `Context` Member Function Definitions

string Context::getMatcher() const { 
  if (isEndOfInput()) return "$$eof";
  return grammar->get(rei_current)->matcher;
}

void Context::dump() const {
  for (int i=0;i<return_stack.size();++i){
    printf("%s ", return_stack[i].rulename.c_str());
  } printf("\n");
  for (int i=stack_log.size()-1;i>=0;--i){
    printf("%s%i%s ", stack_log[i].rulename.c_str(), stack_log[i].direction, stack_log[i].retName.c_str());
  } printf("\n");
}

string Context::return_stack_string() const {
  string ret;
  for (int i=0;i<return_stack.size();++i){
    char tmp[255];
    sprintf(tmp, "%i", return_stack[i].uniqueid);
    ret += return_stack[i].rulename+":"+tmp+";";
  }
  return ret;
}

// `Grammar` Member Function Definitions

Rule* Grammar::getRule(string rulename){
  map<string, Rule>::iterator iter = rules.find(rulename);
  if (iter == rules.end()) return 0;
  return &(iter->second);
}

Rule* Grammar::newRule(string rulename){
  map<string, Rule>::iterator iter = rules.find(rulename);
  if (iter != rules.end()) return 0;
  rules[rulename].init(rulename);
  return getRule(rulename);
}

RuleEdge* Grammar::get(REI rei){
  Rule* rule = getRule(rei.rulename);
  if (!rule) return 0;
  return rule->get(rei);
}

vector<Context> Grammar::startRule(Rule* r) {
  if (this->current_firsts_lookup_stack.size() != 0) throw "THE_STACK not empty at beginning!!";
  Context ctx(this, REI());
  return r->getFirsts(ctx, this);
}

vector<Context> Grammar::startRule_NR(Rule* r) {
  if (this->current_firsts_lookup_stack.size() != 0) throw "THE_STACK not empty at beginning!!";
  Context ctx(this, REI());
  return r->getFirsts_NR(ctx, this);
}

vector<Context> Grammar::step(const Context& ctx) {
  if (ctx.isEndOfInput()) return vector<Context>();//throw "Grammar::step(ctx) with ctx.isEndState() returning true";
  return get(ctx.rei_current)->getNexts(ctx, this);
}

vector<Context> Grammar::first_set(const Context& ctx) {
  if (ctx.isEndOfInput()) return vector<Context>();//throw "Grammar::step_NR(ctx) with ctx.isEndState() returning true";
  if (get(ctx.getrei())->isTerminal()) return vector<Context>(1, ctx);
  else {
    Context newCtx = ctx;
    newCtx.return_stack.push_back(ctx.getrei());
    vector<Context> subrule_firsts = this->getRule(ctx.getMatcher())->getFirsts(newCtx, this);
    for (int j=0;j<subrule_firsts.size();++j) subrule_firsts[j].setIsToChildRule(ctx.getMatcher());
    return subrule_firsts;
  }
}

vector<Context> Grammar::step_NR(const Context& ctx) {
  if (ctx.isEndOfInput()) return vector<Context>();//throw "Grammar::step_NR(ctx) with ctx.isEndState() returning true";
  return get(ctx.rei_current)->getNexts_NR(ctx, this);
}

// `Rule` Member Function Definitions

void Rule::init(string rulename){
  this->rulename = rulename;
  rei_uniqid_next = 0;
  first = newrei();
  edges.insert(std::pair<REI, RuleEdge>(first, RuleEdge(first, "$$null")));
}

RuleEdge* Rule::get(REI rei){
  map<REI, RuleEdge>::iterator iter = edges.find(rei);
  if (iter == edges.end()) return 0;
  return &(iter->second);
}

vector<Context> Rule::getFirsts(const Context& prev_ctx, Grammar* grmr){
  static bool isFirstTime = true;
  vector<Context> ret;
  if (isFirstTime) {
    isFirstTime = false;
    ret = get(first)->getNexts(prev_ctx, grmr);  
    isFirstTime = true;
  } else {
    vector<string>& THESTACK = grmr->current_firsts_lookup_stack;  
    if (contains(THESTACK, rulename)) { /*printf("WARNING: Grammar has left-recursion.\n");*/ return vector<Context>(); } //Avoid loops.
    //Allowing left-recursion would result in an infinite number of Contexen...
  
    THESTACK.push_back(rulename);
  
    ret = get(first)->getNexts(prev_ctx, grmr);  
  
    if (THESTACK.back() != rulename) throw "Internal Error: THESTACK out of sync OR a Rule is misnamed";
    THESTACK.pop_back();
  }
  
  return ret;
}

vector<Context> Rule::getFirsts_NR(const Context& prev_ctx, Grammar* grmr){
  return get(first)->getNexts_NR(prev_ctx, grmr);  
}

REI Rule::rule_create_edge(string matcher){
  REI new_rei = this->newrei();
  this->edges.insert(std::pair<REI, RuleEdge>(new_rei, RuleEdge(new_rei, matcher)));
  return new_rei;
}

REI Rule::rule_register_helper(REI prev_rei, const Node& n, int niter){
  if (niter >= n.numleafs()) return prev_rei;
  if (n.get(niter).isleaf()){
    RuleEdge* prev_edge = this->get(prev_rei);
    RuleEdge* this_edge;
    //Look if prev_edge's next set already has a matcher corresponding to the one we want.
    for (int i=0;i<prev_edge->next.size();++i){
      this_edge = this->get(prev_edge->next[i]);
      if (this_edge->matcher == n.get(niter).value) goto do_this_edge;
    }
    //Otherwise, create it.
    { //New scope required because "goto crosses initialization of ‘REI new_rei’".
      REI new_rei = rule_create_edge(n.get(niter).value);
      prev_edge->next.push_back(new_rei);
      this_edge = this->get(new_rei);
    }
    //Recurse.
  do_this_edge:
    return rule_register_helper(this_edge->identifier, n, ++niter);
  } else if (n.get(niter).value == "?") {
    REI last_rei = rule_register_helper(prev_rei, n.get(niter), 0);
    REI ret = rule_create_edge("$$null");
    
    /*   +-----------+            +-----------+   +-----------+
     *==>| prev_rei  |===> ... ==>| last_rei  |-->|    ret    |
     *   +-----------+            +-----------+(2)+----null---+
     *        |            (1)                          ^
     *        \-----------------------------------------/
     */
    
    this->get(prev_rei)->next.push_back(ret);
    this->get(last_rei)->next.push_back(ret);
    
    return rule_register_helper(ret, n, ++niter);
  } else if (n.get(niter).value == "*") {
    REI first_rei = rule_create_edge("$$null");
    REI last_rei = rule_register_helper(first_rei, n.get(niter), 0);
    REI ret = rule_create_edge("$$null");
    
    /*                        /------------------------\
     *                        v          (3)           |
     *   +-----------+   +-----------+            +-----------+   +-----------+
     *==>| prev_rei  |-->| first_rei |===> ... ==>| last_rei  |-->|    ret    |
     *   +-----------+(1)+----null---+            +-----------+(4)+----null---+
     *        |                        (2)                            ^
     *        \-------------------------------------------------------/
     */
    
    this->get(prev_rei)->next.push_back(first_rei); //(1)
    this->get(prev_rei)->next.push_back(ret);       //(2)
    this->get(last_rei)->next.push_back(first_rei); //(3)
    this->get(last_rei)->next.push_back(ret);       //(4)
    
    return rule_register_helper(ret, n, ++niter);
  } else if (n.get(niter).value == "+") {
    REI first_rei = rule_create_edge("$$null");
    REI last_rei = rule_register_helper(first_rei, n.get(niter), 0);
    
    /*                        /------------------------\
     *                        v          (2)           |
     *   +-----------+   +-----------+            +-----------+
     *==>| prev_rei  |-->| first_rei |===> ... ==>| last_rei  |
     *   +-----------+(1)+----null---+            +-----------+
     */
    
    this->get(prev_rei)->next.push_back(first_rei); //(1)
    this->get(last_rei)->next.push_back(first_rei); //(2)

    return rule_register_helper(last_rei, n, ++niter);
  } else {
    throw "Rule::registerRule: Invalid Rule.";
  }
}

void Rule::registerRule(Node n){
  REI last_rei = rule_register_helper(first, n, 0);
  RuleEdge* last_edge = this->get(last_rei);
  last_edge->isReturnable = true;
  last_edge->retName = n.value;
}

// `RuleEdge` Member Function Definitions

vector<Context> RuleEdge::internal__get_return_set(const Context& prev_ctx, Grammar* grmr){
  vector<Context> ret;
  if (prev_ctx.return_stack.size() == 0) {
    ret.push_back(Context(grmr));
    ret[0].setIsEndOfInput(this->identifier.rulename, this->retName);
    return ret;
  } else {
    REI super_rei = prev_ctx.return_stack.back();
    Context newCtx = prev_ctx;
    newCtx.return_stack.pop_back();
    ret = grmr->get(super_rei)->getNexts(newCtx, grmr);
    for (int i=0;i<ret.size();++i) ret[i].setIsToSuperRule(this->identifier.rulename, this->retName);
    return ret;
  }
}

vector<Context> RuleEdge::getNexts(const Context& prev_ctx, Grammar* grmr){
  vector<Context> ret;
  for (int i=0;i<next.size();++i){
    RuleEdge* edge = grmr->get(next[i]);
    if (edge->isNull()){
      append(ret, edge->getNexts(prev_ctx, grmr)); //Note that this also includes the null's return set if any.
    } else if (edge->isTerminal()){
      ret.push_back(Context(prev_ctx, next[i]));
    } else { //Calling a subrule
      Context newCtx = prev_ctx;
      newCtx.return_stack.push_back(next[i]);
      vector<Context> subrule_firsts = grmr->getRule(edge->matcher)->getFirsts(newCtx, grmr);
      for (int j=0;j<subrule_firsts.size();++j) subrule_firsts[j].setIsToChildRule(edge->matcher);
      append(ret, subrule_firsts);
    }
  }
  
  if (isReturnable)
    append(ret, internal__get_return_set(prev_ctx, grmr));
  
  return ret;
}

vector<Context> RuleEdge::getNexts_NR(const Context& prev_ctx, Grammar* grmr){
  vector<Context> ret;
  for (int i=0;i<next.size();++i){
    RuleEdge* edge = grmr->get(next[i]);
    if (edge->isNull()){
      append(ret, edge->getNexts(prev_ctx, grmr)); //Note that this also includes the null's return set if any.
    } else {
      ret.push_back(Context(prev_ctx, next[i]));
    }
  }
  
  if (isReturnable)
    append(ret, internal__get_return_set(prev_ctx, grmr));
  
  return ret;
}