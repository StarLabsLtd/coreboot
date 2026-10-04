#!/usr/bin/env bash
# One pristine normal UI checkbox edit/Esc discard guest, then actual media oracle.
source /home/sean/fresh-normal-focus-guest-after657.M2fS2l/guest-common.sh
prepare ui-cancel
# Positional historical EFI requester is deliberately unused by this phase.
# Outer420 covers admission/codec/cleanup; existing run_stage/run-one retain180.
command=(python3 -B "$firmware/util/qemu/bin/run-deadline.py" --kill-after 3 420 --
	python3 -B "$firmware/util/qemu/bin/run-normal-boot-to-fw-ui.py"
	"$build" "$firmware" "$producer" "$unused_request_fixture" "$bundle" "$receipt/lifecycle"
	--fresh-build --ui-control-cancel-only)
run_command
