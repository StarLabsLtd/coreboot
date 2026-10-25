/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef STARLABS_SMI_H
#define STARLABS_SMI_H

#include <types.h>

int variant_smi_apmc(u8 command);
int starlabs_efi_option_smi(u8 command);

#endif
