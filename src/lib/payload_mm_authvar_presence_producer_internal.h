/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_INTERNAL_H
#define PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_INTERNAL_H

#include <boot/payload_mm_authvar_presence_producer.h>

/* Valid only during the irrevocable presence-publication interval. */
bool payload_mm_authvar_presence_producer_publication_receipt_validate(
	const struct payload_mm_authvar_presence_receipt *receipt);

#endif
