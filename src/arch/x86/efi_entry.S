/* SPDX-License-Identifier: GPL-2.0-only */

/* Call an x64 EDK II EFIAPI entry from coreboot's SysV x64 ABI. */

.code64
.text
.globl x86_64_efi_call
.type x86_64_efi_call, @function
x86_64_efi_call:
	/* Keep the caller's stack and the callee-saved register intact. */
	pushq	%r12
	movq	%rsp, %r12

	/* EFIAPI uses RCX and requires 32 bytes of shadow space. */
	andq	$-16, %rsp
	subq	$0x20, %rsp
	movq	%rsi, %rcx
	callq	*%rdi

	movq	%r12, %rsp
	popq	%r12
	retq
.size x86_64_efi_call, .-x86_64_efi_call
