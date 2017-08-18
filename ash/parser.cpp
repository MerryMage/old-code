#include "parser.h"
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <boost/bind.hpp>
#include <boost/function.hpp>

using std::string;
using std::map;

// Lookup Tables

map<string, boost::function<Node()> > nud;
map<string, boost::function<Node(Node)> > led;
map<string, int> lbp;

// Type reader

Node stmt_read_type();

// "Lexer"

string token;
/*#include <queue>
 std::queue<string> test_token_stream;
 string next_token(){
 string ret = test_token_stream.front();
 test_token_stream.pop();
 return ret;
 }*/

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

// Core of algorithm

Node literal(string a, string b){
  Node ret;
  ret.value = a;
  ret.leafs.push_back(Node::strwrap(b));
  return ret;
}

Node expression(int rbp=0){
  Node left;
  bool donud = false;
  string t = token;
  if (token_type == tok_double) left = literal("double", t);
  else if (token_type == tok_int) left = literal("int", t);
  else if (token_type == tok_ident) left.value = t;
  else donud = true;
  next_token();
  
  if (donud) left = nud[t]();
  
  while (rbp < lbp[token]){
    t = token;
    next_token();
    left = led[t](left);
  }
  return left;
}

// Common definitions

void token_expect(string tok){
  if (token != tok) throw "token_expect failed";
  next_token();
}

Node led_binary(string name, int rbp_, Node left){
  Node right = expression(rbp_);
  Node ret;
  ret.value = name;
  ret.leafs.push_back(left);
  ret.leafs.push_back(right);
  return ret;
}

Node nud_prefix(string name, int rbp_){
  Node right = expression(rbp_);
  Node ret;
  ret.value = name;
  ret.leafs.push_back(right);
  return ret;
}

Node led_postfix(string name, Node left){
  Node ret;
  ret.value = name;
  ret.leafs.push_back(left);
  return ret;
}

Node led_error(Node left){ throw "Hit an ERROR_SYMBOL on led."; }
Node nud_error(){ throw "Hit an ERROR_SYMBOL on nud."; }

// Easy defines for commons

void define_marker(string name){
  lbp[name] = -1;
  led[name] = &led_error;
  nud[name] = &nud_error;
}

void define_prefix(string name, int rbp_){
  if (nud.find(name) != nud.end()) throw "define_prefix on symbol already in nud";
  nud[name] = boost::bind(&nud_prefix, name + ":prefix", rbp_);
}

void define_binary(string name, int lbp_){
  if (lbp.find(name) != lbp.end()) throw "define_binary on symbol already in lbp";
  if (led.find(name) != led.end()) throw "define_binary on symbol already in led";
  lbp[name] = lbp_;
  led[name] = boost::bind(&led_binary, name, lbp_, _1); //rbp == lbp
}

void define_binary_r(string name, int lbp_){ //right-to-left associativity
  if (lbp.find(name) != lbp.end()) throw "define_binary_r on symbol already in lbp";
  if (led.find(name) != led.end()) throw "define_binary_r on symbol already in led";
  lbp[name] = lbp_;
  led[name] = boost::bind(&led_binary, name, lbp_-1, _1); //rbp == lbp-1
}

void define_postfix(string name, int lbp_){
  if (lbp.find(name) != lbp.end()) throw "define_postfix on symbol already in lbp";
  if (led.find(name) != led.end()) throw "define_postfix on symbol already in led";
  lbp[name] = lbp_;
  led[name] = boost::bind(&led_postfix, name + ":postfix", _1);
}

// Customs

Node led_comma(Node left, int rbp_){
  Node ret;
  ret.value = ",";
  ret.leafs.push_back(left);
  while (true){
    ret.leafs.push_back(expression(rbp_));
    if (token != ",") break;
    next_token();
  }
  return ret;
}

/*
 * The current version implicitly uses the call stack as the stack for parentheses and brackets.
 * A more explicit solution might aid error messages.
 */

int cast_rbp;
Node nud_parentheses(){
  if (is_typename(token)){
    Node ret2;
    ret2.value = "(cast)";
    ret2.leafs.push_back(stmt_read_type());
    token_expect(")");
    ret2.leafs.push_back(expression(cast_rbp));
    return ret2;
  }
  
  Node ret = expression();
  token_expect(")");
  
  return ret;
}

Node led_func_call(Node left){
  //if (!left.isleaf()) throw "Function Call where function name is not an atom."; 
  //RE: Above. That was stupid since a function can be caluclated based on it's address. Removing.
  
  Node ret;
  ret.value = "funccall";
  ret.leafs.push_back(left);
  while (token != ")"){
    ret.leafs.push_back(expression(50));
    if (token != ",") break;
    token_expect(",");
  }
  token_expect(")");
  return ret;
}

Node led_bracket(Node left){
  Node ret;
  ret.value = "[]";
  ret.leafs.push_back(left);
  ret.leafs.push_back(expression());
  token_expect("]");
  return ret;
}

Node led_inline_if(Node left, int rbp_){
  Node ret;
  ret.value = "?:";
  ret.leafs.push_back(left);
  //Just keep eating until ":". 
  //Using rbp_ here causes this failure case: 3 ? x = 5 : 4.
  //Next token is "=" when the following statement expects a ":" (because rbp is 149 while lbp["="] is 100)
  ret.leafs.push_back(expression());
  token_expect(":");
  //Here we use rbp_. Whether this is standards-compliant is anybody's guess, since (x ? y : z) = 4 doesn't make sense anyway.
  ret.leafs.push_back(expression(rbp_));
  return ret;
}

// Declarations

void build_tables(){
  lbp["(end)"] = 0; //Must be lowest.
  
  lbp[","] = 50;
  led[","] = boost::bind(&led_comma, _1, 50);
  
  define_binary_r("=", 100);
  define_binary_r("+=", 100);
  define_binary_r("-=", 100);
  define_binary_r("/=", 100);
  define_binary_r("*=", 100);
  define_binary_r("%=", 100);
  define_binary_r("&=", 100);
  define_binary_r("^=", 100);
  define_binary_r("|=", 100);
  define_binary_r("<<=", 100);
  define_binary_r(">>=", 100);
  
  //Terinary (a ? b : c)
  lbp["?"] = 150;
  led["?"] = boost::bind(&led_inline_if, _1, 149); //149 because it's right-to-left associative
  define_marker(":");
  
  define_binary("||", 200);
  define_binary("&&", 300);
  define_binary("|", 400);
  define_binary("^", 500);
  define_binary("&", 600);
  
  define_binary("==", 700);
  define_binary("!=", 700);
  
  define_binary("<", 800);
  define_binary(">", 800);
  define_binary("<=", 800);
  define_binary(">=", 800);
  
  define_binary("<<", 900);
  define_binary(">>", 900);
  
  define_binary("+", 1000);
  define_binary("-", 1000);
  
  define_binary("*", 1100);
  define_binary("/", 1100);
  define_binary("%", 1100);
  
  define_prefix("sizeof", 1200);
  define_prefix("&", 1200);
  define_prefix("*", 1200);
  //(cast) goes here.
  cast_rbp = 1200;
  define_prefix("!", 1200);
  define_prefix("~", 1200);
  define_prefix("+", 1200);
  define_prefix("-", 1200);
  define_prefix("++", 1200);
  define_prefix("--", 1200);
  
  define_postfix("++", 1300);
  define_postfix("--", 1300);
  define_binary("->", 1300); //SPECIAL -- do later
  define_binary(".", 1300); //SPECIAL -- do later
  //[]
  lbp["["] = 1300;
  led["["] = &led_bracket;
  define_marker("]");
  //(), a()
  lbp["("] = 1300;
  nud["("] = &nud_parentheses;
  led["("] = &led_func_call;
  define_marker(")");
  
  define_marker(";");
}

// Statement Level 

map<string, boost::function<Node()> > stmt;

//"var" for local variables
//"globalvar" for global variables
//TYPE_NAME VAR_NAME ==> (var TYPE_NAME VAR_NAME)
//TYPE_NAME VAR_NAME = INITALIZER ; ==> (var TYPE_NAME VAR_NAME INITALIZER)
//global TYPE_NAME VAR_NAME ==> (globalvar TYPE_NAME VAR_NAME)
//global TYPE_NAME VAR_NAME = INITALIZER ; ==> (globalvar TYPE_NAME VAR_NAME INITALIZER)
Node var_def(string nodename){
  Node ret;
  ret.value = nodename;
  ret.leafs.push_back(stmt_read_type());
  if (token_type != tok_ident) throw "var/globalvar expected an identifer here";
  ret.leafs.push_back(Node::strwrap(token));
  next_token();
  if (token == "=") {
    token_expect("=");
    ret.leafs.push_back(expression(50)); //Can support comma definitions later.
  }
  token_expect(";");
  return ret;
}

Node statement(){
  if (token == ";") {
    next_token();
    return Node();
  }
  if (is_typename(token)) return var_def("var");
  if (stmt.find(token) != stmt.end()){
    string t = token;
    next_token();
    return stmt[t]();
  }
  Node n = expression();
  token_expect(";");
  return n;
}

// { STMT_1 STMT_2 ... } ==> ({} STMT_1 STMT_2 ...)
Node statements(){
  Node ret;
  ret.value = "{}";
  while (token != "}") {
    ret.leafs.push_back(statement());
  }
  token_expect("}");
  return ret;
}
//if ( CONDITION ) IF_TRUE ==> (if CONDITION IF_TRUE)
//if ( CONDITION ) IF_TRUE else IF_FALSE ==> (if CONDITION IF_TRUE IF_FALSE)
Node stmt_if(){
  Node ret;
  ret.value = "if";
  token_expect("(");
  ret.leafs.push_back(expression());
  token_expect(")");
  ret.leafs.push_back(statement());
  if (token == "else") {
    token_expect("else");
    ret.leafs.push_back(statement());
  }
  return ret;
}
//return RET_VAL ; ==> (return RET_VAL)
//return ; ==> (return void)
Node stmt_return(){
  Node ret;
  ret.value = "return";
  if (token != ";") ret.leafs.push_back(expression());
  else ret.leafs.push_back(Node::strwrap("void"));
  token_expect(";");
  return ret;
}
//define RET_TYPE FUNC_NAME ( ARG_TYPE_0 ARG_NAME_0, ARG_TYPE_1 ARG_NAME_1, ... ) { FUNC_BODY }
// ==> (deffunc RET_TYPE FUNC_NAME (ARGS) FUNC_BODY) 
//define RET_TYPE STRUCT_NAME :: FUNC_NAME ( ARG_TYPE_0 ARG_NAME_0, ARG_TYPE_1 ARG_NAME_1, ... ) { FUNC_BODY }
// ==> (defmethod RET_TYPE STRUCT_NAME FUNC_NAME (ARGS) FUNC_BODY)
Node stmt_func(){
  Node ret;
  ret.value = "deffunc";
  ret.leafs.push_back(stmt_read_type());
  if (token_type != tok_ident) throw "stmt_func expected an identifer here (func name)";
  ret.leafs.push_back(Node::strwrap(token));
  next_token();
  
  if (token == "::"){
    ret.value = "defmethod";
    if (!is_typename(ret.leafs[1].value)) throw "stmt_func: identifier before :: is not a typename.";
    token_expect("::");
    if (token_type != tok_ident) throw "stmt_func expected an identifer here (method name)";
    ret.leafs.push_back(Node::strwrap(token));  
    next_token();
  }
  
  token_expect("(");
  Node args;
  args.value = "args";
  if (token != ")"){
    while (true){
      args.leafs.push_back(stmt_read_type());
      if (token_type != tok_ident) throw "stmt_func expected an identifer here (arg name)";
      args.leafs.push_back(Node::strwrap(token));
      next_token();
      if (token == ")") break;
      token_expect(",");
    }
  }
  ret.leafs.push_back(args);
  token_expect(")");
  token_expect("{");
  ret.leafs.push_back(statements());
  return ret;
}
// struct STRUCT_NAME { MEM_TYPE_0 MEM_NAME_0 ; MEM_TYPE_1 MEM_NAME_1 ; ... } ;
// ==> (defstruct STRUCT_NAME MEM_TYPE_0 MEM_NAME_0 MEM_TYPE_1 MEM_NAME_1 ... )
Node stmt_struct(){
  Node ret;
  ret.value = "defstruct";
  ret.leafs.push_back(Node::strwrap(token));
  next_token();
  token_expect("{");
  while (token != "}"){
    ret.leafs.push_back(stmt_read_type());
    if (token_type != tok_ident) throw "stmt_struct expected an identifer here (element name)";
    ret.leafs.push_back(Node::strwrap(token));
    next_token();
    token_expect(";");
  }
  token_expect("}");
  token_expect(";");
  return ret;
}
Node stmt_end(){
  Node ret;
  ret.value = "(end)";
  return ret;
}

void define_stmt(string name, boost::function<Node()> fn){
  stmt[name] = fn;
  lbp[name] = -2;
}
void define_stmt_term(string name){
  lbp[name] = -2;
}
void build_stmt_table(){
  stmt["{"] = &statements;
  define_stmt_term("}");
  stmt["if"] = &stmt_if;
  define_stmt_term("else");
  stmt["return"] = &stmt_return;
  stmt["define"] = &stmt_func;
  stmt["global"] = boost::bind(&var_def, "globalvar");
  stmt["struct"] = &stmt_struct;
  stmt["(end)"] = &stmt_end;
}

// Type Reader (Implementation)

Node stmt_read_type_wrap(Node in){
  Node ret;
  if (token == "["){
    ret.value = "array";
    ret.leafs.push_back(in);
    token_expect("[");
    if (token_type != tok_int) throw "readtype of array expected int inside of [...]";
    ret.leafs.push_back(Node::strwrap(token));
    next_token();
    token_expect("]");
    return stmt_read_type_wrap(ret);
  } else if (token == "*") {
    ret.value = "ptr";
    ret.leafs.push_back(in);
    token_expect("*");
    return stmt_read_type_wrap(ret);
  }
  return in;
}
Node stmt_read_type(){
  if (!is_typename(token)) throw "stmt_read_type expects a typename";
  Node ret;
  ret.value = token;
  next_token();
  return stmt_read_type_wrap(ret);
}
// test cases:
// int[20][13] ==> (array (array int 20) 13)
// double*** ==> (ptr (ptr (ptr double)))
// bool*[34] ==> (array (ptr bool) 34)
