module {
  llvm.func @main() -> i32 {
    %0 = llvm.mlir.constant(0 : i32) : i32
    %1 = llvm.mlir.constant(1 : i32) : i32
    %2 = llvm.mlir.constant(1 : index) : i64
    %3 = llvm.mlir.constant(5 : index) : i64
    llvm.br ^bb1(%2, %0, %1 : i64, i32, i32)
  ^bb1(%4: i64, %5: i32, %6: i32):  // 2 preds: ^bb0, ^bb2
    %7 = llvm.icmp "slt" %4, %3 : i64
    llvm.cond_br %7, ^bb2, ^bb3
  ^bb2:  // pred: ^bb1
    %8 = llvm.add %5, %6  : i32
    %9 = llvm.add %4, %2  : i64
    llvm.br ^bb1(%9, %6, %8 : i64, i32, i32)
  ^bb3:  // pred: ^bb1
    llvm.return %6 : i32
  }
}

