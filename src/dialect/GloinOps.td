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

def StructDefinitionOp : Gloin_Op<"struct_definition", [Symbol]> {
    let summary = "Declare a checked nominal struct and its ordered field types";
    let arguments = (ins SymbolNameAttr:$sym_name, TypeAttr:$layout_type,
                     ArrayAttr:$field_source_types);
    let hasVerifier = 1;
}

// Checked function boundaries retain source identity in func signatures while
// the existing function bodies still work with their LLVM-compatible layout.
def FromLayoutOp : Gloin_Op<"from_layout", [Pure]> {
    let summary = "Convert an LLVM layout value to a checked source value";
    let arguments = (ins AnyType:$value);
    let results = (outs AnyType:$source_value);
    let hasVerifier = 1;
}

def ToLayoutOp : Gloin_Op<"to_layout", [Pure]> {
    let summary = "Convert a checked source value to its LLVM layout";
    let arguments = (ins AnyType:$source_value);
    let results = (outs AnyType:$value);
    let hasVerifier = 1;
}

def AbiCallOp : Gloin_Op<"abi_call"> {
    let summary = "Call a declared runtime ABI function with layout values";
    let arguments = (ins FlatSymbolRefAttr:$callee, Variadic<AnyType>:$args);
    let results = (outs Variadic<AnyType>:$results);
    let hasVerifier = 1;
}

// A nominal payloadless enum is checked before IR construction. These ops
// retain enum intent until the Gloin-to-LLVM conversion pass.
def EnumConstantOp : Gloin_Op<"enum_constant", [Pure]> {
    let summary = "Construct a checked payloadless enum variant";
    let arguments = (ins I32Attr:$tag, TypeAttr:$source_type,
                     TypeAttr:$layout_type);
    let results = (outs AnyType:$value);
    let hasVerifier = 1;
}

def EnumCompareOp : Gloin_Op<"enum_compare", [Pure]> {
    let summary = "Compare two values of the same checked enum type";
    let arguments = (ins AnyType:$lhs, AnyType:$rhs, BoolAttr:$not_equal,
                     TypeAttr:$source_type);
    let results = (outs I1:$result);
    let hasVerifier = 1;
}

// Basic Constant Op
def ConstantOp : Gloin_Op<"constant", [Pure]> {
  let summary = "constant operation";
  let arguments = (ins AnyAttr:$value);
  let results = (outs AnyType:$result);
  let hasVerifier = 1;
  // let assemblyFormat = "$value attr-dict `:` type($result)";
}

// A failed checked-language precondition traps before the guarded operation.
def AssertOp : Gloin_Op<"assert"> {
    let summary = "Require a checked language runtime precondition";
    let arguments = (ins I1:$condition);
}

def CheckedIntegerBinaryOp : Gloin_Op<"checked_int_binary"> {
    let summary = "Integer arithmetic with Gloin overflow and division checks";
    let arguments = (ins AnyType:$lhs, AnyType:$rhs, StrAttr:$kind,
                     BoolAttr:$is_signed);
    let results = (outs AnyType:$result);
    let hasVerifier = 1;
}

def CheckedFloatBinaryOp : Gloin_Op<"checked_float_binary"> {
    let summary = "Finite floating arithmetic with Gloin division checks";
    let arguments = (ins AnyType:$lhs, AnyType:$rhs, StrAttr:$kind);
    let results = (outs AnyType:$result);
    let hasVerifier = 1;
}

def CheckedIntegerCompareOp : Gloin_Op<"checked_int_compare", [Pure]> {
    let summary = "Compare checked integer values with explicit signedness";
    let arguments = (ins AnyType:$lhs, AnyType:$rhs, StrAttr:$kind,
                     BoolAttr:$is_signed);
    let results = (outs I1:$result);
    let hasVerifier = 1;
}

def CheckedFloatCompareOp : Gloin_Op<"checked_float_compare", [Pure]> {
    let summary = "Compare checked floating values with Gloin NaN rules";
    let arguments = (ins AnyType:$lhs, AnyType:$rhs, StrAttr:$kind);
    let results = (outs I1:$result);
    let hasVerifier = 1;
}

def ArrayLiteralOp : Gloin_Op<"array_literal", [Pure]> {
    let summary = "Construct a checked fixed array from ordered element values";
    let arguments = (ins Variadic<AnyType>:$elements, TypeAttr:$source_type,
                     TypeAttr:$layout_type);
    let results = (outs AnyType:$value);
    let hasVerifier = 1;
}

def RepeatArrayOp : Gloin_Op<"repeat_array"> {
    let summary = "Construct a fixed array by repeating one checked value";
    let arguments = (ins AnyType:$element, TypeAttr:$source_type,
                     TypeAttr:$layout_type);
    let results = (outs AnyType:$value);
    let hasVerifier = 1;
}

def StructLiteralOp : Gloin_Op<"struct_literal", [Pure]> {
    let summary = "Construct a checked struct in source initializer order";
    let arguments = (ins Variadic<AnyType>:$fields, DenseI64ArrayAttr:$indices,
                     TypeAttr:$source_type, TypeAttr:$layout_type,
                     ArrayAttr:$field_source_types);
    let results = (outs AnyType:$value);
    let hasVerifier = 1;
}

def ZeroedArrayOp : Gloin_Op<"zeroed_array", [Pure]> {
    let summary = "Construct a recursively zeroed checked fixed array";
    let arguments = (ins TypeAttr:$source_type, TypeAttr:$layout_type);
    let results = (outs AnyType:$value);
    let hasVerifier = 1;
}

def StringLiteralOp : Gloin_Op<"string_literal", [Pure]> {
    let summary = "Construct a checked string from static byte storage and length";
    let arguments = (ins AnyType:$data, I64:$length,
                     TypeAttr:$source_type, TypeAttr:$layout_type);
    let results = (outs AnyType:$value);
    let hasVerifier = 1;
}

def NullOp : Gloin_Op<"null", [Pure]> {
    let summary = "Construct a checked nullable pointer null value";
    let arguments = (ins TypeAttr:$source_type);
    let results = (outs AnyType:$value);
    let hasVerifier = 1;
}

def ArenaTypedPointerOp : Gloin_Op<"arena_typed_pointer"> {
    let summary = "Type arena byte storage for a checked element initializer";
    let arguments = (ins AnyType:$storage, TypeAttr:$source_type,
                     TypeAttr:$element_type);
    let results = (outs AnyType:$pointer);
    let hasVerifier = 1;
}

def PointerOffsetOp : Gloin_Op<"pointer_offset"> {
    let summary = "Offset a pointer by typed elements, trapping on a null base";
    let arguments = (ins AnyType:$base, AnyType:$offset, TypeAttr:$element_type,
                     TypeAttr:$source_type);
    let results = (outs AnyType:$address);
    let hasVerifier = 1;
}

def RequireNonNullOp : Gloin_Op<"require_nonnull"> {
    let summary = "Require a nullable pointer to be non-null before access";
    let arguments = (ins AnyType:$pointer, TypeAttr:$source_type);
    let results = (outs AnyType:$checked_pointer);
    let hasVerifier = 1;
}

def PointerCompareOp : Gloin_Op<"pointer_compare", [Pure]> {
    let summary = "Compare two checked pointers for equality or inequality";
    let arguments = (ins AnyType:$lhs, AnyType:$rhs, BoolAttr:$not_equal,
                     TypeAttr:$source_type);
    let results = (outs I1:$result);
    let hasVerifier = 1;
}

def ArrayElementAddressOp : Gloin_Op<"array_element_address"> {
    let summary = "Address a fixed-array element, trapping on an out-of-bounds index";
    let arguments = (ins AnyType:$base, I64:$index, TypeAttr:$array_type,
                     TypeAttr:$source_type);
    let results = (outs AnyType:$address);
    let hasVerifier = 1;
}

def SliceFromArrayOp : Gloin_Op<"slice_from_array"> {
    let summary = "Borrow a checked subrange of fixed-array storage";
    let arguments = (ins AnyType:$base, I64:$start, I64:$end,
                     TypeAttr:$array_type, TypeAttr:$source_type, TypeAttr:$layout_type);
    let results = (outs AnyType:$value);
    let hasVerifier = 1;
}

def SliceSubrangeOp : Gloin_Op<"slice_subrange"> {
    let summary = "Borrow a checked subrange of a slice";
    let arguments = (ins AnyType:$base, I64:$start, I64:$end,
                     TypeAttr:$source_type, TypeAttr:$layout_type,
                     TypeAttr:$element_type);
    let results = (outs AnyType:$value);
    let hasVerifier = 1;
}

def SliceElementAddressOp : Gloin_Op<"slice_element_address"> {
    let summary = "Address a slice element with a bounds check";
    let arguments = (ins AnyType:$base, I64:$index, TypeAttr:$source_type,
                     TypeAttr:$layout_type, TypeAttr:$element_type);
    let results = (outs AnyType:$address);
    let hasVerifier = 1;
}

def SliceLengthOp : Gloin_Op<"slice_length", [Pure]> {
    let summary = "Read the length of a slice";
    let arguments = (ins AnyType:$base, TypeAttr:$source_type, TypeAttr:$layout_type);
    let results = (outs I64:$length);
    let hasVerifier = 1;
}

def ArenaFillOp : Gloin_Op<"arena_fill"> {
    let summary = "Initialize each element of a typed arena allocation with a captured value";
    let arguments = (ins AnyType:$base, AnyType:$fill, I64:$count,
                     TypeAttr:$source_type, TypeAttr:$element_source_type,
                     TypeAttr:$element_type);
    let hasVerifier = 1;
}

def StackAllocOp : Gloin_Op<"stack_alloc"> {
    let summary = "Reserve one checked local value in the function entry block";
    let arguments = (ins TypeAttr:$element_type, TypeAttr:$source_type);
    let results = (outs AnyType:$address);
    let hasVerifier = 1;
}

def LoadOp : Gloin_Op<"load"> {
    let summary = "Read a checked value from an address";
    let arguments = (ins AnyType:$address, TypeAttr:$source_type,
                     TypeAttr:$layout_type);
    let results = (outs AnyType:$value);
    let hasVerifier = 1;
}

def StoreOp : Gloin_Op<"store"> {
    let summary = "Write a checked value to an address";
    let arguments = (ins AnyType:$value, AnyType:$address, TypeAttr:$source_type);
    let hasVerifier = 1;
}

def FieldAddressOp : Gloin_Op<"field_address", [Pure]> {
    let summary = "Address a checked struct field by its verified layout index";
    let arguments = (ins AnyType:$base, TypeAttr:$struct_type, I64Attr:$index,
                     TypeAttr:$source_type, TypeAttr:$field_source_type);
    let results = (outs AnyType:$address);
    let hasVerifier = 1;
}

def ExtractFieldOp : Gloin_Op<"extract_field", [Pure]> {
    let summary = "Read a field from a checked struct value";
    let arguments = (ins AnyType:$aggregate, I64Attr:$index,
                     TypeAttr:$source_type, TypeAttr:$layout_type,
                     TypeAttr:$field_source_type);
    let results = (outs AnyType:$value);
    let hasVerifier = 1;
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
