// RUN: ./gloinc_test --gtest_filter=CodeGenTest.GenerateSpawn 2>&1 | FileCheck %s

// CHECK: "builtin.module"()
// CHECK: "func.func"() {{.*}}sym_name = "worker"
// CHECK:   "func.return"

// CHECK: "func.func"() {{.*}}sym_name = "main"
// CHECK:   %[[C1:.*]] = "arith.constant"() <{value = 1 : i32}> : () -> i32
// CHECK:   %[[H:.*]] = "gloin.spawn"(%[[C1]]) <{callee = @worker}> : (i32) -> !gloin.spawn<i32>
// CHECK:   "func.return"
