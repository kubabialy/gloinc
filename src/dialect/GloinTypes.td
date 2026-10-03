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
    let summary = "Source pointer type with pointee and access qualifiers";
    let parameters = (ins "Type":$pointee, "bool":$nullable, "bool":$readOnly);
    let assemblyFormat = "`<` $pointee `,` $nullable `,` $readOnly `>`";
}

def GloinArray : Gloin_Type<"GloinArray", "array"> {
    let summary = "Source fixed-array type";
    let parameters = (ins "Type":$element, "uint64_t":$length);
    let assemblyFormat = "`<` $element `,` $length `>`";
}

def GloinSlice : Gloin_Type<"GloinSlice", "slice"> {
    let summary = "Borrowed source slice type with element access capability";
    let parameters = (ins "Type":$element, "bool":$readOnly);
    let assemblyFormat = "`<` $element `,` $readOnly `>`";
}

def GloinEnum : Gloin_Type<"GloinEnum", "enum"> {
    let summary = "Nominal source enum type";
    let parameters = (ins StringRefParameter<"Qualified enum name">:$name);
    let assemblyFormat = "`<` $name `>`";
}

def GloinString : Gloin_Type<"GloinString", "string"> {
    let summary = "Source string type";
    let assemblyFormat = "";
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

def GloinError : Gloin_Type<"GloinError", "error"> {
    let summary = "Language error with a static message";
    let assemblyFormat = "";
}

def GloinResult : Gloin_Type<"GloinResult", "result"> {
    let summary = "Exclusive success or error result";
    let parameters = (ins "Type":$valueType);
    let assemblyFormat = "`<` $valueType `>`";
}

// A struct's field schema and layout live in its gloin.struct_definition symbol.
def GloinStruct : Gloin_Type<"GloinStruct", "struct"> {
    let summary = "Gloin struct type";
    let parameters = (ins StringRefParameter<"Struct name">:$name);
    let assemblyFormat = "`<` $name `>`";
}

#endif // GLOIN_TYPES
