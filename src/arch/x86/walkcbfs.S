/* SPDX-License-Identifier: GPL-2.0-only */

#include <fmap_config.h>

#if CONFIG(STARLABS_AB_COREBOOT)
#define STARLABS_AB_CMOS_INDEX_PORT CONFIG_PC_CMOS_BASE_PORT_BANK0
#define STARLABS_AB_CMOS_DATA_PORT  (CONFIG_PC_CMOS_BASE_PORT_BANK0 + 1)
#define STARLABS_AB_CMOS_ACTIVE     49
#define STARLABS_AB_CMOS_PENDING    50
#define STARLABS_AB_CMOS_TRIES      51
#define STARLABS_AB_CMOS_CURRENT    52
#define STARLABS_AB_PENDING_VALID   0x80
#define STARLABS_AB_CURRENT_VALID   0x80
#define STARLABS_AB_SLOT_MASK       0x01
#endif

#if CONFIG(CBFS_VERIFICATION)
#error "walkcbfs_asm is not safe to use with CBFS verification!"
#endif

/* we use this instead of CBFS_HEADER_ALIGN because the latter is retired. */
#define CBFS_ALIGNMENT 64

#define CBFS_FILE_MAGIC 0
#define CBFS_FILE_LEN (CBFS_FILE_MAGIC + 8)
#define CBFS_FILE_TYPE (CBFS_FILE_LEN + 4)
#define CBFS_FILE_CHECKSUM (CBFS_FILE_TYPE + 4)
#define CBFS_FILE_OFFSET (CBFS_FILE_CHECKSUM + 4)

#define CBFS_FILE_STRUCTSIZE (CBFS_FILE_OFFSET + 4)

#define COREBOOT_CBFS_START (0xffffffff - CONFIG_ROM_SIZE + 1 + FMAP_SECTION_COREBOOT_START)
#define COREBOOT_END        (COREBOOT_CBFS_START - 1 + FMAP_SECTION_COREBOOT_SIZE)

#if CONFIG(STARLABS_AB_COREBOOT)
#define COREBOOT_B_CBFS_START (0xffffffff - CONFIG_ROM_SIZE + 1 + FMAP_SECTION_COREBOOT_B_START)
#define COREBOOT_B_END        (COREBOOT_B_CBFS_START - 1 + FMAP_SECTION_COREBOOT_B_SIZE)
#endif

.code32
.section .init
.global walkcbfs_asm

/*
 * input %esi: filename
 * input %esp: return address (not pointer to return address!)
 * output %eax: pointer to CBFS header
 * clobbers %ebx, %ecx, %edi
 */
walkcbfs_asm:
	cld

	movl $COREBOOT_CBFS_START, %ebx
	movl $COREBOOT_END, %edx

#if CONFIG(STARLABS_AB_COREBOOT)
	movw $STARLABS_AB_CMOS_INDEX_PORT, %dx
	movb $STARLABS_AB_CMOS_CURRENT, %al
	outb %al, %dx
	movw $STARLABS_AB_CMOS_DATA_PORT, %dx
	inb %dx, %al
	testb $STARLABS_AB_CURRENT_VALID, %al
	jz 1f
	testb $STARLABS_AB_SLOT_MASK, %al
	jz 3f
	movl $COREBOOT_B_CBFS_START, %ebx
	movl $COREBOOT_B_END, %edx
	jmp 3f

1:
	movw $STARLABS_AB_CMOS_INDEX_PORT, %dx
	movb $STARLABS_AB_CMOS_PENDING, %al
	outb %al, %dx
	movw $STARLABS_AB_CMOS_DATA_PORT, %dx
	inb %dx, %al
	movb %al, %ah
	testb $STARLABS_AB_PENDING_VALID, %ah
	jz 2f

	movw $STARLABS_AB_CMOS_INDEX_PORT, %dx
	movb $STARLABS_AB_CMOS_TRIES, %al
	outb %al, %dx
	movw $STARLABS_AB_CMOS_DATA_PORT, %dx
	inb %dx, %al
	testb %al, %al
	jz 2f

	testb $STARLABS_AB_SLOT_MASK, %ah
	jz 3f
	movl $COREBOOT_B_CBFS_START, %ebx
	movl $COREBOOT_B_END, %edx
	jmp 3f

2:
	movw $STARLABS_AB_CMOS_INDEX_PORT, %dx
	movb $STARLABS_AB_CMOS_ACTIVE, %al
	outb %al, %dx
	movw $STARLABS_AB_CMOS_DATA_PORT, %dx
	inb %dx, %al
	testb $STARLABS_AB_SLOT_MASK, %al
	jz 3f
	movl $COREBOOT_B_CBFS_START, %ebx
	movl $COREBOOT_B_END, %edx
3:
#endif

	/* determine filename length */
	mov $0, %eax
1:
	cmpb $0, (%eax,%esi)
	jz 2f
	add $1, %eax
	jmp 1b
2:
	add $1, %eax
walker:
	mov 0(%ebx), %edi /* Check for LARCHIVE header */
	cmp %edi, filemagic
	jne searchfile
	mov 4(%ebx), %edi
	cmp %edi, filemagic+4
	jne searchfile

	/* LARCHIVE header found */
	mov %ebx, %edi
	add $CBFS_FILE_STRUCTSIZE, %edi /* edi = address of first byte after
					 * struct cbfs_file
					 */
	mov %eax, %ecx
	repe cmpsb
	/* zero flag set if strings are equal */
	jnz tryharder

	/* we found it! */
	mov %ebx, %eax
	jmp *%esp

tryharder:
	sub %ebx, %edi
	sub $CBFS_FILE_STRUCTSIZE, %edi /* edi = # of walked bytes */
	sub %edi, %esi /* esi = start of filename */

	/* ebx = ecx = (current+offset+len+ALIGN-1) & ~(ALIGN-1) */
	mov CBFS_FILE_OFFSET(%ebx), %ecx
	bswap %ecx
	add %ebx, %ecx
	mov CBFS_FILE_LEN(%ebx), %edi
	bswap %edi
	add %edi, %ecx
	/* round by 64 bytes */
	add $(CBFS_ALIGNMENT - 1), %ecx
	and $~(CBFS_ALIGNMENT - 1), %ecx

	/* if oldaddr >= addr, leave */
	cmp %ebx, %ecx
	jbe out

	mov %ecx, %ebx

check_for_exit:
	/* if addr <= selected region end, continue */
	cmp %edx, %ebx
	jbe walker

out:
	mov $0, %eax
	jmp *%esp


searchfile:
	/* if filemagic isn't found, move forward 64 bytes */
	add $CBFS_ALIGNMENT, %ebx
	jmp check_for_exit

filemagic:
	.ascii "LARCHIVE"
