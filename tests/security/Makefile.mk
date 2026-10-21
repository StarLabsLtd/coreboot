# SPDX-License-Identifier: GPL-2.0-only

tests-y += tpm2-log-test
tpm2-log-test-srcs += tests/security/tpm2-log-test.c
tpm2-log-test-srcs += tests/stubs/console.c
tpm2-log-test-cflags += -I3rdparty/vboot/firmware/include
tpm2-log-test-config += CONFIG_TPM_LOG_TPM2=1 CONFIG_TPM_HASH_SHA256=1
