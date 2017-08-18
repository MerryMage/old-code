#ifndef __ASH_LLVM_IR_H__
#define __ASH_LLVM_IR_H__

#include <string>
#include <map>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
using std::string;
using std::map;

namespace llvm {
  class Type;
  class Value;
  class BasicBlock;
};

struct TmpBlockScope;

struct AshType;
struct EvalNode;
typedef boost::shared_ptr<AshType> AshTypePtr;
typedef boost::shared_ptr<EvalNode> EvalNodePtr;

struct AshType {
  string name;
  virtual const llvm::Type* llvmtype() = 0;
  virtual EvalNodePtr handle(string oper, EvalNodePtr, TmpBlockScope*, Node, llvm::BasicBlock**) = 0;
};

struct EvalNode {
  virtual AshTypePtr type() = 0;
  virtual llvm::Value* load(llvm::BasicBlock*) = 0; 
  virtual bool canmemptr() = 0;
  virtual llvm::Value* memptr() = 0;
  virtual void store(EvalNodePtr, llvm::BasicBlock*) { throw "Not an lvalue"; }
};

void build_gen_table();

EvalNodePtr llvm_eval_node(TmpBlockScope*, Node, llvm::BasicBlock**);

#endif