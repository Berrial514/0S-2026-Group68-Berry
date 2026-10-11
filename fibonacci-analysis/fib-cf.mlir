module {
  func.func @main() -> i32 {
    %c0_i32 = arith.constant 0 : i32
    %c1_i32 = arith.constant 1 : i32
    %c1 = arith.constant 1 : index
    %c5 = arith.constant 5 : index
    cf.br ^bb1(%c1, %c0_i32, %c1_i32 : index, i32, i32)
  ^bb1(%0: index, %1: i32, %2: i32):  // 2 preds: ^bb0, ^bb2
    %3 = arith.cmpi slt, %0, %c5 : index
    cf.cond_br %3, ^bb2, ^bb3
  ^bb2:  // pred: ^bb1
    %4 = arith.addi %1, %2 : i32
    %5 = arith.addi %0, %c1 : index
    cf.br ^bb1(%5, %2, %4 : index, i32, i32)
  ^bb3:  // pred: ^bb1
    return %2 : i32
  }
}

