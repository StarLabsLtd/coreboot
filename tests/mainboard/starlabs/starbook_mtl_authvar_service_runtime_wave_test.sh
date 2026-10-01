#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
export PROVIDER_STACK_RUNTIME_WAVE=1
exec sh "$root/tests/lib/payload_mm_authvar_service_provider_stack_test.sh"
