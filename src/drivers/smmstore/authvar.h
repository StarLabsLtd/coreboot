/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */

#ifndef DRIVERS_SMMSTORE_AUTHVAR_H
#define DRIVERS_SMMSTORE_AUTHVAR_H

#include <commonlib/authvar_service.h>
#include <stddef.h>
#include <stdint.h>
#include <commonlib/bsd/cb_err.h>

struct lb_header;
void lb_authvar_service(struct lb_header *header);
void smm_authvar_endpoint(struct lb_authvar_service_endpoint *endpoint,
	uintptr_t communication, size_t size);
uint32_t smm_authvar_exec(void);
enum cb_err smm_authvar_set_uint_option(const char *name, uint32_t value);
uint64_t smm_authvar_store_uint_option(const char *name, uint32_t value);

#endif
