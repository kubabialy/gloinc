# Gloin Compiler Development Plan

## Executive Summary

This document outlines the current state of the Gloin compiler and provides a structured plan to address three critical areas: build verification, MLIR lowering evaluation, and specification review. The codebase shows solid architectural foundations with approximately 60-70% of core language features implemented.

## 1. Build Verification & MLIR Linking Issues

### Current Status
- ✅ **Working Components**: Lexer, Parser, MLIR dialect generation, main executable
- ❌ **Critical Issue**: CMake cannot locate `MLIRConfig.cmake` despite previous successful builds
- ❌ **Test Framework**: GoogleTest conflicts with LLVM C++ ABI preventing test compilation

### Root Cause Analysis
1. **MLIR Detection Failure**: CMakeLists.txt:18 shows auto-detection logic for Homebrew LLVM that's failing
2. **ABI Compatibility**: GoogleTest vs LLVM cxxabi.h header conflicts
3. **Environment Drift**: MLIR/LLVM installation paths changed since last successful build

### Resolution Strategy

#### Phase 1.1: Environment Diagnosis
```bash
# Commands to run for diagnosis
which llvm-config
llvm-config --version
find /opt/homebrew -name "MLIRConfig.cmake" 2>/dev/null
find /usr/local -name "MLIRConfig.cmake" 2>/dev/null
echo $LLVM_DIR
echo $MLIR_DIR
```

#### Phase 1.2: Build Configuration Fix
- Update CMakeLists.txt with robust MLIR detection
- Add fallback paths for common LLVM installations
- Implement proper error handling for missing dependencies

#### Phase 1.3: Test Framework Resolution
- Resolve GoogleTest vs LLVM ABI conflicts
- Consider using LLVM's testing framework instead
- Ensure all tests compile and run successfully

#### Phase 1.4: Build Verification
- Clean rebuild from scratch
- Verify all executables (gloinc, gloinc_test) link properly
- Run full test suite (19+ test files)

## 2. MLIR Lowering Coverage Evaluation

### Current Implementation Status

#### ✅ **Implemented Operations** (GloinOps.td)
- **Control Flow**: `constant`, `yield`, `if`, `unless`, `defer`
- **Concurrency**: `spawn`, `join`, `async_call`, `await`
- **Structures**: `struct_def` with Symbol trait
- **Interfaces**: Proper CallOpInterface implementations

#### ✅ **Implemented Types** (GloinTypes.td)
- **Integers**: Endian-aware `int<width, endianness>`
- **Pointers**: `ptr<T>` with nullable/non-nullable semantics
- **Async Types**: `deferred<T>`, `spawn<T>`, `result<T, E>`
- **Structures**: Named `struct<name>` types

#### ❌ **Critical Missing Features**

##### Missing Types
- **Bit-width types**: `bit`, `u4`, `u5` for packed structs
- **Endian-prefixed types**: `be_u32`, `le_u16` syntax
- **Function pointers**: No explicit function pointer support
- **Array types**: Only basic LLVM array support

##### Missing Operations
- **For loops**: Only while loops implemented
- **Switch/Match**: Pattern matching operations
- **Bit manipulation**: Packed struct field operations
- **Memory management**: Arena alloc/free operations
- **Import system**: Module and package operations

### SPEC Coverage Gap Analysis

#### **Major Gaps (High Priority)**
1. **Control Flow Completeness**: Missing for loops and switch statements
2. **Bit-field Implementation**: SPEC defines complex bit positioning with endianness
3. **Import System**: Three-tier system (@std, ./local, #package) completely missing
4. **Type System Variants**: Missing endian-prefixed integer types

#### **Medium Priority Gaps**
1. **Concurrency Integration**: Basic ops exist but missing runtime integration
2. **Error Handling**: Result types defined but not used throughout codebase
3. **String Operations**: Basic struct representation but missing std library functions
4. **Memory Management**: Arena allocation commented out/incomplete

#### **Minor Gaps**
1. **Standard Library**: Comprehensive std functions missing
2. **Optimization Passes**: MLIR optimization not implemented
3. **Verification**: Limited MLIR dialect verification

### Evaluation Strategy

#### Phase 2.1: Feature Mapping
- Create comprehensive SPEC → MLIR operation mapping
- Identify missing operations for each language feature
- Prioritize by feature criticality

#### Phase 2.2: Implementation Testing
- End-to-end testing of current MLIR lowering
- Verify complex scenarios (nested structs, async operations)
- Test edge cases and error conditions

#### Phase 2.3: Gap Analysis Documentation
- Detailed report of missing operations/types
- Implementation complexity assessment
- Dependencies between missing features

## 3. Specification Review & Decision Document

### Critical Decision Points

#### 🔴 **High Priority Decisions (Blocking)**

##### 1. Memory Management Model
**Uncertainty**: Arena allocation syntax and semantics unclear
- **SPEC States**: Arena allocation with defer cleanup
- **Implementation**: Commented out basic arena code in src/codegen.cpp
- **Decision Needed**: 
  - Arena lifecycle management rules
  - Integration with defer statement
  - Thread safety requirements
  - Performance vs memory usage tradeoffs

##### 2. Import System Implementation
**Uncertainty**: How `@std`, `./module`, `#package` resolve and link
- **SPEC States**: Three-tier import system
- **Implementation**: No MLIR operations for imports
- **Decisions Needed**:
  - Module resolution algorithm
  - Circular dependency handling
  - Package management system
  - Binary format for compiled modules

##### 3. Standard Library Scope
**Uncertainty**: What functions belong in `@std` vs external packages
- **SPEC States**: Basic I/O, type conversion functions shown
- **Implementation**: Only basic runtime functions
- **Decisions Needed**:
  - Core std library definition
  - External package ecosystem
  - Versioning strategy
  - Platform-specific functionality

##### 4. Threading Model Semantics
**Uncertainty**: OS thread vs lightweight thread semantics for `spawnable`
- **SPEC States**: Multi-threading for CPU-bound tasks
- **Implementation**: Basic spawn operations without runtime
- **Decisions Needed**:
  - Thread pool vs individual thread creation
  - Thread-local storage rules
  - Synchronization primitives
  - Performance characteristics

#### 🟡 **Medium Priority Decisions**

##### 5. Bit-field Layout Guarantees
**Uncertainty**: Cross-platform bit positioning guarantees
- **SPEC States**: Explicit bit-positioning for portability
- **Implementation**: Basic packed struct support
- **Decisions Needed**:
  - Endianness conversion rules
  - Padding and alignment behavior
  - Compiler optimization constraints

##### 6. Error Handling Propagation
**Uncertainty**: `Result<T, E>` propagation and forced unwinding rules
- **SPEC States**: Result type for operations that can fail
- **Implementation**: Type defined but not used
- **Decisions Needed**:
  - Exception vs error code propagation
  - Forced unwinding conditions
  - Performance overhead considerations

##### 7. Method Resolution Rules
**Uncertainty**: Public/private access and symbol visibility
- **SPEC States**: pub/priv keywords with method lowering
- **Implementation**: Basic method to function transformation
- **Decisions Needed**:
  - Visibility enforcement rules
  - Symbol mangling strategy
  - Link-time optimization opportunities

#### 🟢 **Implementation Clarifications**

##### 8. String Implementation Details
- Null-termination vs length-prefixed semantics
- Unicode support strategy
- Memory ownership rules

##### 9. Defer Execution Semantics
- LIFO guarantee details with early returns
- Exception safety considerations
- Performance implications

##### 10. Async vs Thread Selection Criteria
- When to choose `deferred` vs `spawnable`
- Runtime cost models
- Resource management differences

### Recommendation Framework

#### Decision Criteria
1. **Language Consistency**: Align with explicit, transparent design goals
2. **Performance**: Zero-cost abstractions where possible
3. **Safety**: Memory and thread safety guarantees
4. **Usability**: Developer experience and learning curve
5. **Implementation**: Technical feasibility and maintenance overhead

#### Documentation Structure
Each decision should include:
- Problem statement and context
- Available options with tradeoffs
- Recommendation with rationale
- Implementation considerations
- Future extensibility implications

## 4. Implementation Roadmap

### Phase 1: Build System Stabilization (Week 1)
- [ ] Fix MLIR detection and linking issues
- [ ] Resolve test framework compilation problems
- [ ] Establish reliable CI/CD pipeline
- [ ] Verify all existing tests pass

### Phase 2: Core Feature Completion (Weeks 2-4)
- [ ] Implement for loop operations
- [ ] Add switch/match statement support
- [ ] Complete bit-field type system
- [ ] Implement missing control flow operations

### Phase 3: Import & Module System (Weeks 5-6)
- [ ] Design MLIR operations for imports
- [ ] Implement module resolution algorithm
- [ ] Add package system infrastructure
- [ ] Create standard library foundation

### Phase 4: Advanced Features (Weeks 7-8)
- [ ] Complete memory management (arenas)
- [ ] Integrate error handling throughout
- [ ] Optimize MLIR lowering pipeline
- [ ] Add comprehensive test coverage

### Phase 5: Ecosystem Development (Weeks 9-12)
- [ ] Expand standard library
- [ ] Create development tools
- [ ] Performance optimization
- [ ] Documentation and examples

## 5. Success Metrics

### Technical Metrics
- **Build Success Rate**: 100% reliable builds across platforms
- **Test Coverage**: >90% line coverage for implemented features
- **SPEC Compliance**: 100% of mandatory SPEC features implemented
- **Performance**: Competitive with equivalent C/C++ code

### Quality Metrics
- **Code Quality**: Maintainable, well-documented codebase
- **Developer Experience**: Clear error messages and debugging support
- **Ecosystem**: Growing package and tool ecosystem
- **Community**: Active development and contribution pipeline

## 6. Risk Assessment

### High-Risk Items
1. **MLIR/LLVM Dependency**: External dependency management complexity
2. **Import System**: Complex module resolution and packaging
3. **Performance Requirements**: Meeting zero-cost abstraction goals
4. **Ecosystem Development**: Building critical mass of packages and tools

### Mitigation Strategies
1. **Version Pinning**: Lock to stable MLIR/LLVM releases
2. **Incremental Implementation**: Phase complex features
3. **Performance Testing**: Continuous benchmarking
4. **Community Building**: Early adopter engagement and feedback

---

*This document is a living plan and will be updated as the project evolves and new information becomes available.*