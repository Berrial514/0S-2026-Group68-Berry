module {
  func.func @main() -> i32 {
    %c0_i32 = arith.constant 0 : i32
    %c1_i32 = arith.constant 1 : i32
    %c1 = arith.constant 1 : index
    %c5 = arith.constant 5 : index

    %result:2 = scf.for %i = %c1 to %c5 step %c1
        iter_args(%a = %c0_i32, %b = %c1_i32) -> (i32, i32) {
      %next = arith.addi %a, %b : i32
      scf.yield %b, %next : i32, i32
    }

    return %result#1 : i32
  }
}
