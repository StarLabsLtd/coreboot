/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_MEDIA_H
#define BOOT_PAYLOAD_MM_AUTHVAR_MEDIA_H

#include <boot/payload_mm_authvar.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define PAYLOAD_MM_AUTHVAR_MEDIA_PORT_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_MEDIA_CONTEXT_CAPACITY 256U
/* One protected snapshot/readback set; erase blocks larger than this reject. */
#define PAYLOAD_MM_AUTHVAR_MEDIA_SCRATCH_CAPACITY 4096U

enum payload_mm_authvar_media_result {
	PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS = 0,
	PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED,
	PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED,
	PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR,
};

/*
 * Only a successful begin() acquires global backend/SPI exclusion, held until
 * end(). An error return must leave it unwound. The returned nonzero media
 * generation remains stable until end() and advances after every out-of-band
 * media change. This permits cache reuse across sessions with different owner
 * tokens. If backend begin succeeds but wrapper validation fails, the wrapper
 * ends it internally. After media_begin() succeeds, the single-flight caller
 * owns the token and must call media_end() exactly once on every exit path;
 * session APIs must not be shared or invoked concurrently. sync() means durable
 * completion.
 * Callbacks must not retain buffer pointers. All offsets are relative to the
 * sealed SMMSTORE. The copied context is immutable; mutable backend state
 * referenced by it must remain protected.
 */
typedef enum payload_mm_authvar_media_result
payload_mm_authvar_media_begin_fn(const void *context, uint64_t *generation);
typedef enum payload_mm_authvar_media_result
payload_mm_authvar_media_read_fn(const void *context, uint32_t offset,
	void *buffer, size_t size, size_t *completed);
typedef enum payload_mm_authvar_media_result
payload_mm_authvar_media_program_fn(const void *context, uint32_t offset,
	const void *buffer, size_t size);
typedef enum payload_mm_authvar_media_result
payload_mm_authvar_media_erase_fn(const void *context, uint32_t offset,
	size_t size);
typedef enum payload_mm_authvar_media_result
payload_mm_authvar_media_sync_fn(const void *context);
typedef enum payload_mm_authvar_media_result
payload_mm_authvar_media_end_fn(const void *context);

struct payload_mm_authvar_media_port {
	uint32_t revision;
	uint32_t size;
	payload_mm_authvar_media_begin_fn *begin;
	payload_mm_authvar_media_read_fn *read;
	payload_mm_authvar_media_program_fn *program;
	payload_mm_authvar_media_erase_fn *erase;
	payload_mm_authvar_media_sync_fn *sync;
	payload_mm_authvar_media_end_fn *end;
	const void *context;
	size_t context_size;
};

enum cb_err payload_mm_authvar_media_install(
	const struct payload_mm_authvar_media_port *trusted_port);
bool payload_mm_authvar_media_available(void);
enum payload_mm_authvar_media_result payload_mm_authvar_media_begin(
	uint64_t *generation, uint64_t *token);
enum payload_mm_authvar_media_result payload_mm_authvar_media_read(
	uint64_t generation, uint64_t token, uint32_t offset, void *buffer,
	size_t size);
enum payload_mm_authvar_media_result payload_mm_authvar_media_program(
	uint64_t generation, uint64_t token, uint32_t offset, const void *buffer,
	size_t size);
enum payload_mm_authvar_media_result payload_mm_authvar_media_erase(
	uint64_t generation, uint64_t token, uint32_t offset, size_t size);
enum payload_mm_authvar_media_result payload_mm_authvar_media_end(
	uint64_t generation, uint64_t token);
/*
 * Terminal executor failure path. Exact active generation/token ownership is
 * required, but every valid or invalid invocation permanently poisons the port,
 * invalidates its cache and returns DEVICE_ERROR. It never releases ownership.
 * An active owner must still call end() exactly once; only that sealed end
 * callback may run after this call.
 */
enum payload_mm_authvar_media_result payload_mm_authvar_media_fail_closed(
	uint64_t generation, uint64_t token);

void payload_mm_authvar_media_cache_bind(uint64_t generation, uint64_t token);
void payload_mm_authvar_media_cache_invalidate(void);
bool payload_mm_authvar_media_cache_valid(uint64_t generation, uint64_t token);
uint64_t payload_mm_authvar_media_result_status(
	enum payload_mm_authvar_media_result result);

#endif
