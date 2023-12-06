# Context switch
#
#   void swtch(struct context *old, struct context *new);
# 
# Save current registers in old. Load from new.	


.globl swtch
swtch:

# 使用sd指令，将返回地址寄存器ra，栈指针寄存器sp，以及保存的寄存器s0-s11，
# 按顺序存储到a0指向的内存地址，每个寄存器占用8个字节，共计112个字节。
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)
# 使用ld指令，从a1指向的内存地址，
# 按顺序加载返回地址寄存器ra，栈指针寄存器sp，以及保存的寄存器s0-s11，每个寄存器占用8个字节，共计112个字节

        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)

        
# 使用ret指令，返回到ra寄存器中存储的地址，即下一个线程的执行位置
        ret

	
