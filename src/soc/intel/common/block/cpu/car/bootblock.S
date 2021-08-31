
.section ".rom.data", "a", @progbits
.section ".rom.text", "ax", @progbits
.code16
.globl _start
.type _start, @function

_start16bit:
 cli

 movl %eax, %ebp

 movb $0x01, %al; outb %al, $0x80

 xorl %eax, %eax
 movl %eax, %cr3
 movw %cs, %ax
 shlw $4, %ax
 movw $nullidt_offset, %bx
 subw %ax, %bx
 lidt %cs:(%bx)
 movw $gdtptr16_offset, %bx
 subw %ax, %bx
 lgdtl %cs:(%bx)

 movl %cr0, %eax
 andl $0x7FFAFFD1, %eax
 orl $0x60000001, %eax
 movl %eax, %cr0

 movl %ebp, %eax

 ljmpl $0x08, $__protected_start

.align 4
.globl gdtptr16
gdtptr16:
 .word gdt_end - gdt -1
 .long gdt

.align 4
.globl nullidt
nullidt:
 .word 0
 .long 0
 .word 0

.globl _estart
_estart:
 .code32
 .section ".reset"
 .code16
.globl reset_vector
reset_vector:
 .byte 0xe9
 .int _start - ( . + 2 )

 .org 0x8;
 .code32
 jmp protected_start
 .previous

 .code32

 .align 4
.globl gdtptr
gdt:
gdtptr:
 .word gdt_end - gdt -1
 .long gdt
 .word 0

 .word 0xffff, 0x0000
 .byte 0x00, 0x9b, 0xcf, 0x00

 .word 0xffff, 0x0000
 .byte 0x00, 0x93, 0xcf, 0x00

 .word 0xffff, 0x0000
 .byte 0x00, 0x9b, 0xaf, 0x00

gdt_end:
 .align 4
.globl protected_start
protected_start:

 lgdt %cs:gdtptr
 ljmp $0x08, $__protected_start

__protected_start:

 movl %eax, %ebp

 movb $0x10, %al; outb %al, $0x80

 movw $0x10, %ax
 movw %ax, %ds
 movw %ax, %es
 movw %ax, %ss
 movw %ax, %fs
 movw %ax, %gs

 movl %ebp, %eax
 movl $0x8000f880 , %eax
 movw $0xcf8, %dx
 outl %eax, %dx

 movl $0x3c030000, %eax
 movw $0xcfc, %dx
 outl %eax, %dx

switch_on:

 movw $0x66, %dx
1:
 inb %dx, %al
 andb $0x02, %al
 jnz 1b

 movb $0x81, %al
 outb %al, %dx

2:
 inb %dx, %al
 andb $0x02, %al
 jnz 2b

 movb $0x19, %al
 movw $0x62, %dx
 outb %al, %dx

 movw $0x66, %dx
3:
 inb %dx, %al
 andb $0x02, %al
 jnz 3b

 movb $0xee, %al
 movw $0x62, %dx
 outb %al, %dx

 movl $0x00010000, %ecx
 .section ".id", "a", @progbits

 .globl __id_start
__id_start:
ver:
 .asciz "magical1"
vendor:
 .asciz "Star"
part:
 .asciz "Lite"
.long __id_end + 0x80 - ver
.long __id_end + 0x80 - vendor
.long __id_end + 0x80 - part
.long 0x200000
 .globl __id_end

__id_end:
.previous
.section ".fit_pointer", "a", @progbits
 .code32
.global fit_pointer
fit_pointer:
.long fit_table
.long 0
.previous

.section ".rom.data", "a", @progbits
.align 16
.global fit_table
.global fit_table_end
fit_table:

.long 0x5449465f
.long 0x2020205f

.long 0x00000001

.word 0x0100

.byte 0x80

.byte 0x7d
.fill 4*16
fit_table_end:
.previous

 .code32

.global stash_timestamp
stash_timestamp:

 movl %eax, %ebp

 finit
 rdtsc
 movd %eax, %mm0
 movd %edx, %mm1

 movl %ebp, %eax
.section ".rom.data"
.balign 16
L1:
.int 0xff, 0, 0, 0
L2:
.int 0xffff, 0, 0, 0
.section ".rom.text"

L1001:
L1002:

.section ".rom.data"
.balign 1
L1003:
.byte 0x66
.byte 0x61
.byte 0x6c
.byte 0x6c
.byte 0x62
.byte 0x61
.byte 0x63
.byte 0x6b
.byte 0x2f
.byte 0x76
.byte 0x65
.byte 0x72
.byte 0x73
.byte 0x74
.byte 0x61
.byte 0x67
.byte 0x65
.byte 0x00
.section ".rom.text"

 mov %eax, %ebp

 mov $27 , %ecx
 rdmsr
 and $256 , %eax

 test %eax, %eax
 jz L1008
L1004:
 mov $767 , %ecx
 rdmsr
 and $3072 , %eax
 test %eax, %eax
 jz L1008
L1005:
 mov $6 , %al
 mov $3321 , %dx
 outb %al, %dx

 jmp L1007
L1006:
 hlt

L1007:
 jmp L1006

L1008:

 mov $L1003+0 , %esi
 mov $1f, %esp
 jmp walkcbfs_asm
 1:

 mov %eax, %esp

 cmp $0 , %esp
 jz L1010
L1009:
 mov %eax, %esp
 add $20 , %esp

 movl (%esp), %esp

 and $255 , %esp

 shl $24 , %esp

 mov %eax, %edi
 add $20 , %edi

 movl (%edi), %edi

 and $65280 , %edi

 shl $8 , %edi

 or %edi, %esp

 mov %eax, %edi
 add $20 , %edi

 movl (%edi), %edi

 and $16711680 , %edi

 shr $8 , %edi

 or %edi, %esp

 mov %eax, %edi
 add $20 , %edi

 movl (%edi), %edi

 and $4278190080 , %edi

 shr $24 , %edi

 or %edi, %esp
 add %esp, %eax

 mov %eax, %esp

 jmp L1011

L1010:
 mov $0 , %esp

L1011:
 mov %esp, %edi

 cmp $0 , %edi
 jz L1013
L1012:
 add $4 , %esp

 movl (%esp), %esp

 jmp L1014

L1013:
 mov $0 , %esp

L1014:
 test %esp, %esp
 jz L1017
L1015:
 mov %ebp, %eax
 jmp *%esp

 jmp L1017
L1016:
 hlt

L1017:
 jmp L1016

walkcbfs_asm:
 cld

 mov 0xfffffffc, %eax
 mov ((0 + 4) + 4)(%eax), %ecx
 bswap %ecx
 mov $0, %ebx
 sub %ecx, %ebx
 mov (((((0 + 4) + 4) + 4) + 4) + 4)(%eax), %ecx
 bswap %ecx
 add %ecx, %ebx

 mov $0, %eax
1:
 cmpb $0, (%eax,%esi)
 jz 2f
 add $1, %eax
 jmp 1b
2:
 add $1, %eax
walker:
 mov 0(%ebx), %edi
 cmp %edi, filemagic
 jne searchfile
 mov 4(%ebx), %edi
 cmp %edi, filemagic+4
 jne searchfile

 mov %ebx, %edi
 add $(((((0 + 8) + 4) + 4) + 4) + 4), %edi
 mov %eax, %ecx
 repe cmpsb

 jnz tryharder

 mov %ebx, %eax
 jmp *%esp

tryharder:
 sub %ebx, %edi
 sub $(((((0 + 8) + 4) + 4) + 4) + 4), %edi
 sub %edi, %esi

 mov ((((0 + 8) + 4) + 4) + 4)(%ebx), %ecx
 bswap %ecx
 add %ebx, %ecx
 mov (0 + 8)(%ebx), %edi
 bswap %edi
 add %edi, %ecx

 add $(64 - 1), %ecx
 and $~(64 - 1), %ecx

 cmp %ebx, %ecx
 jbe out

 mov %ecx, %ebx

check_for_exit:

 mov 0xfffffffc, %ecx
 mov (((0 + 4) + 4) + 4)(%ecx), %ecx
 bswap %ecx
 not %ecx
 add $1, %ecx

 cmp %ecx, %ebx

 jbe walker

out:
 mov $0, %eax
 jmp *%esp


searchfile:

 add $64, %ebx
 jmp check_for_exit

filemagic:
 .ascii "LARCHIVE"
