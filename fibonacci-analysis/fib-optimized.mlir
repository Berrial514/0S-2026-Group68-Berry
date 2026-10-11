module {
  func.func @main() -> i32 {
    %c0_i32 = arith.constant 0 : i32
    %c1_i32 = arith.constant 1 : i32
    %c1 = arith.constant 1 : index
    %c5 = arith.constant 5 : index
    %0:2 = scf.for %arg0 = %c1 to %c5 step %c1 iter_args(%arg1 = %c0_i32, %arg2 = %c1_i32) -> (i32, i32) {
      %1 = arith.addi %arg1, %arg2 : i32
      scf.yield %arg2, %1 : i32, i32
    }
    return %0#1 : i32
  }
}

