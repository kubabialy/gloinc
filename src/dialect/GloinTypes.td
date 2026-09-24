#ifndef GLOIN_TYPES
#define GLOIN_TYPES

include "GloinBase.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/EnumAttr.td"

class Gloin_Type<string name, string typeMnemonic, list<Trait> traits = []>
    : TypeDef<Gloin_Dialect, name, traits> {
  let mnemonic = typeMnemonic;
}

// Endianness definition
def LittleEndian : I32EnumAttrCase<"Little", 0>;
def BigEndian    : I32EnumAttrCase<"Big", 1>;
def NativeEndian : I32EnumAttrCase<"Native", 2>;

def EndiannessAttr : I32EnumAttr<"Endianness", "Endianness of integer types",
                                 [LittleEndian, BigEndian, NativeEndian]> {
  let cppNamespace = "::gloin";
}

// Custom Integer Type with Endianness
// e.g., !gloin.int<32, Big>
def GloinInteger : Gloin_Type<"GloinInteger", "int"> {
    let summary = "Integer type with explicit width and endianness";
    let parameters = (ins "unsigned":$width, "Endianness":$endianness);
    let assemblyFormat = "`<` $width `,` $endianness `>`";
}

// Pointer Type
def GloinPointer : Gloin_Type<"GloinPointer", "ptr"> {
    let summary = "Pointer type";
    let parameters = (ins "Type":$pointee);
    let assemblyFormat = "`<` $pointee `>`";
}

// Defer Type (Async handle)
def GloinDeferred : Gloin_Type<"GloinDeferred", "deferred"> {
    let summary = "Handle for async operations";
    let parameters = (ins "Type":$valueType);
    let assemblyFormat = "`<` $valueType `>`";
}

// Spawn Type (Thread handle)
def GloinSpawn : Gloin_Type<"GloinSpawn", "spawn"> {
    let summary = "Handle for spawned threads";
    let parameters = (ins "Type":$valueType);
    let assemblyFormat = "`<` $valueType `>`";
}

// Result Type <T, E>
def GloinResult : Gloin_Type<"GloinResult", "result"> {
    let summary = "Result type for operations that can fail";
    let parameters = (ins "Type":$okType, "Type":$errType);
    let assemblyFormat = "`<` $okType `,` $errType `>`";
}

// Struct Type (Placeholder for named structs)
// Realistically, layout info would be in a symbol table or definition Op
def GloinStruct : Gloin_Type<"GloinStruct", "struct"> {
    let summary = "Gloin struct type";
    let parameters = (ins StringRefParameter<"Struct name">:$name);
    let assemblyFormat = "`<` $name `>`";
}

#endif // GLOIN_TYPES
