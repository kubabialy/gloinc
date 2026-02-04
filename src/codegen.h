#ifndef CODEGEN_H
#define CODEGEN_H

#include "AST.h"
#include "sema.h" // For type info
#include "dialect/GloinDialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

#include <memory>
#include <vector>
#include <map>

class CodeGen {
public:
    CodeGen(mlir::MLIRContext& context);

    mlir::ModuleOp generate(const std::vector<std::unique_ptr<Statement>>& program);
    void dump();

private:
    mlir::MLIRContext& context;
    mlir::OpBuilder builder;
    mlir::ModuleOp theModule;
    
    // Symbol table for variables
    struct SymbolInfo {
        mlir::Value value; // The SSA value or the MemRef address
        bool is_address;   // True if 'value' is a pointer/memref to the actual data
        mlir::Type type;   // The MLIR type of the value
    };
    
    struct GenScope {
        std::map<std::string, SymbolInfo> values;
        std::vector<const DeferStatement*> deferred;
        std::shared_ptr<GenScope> parent;
    };
    std::shared_ptr<GenScope> current_scope;

    // Type registry
    std::map<std::string, mlir::Type> type_table;
    // Struct field indices: struct_name -> field_name -> index
    std::map<std::string, std::map<std::string, int>> struct_field_indices;
    // Struct field types: struct_name -> field_name -> type
    std::map<std::string, std::map<std::string, mlir::Type>> struct_field_types;
    // Struct field offsets (bits) for packed structs
    std::map<std::string, std::map<std::string, int>> struct_field_offsets;
    // Struct field widths (bits) for packed structs
    std::map<std::string, std::map<std::string, int>> struct_field_widths;
    // Struct endianness: struct_name -> is_big_endian
    std::map<std::string, bool> struct_is_big_endian;

    void enter_scope();
    void leave_scope();
    void declare(const std::string& name, mlir::Value value, bool is_address, mlir::Type type);
    SymbolInfo lookup(const std::string& name);

    // Visitation methods
    void gen_statement(const Statement* stmt);
    mlir::Value gen_expression(const Expression* expr);
    mlir::Value gen_address(const Expression* expr);
    mlir::Type get_expression_type(const Expression* expr);
    
    // Helper to resolve type from AST/String to MLIR Type
    mlir::Type resolve_type(const std::string& type_name);
    int64_t get_type_size(mlir::Type type);
    void create_runtime_functions();

    void emit_deferred();

    // Specific handlers
    void gen_function(const std::string& name, const std::vector<std::string>& args, const Statement* body);
};

#endif // CODEGEN_H
