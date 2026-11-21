/* SPDX-License-Identifier: GPL-2.0-only */

/* System V arguments: entry, arg1, arg2, arg3, arg4. */
#define FP_STATE	32
#define SAVED_CR0	544
#define SAVED_CR4	552
#define SAVED_EFER	560
#define RESULT		568
#define ENTRY		576
#define ARG1		584
#define ARG2		592
#define ARG3		600
#define ARG4		608
#define MXCSR		616
#define FRAME_SIZE	640

.section .text.efi_variable_service_call, "ax", @progbits
.code64
.globl efi_variable_service_call
.type efi_variable_service_call, @function
efi_variable_service_call:
	pushq	%rbp
	movq	%rsp, %rbp
	/* Keep the Microsoft home space separate from the aligned FXSAVE image. */
	subq	$FRAME_SIZE, %rsp
	movq	%rdi, ENTRY(%rsp)
	movq	%rsi, ARG1(%rsp)
	movq	%rdx, ARG2(%rsp)
	movq	%rcx, ARG3(%rsp)
	movq	%r8, ARG4(%rsp)
	movq	%cr0, %rax
	movq	%rax, SAVED_CR0(%rsp)
	andq	$-13, %rax /* Clear EM and TS before using x87/SSE. */
	movq	%rax, %cr0
	movq	%cr4, %rax
	movq	%rax, SAVED_CR4(%rsp)
	orq	$0x600, %rax /* OSFXSR and OSXMMEXCPT. */
	movq	%rax, %cr4

	/* AMD FFXSR skips the XMM state at CPL0, so it cannot remain enabled. */
	movl	$0xc0000080, %ecx
	rdmsr
	movl	%eax, SAVED_EFER(%rsp)
	movl	%edx, SAVED_EFER+4(%rsp)
	testl	$0x4000, %eax
	jz	.Lsave_state
	andl	$~0x4000, %eax
	wrmsr
.Lsave_state:
	fxsave64 FP_STATE(%rsp)
	/* The interrupted task may have a full stack or unmasked exceptions. */
	fninit
	movl	$0x1f80, MXCSR(%rsp)
	ldmxcsr	MXCSR(%rsp)
	movq	ARG1(%rsp), %rcx
	movq	ARG2(%rsp), %rdx
	movq	ARG3(%rsp), %r8
	movq	ARG4(%rsp), %r9
	movq	ENTRY(%rsp), %rax
	call	*%rax
	movq	%rax, RESULT(%rsp)
	fxrstor64 FP_STATE(%rsp)

	testl	$0x4000, SAVED_EFER(%rsp)
	jz	.Lrestore_control
	movl	$0xc0000080, %ecx
	movl	SAVED_EFER(%rsp), %eax
	movl	SAVED_EFER+4(%rsp), %edx
	wrmsr
.Lrestore_control:
	movq	SAVED_CR4(%rsp), %rax
	movq	%rax, %cr4
	movq	SAVED_CR0(%rsp), %rax
	movq	%rax, %cr0
	movq	RESULT(%rsp), %rax
	leave
	ret
.size efi_variable_service_call, . - efi_variable_service_call

.section .note.GNU-stack, "", @progbits
