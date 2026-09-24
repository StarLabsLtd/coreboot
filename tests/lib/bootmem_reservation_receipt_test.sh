#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

for optimization in 0 2; do
	cc -std=gnu11 -O$optimization -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-DBOOTMEM_RECEIPT_TEST \
		'-DCONFIG(x)=CONFIG_##x' \
		-DCONFIG_BOOTMEM_ALIGNED_RESERVATIONS=1 \
		-DCONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT=1 \
		-DCONFIG_BOOTMEM_DRAM_PROVENANCE=0 \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		"$root/tests/lib/bootmem_reservation_receipt_test.c" \
		"$root/src/lib/bootmem_reservation_receipt.c" \
		-o "$tmp/test-$optimization"
	for case in kat success replay stale s3 alias tag mac malformed \
		abort-signer abort-verifier close-ready-verifier ordering \
		double-close-before-fill double-close-during-fill claim-close; do
		ASAN_OPTIONS=detect_leaks=1 "$tmp/test-$optimization" "$case"
	done
done

cc -m32 -std=gnu11 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
	-Wno-unused-parameter -Wno-sign-compare '-DCONFIG(x)=CONFIG_##x' \
	-DCONFIG_DEFAULT_CONSOLE_LOGLEVEL=0 -DCONFIG_BOOTMEM_ALIGNED_RESERVATIONS=1 \
	-DCONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT=1 \
	-DCONFIG_BOOTMEM_DRAM_PROVENANCE=0 -DCONFIG_CAPSULE_BROKER_FIXED_BUFFERS=0 \
	-DCONFIG_CONSOLE_CBMEM=0 -DCONFIG_FATAL_ASSERTS=0 -DENV_HAS_CBMEM=1 \
	-DENV_X86=1 -DENV_ROMSTAGE_OR_BEFORE=0 -D__COREBOOT__ -D__RAMSTAGE__ \
	-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" -c "$root/src/lib/bootmem.c" \
	-o "$tmp/bootmem-enabled.o"
file "$tmp/bootmem-enabled.o" | grep -q 'ELF 32-bit LSB relocatable'

cc -m32 -std=gnu11 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
	-Wno-unused-parameter -Wno-sign-compare '-DCONFIG(x)=CONFIG_##x' \
	-DCONFIG_DEFAULT_CONSOLE_LOGLEVEL=0 -DCONFIG_BOOTMEM_ALIGNED_RESERVATIONS=1 \
	-DCONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT=0 \
	-DCONFIG_BOOTMEM_DRAM_PROVENANCE=0 -DCONFIG_CAPSULE_BROKER_FIXED_BUFFERS=0 \
	-DCONFIG_CONSOLE_CBMEM=0 -DCONFIG_FATAL_ASSERTS=0 -DENV_HAS_CBMEM=1 \
	-DENV_X86=1 -DENV_ROMSTAGE_OR_BEFORE=0 -D__COREBOOT__ -D__RAMSTAGE__ \
	-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" -c "$root/src/lib/bootmem.c" \
	-o "$tmp/bootmem-disabled.o"
! nm -u "$tmp/bootmem-disabled.o" | grep -q bootmem_reservation_receipt

build_mutant()
{
	name=$1
	case=$2
	shift 2
	cp "$root/src/lib/bootmem_reservation_receipt.c" "$tmp/$name.c"
	before=$(sha256sum "$tmp/$name.c" | cut -d' ' -f1)
	for expression in "$@"; do
		sed -i "$expression" "$tmp/$name.c"
	done
	after=$(sha256sum "$tmp/$name.c" | cut -d' ' -f1)
	[ "$before" != "$after" ]
	cc -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-function \
		-DBOOTMEM_RECEIPT_TEST \
		'-DCONFIG(x)=CONFIG_##x' \
		-DCONFIG_BOOTMEM_ALIGNED_RESERVATIONS=1 \
		-DCONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT=1 \
		-DCONFIG_BOOTMEM_DRAM_PROVENANCE=0 -I"$root/src/include" \
		-I"$root/src/lib" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		"$root/tests/lib/bootmem_reservation_receipt_test.c" "$tmp/$name.c" \
		-o "$tmp/$name"
	! "$tmp/$name" "$case" >/dev/null 2>&1
}

build_mutant mac-binding mac 's/equal(mac, c.mac, 32)/true/'
build_mutant exact-tag tag 's/c.tag == BM_MEM_TABLE/true/'
build_mutant terminal-malformed malformed \
	's/if (rv)/if (false \&\& rv)/'
build_mutant publication-order ordering \
	's/__atomic_compare_exchange_n(\&v->state, \&empty, AUTHORITY_READY/__atomic_compare_exchange_n(\&s->state, \&empty, AUTHORITY_READY/'
build_mutant publisher-abort-scrub abort-signer \
	'/static void publisher_abort/,/^}/s/scrub_authority_body(a);//'
build_mutant repeated-close-ownership double-close-before-fill \
	's/if (state == AUTHORITY_ABORT_REQUESTED)/if (false)/'
build_mutant claimed-close-ownership claim-close \
	's/if (state == AUTHORITY_TERMINAL)/if (false)/'

cc -std=gnu11 -O2 -Wall -Wextra -Werror \
	'-DCONFIG(x)=CONFIG_##x' -DCONFIG_BOOTMEM_ALIGNED_RESERVATIONS=1 \
	-DCONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT=1 \
	-DCONFIG_BOOTMEM_DRAM_PROVENANCE=0 -I"$root/src/include" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-c "$root/src/lib/bootmem_reservation_receipt.c" -o "$tmp/receipt.o"
nm -g --defined-only "$tmp/receipt.o" | grep -q bootmem_reservation_receipt_verify_consume
objdump -d "$tmp/receipt.o" | grep -q 'bootmem_reservation_receipt_close'

for stage in RAMSTAGE SMM; do
	cc -m32 -std=gnu11 -Os -ffreestanding -fno-builtin -Wall -Wextra -Werror \
		'-DCONFIG(x)=CONFIG_##x' -DCONFIG_BOOTMEM_ALIGNED_RESERVATIONS=1 \
		-DCONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT=1 \
		-DCONFIG_BOOTMEM_DRAM_PROVENANCE=0 -D__COREBOOT__ -D__${stage}__ \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-c "$root/src/lib/bootmem_reservation_receipt.c" \
		-o "$tmp/receipt-$stage.o"
	file "$tmp/receipt-$stage.o" | grep -q 'ELF 32-bit LSB relocatable'
done
objdump -dr "$tmp/receipt-SMM.o" | \
	sed -n '/<bootmem_reservation_receipt_close>/,+40p' | \
	grep -q 'scrub_authority_body'
objdump -d "$tmp/receipt-SMM.o" | \
	sed -n '/<bootmem_reservation_receipt_scrub>/,+24p' | \
	grep -Eq 'mov[bwl].*\$0x0.*\('

grep -qx 'ramstage-$(CONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT) += bootmem_reservation_receipt.c' \
	"$root/src/lib/Makefile.mk"
grep -qx 'smm-$(CONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT) += bootmem_reservation_receipt.c' \
	"$root/src/lib/Makefile.mk"
! grep -Eq '^(ramstage|smm)-y .*bootmem_reservation_receipt' \
	"$root/src/lib/Makefile.mk"

! grep -Eq 'APM_CNT|SMMSTORE|cbmem|lb_add|BOOT_STATE_INIT_ENTRY' \
	"$root/src/lib/bootmem_reservation_receipt.c"
