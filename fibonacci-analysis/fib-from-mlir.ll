; ModuleID = 'LLVMDialectModule'
source_filename = "LLVMDialectModule"

define i32 @main() {
  br label %1

1:                                                ; preds = %6, %0
  %2 = phi i64 [ %8, %6 ], [ 1, %0 ]
  %3 = phi i32 [ %4, %6 ], [ 0, %0 ]
  %4 = phi i32 [ %7, %6 ], [ 1, %0 ]
  %5 = icmp slt i64 %2, 5
  br i1 %5, label %6, label %9

6:                                                ; preds = %1
  %7 = add i32 %3, %4
  %8 = add i64 %2, 1
  br label %1

9:                                                ; preds = %1
  ret i32 %4
}

!llvm.module.flags = !{!0}

!0 = !{i32 2, !"Debug Info Version", i32 3}
