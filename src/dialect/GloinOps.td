#ifndef GLOIN_OPS
#define GLOIN_OPS

include "GloinBase.td"
include "GloinTypes.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/Interfaces/ControlFlowInterfaces.td"
include "mlir/Interfaces/CallInterfaces.td"
include "mlir/IR/SymbolInterfaces.td" // Added for Symbol trait

class Gloin_Op<string mnemonic, list<Trait> traits = []> :
    Op<Gloin_Dialect, mnemonic, traits>;

// Basic Constant Op
def ConstantOp : Gloin_Op<"constant", [Pure]> {
  let summary = "constant operation";
  let arguments = (ins AnyAttr:$value);
  let results = (outs AnyType:$result);
  // let assemblyFormat = "$value attr-dict `:` type($result)";
}

// Yield Op (for blocks)
def YieldOp : Gloin_Op<"yield", [Pure, Terminator]> {
    let summary = "Yield values from a region";
    let arguments = (ins Variadic<AnyType>:$results);
    // let assemblyFormat = "attr-dict ($results^ `:` type($results))?";
}

// If Op
def IfOp : Gloin_Op<"if", [RecursiveMemoryEffects, NoRegionArguments]> {
    let summary = "If statement";
    let arguments = (ins I1:$condition);
    let regions = (region SizedRegion<1>:$thenRegion, AnyRegion:$elseRegion);
    
    // let assemblyFormat = "$condition $thenRegion (`else` $elseRegion)? attr-dict";
}

// Unless Op (Sugar for If !condition)
def UnlessOp : Gloin_Op<"unless", [RecursiveMemoryEffects, NoRegionArguments]> {
    let summary = "Unless statement (inverted if)";
    let arguments = (ins I1:$condition);
    let regions = (region SizedRegion<1>:$bodyRegion);
    
    // let assemblyFormat = "$condition $bodyRegion attr-dict";
}

// Defer Op
// Executes the region on scope exit
def DeferOp : Gloin_Op<"defer", []> {
    let summary = "Defer execution until scope exit";
    let regions = (region SizedRegion<1>:$bodyRegion);
    // let assemblyFormat = "$bodyRegion attr-dict";
}

// Concurrency Ops
// run engine.compute_heavy_pi(1000000) -> Spawn<T>
def SpawnOp : Gloin_Op<"spawn", [CallOpInterface]> {
    let summary = "Run a spawnable function in a new thread";
    let arguments = (ins FlatSymbolRefAttr:$callee, Variadic<AnyType>:$operands);
    let results = (outs GloinSpawn:$handle);
    
    let extraClassDeclaration = [{
        ::mlir::CallInterfaceCallable getCallableForCallee() {
            return (*this)->getAttrOfType<::mlir::SymbolRefAttr>("callee");
        }
        
        void setCalleeFromCallable(::mlir::CallInterfaceCallable callee) {
            (*this)->setAttr("callee", callee.get<::mlir::SymbolRefAttr>());
        }
        
        ::mlir::Operation::operand_range getArgOperands() {
            return getOperands();
        }
        
        ::mlir::MutableOperandRange getArgOperandsMutable() {
            return getOperandsMutable();
        }

        ::mlir::ArrayAttr getArgAttrsAttr() {
          return nullptr;
        }

        ::mlir::ArrayAttr getResAttrsAttr() {
          return nullptr;
        }

        void setArgAttrsAttr(::mlir::ArrayAttr attrs) {
          // No-op for now or throw error if not supported
        }

        void setResAttrsAttr(::mlir::ArrayAttr attrs) {
           // No-op
        }

        ::mlir::Attribute removeArgAttrsAttr() {
            return nullptr;
        }

        ::mlir::Attribute removeResAttrsAttr() {
            return nullptr;
        }
    }];
}

def JoinOp : Gloin_Op<"join", []> {
    let summary = "Join a spawn handle";
    let arguments = (ins GloinSpawn:$handle);
    let results = (outs GloinResult:$result); 
    // let assemblyFormat = "$handle attr-dict `:` type($handle) `->` type($result)";
}

// Async Call Op (for deferred functions)
// def handle: Deferred<Result<T, E>> = fetch_config(...);
def AsyncCallOp : Gloin_Op<"async_call", [CallOpInterface]> {
    let summary = "Call a function asynchronously";
    let arguments = (ins FlatSymbolRefAttr:$callee, Variadic<AnyType>:$operands);
    let results = (outs GloinDeferred:$handle);
    
    let extraClassDeclaration = [{
        ::mlir::CallInterfaceCallable getCallableForCallee() {
            return (*this)->getAttrOfType<::mlir::SymbolRefAttr>("callee");
        }
        
        void setCalleeFromCallable(::mlir::CallInterfaceCallable callee) {
            (*this)->setAttr("callee", callee.get<::mlir::SymbolRefAttr>());
        }
        
        ::mlir::Operation::operand_range getArgOperands() {
            return getOperands();
        }
        
        ::mlir::MutableOperandRange getArgOperandsMutable() {
            return getOperandsMutable();
        }

        ::mlir::ArrayAttr getArgAttrsAttr() {
          return nullptr;
        }

        ::mlir::ArrayAttr getResAttrsAttr() {
          return nullptr;
        }

        void setArgAttrsAttr(::mlir::ArrayAttr attrs) {
          // No-op for now or throw error if not supported
        }

        void setResAttrsAttr(::mlir::ArrayAttr attrs) {
           // No-op
        }

        ::mlir::Attribute removeArgAttrsAttr() {
            return nullptr;
        }

        ::mlir::Attribute removeResAttrsAttr() {
            return nullptr;
        }
    }];
}

// Await/Join for Deferred (distinct from thread join)
def AwaitOp : Gloin_Op<"await", []> {
    let summary = "Join a deferred handle";
    let arguments = (ins GloinDeferred:$handle);
    let results = (outs GloinResult:$result);
}

// Struct Definition Op (to capture layout)
// def struct Person { ... }
def StructDefOp : Gloin_Op<"struct_def", [Symbol]> {
    let summary = "Define a struct layout";
    let arguments = (ins 
        SymbolNameAttr:$sym_name, 
        BoolAttr:$is_packed,
        TypeArrayAttr:$field_types,
        StrArrayAttr:$field_names,
        // Optional: specific offsets for bit-fields if needed
        OptionalAttr<I64ArrayAttr>:$bit_offsets
    );
    let regions = (region AnyRegion:$body); // Optional body for methods?
}

#endif // GLOIN_OPS
