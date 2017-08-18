#include <vector>
#include <string>
using std::string;
using std::vector;

void fail(string err){
  printf(">>> fail(\"%s\");\n", err.c_str());
  throw err;
  exit(-1);
}

// Cheap lists (cheap as in easy to implement)

struct Node {
  string value;
  vector<Node> leafs;
  bool isleaf(){return leafs.size() == 0;}
  void print(){
    if (isleaf()) {
      printf(value.c_str());
      return;
    }
    printf("(");
    printf(value.c_str());
    for (int i=0;i<leafs.size();i++){
      printf(" ");
      leafs[i].print();
    }
    printf(")");
  }
  Node* leaf_with_value(string value){
    for (int i=0;i<leafs.size();++i){
      if (leafs[i].value == value) return &(leafs[i]);
    }
    return 0;
  }
  static inline Node strwrap(string str){
    Node ret; ret.value = str; return ret;
  }
};

#include <map>
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
using std::map;

#if 0

/*
 Original statements:
 
 $statement fn_def nud $type $identifier "(" $args? ")" "{" $statements "}";
 $statement vardef nud $type $identifier ("=" $expression)? ";";

 Normalization:
 
 $statement fn_def nud $type $identifier "(" ")" "{" $statements "}";
 $statement fn_def nud $type $identifier "(" $args ")" "{" $statements "}";
 $statement vardef nud $type $identifier ";";
 $statement vardef nud $type $identifier "=" $expression ";";
 
 Tree:
 
 $statement: (($type ($identifier ("(" (")" ("{" ($statements ("}" (__RETURN__ fn_def))))) 
                                       ($args (")" ("{" ($statements ("}" (__RETURN__ fn_def)))))))
                                  (";" (__RETURN__ vardef))
                                  ("=" ($expression (";" (__RETURN__ vardef)))))))
 
 Actual output of below program: (prettified)
 
            ( ($type ($identifier ("(" (")" ("{" ($statements ("}" (__RETURN__ fn_def)))))
                                       ($args (")" ("{" ($statements ("}" (__RETURN__ fn_def)))))))
                                  (";" (__RETURN__ vardef))
                                  ("=" ($expression (";" (__RETURN__ vardef)))))))
 
 Perfect match!
*/

//Node use_tree_to_parse(Node tree,  

void tree_add(Node* n, string rule_name, vector<string>::iterator ruleiter, vector<string>::iterator enditer){
  if (ruleiter == enditer) {
    if (n->leaf_with_value("__RETURN__")) fail("tree_add: This rule already exists in the tree.");
    Node retnode;
    retnode.value = "__RETURN__";
    retnode.leafs.push_back(Node::strwrap(rule_name));
    n->leafs.push_back(retnode);
    return;
  }
  Node* nextnode;
  if (!(nextnode = n->leaf_with_value(*ruleiter))){
    n->leafs.push_back(Node::strwrap(*ruleiter));
    nextnode = n->leaf_with_value(*ruleiter);
  }
  tree_add(nextnode, rule_name, ++ruleiter, enditer);
}

int main(){
  Node tree;
  {
    vector<string> rule;
    rule.push_back("$type");
    rule.push_back("$identifier");
    rule.push_back("(");
    rule.push_back(")");
    rule.push_back("{");
    rule.push_back("$statements");
    rule.push_back("}");
    
    tree_add(&tree, "fn_def", rule.begin(), rule.end());
  }
  {
    vector<string> rule;
    rule.push_back("$type");
    rule.push_back("$identifier");
    rule.push_back("(");
    rule.push_back("$args");
    rule.push_back(")");
    rule.push_back("{");
    rule.push_back("$statements");
    rule.push_back("}");
    
    tree_add(&tree, "fn_def", rule.begin(), rule.end());
  }
  {
    vector<string> rule;
    rule.push_back("$type");
    rule.push_back("$identifier");
    rule.push_back(";");
    
    tree_add(&tree, "vardef", rule.begin(), rule.end());
  }
  {
    vector<string> rule;
    rule.push_back("$type");
    rule.push_back("$identifier");
    rule.push_back("=");
    rule.push_back("$expression");
    rule.push_back(";");
    
    tree_add(&tree, "vardef", rule.begin(), rule.end());
  }
  tree.print();
}

#endif


/*

  Unfortunately the above tree-based solution does not account for grammars with recursion within a rule.
  These rules will have to be manually expanded into subrules that call themselves.
 
  However alternatives do exist.
  Below, we show a possible graph based solution that solely keeps track of edges.

 (Note: The below breath-first searcher was rewritten twice and has now been abandoned. 
  I found the tight binding of lookahead and parsing to be inelegant.
  Instead see test2.cpp, commit 9cf1bf76d179f5a860092a22d75ff7dec2d78c19 for a nicer version.)
 
*/


struct StringStream {
  typedef boost::shared_ptr<StringStream> Ptr;
  string buffer;
  Ptr create(int eat_len=0){
    Ptr newSS(new StringStream);
    newSS->buffer = buffer;
    newSS->buffer.erase(0, eat_len);
    return newSS;
  }
  bool match(const string& other){
    if (other.size() > buffer.size()) return false;
    for (int i=0;i<other.size();++i)
      if (other[i] != buffer[i])
        return false;
    return true;
  }
  bool iseof(){ return buffer.size()==0; }
  char get(int pos){
    if (pos >= buffer.size()) return 0;
    return buffer[pos];
  }
};

typedef boost::function<bool (StringStream*, int*)> MatcherFunc;

struct Rule {
  typedef boost::shared_ptr<Rule> Ptr;
  struct Edge {
    typedef boost::shared_ptr<Rule::Edge> Ptr;
    Rule* parent;
    string matcher;
    vector<Rule::Edge::Ptr> nexts;
    
    bool isReturn;
    string returnName;
  };
  string name;
  Rule::Edge::Ptr start;
  Rule::Edge::Ptr create_edge(string matcher, bool isReturn=false, string returnName=string()){
    Rule::Edge::Ptr ret(new Rule::Edge);
    ret->parent = this;
    ret->matcher = matcher;
    ret->isReturn = isReturn;
    ret->returnName = returnName;
    return ret;
  }
};

map<string, Rule> rules;
map<string, boost::function<bool (StringStream::Ptr, int*, Node*)> > special_matcher;

struct ExecutingContext {
  typedef boost::shared_ptr<ExecutingContext> Ptr;
  Rule::Edge::Ptr current_edge;
  ExecutingContext::Ptr parent;
  StringStream::Ptr input_stream;
  Node ret_node;
  
  Ptr create_new(Rule::Edge::Ptr edge, int stream_eat_len=0){
    Ptr ret(new ExecutingContext);
    ret->current_edge = edge;
    ret->parent = parent;
    ret->input_stream = input_stream->create(stream_eat_len);
    ret->ret_node = ret_node;
  }
  Ptr create_new_with_node(Rule::Edge::Ptr edge, int stream_eat_len, Node n){
    Ptr ret = create_new(stream_eat_len);
    ret.ret_node.push_back(n);
  }
  Ptr create_child(Rule::Edge::Ptr edge){
    Ptr ret(new ExecutingContext);
    ret->current_edge = edge;
    ret->parent = Ptr(new ExecutingContext);
    ret->parent->current_edge = current_edge;
    ret->parent->parent = parent;
    ret->parent->ret_node = ret_node;
    //ret->parent->input_stream ignored. Will be restored in create_parent.
    ret->input_stream = input_stream->create();
    //ret->ret_node should be new and fresh
  }
  Ptr create_parent(string ret_node_name){
    Ptr ret(new ExecutingContext);
    ret->current_edge = parent->current_edge;
    ret->parent = parent->parent;
    ret->input_stream = input_stream->create();
    ret->ret_node = parent->ret_node;
    this->ret_node.value = ret_node_name;
    ret->ret_node.push_back(this->ret_node);
  }
  
  Ptr test_edge(Rule::Edge::Ptr edge){
    if (edge.matcher[0] == '$'){
      if (edge.matcher[1] == '$'){
        int stream_eat_len; Node n;
        if (special_matcher[current_edge.matcher](input_stream, &stream_eat_len, &n)){
          return create_new_with_node(edge, stream_eat_len, n);
        } else {
          return Ptr(0);
        }
      } else {
        //TODO: May need to deal with left-recursiveness someday
        return create_child(rules[edge.matcher].start);
      }
    } else if (input_stream->match(edge.matcher)){
      return create_new(edge, edge.matcher.length());
    } else {
      return Ptr(0);
    }
  }
  
  virtual vector<ExecutingContext::Ptr> step(){
    vector<ExecutingContext::Ptr> ret;
    
    for (int i=0;i<current_edge.nexts.size();++i){
      Ptr newEC = test_edge(current_edge.nexts[i]);
      if (newEC) ret.push_back(newEC);
    }
    
    if (current_edge.isReturn){
      Ptr newEC = create_parent(current_edge.returnName);
      ret.push_back(newEC);
    }    
    
    return ret;
  }
};

void build_tables(){
  StringStream::Ptr ss(new StringStream);
  ss->buffer = "a+a";
  
  rules["$ident"].start = rules["$ident"].create_edge("");
}

#endif