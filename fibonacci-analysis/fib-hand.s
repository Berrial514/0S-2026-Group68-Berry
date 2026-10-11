    .section .rodata
input_fmt:
    .string "%d"
output_fmt:
    .string "%d\n"

    .text
    .global main

main:
    addi sp, sp, -48
    sd ra, 40(sp)
    sd s0, 32(sp)
    sd s1, 24(sp)
    sd s2, 16(sp)
    sd s3, 8(sp)

    la a0, input_fmt
    mv a1, sp
    call scanf
    lw s0, 0(sp)        # n

    li s1, 0            # a = 0
    li s2, 1            # b = 1
    li s3, 1            # i = 1

    la a0, output_fmt
    mv a1, s1
    call printf

    la a0, output_fmt
    mv a1, s2
    call printf

loop:
    bge s3, s0, done    # if i >= n, done
    mv t0, s2           # t = b
    addw s2, s1, s2     # b = a + b
    mv s1, t0           # a = t

    la a0, output_fmt
    mv a1, s2
    call printf

    addiw s3, s3, 1     # i = i + 1
    j loop

done:
    ld ra, 40(sp)
    ld s0, 32(sp)
    ld s1, 24(sp)
    ld s2, 16(sp)
    ld s3, 8(sp)
    addi sp, sp, 48
    ret
