#include "support/cli_fixture.h"
#include <regex>

namespace {
class CoreAcceptanceTest : public gloin_test::CliFixture {
  protected:
    std::string fixture(const std::string &name) {
        const char *override_path = std::getenv("GLOIN_TEST_FIXTURES");
        const auto path =
            std::string(override_path ? override_path : gloin_test::core_fixtures) + "/" + name;
        EXPECT_TRUE(llvm::sys::fs::is_regular_file(path)) << path;
        return path;
    }
    void runs(const std::string &name, const std::string &expected) {
        const auto file = fixture(name);
        expect_success(invoke({"--check", file}), "");
        // Independent compile/JIT processes must produce the same observable result.
        for (unsigned i = 0; i < 3; ++i)
            expect_run(invoke({file}), std::stoi(expected));
    }
    void rejects(const std::string &name, const std::string &reason) {
        const auto file = fixture(name);
        for (const std::string mode : {"--run", "--check", "--emit-ir", "--emit-llvm"}) {
            SCOPED_TRACE(mode);
            const auto result = invoke({mode, file});
            expect_error(result, 1, "error:");
            // Match diagnostic text, never the fixture filename (e.g. "constant_cycle").
            const auto message = result.err.find(": error: ");
            ASSERT_NE(message, std::string::npos) << result.err;
            const auto end = result.err.find('\n', message);
            EXPECT_NE(result.err.substr(message, end - message).find(reason), std::string::npos)
                << result.err;
            const auto position = result.err.find(file + ":");
            ASSERT_NE(position, std::string::npos) << result.err;
            EXPECT_TRUE(std::regex_search(result.err.substr(position + file.size()),
                                          std::regex(R"(^:[1-9][0-9]*:[1-9][0-9]*: error:)")))
                << result.err;
        }
    }
    void traps(const std::string &name) {
        const auto file = fixture(name);
        expect_success(invoke({"--check", file}), "");
        const auto result = invoke({file});
        EXPECT_LT(result.status, 0) << result.err;
        EXPECT_TRUE(result.out.empty()) << result.out;
        EXPECT_TRUE(result.err.empty()) << result.err;
        EXPECT_TRUE(result.message.find("Trace") != std::string::npos ||
                    result.message.find("Illegal instruction") != std::string::npos)
            << result.message;
    }
};
} // namespace

TEST_F(CoreAcceptanceTest, RunMinimal) { runs("run/minimal.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunFixedArrays) {
    runs("run/fixed_arrays.gloin", "42");
    const auto executable = directory + "/fixed-arrays";
    expect_success(invoke_raw({"-o", executable, fixture("run/fixed_arrays.gloin")}), "");
    const auto out = directory + "/fixed-arrays.stdout";
    const auto err = directory + "/fixed-arrays.stderr";
    const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
    std::string message;
    bool launch_failed = false;
    const int code = llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects,
                                                10, 0, &message, &launch_failed);
    EXPECT_FALSE(launch_failed) << message;
    EXPECT_EQ(code, 42) << message << read(err);
    EXPECT_TRUE(read(out).empty());
    EXPECT_TRUE(read(err).empty());
}
TEST_F(CoreAcceptanceTest, RejectFixedArrayLength) {
    rejects("reject/fixed_array_length.gloin", "element count");
}
TEST_F(CoreAcceptanceTest, RejectFixedArrayElement) {
    rejects("reject/fixed_array_element.gloin", "element type mismatch");
}
TEST_F(CoreAcceptanceTest, RejectFixedArrayImmutable) {
    rejects("reject/fixed_array_immutable.gloin", "Cannot write");
}
TEST_F(CoreAcceptanceTest, RejectFixedArrayIndexType) {
    rejects("reject/fixed_array_index_type.gloin", "index must be an integer");
}
TEST_F(CoreAcceptanceTest, RejectRecursiveFixedArray) {
    rejects("reject/fixed_array_recursive.gloin", "Recursive by-value struct");
}
TEST_F(CoreAcceptanceTest, RejectFixedArraySize) {
    rejects("reject/fixed_array_size.gloin", "Invalid fixed-array type");
}
TEST_F(CoreAcceptanceTest, TrapFixedArrayBounds) {
    traps("trap/fixed_array_bounds.gloin");
}
TEST_F(CoreAcceptanceTest, TrapFixedArrayNegative) {
    traps("trap/fixed_array_negative.gloin");
}
TEST_F(CoreAcceptanceTest, TrapFixedArrayEmptyIndex) {
    traps("trap/fixed_array_empty_index.gloin");
}

TEST_F(CoreAcceptanceTest, RunArena) { runs("run/arena.gloin", "42"); }
TEST_F(CoreAcceptanceTest, RejectArenaTypeArgument) {
    rejects("reject/arena_value.gloin", "Expected expression");
}
TEST_F(CoreAcceptanceTest, TrapFreedArena) { traps("trap/arena_freed.gloin"); }

TEST_F(CoreAcceptanceTest, RunForwardCall) { runs("run/forward_call.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunControlFlow) { runs("run/control_flow.gloin", "3"); }

TEST_F(CoreAcceptanceTest, RunBases) { runs("run/bases.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunScalarI8) { runs("run/scalar_i8.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunScalarI16) { runs("run/scalar_i16.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunScalarI32) { runs("run/scalar_i32.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunScalarI64) { runs("run/scalar_i64.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunScalarU8) { runs("run/scalar_u8.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunScalarU16) { runs("run/scalar_u16.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunScalarU32) { runs("run/scalar_u32.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunScalarU64) { runs("run/scalar_u64.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunScalarInt) { runs("run/scalar_int.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunScalarUsize) { runs("run/scalar_usize.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunScalarF32) { runs("run/scalar_f32.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunScalarF64) { runs("run/scalar_f64.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunBoolVoid) { runs("run/bool_void.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunShortCircuit) { runs("run/short_circuit.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunRecursion) { runs("run/recursion.gloin", "120"); }

TEST_F(CoreAcceptanceTest, RunScopeInitialization) { runs("run/scope_initialization.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunNestedEarlyReturns) { runs("run/nested_early_returns.gloin", "21"); }

TEST_F(CoreAcceptanceTest, RunForComponents) { runs("run/for_components.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunPrecedence) { runs("run/precedence.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunUtf8Comments) { runs("run/utf8_comments.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunNegativeResult) { runs("run/negative_result.gloin", "-1"); }

TEST_F(CoreAcceptanceTest, RunZeroResult) { runs("run/zero_result.gloin", "0"); }

TEST_F(CoreAcceptanceTest, RunMinimumResult) { runs("run/minimum_result.gloin", "-2147483648"); }

TEST_F(CoreAcceptanceTest, RunMaximumResult) { runs("run/maximum_result.gloin", "2147483647"); }

TEST_F(CoreAcceptanceTest, RejectCanonical01) {
    rejects("reject/canonical_01.gloin", "top-level function or constant");
}

TEST_F(CoreAcceptanceTest, RejectCanonical02) {
    rejects("reject/canonical_02.gloin", "top-level function or constant");
}

TEST_F(CoreAcceptanceTest, RejectCanonical03) {
    rejects("reject/canonical_03.gloin", "cannot be combined");
}

TEST_F(CoreAcceptanceTest, RejectCanonical04) {
    rejects("reject/canonical_04.gloin", "explicit binding type");
}

TEST_F(CoreAcceptanceTest, RejectCanonical05) {
    rejects("reject/canonical_05.gloin", "explicit parameter type");
}

TEST_F(CoreAcceptanceTest, RejectCanonical06) {
    rejects("reject/canonical_06.gloin", "explicit return type");
}

TEST_F(CoreAcceptanceTest, RejectCanonical07) { rejects("reject/canonical_07.gloin", "semicolon"); }

TEST_F(CoreAcceptanceTest, RejectCanonical08) { rejects("reject/canonical_08.gloin", "ASCII"); }

TEST_F(CoreAcceptanceTest, RejectCanonical09) {
    rejects("reject/canonical_09.gloin", "identifier");
}

TEST_F(CoreAcceptanceTest, RejectCanonical10) {
    rejects("reject/canonical_10.gloin", "Unknown or unsupported core type");
}

TEST_F(CoreAcceptanceTest, RejectCanonical11) {
    rejects("reject/canonical_11.gloin", "must be bool");
}

TEST_F(CoreAcceptanceTest, RejectImmutable) { rejects("reject/immutable.gloin", "immutable"); }

TEST_F(CoreAcceptanceTest, RejectUninitialized) {
    rejects("reject/uninitialized.gloin", "uninitialized");
}

TEST_F(CoreAcceptanceTest, RejectPartialInitialization) {
    rejects("reject/partial_initialization.gloin", "uninitialized");
}

TEST_F(CoreAcceptanceTest, RejectUnknownName) {
    rejects("reject/unknown_name.gloin", "Undefined variable");
}

TEST_F(CoreAcceptanceTest, RejectMixedWidths) {
    rejects("reject/mixed_widths.gloin", "Type mismatch");
}

TEST_F(CoreAcceptanceTest, RejectSignedness) {
    rejects("reject/signedness.gloin", "Type mismatch");
}

TEST_F(CoreAcceptanceTest, RejectLiteralRange) {
    rejects("reject/literal_range.gloin", "out of range");
}

TEST_F(CoreAcceptanceTest, RejectFloatRange) {
    rejects("reject/float_range.gloin", "out of range");
}

TEST_F(CoreAcceptanceTest, RejectFloatUnderflow) {
    rejects("reject/float_underflow.gloin", "underflows to zero");
}

TEST_F(CoreAcceptanceTest, RejectUnsignedNegative) {
    rejects("reject/unsigned_negative.gloin", "unsigned type");
}

TEST_F(CoreAcceptanceTest, RejectBoolArithmetic) {
    rejects("reject/bool_arithmetic.gloin", "Invalid binary operator");
}

TEST_F(CoreAcceptanceTest, RejectFloatRemainder) {
    rejects("reject/float_remainder.gloin", "Invalid binary operator");
}

TEST_F(CoreAcceptanceTest, RejectUnsignedNegation) {
    rejects("reject/unsigned_negation.gloin", "Invalid unary operator");
}

TEST_F(CoreAcceptanceTest, RejectScopeEscape) {
    rejects("reject/scope_escape.gloin", "Undefined variable");
}

TEST_F(CoreAcceptanceTest, RejectLoopScopeEscape) {
    rejects("reject/loop_scope_escape.gloin", "Undefined variable");
}

TEST_F(CoreAcceptanceTest, RejectConstantFailure) {
    rejects("reject/constant_failure.gloin", "zero");
}

TEST_F(CoreAcceptanceTest, RejectConstantRuntime) {
    rejects("reject/constant_runtime.gloin", "constant");
}

TEST_F(CoreAcceptanceTest, RejectCallArity) {
    rejects("reject/call_arity.gloin", "number of arguments");
}

TEST_F(CoreAcceptanceTest, RejectReturnType) {
    rejects("reject/return_type.gloin", "Return type mismatch");
}

TEST_F(CoreAcceptanceTest, RejectVoidValue) {
    rejects("reject/void_value.gloin", "void is only allowed");
}

TEST_F(CoreAcceptanceTest, RejectDuplicate) {
    rejects("reject/duplicate.gloin", "Duplicate declaration");
}

TEST_F(CoreAcceptanceTest, RejectChainedAssignment) {
    rejects("reject/chained_assignment.gloin", "Chained assignment");
}

TEST_F(CoreAcceptanceTest, RejectNumericCast) {
    rejects("reject/numeric_cast.gloin", "Expected expression");
}

TEST_F(CoreAcceptanceTest, RejectMalformedNumber) {
    rejects("reject/malformed_number.gloin", "literal");
}

TEST_F(CoreAcceptanceTest, RejectMissingReturn) {
    rejects("reject/missing_return.gloin", "without returning a value");
}

TEST_F(CoreAcceptanceTest, RejectParameterMutation) {
    rejects("reject/parameter_mutation.gloin", "immutable");
}

TEST_F(CoreAcceptanceTest, RejectUnreachable) {
    rejects("reject/unreachable.gloin", "Unreachable");
}

TEST_F(CoreAcceptanceTest, RejectBadEntry) { rejects("reject/bad_entry.gloin", "main"); }

TEST_F(CoreAcceptanceTest, RejectConstantCycle) {
    rejects("reject/constant_cycle.gloin", "Undefined constant");
}

TEST_F(CoreAcceptanceTest, RunsStrings) {
    runs("reject/deferred_strings.gloin", "42");
}

TEST_F(CoreAcceptanceTest, RejectDeferredCharacters) {
    rejects("reject/deferred_characters.gloin", "Character");
}

TEST_F(CoreAcceptanceTest, RejectPointerTypeMismatch) {
    rejects("reject/pointer_type_mismatch.gloin", "Type mismatch");
}

TEST_F(CoreAcceptanceTest, RejectNullReference) {
    rejects("reject/null_reference.gloin", "null requires");
}

TEST_F(CoreAcceptanceTest, RejectBracketArrayLiteral) {
    rejects("reject/deferred_array.gloin", "Array literals");
}

TEST_F(CoreAcceptanceTest, RejectDeferredSlice) {
    rejects("reject/deferred_slice.gloin", "Expected ';' in array type");
}

TEST_F(CoreAcceptanceTest, RejectDeferOperand) { rejects("reject/defer_operand.gloin", "defer requires"); }

TEST_F(CoreAcceptanceTest, RejectFunctionMember) {
    rejects("reject/function_member.gloin", "Function values");
}

TEST_F(CoreAcceptanceTest, RejectIndexNonArray) {
    rejects("reject/deferred_index.gloin", "Indexing requires a fixed array");
}

TEST_F(CoreAcceptanceTest, RejectDeferredSpawn) {
    rejects("reject/deferred_spawn.gloin", "legacy");
}

TEST_F(CoreAcceptanceTest, RejectDeferredAwait) {
    rejects("reject/deferred_await.gloin", "legacy");
}

TEST_F(CoreAcceptanceTest, RejectDeferredRun) { rejects("reject/deferred_run.gloin", "core"); }

TEST_F(CoreAcceptanceTest, RejectDeferredRange) {
    rejects("reject/deferred_range.gloin", "expression");
}

TEST_F(CoreAcceptanceTest, RejectDeferredBreak) {
    rejects("reject/deferred_break.gloin", "expression");
}

TEST_F(CoreAcceptanceTest, RejectDeferredContinue) {
    rejects("reject/deferred_continue.gloin", "expression");
}

TEST_F(CoreAcceptanceTest, RejectDeferredBitwise) {
    rejects("reject/deferred_bitwise.gloin", "semicolon");
}

TEST_F(CoreAcceptanceTest, RejectDeferredShift) {
    rejects("reject/deferred_shift.gloin", "semicolon");
}

TEST_F(CoreAcceptanceTest, RejectDeferredCompound) {
    rejects("reject/deferred_compound.gloin", "semicolon");
}

TEST_F(CoreAcceptanceTest, RejectDeferredUnaryPlus) {
    rejects("reject/deferred_unary_plus.gloin", "expression");
}

TEST_F(CoreAcceptanceTest, RejectDeferredQuestion) {
    rejects("reject/deferred_question.gloin", "semicolon");
}

TEST_F(CoreAcceptanceTest, RejectDeferredSwitch) {
    rejects("reject/deferred_switch.gloin", "Expected expression");
}

TEST_F(CoreAcceptanceTest, RejectDeferredMatch) {
    rejects("reject/deferred_match.gloin", "Expected expression");
}

TEST_F(CoreAcceptanceTest, StandardHelloWorld) {
    auto file = fixture("run/hello_world.gloin");
    expect_success(invoke({"--check", file}), "");
    expect_success(invoke({"--run", file}), "Hello World!\n");
}

TEST_F(CoreAcceptanceTest, RejectBareLocalImport) {
    rejects("reject/bare_local_import.gloin", "Unsupported module path");
}

TEST_F(CoreAcceptanceTest, RejectDeferredPackageImport) {
    rejects("reject/deferred_package_import.gloin", "Unsupported module path");
}

TEST_F(CoreAcceptanceTest, RejectDeferredPackedStruct) {
    rejects("reject/deferred_packed_struct.gloin", "Packed structs");
}

TEST_F(CoreAcceptanceTest, RejectDeferredPacked) {
    rejects("reject/deferred_packed.gloin", "Packed structs");
}

TEST_F(CoreAcceptanceTest, RejectDeferredEnum) {
    rejects("reject/deferred_enum.gloin", "identifier");
}

TEST_F(CoreAcceptanceTest, RejectDeferredGeneric) {
    rejects("reject/deferred_generic.gloin", "Generic");
}

TEST_F(CoreAcceptanceTest, RejectDeferredExtern) {
    rejects("reject/deferred_extern.gloin", "top-level function or constant");
}

TEST_F(CoreAcceptanceTest, RejectDeferredSpawnable) {
    rejects("reject/deferred_spawnable.gloin", "Concurrency");
}

TEST_F(CoreAcceptanceTest, RejectDeferredDeferred) {
    rejects("reject/deferred_deferred.gloin", "Concurrency");
}

TEST_F(CoreAcceptanceTest, RejectDeferredTypeI128) {
    rejects("reject/deferred_type_i128.gloin", "Unknown or unsupported core type");
}

TEST_F(CoreAcceptanceTest, RejectDeferredTypeU128) {
    rejects("reject/deferred_type_u128.gloin", "Unknown or unsupported core type");
}

TEST_F(CoreAcceptanceTest, RejectDeferredTypeF128) {
    rejects("reject/deferred_type_f128.gloin", "Unknown or unsupported core type");
}

TEST_F(CoreAcceptanceTest, RejectDeferredTypeChar) {
    rejects("reject/deferred_type_char.gloin", "Unknown or unsupported core type");
}

TEST_F(CoreAcceptanceTest, RejectDeferredTypeI4) {
    rejects("reject/deferred_type_i4.gloin", "Unknown or unsupported core type");
}

TEST_F(CoreAcceptanceTest, RejectDeferredTypeBeU16) {
    rejects("reject/deferred_type_be_u16.gloin", "Unknown or unsupported core type");
}

TEST_F(CoreAcceptanceTest, RejectDeferredTypeU16Be) {
    rejects("reject/deferred_type_u16_be.gloin", "Unknown or unsupported core type");
}

TEST_F(CoreAcceptanceTest, RejectUnimportedArena) {
    rejects("reject/unimported_arena.gloin", "Unknown or unsupported core type");
}

TEST_F(CoreAcceptanceTest, RejectDeferredTypeResult) {
    rejects("reject/deferred_type_Result.gloin", "Unknown or unsupported core type");
}

TEST_F(CoreAcceptanceTest, RejectDeferredTypeDeferred) {
    rejects("reject/deferred_type_Deferred.gloin", "Unknown or unsupported core type");
}

TEST_F(CoreAcceptanceTest, RejectDeferredTypeSpawn) {
    rejects("reject/deferred_type_Spawn.gloin", "Unknown or unsupported core type");
}

TEST_F(CoreAcceptanceTest, TrapOverflowI8) { traps("trap/overflow_i8.gloin"); }

TEST_F(CoreAcceptanceTest, TrapOverflowI16) { traps("trap/overflow_i16.gloin"); }

TEST_F(CoreAcceptanceTest, TrapOverflowI32) { traps("trap/overflow_i32.gloin"); }

TEST_F(CoreAcceptanceTest, TrapOverflowI64) { traps("trap/overflow_i64.gloin"); }

TEST_F(CoreAcceptanceTest, TrapOverflowU8) { traps("trap/overflow_u8.gloin"); }

TEST_F(CoreAcceptanceTest, TrapOverflowU16) { traps("trap/overflow_u16.gloin"); }

TEST_F(CoreAcceptanceTest, TrapOverflowU32) { traps("trap/overflow_u32.gloin"); }

TEST_F(CoreAcceptanceTest, TrapOverflowU64) { traps("trap/overflow_u64.gloin"); }

TEST_F(CoreAcceptanceTest, TrapDivisionZero) { traps("trap/division_zero.gloin"); }

TEST_F(CoreAcceptanceTest, TrapSignedRemainder) { traps("trap/signed_remainder.gloin"); }

TEST_F(CoreAcceptanceTest, TrapDivisionZeroF32) { traps("trap/division_zero_f32.gloin"); }

TEST_F(CoreAcceptanceTest, TrapOverflowF32) { traps("trap/overflow_f32.gloin"); }

TEST_F(CoreAcceptanceTest, TrapDivisionZeroF64) { traps("trap/division_zero_f64.gloin"); }

TEST_F(CoreAcceptanceTest, TrapOverflowF64) { traps("trap/overflow_f64.gloin"); }

TEST_F(CoreAcceptanceTest, RunOrdinaryStruct) { runs("run/ordinary_struct.gloin", "42"); }

TEST_F(CoreAcceptanceTest, RunPointers) { runs("run/pointers.gloin", "100"); }
TEST_F(CoreAcceptanceTest, TrapNullDereference) { traps("trap/null_dereference.gloin"); }
TEST_F(CoreAcceptanceTest, RunMethods) { runs("run/methods.gloin", "42"); }
TEST_F(CoreAcceptanceTest, RejectMethodReceiver) { rejects("reject/method_receiver.gloin", "receiver"); }
TEST_F(CoreAcceptanceTest, TrapNullMethod) { traps("trap/null_method.gloin"); }
TEST_F(CoreAcceptanceTest, RunDefer) { runs("run/defer.gloin", "42"); }
TEST_F(CoreAcceptanceTest, TrapDeferCleanup) { traps("trap/defer_cleanup.gloin"); }

TEST_F(CoreAcceptanceTest, RunLocalModule) { runs("run/local_module.gloin", "25"); }
TEST_F(CoreAcceptanceTest, RunModuleAsRoot) { runs("modules/utils.gloin", "25"); }
TEST_F(CoreAcceptanceTest, RejectPrivateModuleMember) {
    rejects("reject/private_module_member.gloin", "Unknown or private member");
}

TEST_F(CoreAcceptanceTest, StandardLibraryConversions) {
    const auto file = fixture("run/standard_library.gloin");
    expect_success(invoke({"--check", file}), "");
    expect_success(invoke({file}), "42\n");
}
TEST_F(CoreAcceptanceTest, RejectImplicitStandardResult) {
    rejects("reject/standard_result_type.gloin", "Type mismatch");
}
TEST_F(CoreAcceptanceTest, TrapFormattingWithFreedArena) { traps("trap/format_freed.gloin"); }
