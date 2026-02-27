.set MB2_MAGIC, 0xE85250D6
.set MB2_ARCH, 0
.set MB2_LENGTH, multiboot_header_end - multiboot_header_start
.set MB2_CHECKSUM, -(MB2_MAGIC + MB2_ARCH + MB2_LENGTH)

.section .multiboot, "a"
.align 8
multiboot_header_start:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long MB2_LENGTH
    .long MB2_CHECKSUM

    # End tag
    .align 8
    .short 0
    .short 0
    .long 8
multiboot_header_end:

.section .text
.code32
.global multiboot_entry
.type multiboot_entry, @function
multiboot_entry:
    cli

    movl %eax, boot_magic
    movl %ebx, boot_info_ptr

    lgdt gdt32_ptr

    movl $pml4_table, %eax
    movl %eax, %cr3

    movl %cr4, %eax
    orl $0x20, %eax           # CR4.PAE
    movl %eax, %cr4

    movl $0xC0000080, %ecx    # EFER
    rdmsr
    orl $0x100, %eax          # EFER.LME
    wrmsr

    movl %cr0, %eax
    orl $0x80000001, %eax     # CR0.PG | CR0.PE
    movl %eax, %cr0

    ljmp $0x08, $long_mode_start

.code64
long_mode_start:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    movq $stack_top, %rsp
    xorq %rbp, %rbp

    movl boot_magic(%rip), %edi
    movl boot_info_ptr(%rip), %esi

    call kernel_main

1:
    hlt
    jmp 1b

.align 8
gdt64:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF
    .quad 0x00AF92000000FFFF
gdt64_end:

gdt32_ptr:
    .word gdt64_end - gdt64 - 1
    .long gdt64

.align 4096
.global pml4_table
pml4_table:
    .quad pdpt_table + 0x03
    .fill 511, 8, 0

.align 4096
pdpt_table:
    .quad pd_table + 0x03
    .fill 511, 8, 0

.align 4096
pd_table:
.set i, 0
.rept 512
    .quad (i << 21) + 0x83    # Present | RW | Huge(2 MiB)
    .set i, i + 1
.endr

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

.align 4
boot_magic:
    .long 0
boot_info_ptr:
    .long 0

.section .note.GNU-stack,"",@progbits
