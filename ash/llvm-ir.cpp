// LLVM IR Builder
#include "parser.h"
#include "llvm-ir.h"
#include <llvm/DerivedTypes.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/BasicBlock.h>
#include <llvm/InstrTypes.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Support/IRBuilder.h>


#include <iostream>
#include <vector>
using std::vector;

llvm::ConstantInt* CONST_INT_ZERO;
#define LLVM_GET_INT32(number) llvm::ConstantInt::get(llvm::getGlobalContext(), llvm::APInt(32, (number)))
EvalNodePtr VOID_NODE;

//Insertion Point

llvm::BasicBlock* current_insert_bb;

void SetInsertBB(llvm::BasicBlock* block){
  current_insert_bb = block;
}


//Scoping and Cleanup

struct TmpScope {
  llvm::Block* first_cleanup_block;
  llvm::Block* last_cleanup_block;
  map<string, LocalVariable::Ptr> mapping;
};

typedef std::stack<TmpScope> Bookkeeping_ScopeStack;
Bookkeeping_ScopeStack bookkeeping_ScopeStack;

LocalVariable::Ptr getLocalVariable(string name){
  //TODO: Go backwards up the mappings to find it.
}

LocalVariable::Ptr createLocalVariable(AshTypePtr type, string name){
  TmpScope& scope = bookkeeping_ScopeStack.top();
  if (scope.mapping.find(name) != scope.mapping.end()) throw "createLocalVariable: Redeclaration of Local Variable";
  
  llvm::AllocaInst* _alloca = createAlloca(type);
  
  LocalVariable::Ptr ret(new LocalVariable(type, name, _alloca));
  scope.mappings[name] = ret;
  return ret;
}

struct RAII_Scope {
  RAII_Scope(){
    bookkeeping_ScopeStack.Push(TmpScope());
  }
  ~RAII_Scope(){
    TmpScope& scope = bookkeeping_ScopeStack.top();
    if (scope.first_cleanup_block || scope.last_cleanup_block){
      llvm::Block bb_after_cleanup = createBB("cleanup:after");
      
      EmitBranch(scope.first_cleanup_block);
      
      SetInsertPoint(scope.last_cleanup_block);
      EmitBranch(bb_after_cleanup);
      
      SetInsertPoint(bb_after_cleanup);
    }
    bookkeeping_ScopeStack.Pop();
  }
};

struct RAII_EmitCleanup {
  llvm::BasicBlock* bb_prev;
  RAII_EmitCleanup(){
    bb_prev = current_insert_bb;
    
    llvm::Block* bb_cleanup = createBB("cleanup");
    
    TmpScope& scope = bookkeeping_ScopeStack.top();
    if (scope.first_cleanup_block || scope.last_cleanup_block){
      //Should never happen, but what the heck.
      printf("Warning: Possible double RAII_EmitCleanup in a row?\n");
      SetInsertPoint(scope.last_cleanup_block);
      EmitBranch(bb_cleanup);
    } else {
      scope.first_cleanup_block = bb_cleanup;
    }
    SetInsertPoint(bb_cleanup);
  }
  ~RAII_EmitCleanup(){
    TmpScope& scope = bookkeeping_ScopeStack.top();
    scope.last_cleanup_block = current_insert_bb;
    SetInsertPoint(bb_prev);
  }
};
  
//Break and Continue

typedef std::stack<llvm::BasicBlock*> Bookkeeping_BreakContinueStack;
Bookkeeping_BreakContinueStack bookkeeping_BreakStack;
Bookkeeping_BreakContinueStack bookkeeping_ContinueStack;

struct RAII_BreakContinue {
  RAII_BreakContinue(llvm::BasicBlock* bb_break, llvm::BasicBlock* bb_cont){
    bookkeeping_BreakStack.Push(bb_break);
    bookkeeping_ContinueStack.Push(bb_cont);
  }
  ~RAII_BreakContinue(){
    bookkeeping_BreakStack.Pop();
    bookkeeping_ContinueStack.Pop();
  }
};

struct RAII_Break {
  RAII_BreakContinue(llvm::BasicBlock* bb_break, llvm::BasicBlock* bb_cont){
    Bookkeeping_BreakStack.Push(bb_break);
  }
  ~RAII_BreakContinue(){
    Bookkeeping_BreakStack.Pop();
  }
};
  
//Emission of Expressions
  
  //TODO
  
//Emission of Statements

void EmitStmtWhile(Node n) {
  if (n.numleafs() != 2) throw "EmitStmtWhile: Expected 2 args";
  
  llvm::BasicBlock* bb_cond = createBB("while:cond");
  llvm::BasicBlock* bb_body = createBB("while:body");
  llvm::BasicBlock* bb_after = createBB("while:after");
  
  RAII_BreakContinue raii_bc(bb_after, bb_cond);
  
  llvm::Value* val_cond;
  {
    RAII_Scope scope_cond;
    SetInsertBB(bb_cond);
    val_cond = EmitConvertLLVMValueToBool(EmitExpr(n.get(0)));
  }
  EmitCondBranch(val_cond, bb_body, bb_after);
    
  {
    RAII_Scope scope_body;
    SetInsertBB(bb_body);
    EmitStmt(n.get(1));
  }
  EmitBranch(bb_cond);
    
  SetInsertBB(bb_after);
}

void EmitStmtIf(Node n) {
  if (n.numleafs() != 2 && n.numleafs() != 3) throw "EmitStmtIf: Expected 2 or 3 args"
//  if (n.value == "?:" && n.leafs.size() != 3) throw "EmitStmtIf: ?: operator expects 3 args";
  
  llvm::BasicBlock* bb_then = createBB("if:then");
  llvm::BasicBlock* bb_else = createBB("if:else");
  llvm::BasicBlock* bb_after = createBB("if:after");
  
  {
    RAII_Scope scope_cond;
    llvm::Value* val_cond = EmitConvertLLVMValueToBool(EmitExpr(n.get(0)));
    EmitCondBranch(val_cond, bb_then, bb_else);
  
    {
      RAII_Scope scope;
      SetInsertBB(bb_then);
      EmitStmt(n.get(1));
    }
    EmitBranch(bb_after);
  
    {
      RAII_Scope scope;
      SetInsertBB(bb_else);
      if (n.numleafs() == 3) EmitStmt(n.get(2));
    }
    EmitBranch(bb_after);
  
    SetInsertBB(bb_after);
  }
}

void EmitStmtVar(Node n){
  AshTypePtr vartype = ash_get_type(n.get(0));
  LocalVariable::Ptr var = createLocalVariable(vartype, n.get(1).value);
  if (true /*vartype.isConstantSizedType()*/){
    //TODO: Do this handling stuff
  } else {
    //TODO: Non constant sized type
  }
  
  if (n.numleafs() == 3){
    vartype->EmitInitailizer(var, n.get(2));
  } else if (n.numleafs() == 2){
    vartype->EmitInitailizer(var);
  } else {
    throw "EmtStmtVar: expected 2 or 3 args";
  }
  
  //TODO: Stuff and Stuff.
  
  {
    RAII_EmitCleanup ecleanup;
    vartype->EmitFinalizer(var);
  }
}
  
  /* Don't implement break, continue, goto and switch yet. 
   Cleanup of the former three is complex; the lastmost depends on the first.
   
   void EmitStmtBreak(Node n){
   if (n.numleafs() != 0) throw "EmitStmtBreak: Expected 0 args";
   if (Bookkeeping_BreakStack.empty()) throw "EmitStmtBreak: break not valid";
   EmitBranch();
   //TODO: NEED TO EMIT CLEANUPS AND COMPLICATED STUFF ARGHHHHHH
   SetInsertNullify();
   }
   
   void EmitStmtContinue(Node n){
   if (n.numleafs() != 0) throw "EmitStmtContinue: Expected 0 args";
   if (Bookkeeping_ContinueStack.empty()) throw "EmitStmtBreak: continue not valid";
   }*/

typedef std::map<string, boost::function<void(Node)> > Table_EmitStmt;
Table_EmitStmt table_EmitStmt;

void Init_BuildEmitStmtTable(){
  table_EmitStmt["if"] = &EmitStmtIf;
  table_EmitStmt["while"] = &EmitStmtWhile;
  table_EmitStmt["var"] = &EmitStmtVar;
}

void EmitStmt(Node n){
  typename Table_EmitStmt::iterator iter = table_EmitStmt.find(n.value);
  if (iter != table_EmitStmt.end()){
    iter->second();
  } else {
    EmitAnyExpr(n, 0, false, true); //TODO
  }
}