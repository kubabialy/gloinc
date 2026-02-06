# Priority 1 Blockers - Detailed Implementation Plans

## Overview

This document provides detailed, ready-to-execute implementation plans for the three Priority 1 blockers identified in the MLIR lowering coverage analysis:

1. **For Loop Implementation** (missing from codegen.cpp:528)
2. **Import System Framework** (missing from codegen.cpp:544) 
3. **Endianness Support Integration** (missing from resolve_type:281)

Each blocker includes complete code analysis, step-by-step implementation strategy, dependencies, and comprehensive test cases.

---

## 1. FOR LOOP IMPLEMENTATION

### **Current State Analysis**
- ✅ **AST Complete**: `ForStatement` defined in `AST.h:294-310`
- ✅ **Parser Working**: Creates AST nodes with 4 components (init, condition, increment, body)
- ❌ **Missing**: Handler in `CodeGen::gen_statement()` at line 528
- ✅ **Reference Pattern**: While loop implementation at lines 500-528

### **Implementation Location**
**File**: `src/codegen.cpp`
**Function**: `CodeGen::gen_statement()`
**Line**: Add after line 528 (while loop handler, before BlockStatement)

### **Complete Implementation Code**

```cpp
else if (auto* for_stmt = dynamic_cast<const ForStatement*>(stmt)) {
    // Step 1: Create basic blocks
    auto* currentBlock = builder.getBlock();
    auto* region = currentBlock->getParent();
    
    auto* initBlock = builder.createBlock(region);     // For init execution
    auto* condBlock = builder.createBlock(region);     // Condition check
    auto* bodyBlock = builder.createBlock(region);     // Loop body
    auto* incBlock = builder.createBlock(region);      // Increment execution
    auto* mergeBlock = builder.createBlock(region);    // Exit point
    
    // Step 2: Jump to init block
    builder.setInsertionPointToEnd(currentBlock);
    builder.create<mlir::cf::BranchOp>(builder.getUnknownLoc(), initBlock);
    
    // Step 3: Init Block - execute initializer
    builder.setInsertionPointToStart(initBlock);
    if (for_stmt->init) {
        gen_statement(for_stmt->init.get());
    }
    builder.create<mlir::cf::BranchOp>(builder.getUnknownLoc(), condBlock);
    
    // Step 4: Condition Block
    builder.setInsertionPointToStart(condBlock);
    mlir::Value cond;
    if (for_stmt->condition) {
        cond = gen_expression(for_stmt->condition.get());
    } else {
        // No condition means infinite loop
        cond = builder.create<mlir::arith::ConstantIntOp>(builder.getUnknownLoc(), 1, 1);
    }
    builder.create<mlir::cf::CondBranchOp>(builder.getUnknownLoc(), cond, bodyBlock, mlir::ValueRange(), mergeBlock, mlir::ValueRange());
    
    // Step 5: Body Block
    builder.setInsertionPointToStart(bodyBlock);
    if (const auto* block = dynamic_cast<const BlockStatement*>(for_stmt->body.get())) {
         for(const auto& s : block->statements) gen_statement(s.get());
    }
    // Jump to increment after body
    if (bodyBlock->empty() || !bodyBlock->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
        builder.create<mlir::cf::BranchOp>(builder.getUnknownLoc(), incBlock);
    }
    
    // Step 6: Increment Block
    builder.setInsertionPointToStart(incBlock);
    if (for_stmt->increment) {
        gen_expression(for_stmt->increment.get());
    }
    builder.create<mlir::cf::BranchOp>(builder.getUnknownLoc(), condBlock);
    
    // Step 7: Continue in merge block
    builder.setInsertionPointToStart(mergeBlock);
    if (mergeBlock->hasNoPredecessors()) {
         builder.create<mlir::LLVM::UnreachableOp>(builder.getUnknownLoc());
    }
}
```

### **Dependencies**
- No new functions required
- Uses existing scope management (init handles its scope, body uses block scope)
- Integrates with existing MLIR builder patterns

### **Test Cases**
```cpp
// Test 1: Basic for loop
def test_basic_for() -> i32 {
    def mut sum: i32 = 0;
    for def i: i32 = 0; i < 10; i = i + 1 {
        sum = sum + i;
    }
    return sum;  // Should be 45
}

// Test 2: For loop without condition (infinite)
def test_for_infinite() {
    for def i: i32 = 0; ; i = i + 1 {
        if i > 3 { return; }
    }
}
```

---

## 2. IMPORT SYSTEM FRAMEWORK

### **Current State Analysis**
- ✅ **AST Complete**: `ImportStatement` defined in `AST.h:324-332`
- ✅ **Parser Working**: Parses import paths correctly (lines 1017-1034)
- ❌ **Missing**: Handler in `CodeGen::gen_statement()` at line 544
- ❌ **Missing**: Module loading infrastructure
- ❌ **Missing**: Symbol table management for imports

### **Implementation Strategy**

#### **Phase 2.1: Basic Handler (codegen.cpp:544)**
```cpp
else if (auto* import_stmt = dynamic_cast<const ImportStatement*>(stmt)) {
    handle_import(import_stmt->path);
}
```

#### **Phase 2.2: Import Infrastructure (codegen.h)**
Add after line 61:
```cpp
// Module and import management
std::map<std::string, std::shared_ptr<mlir::ModuleOp>> imported_modules;
std::map<std::string, std::string> module_symbols;  // path -> symbol_prefix
void handle_import(const std::string& import_path);
mlir::ModuleOp load_module(const std::string& path);
void import_symbols_from_module(mlir::ModuleOp module, const std::string& prefix = "");
```

#### **Phase 2.3: Core Import Handler**
```cpp
void CodeGen::handle_import(const std::string& import_path) {
    // Check if already imported
    if (imported_modules.count(import_path)) {
        return;
    }
    
    mlir::ModuleOp imported_module;
    std::string symbol_prefix;
    
    if (import_path.find("@std/") == 0) {
        // Standard library import: @std/io
        std::string std_module = import_path.substr(5); // Remove "@std/"
        imported_module = load_std_module(std_module);
        symbol_prefix = "std_" + std_module + "_";
    }
    else if (import_path.find("./") == 0 || import_path.find("../") == 0) {
        // Relative file import: ./utils
        std::string file_path = resolve_relative_path(import_path);
        imported_module = load_module_from_file(file_path);
        symbol_prefix = ""; // No prefix for local imports
    }
    else if (import_path.find("#") == 0) {
        // Package manager import: #serde
        std::string package_name = import_path.substr(1); // Remove "#"
        imported_module = load_package_module(package_name);
        symbol_prefix = package_name + "_";
    }
    
    if (imported_module) {
        imported_modules[import_path] = std::make_shared<mlir::ModuleOp>(imported_module);
        module_symbols[import_path] = symbol_prefix;
        import_symbols_from_module(imported_module, symbol_prefix);
    }
}

void CodeGen::import_symbols_from_module(mlir::ModuleOp module, const std::string& prefix) {
    // Import all functions from module
    for (auto& op : module.getBody()->getOperations()) {
        if (auto funcOp = llvm::dyn_cast<mlir::func::FuncOp>(op)) {
            std::string original_name = funcOp.getName().str();
            std::string mangled_name = prefix + original_name;
            
            // Clone function into current module
            auto clonedFunc = funcOp.clone();
            clonedFunc.setName(mangled_name);
            theModule.push_back(clonedFunc);
            
            // Register in symbol table
            declare(mangled_name, clonedFunc, false, clonedFunc.getFunctionType().getResult(0));
        }
    }
}
```

### **Dependencies**
- New member variables for tracking imports
- Integration with existing `declare()` and `lookup()` methods
- Module loading functions (placeholder implementations)

### **Test Cases**
```cpp
// Test 1: Standard library import
import @std/io;

def test_std_import() {
    print("Hello from std import");
}

// Test 2: Relative file import  
import "./utils";

def test_relative_import() -> i32 {
    return utils_add(5, 3);
}

// Test 3: Package import
import #serde;

def test_package_import() {
    let data = serde_serialize("test");
}
```

---

## 3. ENDIANNESS SUPPORT INTEGRATION

### **Current State Analysis**
- ✅ **Lexer Support**: All be_/le_ tokens defined (lines 675-679 in lexer.cpp)
- ✅ **MLIR Types**: `GloinIntegerType` with endianness exists in GloinTypes.td
- ✅ **Struct Integration**: Endianness support for packed structs (lines 366-371)
- ❌ **Missing**: Handler in `resolve_type()` function at line 281
- ❌ **Missing**: Endianness tracking and conversion infrastructure

### **Implementation Location**
**File**: `src/codegen.cpp`
**Function**: `CodeGen::resolve_type()`
**Line**: Modify starting at line 281

### **Complete Implementation Code**

#### **Phase 3.1: Extended resolve_type()**
```cpp
mlir::Type CodeGen::resolve_type(const std::string& type_name) {
    // Check existing type table first
    if (type_table.count(type_name)) {
        return type_table[type_name];
    }
    
    // Handle endianness-prefixed integer types (NEW)
    if (type_name.find("be_") == 0 || type_name.find("le_") == 0) {
        bool is_big_endian = (type_name.find("be_") == 0);
        std::string base_type = type_name.substr(3); // Remove "be_" or "le_"
        
        // Parse base integer type
        mlir::Type base_mlir_type;
        if (base_type == "i8") base_mlir_type = builder.getI8Type();
        else if (base_type == "i16") base_mlir_type = builder.getI16Type();
        else if (base_type == "i32") base_mlir_type = builder.getI32Type();
        else if (base_type == "i64") base_mlir_type = builder.getI64Type();
        else if (base_type == "i128") base_mlir_type = builder.getI128Type();
        else if (base_type == "u8") base_mlir_type = builder.getI8Type();
        else if (base_type == "u16") base_mlir_type = builder.getI16Type();
        else if (base_type == "u32") base_mlir_type = builder.getI32Type();
        else if (base_type == "u64") base_mlir_type = builder.getI64Type();
        else if (base_type == "u128") base_mlir_type = builder.getI128Type();
        else {
            // Unknown base type, fall back
            return builder.getI32Type();
        }
        
        // Create endianness-aware type
        auto endian_type = gloin::GloinIntegerType::get(&context, base_mlir_type, is_big_endian);
        
        // Cache the type
        type_table[type_name] = endian_type;
        
        // Store endianness info for later use
        integer_type_endianness[type_name] = is_big_endian;
        
        return endian_type;
    }
    
    // Check for Array [T; N] (existing)
    if (type_name.front() == '[' && type_name.back() == ']') {
         size_t semicolonPos = type_name.find(';');
         if (semicolonPos != std::string::npos) {
             std::string subTypeStr = type_name.substr(1, semicolonPos - 1);
             std::string sizeStr = type_name.substr(semicolonPos + 1, type_name.size() - semicolonPos - 2);
             
             mlir::Type elemType = resolve_type(subTypeStr);
             int64_t size = std::stoll(sizeStr);
             
             return mlir::LLVM::LLVMArrayType::get(elemType, size);
         }
    }
    
    // Fallback or error?
    return builder.getI32Type();
}
```

#### **Phase 3.2: Add Endianness Tracking (codegen.h)**
Add after line 60:
```cpp
// Integer type endianness tracking
std::map<std::string, bool> integer_type_endianness; // type_name -> is_big_endian
bool get_type_endianness(const std::string& type_name);
```

#### **Phase 3.3: Endianness Helper**
```cpp
bool CodeGen::get_type_endianness(const std::string& type_name) {
    if (integer_type_endianness.count(type_name)) {
        return integer_type_endianness[type_name];
    }
    return false; // Default to little-endian
}
```

### **Dependencies**
- Extend `resolve_type()` function
- Add member variables for endianness tracking
- Integration with existing `GloinIntegerType` from dialect
- Integration with existing type table caching

### **Test Cases**
```cpp
// Test 1: Basic endianness type declaration
def test_basic_endianness() {
    def x: be_i32 = 0x12345678;
    def y: le_i32 = 0x12345678;
    // x and y should have different byte representations
}

// Test 2: Endianness in structs
def packed(be_u32) Header {
    def version: u16 at 0,
    def flags: u16 at 16
}

def test_struct_endianness() {
    def header: Header = Header { version: 1, flags: 0x1234 };
    // Fields should be extracted with proper endianness
}

// Test 3: Array of endianness types
def test_endianness_array() {
    def values: [be_i16; 4] = [1, 2, 3, 4];
    // Each element should be stored in big-endian format
}
```

---

## IMPLEMENTATION SEQUENCE

### **Week 1: Core Infrastructure**
1. **Day 1-2**: Implement For Loop handler
2. **Day 3-4**: Add basic endianness support to `resolve_type()`
3. **Day 5**: Test and validate for loops + endianness integration

### **Week 2: Import System**
4. **Day 1-3**: Implement import framework and module loading
5. **Day 4-5**: Test and validate import system integration

### **Testing Strategy**
For each feature:
1. **Unit Tests**: Individual component testing
2. **Integration Tests**: End-to-end compilation
3. **MLIR Validation**: Generated MLIR verification
4. **Execution Tests**: mlir-runner functionality

### **Success Criteria**
- ✅ For loops compile and execute correctly
- ✅ Import statements resolve and link properly  
- ✅ Endianness types work with assignments and structs
- ✅ All existing functionality remains unchanged
- ✅ End-to-end tests pass

---

## NEXT STEPS

After implementing these three Priority 1 blockers:
1. **SPEC Coverage**: Increase from ~65% to ~85%
2. **Basic Programs**: Can write realistic Gloin code with loops, imports, and network types
3. **Foundation Ready**: Remaining features become incremental improvements

This sets up perfectly for Priority 2 features (defer integration, bit-field completion, concurrency runtime) and ultimately for Phase 3 specification review.