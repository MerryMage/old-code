#include "gll_library.hpp"


//USER DEFINED AREA START
#include <cstdio>
#include <ctype.h>
int next_token(){
  int ret = getchar();
  while (isspace(ret)) ret=getchar();
  return ret;
}
bool doMatch(const char* matcher, int token){
  if (matcher == "$$eof") return token == EOF;
  return matcher[0] == token;
}
void gll_parser();
int main(){gll_parser();}
//USER DEFINED AREA END


void gll_parser(){
#define EOF (-1)
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
    token = next_token();
    token_position++;
    while (work_set.size() != 0) {
      int state = work_set.back().state;
      c_u = work_set.back().c_u;
      work_set.pop_back();
      switch (state){
      STATE(INITIAL) // start -> [.] $expr
        if (doMatch("x", token) ||
            doMatch("x", token) ||
            doMatch("x", token) ||
            /* This case with $$eof should only ever happen in STATE_INITIAL. If this is appearing somewhere else... well, let's just say it shouldn't happen. */
            doMatch("$$eof", token)) {
          goto STATE_expr;
        }
        break;
      STATE(expr) // $expr
        if (doMatch("x", token)) {
          // $expr -> [.] $A ...
          ADD_NOW(STATE_expr_2_ENTRY, c_u);
        }
        if (doMatch("x", token) ||
            doMatch("x", token)) {
          // $expr -> [.] $C ...
          ADD_NOW(STATE_expr_4_ENTRY, c_u);
        }
        // $expr -> [.]
        goto DO_POP;
        break;
      STATE(expr_2_ENTRY) // $expr -> [.] $A ...
        c_u = gss_create(STATE_expr_2, c_u, token_position, &gss, &P_set);
        goto STATE_A;
      STATE(expr_2) // $expr -> $A [.] ...
        if (doMatch("a", token)) {
          // $expr -> $A [.] a ...
          ADD_NEXT(STATE_expr_3, c_u);
        }
        break;
      STATE(expr_3) // $expr -> $A a [.] ...
        // $expr -> $A a [.]
        goto DO_POP;
        break;
      STATE(expr_4_ENTRY) // $expr -> [.] $C ...
        c_u = gss_create(STATE_expr_4, c_u, token_position, &gss, &P_set);
        goto STATE_C;
      STATE(expr_4) // $expr -> $C [.] ...
        if (doMatch("c", token)) {
          // $expr -> $C [.] c ...
          ADD_NEXT(STATE_expr_5, c_u);
        }
        break;
      STATE(expr_5) // $expr -> $C c [.] ...
        // $expr -> $C c [.]
        goto DO_POP;
        break;
      STATE(A) // $A
        if (doMatch("x", token)) {
          // $A -> [.] $B ...
          ADD_NOW(STATE_A_2_ENTRY, c_u);
        }
        break;
      STATE(A_2_ENTRY) // $A -> [.] $B ...
        c_u = gss_create(STATE_A_2, c_u, token_position, &gss, &P_set);
        goto STATE_B;
      STATE(A_2) // $A -> $B [.] ...
        if (doMatch("b", token)) {
          // $A -> $B [.] b ...
          ADD_NEXT(STATE_A_3, c_u);
        }
        break;
      STATE(A_3) // $A -> $B b [.] ...
        // $A -> $B b [.]
        goto DO_POP;
        break;
      STATE(B) // $B
        if (doMatch("x", token) ||
            doMatch("x", token) ||
            doMatch("x", token) ||
            doMatch("x", token)) {
          // $B -> [.] $expr ...
          ADD_NOW(STATE_B_2_ENTRY, c_u);
        }
        break;
      STATE(B_2_ENTRY) // $B -> [.] $expr ...
        c_u = gss_create(STATE_B_2, c_u, token_position, &gss, &P_set);
        goto STATE_expr;
      STATE(B_2) // $B -> $expr [.] ...
        if (doMatch("x", token)) {
          // $B -> $expr [.] x ...
          ADD_NEXT(STATE_B_3, c_u);
        }
        break;
      STATE(B_3) // $B -> $expr x [.] ...
        // $B -> $expr x [.]
        goto DO_POP;
        break;
      STATE(C) // $C
        if (doMatch("x", token)) {
          // $C -> [.] $B ...
          ADD_NOW(STATE_C_2_ENTRY, c_u);
        }
        if (doMatch("x", token)) {
          // $C -> [.] $A ...
          ADD_NOW(STATE_C_4_ENTRY, c_u);
        }
        break;
      STATE(C_2_ENTRY) // $C -> [.] $B ...
        c_u = gss_create(STATE_C_2, c_u, token_position, &gss, &P_set);
        goto STATE_B;
      STATE(C_2) // $C -> $B [.] ...
        if (doMatch("d", token)) {
          // $C -> $B [.] d ...
          ADD_NEXT(STATE_C_3, c_u);
        }
        break;
      STATE(C_3) // $C -> $B d [.] ...
        // $C -> $B d [.]
        goto DO_POP;
        break;
      STATE(C_4_ENTRY) // $C -> [.] $A ...
        c_u = gss_create(STATE_C_4, c_u, token_position, &gss, &P_set);
        goto STATE_A;
      STATE(C_4) // $C -> $A [.] ...
        if (doMatch("e", token)) {
          // $C -> $A [.] e ...
          ADD_NEXT(STATE_C_5, c_u);
        }
        break;
      STATE(C_5) // $C -> $A e [.] ...
        // $C -> $A e [.]
        goto DO_POP;
        break;

      exit(-2);
      case DO_POP:
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
      }

    }
    if (token == EOF) break;
    if (next_set.size() == 0) printf("Failure at position %i\n", token_position);

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


enum ___STATE___ {
  STATE_INITIAL,
  STATE_expr,
  STATE_A,
  STATE_B,
  STATE_C,
  STATE_expr_2,
  STATE_expr_2_ENTRY,
  STATE_expr_3,
  STATE_expr_4,
  STATE_expr_4_ENTRY,
  STATE_expr_5,
  STATE_A_2,
  STATE_A_2_ENTRY,
  STATE_A_3,
  STATE_B_2,
  STATE_B_2_ENTRY,
  STATE_B_3,
  STATE_C_2,
  STATE_C_2_ENTRY,
  STATE_C_3,
  STATE_C_4,
  STATE_C_4_ENTRY,
  STATE_C_5,
  DO_POP
};
