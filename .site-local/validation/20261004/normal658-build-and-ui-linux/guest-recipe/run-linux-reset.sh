#!/usr/bin/env bash
# One pristine normal VM: pinned631 Linux Runtime request, guestRESET, UI/delete.
source /home/sean/fresh-normal-focus-guest-after657.M2fS2l/guest-common.sh
prepare linux-reset
# The first guest must pass, and the same recipes/build/source must stay frozen.
test "$(cat "$driver/ui-cancel/outer.status")" = 0
sha256sum -c "$driver/ui-cancel/recipe-before.sha256" > "$receipt/ui-recipe-check.log"
sha256sum -c "$driver/ui-cancel/source-before.sha256" > "$receipt/ui-source-check.log"
sha256sum -c "$driver/ui-cancel/inputs-before.sha256" > "$receipt/ui-inputs-check.log"
# No pflash from the UI guest is passed here: both start from the same pristine ROM.
# Outer420 covers admission/codec/cleanup; existing whole guest deadline stays180.
command=(python3 -B "$firmware/util/qemu/bin/run-deadline.py" --kill-after 3 420 --
	python3 -B "$firmware/util/qemu/bin/run-linux-boot-to-fw-ui.py"
	"$build" "$firmware" "$producer" "$requester" "$bundle" "$receipt/lifecycle" --fresh-build)
run_command
