#ifndef __ASH__NODE_H_
#define __ASH__NODE_H_

#include <string>
#include <vector>

// Cheap lists (cheap as in easy to implement)

struct Node {
  std::string value;
  std::vector<Node> leafs;
  
  bool        isleaf()   const {return leafs.size() == 0;}
  int         numleafs() const {return leafs.size();}
  Node&       get(int i)       {return leafs[i];}
  const Node& get(int i) const {return leafs[i];}
  void        print()    const {
    if (isleaf()) {
      printf("%s", value.c_str());
      return;
    }
    printf("(");
    printf("%s", value.c_str());
    for (int i=0;i<leafs.size();i++){
      printf(" ");
      leafs[i].print();
    }
    printf(")");
  }
  
  static inline Node strwrap(std::string str){
    Node ret; ret.value = str; return ret;
  }
};

//void Node::print() const 

// vector functions

template <typename T>
void append(std::vector<T>& a, const std::vector<T> b){
  a.insert(a.end(), b.begin(), b.end());
}

template <typename T>
bool contains(const std::vector<T>& a, const T& b){
  for (int i=0;i<a.size();++i){
    if (a[i] == b) return true;
  }
  return false;
}

#endif