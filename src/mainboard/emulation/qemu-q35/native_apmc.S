/* SPDX-License-Identifier: GPL-2.0-only */

/* SysV x86-64: ignored context in RDI, port in RSI, command in RDX. */
.section .text.q35_native_apmc, "ax", @progbits
.code64
.global q35_native_apmc
.type q35_native_apmc, @function
q35_native_apmc:
	movzbl %dl, %eax
	movzwl %si, %edx
	xorl %ecx, %ecx
	outb %al, %dx
	xorl %eax, %eax
	ret
.size q35_native_apmc, .-q35_native_apmc

/* i386 private bootstrap: preserve the real ECX:EAX logical wire value. */
.section .text.q35_native_apmc_bootstrap, "ax", @progbits
.code32
.global q35_native_apmc_bootstrap
.type q35_native_apmc_bootstrap, @function
q35_native_apmc_bootstrap:
	outb %al, %dx
	ret
.size q35_native_apmc_bootstrap, .-q35_native_apmc_bootstrap
