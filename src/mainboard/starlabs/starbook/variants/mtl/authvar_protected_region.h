/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STARBOOK_MTL_AUTHVAR_PROTECTED_REGION_H
#define STARBOOK_MTL_AUTHVAR_PROTECTED_REGION_H

#include <commonlib/region.h>

/* Read-only live TSEG and locked SMRR proof for the executing CPU, in SMM.
 * Output is unchanged on failure. This does not attest other CPUs, SPI writes,
 * the loader identity, an invocation claim or the placement of any object.
 */
enum cb_err starbook_mtl_authvar_protected_region_read(struct region *region);

#endif
