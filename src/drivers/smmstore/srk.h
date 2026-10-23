/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */

#ifndef DRIVERS_SMMSTORE_SRK_H
#define DRIVERS_SMMSTORE_SRK_H

#include <stdbool.h>
#include <stdint.h>

/* Private SMM copy from the protected flash record, never from the mailbox. */
bool smmstore_copy_srk_pin(uint8_t name[34]);

#endif
