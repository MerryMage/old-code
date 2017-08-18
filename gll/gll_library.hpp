#include <map>
#include <vector>
#include <string>

using std::map;
using std::vector;
using std::string;

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
    if ((*P_set)[new_node].size())
      printf("WARNING: This implementation may not handle left recursion properly.\n");
    for (int i=0;i<(*P_set)[new_node].size();++i){
      //TODO: Check if (*P_set)[new_node][i] has already been analzyed, otherwise fail dramatically.
      //My gut tells me that it would always have already been, though I'm unable to prove it, hence this.
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
  if (contains(next_set, fb)) printf("WARNING: Possible dupe state\n"); \
  if (debug) printf("next:%s\n", fb.to_string().c_str()); \
  next_set.push_back(fb); \
}

#define STATE(_STATE) \
STATE_##_STATE: \
case STATE_##_STATE: \
if (debug) printf("STATE_" #_STATE " (%i/%i)\n", STATE_##_STATE, token_position);

