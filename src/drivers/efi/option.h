/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef DRIVERS_EFI_OPTION_H
#define DRIVERS_EFI_OPTION_H

#include <types.h>

enum cb_err efi_option_get_uint(const char *name, uint32_t *value);

/* Bound the SMM stack used to read a batch of integer options. */
#define EFI_UINT_OPTION_BATCH_MAX 4

struct efi_uint_option {
	const char *name;
	uint32_t value;
	enum cb_err result;
};

enum cb_err efi_option_get_uints(struct efi_uint_option *options, size_t count);
enum cb_err efi_option_set_uint(const char *name, uint32_t value);

#endif /* DRIVERS_EFI_OPTION_H */
