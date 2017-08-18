  #include <map>
#include <vector>
#include <string>

using std::map;
using std::vector;
using std::string;

/*#include "pgraph.h"

std::vector<std::string> normalize_firsts(Grammar* grmr, string rulename, bool* has_null=0){
  if (has_null) *has_null = false;
  Rule* rule = grmr->getRule(rulename);
  vector<Context> ctxen = grmr->startRule(rule);
  vector<std::string> ret;
  for (int i=0;i<ctxen.size();++i){
    string matcher = ctxen[i].getMatcher();
    if (ctxen[i].isEndOfInput()){
      if (has_null) *has_null = true;
      continue;
    }
    if (!contains(ret, matcher))
      ret.push_back(matcher);
  }
  return ret;
}*/

#include "node.h"

template <typename IdentType>
class DirectedGraph {
  typedef std::map<IdentType, std::vector<IdentType> > MapType;
  MapType _data;
public:
  bool join(IdentType from_node, IdentType to_node){
    if (!contains(_data[from_node], to_node)){
      _data[from_node].push_back(to_node);
      _data[to_node]; //Access required for dot_dump to work properly
      return false;
    } else return true;
  }
  std::vector<IdentType> next(IdentType from_node){
    return _data[from_node];
  }
  void dot_dump(){
    int next_uniq = 0;
    map<IdentType, int> uniqs;
    
    typename MapType::iterator iter = _data.begin();
    while (iter != _data.end()){
      uniqs[iter->first] = next_uniq++;
      ++iter;
    }
    
    printf("strict digraph _GRAPH_NAME {\n");
    printf("  edge [arrowhead=normal];\n");
    
    iter = _data.begin();
    while (iter != _data.end()){
      int this_uniq = uniqs[iter->first];
      printf("  n%i [label=\"%s\"];\n", this_uniq, iter->first.to_string().c_str());
      
      std::vector<IdentType>& nexts = iter->second;
      for (int i=0;i<nexts.size();++i){
        printf("  n%i -> n%i;\n", this_uniq, uniqs[nexts[i]]);
      }
      ++iter;
    }
    printf("}\n");
  }
};
/*
template <typename FirstType, typename SecondType>
class OneToMany {
  typedef std::map<FirstType, std::vector<SecondType> > MapType;
  MapType _data;
public:
  void join(FirstType from_node, SecondType to_node){
    if (!contains(_data[from_node], to_node))
      _data[from_node].push_back(to_node);
  }
  std::vector<FirstType> next(SecondType from_ndoe){
    return _data[from_node];
  }
};


struct REI { //RuleEdgeIdentifier
  string rulename;
  int uniqueid;
  //STL requirement:
  bool operator < (const REI& other) const {
    if (rulename == other.rulename) return uniqueid < other.uniqueid;
    return rulename < other.rulename;
  }
  bool operator == (const REI& other) const {
    return (rulename == other.rulename) && (uniqueid == other.uniqueid);
  }
};

template <typename MATCHER>
struct Rule2 {
  Rule2(string rulename) : rulename(rulename), rei_uniqid_next(0) {}
  
  const string& getRulename() { return rulename; }
  const MATCHER& getMatcher(REI rei) { return matchers[rei]; }
  const vector<REI> getNext(REI rei) { return graph.next(rei); }
  
private:
  DirectedGraph<REI> graph;
  map<REI, MatcherType> matchers;
  string rulename;
  MATCHER matcher;
  int rei_uniqid_next;
  REI newrei(){
    REI ret;
    ret.rulename = rulename;
    do {
      ret.uniqueid = ++rei_uniqid_next;
    } while (edges.find(ret) != edges.end());
    return ret;
  }
};

template <typename RULE>
struct Grammar2 {
  typedef map<string, Rule> RuleMap;
  RuleMap rules;
  void addRule(string rulename, Node);
};*/

#include <cstdio>
#include <ctype.h>

enum state_enum {
  STATE_INITIAL,
  STATE_S,
  STATE_S_1,
  STATE_S_2,
  STATE_S_1_1,
  STATE_S_1_2,
  STATE_S_1_3,
  STATE_S_2_1
};

struct Quux {
  Quux(int state, int token_position) : state(state), token_position(token_position) {}
  int state;
  int token_position;
  string to_string() const { char buf[255]; sprintf(buf, "%i/%i", state, token_position); return buf; }
  bool operator < (const Quux& o) const { 
    if (state == o.state) return token_position < o.token_position;
    return state < o.state;
  }
  bool operator != (const Quux& o) const { return (state != o.state) || (token_position != o.token_position); }
  bool operator == (const Quux& o) const { return (state == o.state) && (token_position == o.token_position); }
};

struct FooBar {
  int state;
  Quux c_u;
  FooBar(int state, Quux c_u) : state(state), c_u(c_u) {}
  string to_string() const { char buf[255]; sprintf(buf, "%i/%s", state, c_u.to_string().c_str()); return buf; }
  bool operator < (const FooBar& o) const { 
    if (state == o.state) return c_u < o.c_u;
    return state < o.state;
  }
  bool operator == (const FooBar& o) const { return (state == o.state) && (c_u == o.c_u); }
};

typedef map<Quux, vector<int> > P_set_type;

Quux gss_create(int next_state, Quux c_u, int token_position, DirectedGraph<Quux>* gss, P_set_type *P_set){
  Quux new_node(next_state, token_position);
  if (!gss->join(new_node, c_u)){
    for (int i=0;i<(*P_set)[new_node].size();++i){
      printf("This implementation doesn't handle left recursion yet.");
      exit(-2);
    }
  }
  printf("new_gss:%s\n", c_u.to_string().c_str());
  return new_node;
}

#define ADD_NOW(_STATE, QUUX) \
{ \
  FooBar fb(_STATE, QUUX); \
  if (!contains(current_set, fb)) { \
    if (debug) printf("now:%s\n", fb.to_string().c_str()); \
    work_set.push_back(fb); \
    current_set.push_back(fb); \
  } \
}

#define ADD_NEXT(_STATE, QUUX) \
{ \
  FooBar fb(_STATE, QUUX); \
  if (debug) printf("next:%s\n", fb.to_string().c_str()); \
  next_set.push_back(fb); \
}

#define STATE(_STATE) \
STATE_##_STATE: \
case STATE_##_STATE: \
  printf("STATE_" #_STATE " (%i/%i)\n", STATE_##_STATE, token_position);

/*
Grammar:
 start -> S
 S -> x S x | x
 
The following is a prototype for what *should* be genereated by a GLL parser generator.
*/

int main(){
  DirectedGraph<Quux> gss;
  Quux c_u(STATE_INITIAL, 0);
  Quux u_nought(-1, -1);
  gss.join(c_u, u_nought);
  vector<FooBar> current_set, next_set, work_set;
  current_set.push_back(FooBar(STATE_INITIAL, c_u));
  work_set = current_set;
  P_set_type P_set;
  int token;
  int token_position = -1;
  bool debug=true;
  
  while (true){
    token = getchar();
    while (isspace(token)) token = getchar();
    token_position++;
    
    while (work_set.size() != 0){
      int state = work_set.back().state;
      c_u = work_set.back().c_u;
      work_set.pop_back();
      
      switch (state) {
        STATE(INITIAL) // start -> . S
          if (token == 'x'){
            goto STATE_S;
          } else {
            printf("Failure at start!\n");
            return -1;
          }
          break;
        STATE(S) // S -> x S x | x
          //This type of place could probably SERIOUSLY be OPTIMIZED by first sets 
          //analysis or SOMETHING FUNKY to remove the duplicate comparison or even merge the states
          if (token == 'x'){
            ADD_NOW(STATE_S_1, c_u);
          }
          if (token == 'x'){
            ADD_NOW(STATE_S_2, c_u);
          }
          break;
        STATE(S_1) // S -> . x S x
          ADD_NEXT(STATE_S_1_1, c_u);
          break;
        STATE(S_1_1) // S -> x . S x
          if (token == 'x'){
            c_u = gss_create(STATE_S_1_2, c_u, token_position, &gss, &P_set);
            goto STATE_S;
          } else break;
        STATE(S_1_2) // S -> x S . x
          if (token == 'x'){
            ADD_NEXT(STATE_S_1_3, c_u);
            break;
          } else break;
        STATE(S_1_3) // S -> x S x .
          goto DO_POP;
        STATE(S_2) // S -> . x
          ADD_NEXT(STATE_S_2_1, c_u);
          break;
        STATE(S_2_1) // S -> x .
          goto DO_POP;
        DO_POP:
          if (debug) printf("DO_POP(%i)\n", token_position);
          if (c_u != u_nought){
            P_set[c_u].push_back(token_position);
            vector<Quux> children = gss.next(c_u);
            for (int i=0;i<children.size();++i){
              FooBar fb(c_u.state, children[i]);
              ADD_NOW(c_u.state, children[i]);
            }
          }
          break;
      }  
    }
    
    if (token == EOF) break;
    
    current_set = next_set;
    work_set = next_set;
    next_set = vector<FooBar>();
  }
  
  if (debug){
    gss.dot_dump();
    for (int i=0;i<current_set.size();++i)
      printf("%s\n", current_set[i].to_string().c_str());
  }
  
  FooBar success_indicator(STATE_INITIAL, u_nought);
  if (contains(current_set, success_indicator)) printf("Success!\n");
  else printf("Failure at end!\n");
  
}