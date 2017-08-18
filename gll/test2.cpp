#include "pgraph.h"

// `RuleEdge` definition

struct RuleEdge {
  RuleEdge(REI rei, string m) : identifier(rei), matcher(m), isReturnable(false) {}
  REI identifier;
  string matcher;
  vector<REI> next;
  vector<Context> getNexts(const Context&, Grammar*);
  bool isNull(){ return matcher == "$$null"; }
  bool isTerminal(){ return matcher[0] != '$' || (matcher[0] == '$' && matcher[1] == '$'); }
  bool isReturnable;
  string retName;
private:
  vector<Context> internal__get_return_set(const Context& prev_ctx, Grammar* grmr);
};

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

vector<Context> Grammar::step(const Context& ctx) {
  if (ctx.isEndOfInput()) return vector<Context>();//throw "Grammar::step(ctx) with ctx.isEndState() returning true";
  return get(ctx.rei_current)->getNexts(ctx, this);
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
  vector<string>& THESTACK = grmr->current_firsts_lookup_stack;  
  if (contains(THESTACK, rulename)) { printf("WARNING: Grammar has left-recursion.\n"); return vector<Context>(); } //Avoid loops.
  //Allowing left-recursion would result in an infinite number of Contexen...
  
  THESTACK.push_back(rulename);
  
  vector<Context> ret = get(first)->getNexts(prev_ctx, grmr);  
  
  if (THESTACK.back() != rulename) throw "Internal Error: THESTACK out of sync OR a Rule is misnamed";
  THESTACK.pop_back();
  
  return ret;
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

void basic_test_1__print_recursive(Grammar* grmr, const vector<Context> ctxen, string prefix=string()){
  if (ctxen.size()==0){
    printf("%s\n", prefix.c_str());
  } else if (prefix.length()>20) {
    printf("%s [SNIP]\n", prefix.c_str());
  } else {
    for (int i=0;i<ctxen.size();++i){
      string toprint = prefix+" "+ctxen[i].getMatcher();
      basic_test_1__print_recursive(grmr, grmr->step(ctxen[i]), toprint);
    }
  }
}

/*
 * This would only work on non-fractal or terminating fractal grammars.
 * Note: "fractal" is a term I made up used to describe the recursive nature of some grammar graphs.
 */
struct DotGraphDumper {
  struct DGNode {
    static int uniq_counter;
    DGNode() : uniq(++uniq_counter) {}
    int uniq;
    vector<string> nexts;
    string display_name;
  };
  map<string, DGNode> nodes;
  Grammar* grmr;
  DotGraphDumper(Grammar* grmr, Rule* rule) : grmr(grmr) {
    DGNode::uniq_counter = -1;
    vector<Context> ctxen = grmr->startRule(rule);
    nodes["$$BEGIN$$"].display_name = "$$BEGIN$$";
    for (int i=0;i<ctxen.size();++i){
      nodes["$$BEGIN$$"].nexts.push_back(getname(ctxen[i]));
      dofor(ctxen[i]);
    }
  }
  string getname(const Context& ctx){
    return ctx.getMatcher()+";"+ctx.return_stack_string();
  }
  void dofor(const Context& ctx){
    string thisname=getname(ctx);
    vector<Context> ctxen = grmr->step(ctx);
    nodes[thisname].display_name = ctx.getMatcher();
    for (int i=0;i<ctxen.size();++i){
      string nextname=getname(ctxen[i]);
      if (!contains(nodes[thisname].nexts, nextname)){
        nodes[thisname].nexts.push_back(nextname);
        dofor(ctxen[i]);
      }
    }
  }
  void dump(){
    map<string, DGNode>::iterator iter = nodes.begin();
    printf("strict digraph _GRAPH_NAME {\n");
    printf("edge [arrowhead=normal];\n");
    while (iter != nodes.end()){
      printf("n%i [label=\"%s\"];\n", iter->second.uniq, iter->second.display_name.c_str());
      
      vector<string>& nexts = iter->second.nexts;
      for (int i=0;i<nexts.size();++i){
        printf("n%i -> n%i;\n", iter->second.uniq, nodes[nexts[i]].uniq);
      }
      ++iter;
    }
    printf("}\n");
  }
};

int DotGraphDumper::DGNode::uniq_counter = -1;

/*
 * Some basic tests, involving creating grammar rules then enumerating possible parse trees.
 */
void basic_test_1(){
  printf("/*\n");
  REI null_rei;
  Grammar grmr;
  Rule* r = grmr.newRule("$example");
  if (!r) printf("Rule already exists?!\n");
  
  printf("Four Rules Test\n");
  
  {
    // "3" "+" "4"
    Node grammar_rule;
    grammar_rule.value = "example1";
    grammar_rule.leafs.push_back(Node::strwrap("3"));
    grammar_rule.leafs.push_back(Node::strwrap("+"));
    grammar_rule.leafs.push_back(Node::strwrap("4"));
    r->registerRule(grammar_rule);
  }
  
  {
    // "3" "*" "4"
    Node grammar_rule;
    grammar_rule.value = "example2";
    grammar_rule.leafs.push_back(Node::strwrap("3"));
    grammar_rule.leafs.push_back(Node::strwrap("*"));
    grammar_rule.leafs.push_back(Node::strwrap("4"));
    r->registerRule(grammar_rule);
  }
  
  {
    // "3" "-"? "4"
    Node grammar_rule;
    grammar_rule.value = "example3";
    grammar_rule.leafs.push_back(Node::strwrap("3"));
    {
      Node gr1;
      gr1.value = "?";
      gr1.leafs.push_back(Node::strwrap("-"));
      grammar_rule.leafs.push_back(gr1);
    }
    grammar_rule.leafs.push_back(Node::strwrap("4"));
    r->registerRule(grammar_rule);
  }
  
  /*
   * Effectively this means that the grammar has a grand total of 4 legal sentences:
   * 1. 3+4
   * 2. 3*4
   * 3. 3-4
   * 4. 34
   *
   * Yeah. I'm unimaginative.
   */
  
  {
    vector<Context> ctxen = grmr.startRule(r);
    basic_test_1__print_recursive(&grmr, ctxen);
  }
  
  Rule* r2 = grmr.newRule("$example2");
  if (!r2) printf("Rule already exists?!\n");
  
  /*
   * Left-recursion test
   * In the current implementation, this should fail.
   * I can forsee that it will be possible to support left-recursion, through graph trickery.
   * Would require matching avaliable post-loops and terminating recursion once one of those were matched.
   * Though this would require greater intertwining of the parsing component and the grammar graph 
   * component than I am comfortable with.
   */
  printf("Left Recursion Test (should fail to work as desired)\n");
  
  {
    // $example2 "+" $$i
    Node grammar_rule;
    grammar_rule.value = "addition";
    grammar_rule.leafs.push_back(Node::strwrap("$example2"));
    grammar_rule.leafs.push_back(Node::strwrap("+"));
    grammar_rule.leafs.push_back(Node::strwrap("$$i"));
    r2->registerRule(grammar_rule);
  }
  
  {
    // $$i
    Node grammar_rule;
    grammar_rule.value = "nud";
    grammar_rule.leafs.push_back(Node::strwrap("$$i"));
    r2->registerRule(grammar_rule);
  }
  
  {
    vector<Context> ctxen = grmr.startRule(r2);
    basic_test_1__print_recursive(&grmr, ctxen);
  }
  
  /*
   * Right-recursion test
   */
  printf("Right Recursion Test (infinite space, so cut-off required)\n");
  
  Rule* r_mul = grmr.newRule("$r_mul");
  {
    // $expr ("*" $expr)*
    Node grammar_rule;
    grammar_rule.value = "mul";
    grammar_rule.leafs.push_back(Node::strwrap("$expr"));
    {
      Node g2;
      g2.value = "*";
      g2.leafs.push_back(Node::strwrap("*"));
      g2.leafs.push_back(Node::strwrap("$expr"));
      grammar_rule.leafs.push_back(g2);
    }
    r_mul->registerRule(grammar_rule);
  }
  
  Rule* r_add = grmr.newRule("$r_add");
  {
    // $r_mul ("+" $r_mul)*
    Node grammar_rule;
    grammar_rule.value = "add";
    grammar_rule.leafs.push_back(Node::strwrap("$r_mul"));
    {
      Node g2;
      g2.value = "*";
      g2.leafs.push_back(Node::strwrap("+"));
      g2.leafs.push_back(Node::strwrap("$r_mul"));
      grammar_rule.leafs.push_back(g2);
    }
    r_add->registerRule(grammar_rule);
  }
  
  Rule* r_expr = grmr.newRule("$expr");
  {
    // $$i
    Node grammar_rule;
    grammar_rule.value = "expr";
    grammar_rule.leafs.push_back(Node::strwrap("$$i"));
    r_expr->registerRule(grammar_rule);
  }
  
  /*  Expected Full Grammar Graph for $r_add:
   *                               ------------------------------------------------\
   *    ----------------------\   /               -----------------------\          \
   *   /                       v /               /                        v          v
   * $$i --> "*" --> $$i  --> null --> "+" --> $$i --> "*" --> $$i ----> null ---> $$eof
   *          ^       /                 ^               ^       /         /
   *           \------                   \               \------         /
   *                                      \------------------------------        
   * 
   *  After removing the nulls:
   *  -----------------------------------------------------------\
   *  | ----------------------\          -----------------------\ \
   *  |/                       v        /                        v v
   * $$i --> "*" --> $$i  --> "+" --> $$i --> "*" --> $$i ---->  $$eof
   *          ^       /\       ^^      /       ^      /|           ^
   *           \------  \       \\-----         \----- |           |
   *                     \       \----------------------           |
   *                      -----------------------------------------/
   *
   *  Expected Full Grammar Graph for $r_mul:
   *    ----------------------\
   *   /                       v
   * $$i --> "*" --> $$i --> $$eof
   *          ^       /
   *           \------
   *
   *  Expected Full Grammar Graph for $expr:
   * $$i --> $$eof
   */
  
  {
    vector<Context> ctxen = grmr.startRule(r_add);
    basic_test_1__print_recursive(&grmr, ctxen);
    printf("*/\n");
    //This is a dot format graph.
    //Process output as so: 
    // ./test2.cpp > graph.dot
    // dot -Tpng < graph.dot > graph.png
    // open graph.png
    //The expected graph is in ASCII art above (without the nulls).
    DotGraphDumper(&grmr, r_add).dump();
  }
}


// Lexer

string token;
enum TokenType {
  tok_eof, tok_ident, tok_double, tok_int, tok_symbol
};
int gettok() {
  static int chbuf = ' ';
  while (isspace(chbuf)) chbuf = getchar();
  if (isalpha(chbuf)) {
    token = chbuf;
    while (isalnum((chbuf = getchar()))) token += chbuf;
    return tok_ident;
  }
  if (isdigit(chbuf) || chbuf == '.') {
    token = "";
    do {
      token += chbuf;
      chbuf = getchar();
    } while (isdigit(chbuf) || chbuf == '.');
    
    if (token.find('.') == string::npos) return tok_int;
    return tok_double;
  }
  if (chbuf == '#') {
    // Comment until end of line.
    do {chbuf = getchar();}
    while (chbuf != EOF && chbuf != '\n' && chbuf != '\r');
    
    if (chbuf != EOF) return gettok();
  }
  // Check for end of file.  Don't eat the EOF.
  if (chbuf == EOF) {
    token = "(end)";
    return tok_eof;
  }
  //We really need a proper
  static const string specials("(){}[]");
  if (specials.find(chbuf) != string::npos){
    token = chbuf;
    chbuf = getchar();
  } else {
    token = "";
    do {
      token += chbuf;
      chbuf = getchar();
    } while (!isalpha(chbuf) && !isdigit(chbuf) && !isspace(chbuf) && specials.find(chbuf) == string::npos);
  }
  return tok_symbol;
}
int token_type;
void next_token(){
  token_type = gettok();
}

/*
 * Using the above routines for what amounts to LL(1) parsing. (At least I *think* it's LL(1).)
 * May be slightly more powerful than LL(1) because of the automatic prefix merging for alternatives.
 *
 * 3+5*6*2+7*(3+4)
 * would produce the following tree:
 *   (add (mul (expr 3)) 
 *        (mul (expr 5) (expr 6) (expr 2)) 
 *        (mul (expr 7) 
 *             (backets (add (mul (expr 3)) 
 *                           (mul (expr 4))))))
 *
 * 3+
 * would produce the following output:
 *   Read "(end)" which was unexpected, expecting: "$$i" or "("
 */
void basic_test_2(){
  Grammar grmr;
  
  /*
   * I'd like to note that while this method of inputting rules is clumsy at first sight,
   * it allows one to easily programmatically insert rules should one want to.
   * (Like for on-the-fly syntax modification, for example.)
   */
  
  Rule* r_mul = grmr.newRule("$r_mul");
  {
    // $r_mul := $expr ("*" $expr)*
    Node grammar_rule;
    grammar_rule.value = "mul";
    grammar_rule.leafs.push_back(Node::strwrap("$expr"));
    {
      Node g2;
      g2.value = "*";
      g2.leafs.push_back(Node::strwrap("*"));
      g2.leafs.push_back(Node::strwrap("$expr"));
      grammar_rule.leafs.push_back(g2);
    }
    r_mul->registerRule(grammar_rule);
  }
  
  Rule* r_add = grmr.newRule("$r_add");
  {
    // $r_add := $r_mul ("+" $r_mul)*
    Node grammar_rule;
    grammar_rule.value = "add";
    grammar_rule.leafs.push_back(Node::strwrap("$r_mul"));
    {
      Node g2;
      g2.value = "*";
      g2.leafs.push_back(Node::strwrap("+"));
      g2.leafs.push_back(Node::strwrap("$r_mul"));
      grammar_rule.leafs.push_back(g2);
    }
    r_add->registerRule(grammar_rule);
  }
  
  Rule* r_expr = grmr.newRule("$expr");
  {
    // $expr := $$i
    Node grammar_rule;
    grammar_rule.value = "expr";
    grammar_rule.leafs.push_back(Node::strwrap("$$i"));
    r_expr->registerRule(grammar_rule);
  }
  {
    // $expr := "(" $r_add ")"
    Node grammar_rule;
    grammar_rule.value = "backets";
    grammar_rule.leafs.push_back(Node::strwrap("("));
    grammar_rule.leafs.push_back(Node::strwrap("$r_add"));
    grammar_rule.leafs.push_back(Node::strwrap(")"));
    r_expr->registerRule(grammar_rule);
  }
  
  /*
   * Example Usage
   */
  
  //(1) Start it off.
  vector<Context> ctxen = grmr.startRule(r_add);
  Node result;
  vector<Node*> node_refs;
  node_refs.push_back(&result);
  int i;
  while (true){
    //(2) This is where we do the look-ahead. Here it is a one-token lookahead.
    //We accept the first match we see, even if there may be another one.
    //Change the lookahead to infinite and you have a LL(*) parser.
    next_token();
    for (i=0;i<ctxen.size();++i){
      string matcher = ctxen[i].getMatcher();
      if (matcher == "$$i" && token_type == tok_int){
        goto match_success;
      } else if (matcher == "$$eof" && token_type == tok_eof){
        goto match_success;
      } else if (token == matcher){
        goto match_success;
      }
    }
    //Failed to match:
    printf("Read \"%s\" which was unexpected, expecting: ", token.c_str());
    for (i=0;i<ctxen.size();++i){
      if (i != 0) printf(" or ");
      printf("\"%s\"", ctxen[i].getMatcher().c_str());
    }
    printf("\n");
    exit(0);
    
    //(3) Once we've looked-ahead and selected a path to take, we can start heading down that path.
    //If this part was modified to try all possible alternatives, you would have a GLL parser. (I think.)
    //A GLL parser is roughly equal in power to a GLR parser.
  match_success:
    const Context& newCtx = ctxen[i];
    
    //(4) Here we deal with the StackLog.
    //The StackLog tells us our new position in the grammar relative to our previous position.
    //If the StackLog is empty, that means we are still in the same rule as the previous one.
    vector<Context::StackLog> slog = newCtx.getStackLog();
    for (int i=slog.size()-1;i>=0;--i){
      if (slog[i].isToChildRule()){
        node_refs.back()->leafs.push_back(Node());
        node_refs.push_back(&(node_refs.back()->leafs.back()));
      } else {
        node_refs.back()->value = slog[i].getRetName();
        node_refs.pop_back();
      }
    }
    
    //(5) After adjusting the current position in the parse tree as necessary,
    //here we actually eat the input and take action. (i.e: inserting data into parse tree.)
    if (newCtx.getMatcher() == "$$i")
      node_refs.back()->leafs.push_back(Node::strwrap(token));
    else if (newCtx.getMatcher() == "$$eof")
      break;
    
    //(6) Prepare for the next step.
    ctxen = grmr.step(newCtx);
  }
  
  //(7) Finally, we have a complete parse tree which we can do whatever we like with.
  result.print();
  printf("\n");
}

/*
 * Final note:
 * Left-recursion could be handled but would require additional complexity in the engine (thanks
 * to my implementation), so is probably not worth it. I could always fall back to Pratt parsing
 * for the failure cases if the need arises.
 */

int main(){basic_test_2();}