#ifndef __ASH_PARSER_H__
#define __ASH_PARSER_H__

#include <vector>
#include <string>
using std::string;

// Cheap lists (cheap as in easy to implement)

struct Node {
  string value;
  std::vector<Node> leafs;
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
  static inline Node strwrap(string str){
    Node ret; ret.value = str; return ret;
  }
};

void build_tables();
void build_stmt_table();

void next_token(); //needs to be called
Node statement();
Node stmt_func();

//parser.cpp::nud_parentheses needs this, and it is to be defined in llvm.cpp
bool is_typename(string);

#endif