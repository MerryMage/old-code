#ifndef __ASH__PGRAPH_H__
#define __ASH__PGRAPH_H__

#include <vector>
#include <map>
#include <string>
using std::map;
using std::vector;
using std::string;

#include "node.h"

// Class Definitions

struct Context;
struct Grammar;
struct REI;
struct Rule;
struct RuleEdge;

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

struct Context {
  struct StackLog {
    bool isToChildRule() { return direction == +1; }
    bool isToSuperRule() { return direction == -1; }
    string getRetName()  { return retName; }
    string getRulename() { return rulename; }
  private:
    friend struct Context;
    StackLog(string rulename, string retName, signed char direction) : rulename(rulename), retName(retName), direction(direction) {}
    string rulename, retName; signed char direction;
  };
  
  string getMatcher() const;
  const REI& getrei() const { return rei_current; }
  vector<StackLog> getStackLog() const { return stack_log; }
  bool isEndOfInput() const { return end_of_input; }
  
  //For debugging purposes
  void dump() const;
  string return_stack_string() const;
private:
  friend struct RuleEdge;
  friend struct Grammar;
  Context(Grammar* grmr, REI rei=REI()) : grammar(grmr), rei_current(rei), return_stack(), end_of_input(false) { if (rei.rulename == "") rei_current.uniqueid = 0; }
  Context(const Context& ctx, REI rei) : grammar(ctx.grammar), rei_current(rei), return_stack(ctx.return_stack), end_of_input(false) {}
  void setIsToSuperRule(string ruleName, string retName) { stack_log.push_back(StackLog(ruleName, retName, -1)); }
  void setIsToChildRule(string ruleName) { stack_log.push_back(StackLog(ruleName, "", +1)); }
  void setIsEndOfInput(string ruleName, string retName) {
    setIsToSuperRule(ruleName, retName);
    end_of_input = true;
  }
  
  bool end_of_input;
  REI rei_current;
  Grammar* grammar;
  vector<REI> return_stack;
  vector<StackLog> stack_log;
};

struct Grammar {
  vector<string> current_firsts_lookup_stack;
  Rule* getRule(string rulename);
  Rule* newRule(string rulename);
  RuleEdge* get(REI);
  vector<Context> startRule(Rule*);
  vector<Context> startRule_NR(Rule*);
  vector<Context> step(const Context&);
  vector<Context> first_set(const Context&); //<-- Unusual function for NR use only (May not work as one may expect)
  vector<Context> step_NR(const Context&);
private:
  map<string, Rule> rules;
};

struct Rule {
  RuleEdge* get(REI);
  vector<Context> getFirsts(const Context&, Grammar*);
  vector<Context> getFirsts_NR(const Context&, Grammar*);
  void registerRule(Node n);
  void init(string rulename);
  const string& getRulename(){ return rulename; }
  
private:
  map<REI, RuleEdge> edges;
  REI first;
  string rulename;
  
  REI rule_create_edge(string);
  REI rule_register_helper(REI, const Node&, int);
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

bool matcher_is_terminal(const string& matcher);

// `RuleEdge` definition (Internal Use Only)

struct RuleEdge {
  RuleEdge(REI rei, string m) : identifier(rei), matcher(m), isReturnable(false) {}
  REI identifier;
  string matcher;
  vector<REI> next;
  vector<Context> getNexts(const Context&, Grammar*);
  vector<Context> getNexts_NR(const Context&, Grammar*);
  bool isNull(){ return matcher == "$$null"; }
  bool isTerminal(){ return matcher_is_terminal(matcher); }
  bool isReturnable;
  string retName;
private:
  vector<Context> internal__get_return_set(const Context& prev_ctx, Grammar* grmr);
};

#endif // __ASH__PGRAPH_H__