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

struct ExprContext {
  llvm::Block* temp_decls;
  llvm::Block* temp_assigns;
  llvm::Block* expr;
  llvm::Block* temp_frees;
  llvm::Block* temp_to_null;
  EvalNodePtr result;
  llvm::Block* result_free;
};

#include <iostream>
#include <vector>
using std::vector;

llvm::ConstantInt* CONST_INT_ZERO;
#define LLVM_GET_INT32(number) llvm::ConstantInt::get(llvm::getGlobalContext(), llvm::APInt(32, (number)))
EvalNodePtr VOID_NODE;

map<string, EvalNodePtr> global_table;
map<string, AshTypePtr> typetable;
map<string, boost::function<EvalNodePtr(TmpBlockScope*, Node, llvm::BasicBlock**)> > llvmgen; 

//EvalNodes

struct GlobalVariable : public EvalNode {
  typedef boost::shared_ptr<GlobalVariable> Ptr;
  GlobalVariable(AshTypePtr _type, string name, llvm::Module* module) : _type(_type), name(name) {
    _globvar = new llvm::GlobalVariable(*module, _type->llvmtype(), /*isConstant=*/false, llvm::GlobalValue::PrivateLinkage, 0, name /*more optional params...*/);
  }
  string name;
  llvm::GlobalVariable* _globvar;
  AshTypePtr _type; AshTypePtr type() {return _type;}
  llvm::Value* load(llvm::BasicBlock* bb){
    return new llvm::LoadInst(_globvar, name, false, bb); //I *think* false == non-volatile
  }
  bool canmemptr(){ return true; }
  llvm::Value* memptr(){ return _globvar; }
  void store(EvalNodePtr n, llvm::BasicBlock* bb){
    if (n->type()->name != _type->name) throw "Storing non-type type in global variable";
    new llvm::StoreInst(n->load(bb), _globvar, false, bb); //I *think* false == non-volatile
  }
};

struct EvalNode_Value : public EvalNode {
  AshTypePtr _type; AshTypePtr type() {return _type;}
  llvm::Value* _value; llvm::Value* load(llvm::BasicBlock*) {return _value;}
  llvm::Value* memptr(){ throw "rvalues do not have memptr()."; }
  bool canmemptr(){ return false; }
  EvalNode_Value(AshTypePtr type, llvm::Value* value=0) : _type(type), _value(value) {}
};

struct EvalNode_Ref : public EvalNode {
  AshTypePtr _type; AshTypePtr type() {return _type;}
  llvm::Value* _value;
  llvm::Value* load(llvm::BasicBlock* bb) {
    return new llvm::LoadInst(_value, "ref", false, bb); 
  }
  bool canmemptr(){ return true; }
  llvm::Value* memptr(){ return _value; }
  void store(EvalNodePtr n, llvm::BasicBlock* bb){
    new llvm::StoreInst(n->load(bb), _value, false, bb); //false == non-volatile
  }
  EvalNode_Ref(AshTypePtr type, llvm::Value* value) : _type(type), _value(value) {}
};

struct LocalVariable : public EvalNode {
  typedef boost::shared_ptr<LocalVariable> Ptr;
  LocalVariable(AshTypePtr _type, string name, llvm::AllocaInst* _alloca) : _type(_type), name(name), _alloca(_alloca) {}
  string name;
  AshTypePtr _type; AshTypePtr type() {return _type;}
  llvm::AllocaInst* _alloca;
  llvm::Value* load(llvm::BasicBlock* bb){
    if (!isvalid()) throw "!Variable::isvalid()";
    return new llvm::LoadInst(_alloca, name, false, bb); //I *think* false == non-volatile
  }
  bool canmemptr(){ return true; }
  llvm::Value* memptr(){ return _alloca; }
  void store(llvm::Value* v, llvm::BasicBlock* bb){
    new llvm::StoreInst(v, _alloca, false, bb); //false == non-volatile
  }
  void store(EvalNodePtr n, llvm::BasicBlock* bb){
    if (!isvalid()) throw "!Variable::isvalid()";
    if (n->type()->name != _type->name) throw "Storing non-type type in local variable";
    new llvm::StoreInst(n->load(bb), _alloca, false, bb); //I *think* false == non-volatile
  }
  bool isvalid(){ return _alloca; }
};

//Scopes

struct TmpBlockScope {
  TmpBlockScope(llvm::Module* m, llvm::BasicBlock* b, llvm::BasicBlock* b_r) : parent(0), bb_alloca(b), bb_return(b_r), module(m) {}
  TmpBlockScope(TmpBlockScope* p) : parent(p), bb_alloca(p->bb_alloca), bb_return(p->bb_return), module(p->module) { }
  llvm::BasicBlock* bb_alloca;
  llvm::BasicBlock* bb_return;
  TmpBlockScope* parent;
  llvm::Module* module;
  map<string, LocalVariable::Ptr> mappings;
  TmpBlockScope* newScope(){
    return new TmpBlockScope(this);
  }
  TmpBlockScope* popScope(llvm::BasicBlock** block){
    TmpBlockScope* prevScope = parent;
    // Run destructors for everything going out of scope
    map<string, LocalVariable::Ptr>::iterator iter = mappings.begin();
    for (;iter!=mappings.end();++iter){
      iter->second->type()->handle("__destruct", iter->second, this, Node(), block);
    }
    delete this;
    return prevScope;
  }
  LocalVariable::Ptr lookup(string name){
    if (mappings.find(name) != mappings.end()){
      return mappings[name];
    } else if (parent) {
      return parent->lookup(name);
    } else {
      return LocalVariable::Ptr((LocalVariable*)0);
    }
  }
  LocalVariable::Ptr define_tmp(AshTypePtr type, llvm::BasicBlock* current){
    llvm::AllocaInst* _alloca = new llvm::AllocaInst(type->llvmtype(), "tmpstorage:alloca", /*bb_toinsert*/bb_alloca);
    return LocalVariable::Ptr(new LocalVariable(type, "tmpstorage", _alloca));
  }
  LocalVariable::Ptr define(AshTypePtr type, string name, llvm::BasicBlock* current){
    if (mappings.find(name) != mappings.end()) return LocalVariable::Ptr((LocalVariable*)0);
    /*llvm::BasicBlock* bb_toinsert = bb_alloca ? bb_alloca : current;*/
    if (!bb_alloca) throw "Attenpted to declare local variable when it is illogical to do so.";
    llvm::AllocaInst* _alloca = new llvm::AllocaInst(type->llvmtype(), name+":alloca", /*bb_toinsert*/bb_alloca);
    LocalVariable::Ptr ret = LocalVariable::Ptr(new LocalVariable(type, name, _alloca));
    mappings[name] = ret;
    return ret;
  }
};

//Types

struct ValueType : public AshType {
  typedef boost::shared_ptr<ValueType> Ptr;
  //boost::function<EvalNodePtr(TmpBlockScope*, Node, llvm::BasicBlock**)> impl_literal; //Implicit Constructor 
  map<string, boost::function<EvalNodePtr(EvalNodePtr, TmpBlockScope*, Node, llvm::BasicBlock**)> > funcs;
  const llvm::Type* _llvmtype;
  const llvm::Type* llvmtype(){return _llvmtype;}
  bool canmemptr(){ return false; }
  EvalNodePtr handle(string oper, EvalNodePtr a, TmpBlockScope* b, Node c, llvm::BasicBlock** d){
    return funcs[oper](a,b,c,d);
  }
};

ValueType::Ptr TYPE_VOID;
ValueType::Ptr TYPE_DOUBLE;
ValueType::Ptr TYPE_INT;
ValueType::Ptr TYPE_BOOL;
ValueType::Ptr TYPE_POINTER;

struct FunctionType : public AshType {
  typedef boost::shared_ptr<FunctionType> Ptr;
  AshTypePtr rettype;
  vector<AshTypePtr> args;
  llvm::FunctionType* fnType;
  FunctionType(AshTypePtr rettype, vector<AshTypePtr> args) : rettype(rettype), args(args) {
    name = rettype->name + "(*)(";
    vector<const llvm::Type*> llvm_args;
    for (int i=0;i<args.size();++i) {
      llvm_args.push_back(args[i]->llvmtype());
      if (i!=0) name+=",";
      name+=args[i]->name;
    }
    name += ")";
    fnType = llvm::FunctionType::get(rettype->llvmtype(), llvm_args, false); //var_args=false... WE CAN DEAL WITH VAR_ARGS LATER!
  }
  llvm::FunctionType* llvmtype(){ return fnType; }
  EvalNodePtr handle(string oper, EvalNodePtr, TmpBlockScope*, Node, llvm::BasicBlock**){ throw "NOT IMPLEMENTED FOR FUNCTYPES... YET"; }
};

#include <llvm/Support/raw_ostream.h>

struct ArrayType : public AshType {
  AshTypePtr containedtype;
  int numelements;
  llvm::ArrayType* _type;
  ArrayType(AshTypePtr containedtype, int numelements) : containedtype(containedtype), numelements(numelements) {
    _type = llvm::ArrayType::get(containedtype->llvmtype(), numelements);
    char tmpbuff[255];
    sprintf(tmpbuff, "[%i]", numelements);
    name = containedtype->name+tmpbuff;
  }
  llvm::ArrayType* llvmtype(){ return _type; }
  EvalNodePtr handle(string oper, EvalNodePtr left, TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
    if (oper == "[]"){
      EvalNodePtr right = llvm_eval_node(sp, n.leafs[1], block);
      llvm::Value* elempos = right->load(*block);
      if (left->canmemptr()){
        std::vector<llvm::Value*> lookup;
        lookup.push_back(CONST_INT_ZERO);
        lookup.push_back(elempos);
        return EvalNodePtr(new EvalNode_Ref(containedtype,
                                            llvm::GetElementPtrInst::Create(left->memptr(), lookup.begin(), lookup.end(), "ref_[]_"+name, *block)
                                            ));
      } else {
        LocalVariable::Ptr tmpstore = sp->define_tmp(left->type(), *block);
        tmpstore->store(left, *block);
        std::vector<llvm::Value*> lookup;
        lookup.push_back(CONST_INT_ZERO);
        lookup.push_back(elempos);
        return EvalNodePtr(new EvalNode_Ref(containedtype,
                                            llvm::GetElementPtrInst::Create(tmpstore->memptr(), lookup.begin(), lookup.end(), "ref_[]_"+name, *block)
                                            ));
      }
    } else if (oper == "__destruct") {
      //Deconstructor needs to do nothing special.
    } else {
      throw "ArrayType unknown operation";
    }
  }
};

struct PtrType : public AshType {
  AshTypePtr containedtype;
  llvm::PointerType* _type;
  PtrType(AshTypePtr containedtype) : containedtype(containedtype) {
    _type = llvm::PointerType::get(containedtype->llvmtype(), 0);
    name = containedtype->name+"*";
  }
  llvm::PointerType* llvmtype(){ return _type; }
  EvalNodePtr handle(string oper, EvalNodePtr left, TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
    if (oper == "[]"){
      EvalNodePtr right = llvm_eval_node(sp, n.leafs[1], block);
      std::vector<llvm::Value*> lookup;
      lookup.push_back(right->load(*block));
      return EvalNodePtr(new EvalNode_Ref(containedtype,
                                          llvm::GetElementPtrInst::CreateInBounds(left->load(*block), lookup.begin(), lookup.end(), "[]_"+name, *block)
                                          ));
    } else if (oper == "*:prefix") {
      return EvalNodePtr(new EvalNode_Ref(containedtype,
                                          left->load(*block)
                                          ));
    } else if (oper == "+") {
      EvalNodePtr right = llvm_eval_node(sp, n.leafs[1], block);
      std::vector<llvm::Value*> lookup;
      lookup.push_back(right->load(*block));
      return EvalNodePtr(new EvalNode_Value(left->type(),
                                            llvm::GetElementPtrInst::CreateInBounds(left->load(*block), lookup.begin(), lookup.end(), "ptrarithmetic", *block)
                                            ));
    } else if (oper == "-") {
      // (- ptr val) ==> (+ ptr (-:prefix val))
      //i.e: ptr-val ==> ptr+(-val)
      n.value = "+";
      Node n2;
      n2.value = "-:prefix";
      n2.leafs.push_back(n.leafs[1]);
      n.leafs[1] = n2;
      return llvm_eval_node(sp, n, block);
    } else if (oper == "->") {
      // (-> ptr mem) ==> (. (*:prefix ptr) mem)
      //i.e: ptr->mem ==> (*ptr).mem
      n.value = ".";
      Node n2;
      n2.value = "*:prefix";
      n2.leafs.push_back(n.leafs[0]);
      n.leafs[0] = n2;
      return llvm_eval_node(sp, n, block);
    } else if (oper == "__destruct"){
      // Deconstructor needs to do nothing special.
    } else {
      throw "ArrayType unknown operation";
    }
  }
};

struct StructType : public AshType {
  typedef boost::shared_ptr<StructType> Ptr;
  vector<AshTypePtr> memtypes; map<string, int> memnames;
  llvm::StructType* _type;
  StructType(string name, vector<AshTypePtr> __memtypes, vector<string> __memnames) : memtypes(__memtypes) {
    this->name = name;
    vector<const llvm::Type*> llvmtypes;
    for (int i=0;i<memtypes.size();i++){
      this->memnames[__memnames[i]] = i;
      llvmtypes.push_back(__memtypes[i]->llvmtype());
    }
    _type = llvm::StructType::get(llvm::getGlobalContext(), llvmtypes, /*isPacked =*/false);
  }
  llvm::StructType* llvmtype(){ return _type; }
  EvalNodePtr handle(string oper, EvalNodePtr left, TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
    if (oper == ".") {
      string elemname = n.leafs[1].value;
      int elempos = memnames[elemname];
      if (left->canmemptr()){
        std::vector<llvm::Value*> lookup;
        lookup.push_back(CONST_INT_ZERO);
        lookup.push_back(LLVM_GET_INT32(elempos));
        return EvalNodePtr(new EvalNode_Ref(memtypes[elempos],
                                            llvm::GetElementPtrInst::Create(left->memptr(), lookup.begin(), lookup.end(), "ref_"+name+"::"+elemname, *block)
                                            ));
      } else {
        return EvalNodePtr(new EvalNode_Value(memtypes[elempos],
                                              llvm::ExtractValueInst::Create(left->load(*block), elempos, name+"::"+elemname, *block)
                                              ));
      }
    } else if (oper == "__destruct") {
      // Simple structs don't have destructors.
      // TODO: Handle real user-defined destructors.
    } else {
      throw "StructType unknown operation";
    }
  }
};

//Function EvalNode

void OptimizeFunc(llvm::Function*);
struct Function : public EvalNode {
  typedef boost::shared_ptr<Function> Ptr;
  Function(FunctionType::Ptr type, llvm::Function* fn) : _type(type), _value(fn) {}
  Function(llvm::Module* module, FunctionType::Ptr type, vector<string> argnames, string name, Node n, bool noLocalVars=false) : _type(type){
    llvm::FunctionType* fnType = (llvm::FunctionType*)type->llvmtype();
    _value = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, name, module);
    llvm::BasicBlock* bb_alloca = llvm::BasicBlock::Create(llvm::getGlobalContext(), "bb_alloca", _value);
    llvm::BasicBlock* bb_entry = llvm::BasicBlock::Create(llvm::getGlobalContext(), "bb_entry", _value);
    llvm::BasicBlock* bb_return = llvm::BasicBlock::Create(llvm::getGlobalContext(), "bb_return", _value);
    llvm::BasicBlock* bb_last = bb_entry; //<-- Gets modified during compilation
    TmpBlockScope scope(module, noLocalVars ? 0 : bb_alloca, noLocalVars ? 0 : bb_return);
    //Return val:
    if (type->rettype->name != "void") scope.define(type->rettype, "ret", bb_alloca);
    //Arguments:
    llvm::Function::arg_iterator AI = _value->arg_begin();
    for (int i=0;i<argnames.size();++i,++AI){
      scope.define(type->args[i], argnames[i], bb_alloca)->store(AI, bb_alloca);
    }
    
    llvm_eval_node(&scope, n, &bb_last);
    
    llvm::BranchInst::Create(bb_entry, bb_alloca);
    if (bb_last->empty() || !bb_last->back().isTerminator()) llvm::BranchInst::Create(bb_return, bb_last);
    if (type->rettype->name == "void") llvm::ReturnInst::Create(llvm::getGlobalContext(), bb_return);
    else llvm::ReturnInst::Create(llvm::getGlobalContext(), scope.lookup("ret")->load(bb_return), bb_return);
    
    //OptimizeFunc(_value);
  }
  FunctionType::Ptr _type;
  bool canmemptr(){ return false; }
  AshTypePtr type() {return _type;}
  llvm::Function* _value; llvm::Function* load(llvm::BasicBlock*) {return _value;}
  llvm::Value* memptr(){ throw "Function does not have memptr(). If it's supposed to, then it's unimplemented."; }
  vector<string> argnames;
};

bool is_typename(string name){
  return (typetable.find(name) != typetable.end());
}

// Core Dispatcher
int debug_identlevel;
EvalNodePtr llvm_eval_node(TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  debug_identlevel++;
  for (int i=0;i<debug_identlevel;i++) printf("  ");
  n.print(); printf("\n");
  
  EvalNodePtr ret;
  if (n.isleaf()){
    if (LocalVariable::Ptr var = sp->lookup(n.value)) ret = EvalNodePtr(var);
    else if (EvalNodePtr evp = global_table[n.value]) ret = evp;
    else if (n.value == "") ret = VOID_NODE; //<-- Empty statement
    else {
      printf("%s\n", n.value.c_str());
      throw "Unknown Variable or Function name";
    }
  } else if (llvmgen.find(n.value) != llvmgen.end()) {
    ret = llvmgen[n.value](sp, n, block);
  } else {
    printf("%s\n", n.value.c_str());
    throw "LLVM IR Generator: No match";
  }
  
  for (int i=0;i<debug_identlevel;i++) printf("  ");
  printf("%s\n", ret->type()->name.c_str());
  debug_identlevel--;
  
  return ret;
}

AshTypePtr ash_get_type(Node n){
  if (n.isleaf()) return typetable[n.value];
  if (n.value == "array") return AshTypePtr(new ArrayType(ash_get_type(n.leafs[0]), atoi(n.leafs[1].value.c_str())));
  if (n.value == "ptr") return AshTypePtr(new PtrType(ash_get_type(n.leafs[0])));
}

//If something will be handled by a "typehandler", the appropriate function in AshType::funcs will be called.
//Think of them like member functions of a class. [DEFINE REQUIRED]
EvalNodePtr llvm_type_handler(string name, TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  EvalNodePtr left = llvm_eval_node(sp, n.leafs[0], block);
  return left->type()->handle(name, left, sp, n, block);
}
void llvm_define_th(string name){
  llvmgen[name] = boost::bind(&llvm_type_handler, name, _1, _2, _3);
}

// "+", "*", ">>", etc. nodes (binary operations) [TYPE-SPECIFIC DEFINE]
EvalNodePtr llvm_th_binaryop(llvm::Instruction::BinaryOps op, EvalNodePtr _this, TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  EvalNodePtr right = llvm_eval_node(sp, n.leafs[1], block);
  if (_this->type() != right->type()) throw "th_binaryop: Type mismatch (We don't have implicit converstion yet).";
  return EvalNodePtr(new EvalNode_Value(_this->type(),
                                        llvm::BinaryOperator::Create(op, _this->load(*block), right->load(*block), "binaryop", *block)
                                        ));
}
EvalNodePtr llvm_th_binaryop_single(llvm::Instruction::BinaryOps op, EvalNodePtr _other, EvalNodePtr _this, TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  return EvalNodePtr(new EvalNode_Value(_this->type(),
                                        llvm::BinaryOperator::Create(op, _other->load(*block), _this->load(*block), "single", *block)
                                        ));
}
void llvm_define_binaryop(ValueType::Ptr type, string name, llvm::Instruction::BinaryOps op){
  type->funcs[name] = boost::bind(&llvm_th_binaryop, op, _1, _2, _3, _4);
}

// "(cast)" node [TYPE-SPECIFIC DEFINE]
EvalNodePtr llvm_cast_handler(TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  EvalNodePtr val = llvm_eval_node(sp, n.leafs[1], block);
  string type = n.leafs[0].value;
  return typetable[type]->handle("from:"+val->type()->name, val, sp, n, block);
}
EvalNodePtr llvm_th_cast(llvm::Instruction::CastOps op, AshTypePtr newtype, EvalNodePtr val, TmpBlockScope*, Node, llvm::BasicBlock** block){
  return EvalNodePtr(new EvalNode_Value(newtype,
                                        llvm::CastInst::Create(op, val->load(*block), newtype->llvmtype(), newtype->name, *block)
                                        ));
}
void llvm_define_cast(string fromtype, ValueType::Ptr totype, llvm::Instruction::CastOps op){
  totype->funcs["from:"+fromtype] = boost::bind(&llvm_th_cast, op, totype, _1, _2, _3, _4);
}

// "((cast) bool _)" node [TYPE-SPECIFIC DEFINE]
EvalNodePtr llvm_th_bool_cast(llvm::Instruction::OtherOps op, llvm::CmpInst::Predicate p, llvm::Value* val, EvalNodePtr _this, TmpBlockScope*, Node, llvm::BasicBlock** block){
  return EvalNodePtr(new EvalNode_Value(TYPE_BOOL,
                                        llvm::CmpInst::Create(op, p, _this->load(*block), val, "bool", *block)
                                        ));
}
void llvm_define_bool_cast(string fromtype, llvm::Instruction::OtherOps op, llvm::CmpInst::Predicate p, llvm::Value* val){
  TYPE_BOOL->funcs["from:"+fromtype] = boost::bind(&llvm_th_bool_cast, op, p, val, _1, _2, _3, _4);
}

// "{}" node [GENERIC]
EvalNodePtr llvm_block_handler(TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  TmpBlockScope* newsp = sp->newScope();
  for (int i=0;i<n.leafs.size();i++){
    llvm_eval_node(newsp, n.leafs[i], block);
  }
  newsp->popScope(block);
  return VOID_NODE;
}

// "if", "?:" nodes [GENERIC]
EvalNodePtr llvm_if_handler(TmpBlockScope* sp, Node n, llvm::BasicBlock** block, bool need_val){
  if (n.leafs.size() != 2 && n.leafs.size() != 3) throw "llvm_if_handler: expected 2 or 3 args";
  if (n.value == "?:" && n.leafs.size() != 3) throw "llvm_if_handler: ?: operator expects 3 args";
  EvalNodePtr condition = llvm_eval_node(sp, n.leafs[0], block);
  //TODO: some typechecking on condition

  llvm::BasicBlock* bb_then = llvm::BasicBlock::Create(llvm::getGlobalContext(), "if:then", (*block)->getParent());
  llvm::BasicBlock* bb_else = llvm::BasicBlock::Create(llvm::getGlobalContext(), "if:else", (*block)->getParent());
  llvm::BasicBlock* bb_after_if = llvm::BasicBlock::Create(llvm::getGlobalContext(), "if:after", (*block)->getParent());
  llvm::BranchInst::Create(bb_then, bb_else, condition->load(*block), *block);
  
  EvalNodePtr then_val = llvm_eval_node(sp, n.leafs[1], &bb_then);
  llvm::BranchInst::Create(bb_after_if, bb_then);

  EvalNodePtr else_val;
  if (n.leafs.size() == 3) else_val = llvm_eval_node(sp, n.leafs[2], &bb_else); //an else block exists
  llvm::BranchInst::Create(bb_after_if, bb_else);
  
  *block = bb_after_if;

  if (need_val && then_val->type()->name != "void" && else_val->type()->name != "void"){
    //TODO: Type Coercion
    if (then_val->type() != else_val->type()) throw "llvm_if_handler with need_val: something fishy";
    AshTypePtr rettype = then_val->type();
    llvm::PHINode* phi = llvm::PHINode::Create(rettype->llvmtype(), "ifvalue", bb_after_if);
    phi->addIncoming(then_val->load(bb_after_if), bb_then);
    phi->addIncoming(else_val->load(bb_after_if), bb_else);
    return EvalNodePtr(new EvalNode_Value(rettype, phi));
  }
  return VOID_NODE;
}

// "return" node [GENERIC]
EvalNodePtr llvm_return_handler(TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  if (!sp->bb_return) throw "return disallowed at this level. pebkac, not me.";
  if (n.leafs[0].isleaf() && n.leafs[0].value == "void"){
    // do nothing
  } else {
    EvalNodePtr retval = llvm_eval_node(sp, n.leafs[0], block);
    sp->lookup("ret")->store(retval, *block);
  }
  llvm::BranchInst::Create(sp->bb_return, *block);

  *block = llvm::BasicBlock::Create(llvm::getGlobalContext(), "unreachable", (*block)->getParent());

  return VOID_NODE;
}

// "var" node [GENERIC]
EvalNodePtr llvm_var_handler(TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  LocalVariable::Ptr var = sp->define(ash_get_type(n.leafs[0]), n.leafs[1].value, *block);
  if (!var) throw "redeclaring variable already in this scope";
  if (n.leafs.size() == 3){
    var->type()->handle("__construct", var, sp, n.leafs[2], block);
  } else if (n.leafs.size() == 2){
    var->type()->handle("__construct", var, sp, Node(), block);
  } else {
    throw "llvm_var_handler: expected Node with 2 or 3 leafs";
  }
  
  //TODO: SUPPORT MULTIDEFINITIONS WHEN THE TIME COMES
  
  return VOID_NODE;
}
EvalNodePtr llvm_th_default_initializer(EvalNodePtr val, TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  LocalVariable::Ptr var = boost::static_pointer_cast<LocalVariable, EvalNode>(val);
  if (n.isleaf() && n.value == ""){
    return VOID_NODE;
  } else {
    var->store(llvm_eval_node(sp, n, block), *block);
    return VOID_NODE;
  }
}
void llvm_define_uses_default_initializer(ValueType::Ptr type){
  type->funcs["__construct"] = &llvm_th_default_initializer;
}

// "globalvar" node [GENERIC]
EvalNodePtr llvm_globalvar_handler(TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  GlobalVariable::Ptr var(new GlobalVariable(typetable[n.leafs[0].value], n.leafs[1].value, sp->module));
  global_table[n.leafs[1].value] = var;
  
  if (n.leafs.size() == 3){
    var->store(llvm_eval_node(sp, n.leafs[2], block), *block);
  } else if (n.leafs.size() != 2) throw "llvm_var_handler: expected Node with 2 or 3 leafs";
  
  //TODO: SUPPORT MULTIDEFINITIONS WHEN THE TIME COMES
  
  return VOID_NODE;
}

// "=" node [TYPE-SPECIFIC DEFINE]
EvalNodePtr llvm_th_assign(EvalNodePtr lvalue, TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  EvalNodePtr retval = llvm_eval_node(sp, n.leafs[1], block);
  
  lvalue->store(retval, *block);
  
  return retval;
}
void define_uses_simple_assignment(AshTypePtr type){
  type->funcs["="] = &llvm_th_assign;
}

// "funccall" node [GENERIC]
//TODO: Better definition using a global Ash-centric lookup table.
EvalNodePtr llvm_funccall_handler(TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  EvalNodePtr tmp = llvm_eval_node(sp, n.leafs[0], block);
  Function::Ptr fn = boost::static_pointer_cast<Function, EvalNode>(tmp);
  if (!fn) throw "funccall: Calling undefined function";
  llvm::Function* callee = (llvm::Function*)(fn->load(0));
  if (callee->arg_size() != n.leafs.size()-1) throw "funccall: Incorrect number of arguments";
  
  std::vector<llvm::Value*> args;
  for (int i=1;i<n.leafs.size();i++){
    EvalNodePtr arg = llvm_eval_node(sp, n.leafs[i], block);
    //TODO: Typecheck arg
    args.push_back(arg->load(*block));
  }
  
  return EvalNodePtr(new EvalNode_Value(fn->_type->rettype,
                                        llvm::CallInst::Create(callee, args.begin(), args.end(), "", *block)
                                        ));
}

// "deffunc" node [GENERIC]
EvalNodePtr llvm_deffunc_handler(TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  vector<AshTypePtr> __argtypes; vector<string> __argnames;
  string __fnname = n.leafs[1].value;
  Node args = n.leafs[2];
  for (int i=0;i<args.leafs.size();i+=2){
    __argtypes.push_back(ash_get_type(args.leafs[i]));
    __argnames.push_back(args.leafs[i+1].value);
  }
  FunctionType::Ptr fnType(new FunctionType(ash_get_type(n.leafs[0]), __argtypes));
  Function::Ptr fn(new Function(sp->module, fnType, __argnames, __fnname, n.leafs[3]));
  global_table[__fnname] = fn;
  return VOID_NODE;
}

// "defstruct" node [GENERIC]
EvalNodePtr llvm_defstruct_handler(TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  vector<AshTypePtr> __memtypes; vector<string> __memnames;
  string __structname = n.leafs[0].value;
  for (int i=1;i<n.leafs.size();i+=2){
    __memtypes.push_back(ash_get_type(n.leafs[i]));
    __memnames.push_back(n.leafs[i+1].value);
  }
  StructType::Ptr stType(new StructType(__structname, __memtypes, __memnames));
  typetable[__structname] = stType;
  return VOID_NODE;
}

// "&:prefix" node [GENERIC]
EvalNodePtr llvm_getaddr_handler(TmpBlockScope* sp, Node n, llvm::BasicBlock** block) {
  EvalNodePtr arg = llvm_eval_node(sp, n.leafs[0], block);
  return EvalNodePtr(new EvalNode_Value(AshTypePtr(new PtrType(arg->type())), arg->memptr()));
}

//Implicit Constructors
EvalNodePtr llvm_build_double (TmpBlockScope* sp, Node n, llvm::BasicBlock** block) {
  /* NOTE: DOES NOT HANDLE SUFFIXES. */
  return EvalNodePtr(new EvalNode_Value(typetable[n.value], llvm::ConstantFP::get(llvm::getGlobalContext(), llvm::APFloat(llvm::APFloat::IEEEdouble, n.leafs[0].value))));
}
EvalNodePtr llvm_build_int (TmpBlockScope* sp, Node n, llvm::BasicBlock** block) {
   /* NOTE: IF INTEGER IS TOO BIG, IT SHOULD BE CONVERTED TO A 64-BIT NUMBER. ALSO, THIS DOES NOT HANDLE SUFFIXES. (DECIMAL ONLY) */
  return EvalNodePtr(new EvalNode_Value(typetable[n.value], llvm::ConstantInt::get(llvm::getGlobalContext(), llvm::APInt(32, n.leafs[0].value, 10))));
}
/*EvalNodePtr llvm_build_bool (TmpBlockScope* sp, Node n, llvm::BasicBlock** block) {
  return EvalNodePtr(new EvalNode_Value("double", ConstantFP::get(getGlobalContext(), APFloat(APFloat::IEEEdouble, n.leafs[0]))));
}*/

//Null handler
EvalNodePtr th_that_does_nothing(EvalNodePtr val, TmpBlockScope*, Node, llvm::BasicBlock** block){
  return VOID_NODE;
}
void llvm_define_uses_null_destructor(ValueType::Ptr type){
  type->funcs["__destruct"] = &th_that_does_nothing;
}

void build_gen_table(){
  CONST_INT_ZERO = llvm::ConstantInt::get(llvm::getGlobalContext(), llvm::APInt(32, 0));
  
  TYPE_VOID = ValueType::Ptr(new ValueType);
  TYPE_VOID->name = "void";
  TYPE_VOID->_llvmtype = llvm::Type::getVoidTy(llvm::getGlobalContext());
  typetable["void"] = TYPE_VOID;
  
  VOID_NODE = EvalNodePtr(new EvalNode_Value(TYPE_VOID));
  
  //Generics
  llvmgen["(cast)"] = &llvm_cast_handler;
  llvmgen["{}"] = &llvm_block_handler;
  llvmgen["if"] = boost::bind(&llvm_if_handler, _1, _2, _3, false);
  llvmgen["?:"] = boost::bind(&llvm_if_handler, _1, _2, _3, true);
  llvmgen["return"] = &llvm_return_handler;
  llvmgen["var"] = &llvm_var_handler;
  llvmgen["globalvar"] = &llvm_globalvar_handler;
  llvmgen["="] = &llvm_assign_handler;
  llvmgen["funccall"] = &llvm_funccall_handler;
  llvmgen["deffunc"] = &llvm_deffunc_handler;
  llvmgen["defstruct"] = &llvm_defstruct_handler;
  llvmgen["&:prefix"] = &llvm_getaddr_handler;

  //Type-defined operators
  llvm_define_th("+");
  llvm_define_th("-");
  llvm_define_th("*");
  llvm_define_th("/");
  llvm_define_th("<<");
  llvm_define_th(">>");
  llvm_define_th(">>>");
  llvm_define_th("&");
  llvm_define_th("|");
  llvm_define_th("^");
  llvm_define_th("[]");
  llvm_define_th("-:prefix");
  llvm_define_th("+:prefix");
  llvm_define_th("*:prefix");
  llvm_define_th(".");
  llvm_define_th("->");
  llvm_define_th("=");

  //Built-in type definitions: double
  TYPE_DOUBLE = ValueType::Ptr(new ValueType);
  TYPE_DOUBLE->name = "double";
  TYPE_DOUBLE->_llvmtype = llvm::Type::getDoubleTy(llvm::getGlobalContext());
  typetable["double"] = TYPE_DOUBLE;
  llvmgen["double"] = &llvm_build_double;
  llvm_define_uses_default_initializer(TYPE_DOUBLE);
  llvm_define_uses_null_destructor(TYPE_DOUBLE);
  llvm_define_users_simple_assignment(TYPE_DOUBLE);
  
  //Built-in type definitions: int
  TYPE_INT = ValueType::Ptr(new ValueType);
  TYPE_INT->name = "int";
  TYPE_INT->_llvmtype = llvm::Type::getInt32Ty(llvm::getGlobalContext());
  typetable["int"] = TYPE_INT;
  llvmgen["int"] = &llvm_build_int;
  llvm_define_uses_default_initializer(TYPE_INT);
  llvm_define_uses_null_destructor(TYPE_INT);
  llvm_define_users_simple_assignment(TYPE_INT);
  
  //Built-in type definitions: bool
  TYPE_BOOL = ValueType::Ptr(new ValueType);
  TYPE_BOOL->name = "bool";
  TYPE_BOOL->_llvmtype = llvm::Type::getInt1Ty(llvm::getGlobalContext());
  typetable["bool"] = TYPE_BOOL;
  llvm_define_uses_default_initializer(TYPE_BOOL);
  llvm_define_uses_null_destructor(TYPE_BOOL);
  llvm_define_users_simple_assignment(TYPE_BOOL);
  
  //Built-in type definitions: rawpointer
  TYPE_POINTER = ValueType::Ptr(new ValueType);
  TYPE_POINTER->name = "rawpointer";
  TYPE_POINTER->_llvmtype = llvm::Type::getInt8PtrTy(llvm::getGlobalContext());
  typetable["rawpointer"] = TYPE_POINTER;
  llvm_define_uses_default_initializer(TYPE_POINTER);
  llvm_define_uses_null_destructor(TYPE_POINTER);
  llvm_define_users_simple_assignment(TYPE_POINTER);
  
  //double Casts and Operators
  llvm_define_cast("double", TYPE_INT, llvm::Instruction::FPToSI);
  llvm_define_binaryop(TYPE_DOUBLE, "+", llvm::Instruction::FAdd);
  llvm_define_binaryop(TYPE_DOUBLE, "-", llvm::Instruction::FSub);
  llvm_define_binaryop(TYPE_DOUBLE, "*", llvm::Instruction::FMul);
  llvm_define_binaryop(TYPE_DOUBLE, "/", llvm::Instruction::FDiv);

  //int Casts and Operators
  llvm_define_cast("int", TYPE_DOUBLE, llvm::Instruction::SIToFP);
  llvm_define_binaryop(TYPE_INT, "+", llvm::Instruction::Add);
  llvm_define_binaryop(TYPE_INT, "-", llvm::Instruction::Sub);
  llvm_define_binaryop(TYPE_INT, "*", llvm::Instruction::Mul);
  //TODO: divide(s)
  llvm_define_binaryop(TYPE_INT, "<<", llvm::Instruction::Shl);
  llvm_define_binaryop(TYPE_INT, ">>", llvm::Instruction::AShr);
  llvm_define_binaryop(TYPE_INT, ">>>", llvm::Instruction::LShr);
  llvm_define_binaryop(TYPE_INT, "&", llvm::Instruction::And);
  llvm_define_binaryop(TYPE_INT, "|", llvm::Instruction::Or);
  llvm_define_binaryop(TYPE_INT, "^", llvm::Instruction::Xor);
  TYPE_INT->funcs["-:prefix"] = boost::bind(&llvm_th_binaryop_single, llvm::Instruction::Sub, EvalNodePtr(new EvalNode_Value(TYPE_INT, CONST_INT_ZERO)), _1, _2, _3, _4);
  TYPE_INT->funcs["+:prefix"] = boost::bind(&llvm_th_binaryop_single, llvm::Instruction::Add, EvalNodePtr(new EvalNode_Value(TYPE_INT, CONST_INT_ZERO)), _1, _2, _3, _4);
  
  //bool Casts and Operators
  llvm_define_bool_cast("double", llvm::Instruction::FCmp, llvm::CmpInst::FCMP_ONE, llvm::ConstantFP::get(llvm::getGlobalContext(), llvm::APFloat(0.0)));
  llvm_define_bool_cast("int", llvm::Instruction::ICmp, llvm::CmpInst::ICMP_NE, llvm::ConstantInt::get(llvm::getGlobalContext(), llvm::APInt(32, 0)));
  //TODO: Boolean operators (If we're going to have short-circuiting, then we'd need special handling)

  //rawpointer Casts and Operators
  //NOTE: since rawpointer is meant to be opaque, no operations or casts should be defined for it other than the bare minimum.
}

//Builds a function that has signature: void().
llvm::Function* build_anon_function(llvm::Module* module, Node n){
  FunctionType::Ptr fnType(new FunctionType(TYPE_VOID, vector<AshTypePtr>()));
  Function fn(module, fnType, vector<string>(), "", n, /*noLocalVars=*/true);
  return (llvm::Function*)(fn.load(0));
}

void declare_extern_function(llvm::Module* module, AshTypePtr rettype, vector<AshTypePtr> argtype, string name){
  FunctionType::Ptr type(new FunctionType(rettype, argtype));
  llvm::FunctionType* fnType = (llvm::FunctionType*)type->llvmtype();
  llvm::Function* fn = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, name, module);
  global_table[name] = Function::Ptr(new Function(type, fn));
}

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/PassManager.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetSelect.h"
#include "llvm/Transforms/Scalar.h"
#include <iostream>
#include <math.h>

AshTypePtr TYPE_NODE;

extern "C" {
  Node* ASH_IMPL__NODE_CONSTRUCT(){
    return new Node;
  }
  void ASH_IMPL__NODE_DESTRUCT(Node* _this){
    delete _this;
  }
  Node* ASH_IMPL__NODE_CLONE(Node* _this){
    Node* ret = ASH_IMPL__NODE_CONSTRUCT();
    *ret = *_this;
    return ret;
  }
  void ASH_IMPL__NODE_PRINT(Node* _this){
    _this->print(); printf("\n");
  }
  Node* ASH_IMPL__NODE_INDEX(Node* _this, int index){
    return ASH_IMPL__NODE_CLONE(&_this->get(index));
  }
  
  void Node_append(Node* _this, Node* _that){
    
  }
};

EvalNodePtr make_funccall(string fn_name, vector<llvm::Value*> args, llvm::BasicBlock** block){
  Function::Ptr fn = boost::static_pointer_cast<Function, EvalNode>(global_table[fn_name]);
  if (!fn) throw "funccall: Calling undefined function";

  llvm::Function* callee = (llvm::Function*)(fn->load(0));
  if (callee->arg_size() != args.size()-1) throw "funccall: Incorrect number of arguments";

  return EvalNodePtr(new EvalNode_Value(rettype, llvm::CallInst::Create(callee, args.begin(), args.end(), "", *block)));
}

EvalNodePtr th_Node_dot_access(EvalNodePtr _this, TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  llvm::Value* this_val = _this->load(*block);
  if (n.leafs[1].value == "value"){
    throw "Node::value unimplemented as I NEED A STRING CLASS TOO.";
  } else if (n.leafs[1].value == "print"){
    return make_funccall("ASH_IMPL__NODE_PRINT", vector<llvm::Value*>(1, this_val), block);
  } else {
    throw "Unknown member of Node::";
  }
}

EvalNodePtr th_Node_index_access(EvalNodePtr _this, TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  llvm::Value* this_val = _this->load(*block);
  EvalNodePtr right = llvm_eval_node(sp, n.leafs[1], block);
  llvm::Value* elempos = right->load(*block);
  vector<llvm::Value*> args;
  args.push_back(this_val);
  args.push_back(elempos);
  return make_funccall("ASH_IMPL__NODE_INDEX", args, block);
}

EvalNodePtr th_Node_construct(EvalNodePtr _this, TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  return make_funccall("ASH_IMPL__NODE_CONSTRUCT", vector<llvm::Value*>(), block);
}

EvalNodePtr th_Node_destruct(EvalNodePtr _this, TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  llvm::Value* this_val = _this->load(*block);
  return make_funccall("ASH_IMPL__NODE_DESTRUCT", vector<llvm::Value*>(1, this_val), block);
}

EvalNodePtr th_Node_assign(EvalNodePtr _this, TmpBlockScope* sp, Node n, llvm::BasicBlock** block){
  EvalNodePtr _that = llvm_eval_node(sp, n.leafs[1], block);
  llvm::Value* that_val = _that->load(*block);
  EvalNodePtr this_to_be = make_funccall("ASH_IMPL__NODE_CLONE", vector<llvm::Value*>(1, that_val), block);
  th_Node_destruct(_this, sp, n, block);
  _this->store(this_to_be, *block);
}

void build_additional_tables(){
  TYPE_NODE = ValueType::Ptr(new ValueType);
  TYPE_NODE->name = "Node";
  TYPE_NODE->_llvmtype = llvm::Type::getDoubleTy(llvm::getGlobalContext());
  typetable["Node"] = TYPE_NODE;
  //llvm_define_uses_default_initializer(TYPE_NODE);
  //llvm_define_uses_null_destructor(TYPE_NODE);
  //llvm_define_users_simple_assignment(TYPE_NODE);

  TYPE_NODE->funcs["."] = &th_Node_dot_access;
  TYPE_NODE->funcs["__construct"] = &th_Node_construct;
  TYPE_NODE->funcs["__destruct"] = &th_Node_destruct;
  TYPE_NODE->funcs["="] = &th_Node_assign;
  TYPE_NODE->funcs["[]"] = &th_Node_index_access;
}

extern "C" {
  void printDouble(double a){
    std::cout << a << std::endl;
  }
}

llvm::FunctionPassManager* FPM;

void OptimizeFunc(llvm::Function* fn){
  FPM->run(*fn);
}

int main(int argc, char** argv){
  debug_identlevel = -1;
  llvm::Module *TheModule = 0;
  build_tables();
  build_stmt_table();
  build_gen_table();
  //build_additional_tables();
  /*for (int i=1;i<argc;i++){
    string token = argv[i];
    test_token_stream.push(token);
  }
  test_token_stream.push("(end)");*/
  next_token();

  TheModule = new llvm::Module("TheModule", llvm::getGlobalContext());
  
  string error_str;
  llvm::InitializeNativeTarget(); //Needs to run before JIT is created.
  llvm::ExecutionEngine *execEngine = llvm::ExecutionEngine::createJIT(TheModule, &error_str);
  if (!execEngine) printf("Could not start execEngine: %s\n", error_str.c_str());
  
  FPM = new llvm::FunctionPassManager(TheModule);
  FPM->add(new llvm::TargetData(*execEngine->getTargetData()));
  FPM->add(llvm::createPromoteMemoryToRegisterPass());
  FPM->add(llvm::createInstructionCombiningPass());
  FPM->add(llvm::createReassociatePass());
  FPM->add(llvm::createGVNPass());
  FPM->add(llvm::createCFGSimplificationPass());
  FPM->doInitialization();
  
  declare_extern_function(TheModule, TYPE_VOID, vector<AshTypePtr>(1, TYPE_DOUBLE), "printDouble");
  
  try {
    while (true){
      Node tree = statement();
      if (tree.value == "(end)") break;
      tree.print();
      printf("\n");
      llvm::Function* fn = build_anon_function(TheModule, tree);
      llvm::verifyFunction(*fn);

      void *ptr = execEngine->getPointerToFunction(fn);
      //Cast pointer and call function.
      ((void (*)())ptr)();
      ptr=0;
    }
    TheModule->dump();
    delete execEngine;
    delete FPM;
    execEngine = 0;
    TheModule = 0;
    FPM = 0;
  } catch (const char* msg){
    printf("Caught: %s\n", msg);
    if (TheModule) {printf("\n\nPANIC, DUMPING>>>>>>\n\n"); TheModule->dump();}
  }
}
