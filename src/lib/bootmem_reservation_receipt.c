/* SPDX-License-Identifier: GPL-2.0-only */
#include "bootmem_reservation_receipt_internal.h"
#include <commonlib/helpers.h>
#include <string.h>

enum authority_state { AUTHORITY_EMPTY, AUTHORITY_PUBLISHING,
	AUTHORITY_ABORT_REQUESTED, AUTHORITY_READY, AUTHORITY_TERMINAL };
struct sha256_context { uint32_t state[8]; uint64_t bytes; uint8_t block[64]; };
static const uint32_t constants[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

void bootmem_reservation_receipt_scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;
	while (size)
		bytes[--size] = 0;
}
static uint32_t rotr(uint32_t x, unsigned int n) { return x >> n | x << (32 - n); }
static void transform(struct sha256_context *c, const uint8_t block[64])
{
	uint32_t w[64], a, b, d, e, f, g, h, t1, t2, cc;
	for (size_t i = 0; i < 16; i++)
		w[i] = (uint32_t)block[4*i]<<24|(uint32_t)block[4*i+1]<<16|
			(uint32_t)block[4*i+2]<<8|block[4*i+3];
	for (size_t i = 16; i < 64; i++) {
		uint32_t x = w[i-15], y = w[i-2];
		w[i] = w[i-16]+(rotr(x, 7)^rotr(x, 18)^(x>>3))+w[i-7]+(rotr(y, 17)^rotr(y, 19)^(y>>10));
	}
	a = c->state[0]; b = c->state[1]; cc = c->state[2]; d = c->state[3]; e = c->state[4]; f = c->state[5]; g = c->state[6]; h = c->state[7];
	for (size_t i = 0; i < 64; i++) {
		t1 = h+(rotr(e, 6)^rotr(e, 11)^rotr(e, 25))+((e&f)^(~e&g))+constants[i]+w[i];
		t2 = (rotr(a, 2)^rotr(a, 13)^rotr(a, 22))+((a&b)^(a&cc)^(b&cc));
		h = g; g = f; f = e; e = d+t1; d = cc; cc = b; b = a; a = t1+t2;
	}
	c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d; c->state[4] += e; c->state[5] += f; c->state[6] += g; c->state[7] += h;
	bootmem_reservation_receipt_scrub(w, sizeof(w));
}
static void sha_init(struct sha256_context *c)
{
	*c = (struct sha256_context){.state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}};
}
static void sha_update(struct sha256_context *c, const void *data, size_t size)
{
	const uint8_t *p = data; while (size) {size_t used = c->bytes&63, take = MIN(size, 64-used); memcpy(c->block+used, p, take); c->bytes += take; p += take; size -= take; if (!(c->bytes&63))transform(c, c->block); }
}
static void sha_finish(struct sha256_context *c, uint8_t out[32])
{
	uint64_t bits = c->bytes*8; uint8_t pad[72] = {0x80}; size_t n = (c->bytes&63) < 56?56-(c->bytes&63):120-(c->bytes&63);
	for (size_t i = 0; i < 8; i++)
		pad[n+i] = bits>>(56-8*i);
	sha_update(c, pad, n+8);
	for (size_t i = 0; i < 8; i++) {
		out[4*i] = c->state[i]>>24; out[4*i+1] = c->state[i]>>16;
		out[4*i+2] = c->state[i]>>8; out[4*i+3] = c->state[i];
	}
	bootmem_reservation_receipt_scrub(pad, sizeof(pad)); bootmem_reservation_receipt_scrub(c, sizeof(*c));
}
enum cb_err bootmem_reservation_receipt_mac(const uint8_t key[32], const void *message, size_t size, uint8_t mac[32])
{
	struct sha256_context c; uint8_t inner[32], pad[64]; if (!key || (!message && size) || !mac)return CB_ERR_ARG;
	for (size_t i = 0; i < 64; i++)
		pad[i] = (i < 32?key[i]:0)^0x36;
	sha_init(&c); sha_update(&c, pad, 64); sha_update(&c, message, size); sha_finish(&c, inner);
	for (size_t i = 0; i < 64; i++)
		pad[i] = (i < 32?key[i]:0)^0x5c;
	sha_init(&c); sha_update(&c, pad, 64); sha_update(&c, inner, 32); sha_finish(&c, mac);
	bootmem_reservation_receipt_scrub(inner, 32); bootmem_reservation_receipt_scrub(pad, 64); return CB_SUCCESS;
}
static bool valid(const void *p, size_t size, size_t align) {uintptr_t b = (uintptr_t)p; return p && !(b%align) && b <= UINTPTR_MAX-(size-1); }
static bool overlaps(const void *a, size_t as, const void *b, size_t bs)
{
	uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
	if (!valid(a, as, 1) || !valid(b, bs, 1))
		return true;
	return x <= y ? y-x < as : x-y < bs;
}
static bool nonzero(const uint8_t *p, size_t n)
{
	uint8_t v = 0;
	for (size_t i = 0; i < n; i++)
		v |= p[i];
	return v;
}

static void scrub_authority_body(
	struct bootmem_reservation_receipt_authority *authority)
{
	bootmem_reservation_receipt_scrub(authority->secret,
		sizeof(authority->secret));
	authority->generation = 0;
	bootmem_reservation_receipt_scrub(&authority->handle,
		sizeof(authority->handle));
	authority->sequence = 0;
	authority->boot_kind = 0;
	bootmem_reservation_receipt_scrub(authority->reserved,
		sizeof(authority->reserved));
}

#if defined(BOOTMEM_RECEIPT_TEST)
void __weak bootmem_receipt_test_after_claim_cas(
	struct bootmem_reservation_receipt_authority *authority)
{
	(void)authority;
}
#endif

bool bootmem_reservation_receipt_authority_claim(struct bootmem_reservation_receipt_authority *a, struct bootmem_reservation_receipt_authority *s)
{
	uint8_t expected = AUTHORITY_READY; if (!valid(a, sizeof(*a), _Alignof(*a)) || !valid(s, sizeof(*s), _Alignof(*s)) || overlaps(a, sizeof(*a), s, sizeof(*s)))return false;
	if (!__atomic_compare_exchange_n(&a->state, &expected, AUTHORITY_TERMINAL, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return false;
#if defined(BOOTMEM_RECEIPT_TEST)
	bootmem_receipt_test_after_claim_cas(a);
#endif
	memcpy(s->secret, a->secret, sizeof(s->secret));
	s->generation = a->generation;
	s->handle = a->handle;
	s->sequence = a->sequence;
	s->boot_kind = a->boot_kind;
	s->state = AUTHORITY_READY;
	scrub_authority_body(a);
	__atomic_store_n(&a->state, AUTHORITY_TERMINAL, __ATOMIC_RELEASE);
	return s->generation && s->sequence == 1 &&
		s->boot_kind == BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT &&
		s->handle.opaque[0] && s->handle.opaque[1] && nonzero(s->secret, 32);
}
void bootmem_reservation_receipt_close(struct bootmem_reservation_receipt_authority *a)
{
	if (!valid(a, sizeof(*a), _Alignof(*a)))
		return;
	uint8_t state = __atomic_load_n(&a->state, __ATOMIC_ACQUIRE);
	while (true) {
		uint8_t next;

		if (state == AUTHORITY_TERMINAL)
			return;
		if (state == AUTHORITY_ABORT_REQUESTED)
			return;
		next = state == AUTHORITY_PUBLISHING ?
			AUTHORITY_ABORT_REQUESTED : AUTHORITY_TERMINAL;
		if (__atomic_compare_exchange_n(&a->state, &state, next, false,
			__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			if (next == AUTHORITY_ABORT_REQUESTED)
				return;
			scrub_authority_body(a);
			__atomic_store_n(&a->state, AUTHORITY_TERMINAL, __ATOMIC_RELEASE);
			return;
		}
	}
}

static void publisher_abort(struct bootmem_reservation_receipt_authority *a)
{
	const uint8_t state = __atomic_load_n(&a->state, __ATOMIC_ACQUIRE);

	if (state != AUTHORITY_PUBLISHING && state != AUTHORITY_ABORT_REQUESTED)
		return;
	scrub_authority_body(a);
	__atomic_store_n(&a->state, AUTHORITY_TERMINAL, __ATOMIC_RELEASE);
}

#if defined(BOOTMEM_RECEIPT_TEST)
void __weak bootmem_receipt_test_after_claim(
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier)
{
	(void)signer;
	(void)verifier;
}

void __weak bootmem_receipt_test_after_signer_fill(
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier)
{
	(void)signer;
	(void)verifier;
}

void __weak bootmem_receipt_test_after_fill(
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier)
{
	(void)signer;
	(void)verifier;
}

void __weak bootmem_receipt_test_after_verifier_publish(
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier)
{
	(void)signer;
	(void)verifier;
}
#endif

enum cb_err bootmem_reservation_receipt_provision(struct bootmem_reservation_receipt_authority *s, struct bootmem_reservation_receipt_authority *v, uint8_t secret[32], uint32_t kind, uint64_t generation, const struct bootmem_aligned_reservation_handle *h)
{
	bool s_owned = false, v_owned = false;
	bool sv = valid(s, sizeof(*s), _Alignof(*s));
	bool vv = valid(v, sizeof(*v), _Alignof(*v));
	struct bootmem_reservation_receipt_authority x = {0};
	uint8_t empty = AUTHORITY_EMPTY;
	if (!sv || !vv || overlaps(s, sizeof(*s), v, sizeof(*v)) ||
	    !valid(secret, 32, 1) || !valid(h, sizeof(*h), _Alignof(*h)) ||
	    overlaps(secret, 32, h, sizeof(*h)) ||
	    overlaps(secret, 32, s, sizeof(*s)) ||
	    overlaps(secret, 32, v, sizeof(*v)) ||
	    overlaps(h, sizeof(*h), s, sizeof(*s)) ||
	    overlaps(h, sizeof(*h), v, sizeof(*v)) ||
	    kind != BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT || !generation ||
	    !nonzero(secret, 32) || !h->opaque[0] || !h->opaque[1])
		goto fail;
	if (!__atomic_compare_exchange_n(&s->state, &empty, AUTHORITY_PUBLISHING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		goto fail;
	s_owned = true;
	empty = AUTHORITY_EMPTY;
	if (!__atomic_compare_exchange_n(&v->state, &empty, AUTHORITY_PUBLISHING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		goto fail;
	v_owned = true;
#if defined(BOOTMEM_RECEIPT_TEST)
	bootmem_receipt_test_after_claim(s, v);
#endif
	memcpy(x.secret, secret, 32); x.generation = generation; x.handle = *h;
	x.sequence = 1; x.boot_kind = kind;
	memcpy(s->secret, x.secret, sizeof(x.secret)); s->generation = x.generation;
	s->handle = x.handle; s->sequence = x.sequence; s->boot_kind = x.boot_kind;
#if defined(BOOTMEM_RECEIPT_TEST)
	bootmem_receipt_test_after_signer_fill(s, v);
#endif
	memcpy(v->secret, x.secret, sizeof(x.secret)); v->generation = x.generation;
	v->handle = x.handle; v->sequence = x.sequence; v->boot_kind = x.boot_kind;
#if defined(BOOTMEM_RECEIPT_TEST)
	bootmem_receipt_test_after_fill(s, v);
#endif
	empty = AUTHORITY_PUBLISHING;
	if (!__atomic_compare_exchange_n(&v->state, &empty, AUTHORITY_READY, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		goto fail;
	v_owned = false;
#if defined(BOOTMEM_RECEIPT_TEST)
	bootmem_receipt_test_after_verifier_publish(s, v);
#endif
	empty = AUTHORITY_PUBLISHING;
	if (!__atomic_compare_exchange_n(&s->state, &empty, AUTHORITY_READY, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		goto fail;
	s_owned = false;
	if (__atomic_load_n(&v->state, __ATOMIC_ACQUIRE) != AUTHORITY_READY ||
	    __atomic_load_n(&s->state, __ATOMIC_ACQUIRE) != AUTHORITY_READY)
		goto fail;
	bootmem_reservation_receipt_scrub(secret, 32);
	bootmem_reservation_receipt_scrub(&x, sizeof(x));
	return CB_SUCCESS;
fail:
	if (valid(secret, 32, 1))
		bootmem_reservation_receipt_scrub(secret, 32);
	if (sv) {
		if (s_owned)
			publisher_abort(s);
		else
			bootmem_reservation_receipt_close(s);
	}
	if (vv && (!sv || v != s)) {
		if (v_owned)
			publisher_abort(v);
		else
			bootmem_reservation_receipt_close(v);
	}
	bootmem_reservation_receipt_scrub(&x, sizeof(x));
	return CB_ERR;
}
static bool equal(const uint8_t *a, const uint8_t *b, size_t n)
{
	uint8_t d = 0;
	for (size_t i = 0; i < n; i++)
		d |= a[i]^b[i];
	return !d;
}
enum cb_err bootmem_reservation_receipt_verify_consume(struct bootmem_reservation_receipt_authority *v, struct bootmem_reservation_receipt *r)
{
	bool vv = valid(v, sizeof(*v), _Alignof(*v)), rv = valid(r, sizeof(*r), _Alignof(*r)); struct bootmem_reservation_receipt_authority a = {0}; struct bootmem_reservation_receipt c = {0}; uint8_t mac[32] = {0}; enum cb_err status = CB_ERR;
	if (!vv || !rv || overlaps(v, sizeof(*v), r, sizeof(*r)))
		goto out;
	memcpy(&c, r, sizeof(c));
	if (!bootmem_reservation_receipt_authority_claim(v, &a))
		goto out;
	if (c.revision == BOOTMEM_RESERVATION_RECEIPT_REVISION &&
	    c.size == sizeof(c) && c.boot_kind == a.boot_kind && !c.reserved &&
	    c.generation == a.generation && c.sequence == a.sequence &&
	    !memcmp(&c.handle, &a.handle, sizeof(c.handle)) && c.base && c.bytes &&
	    c.base <= UINT64_MAX-c.bytes && c.tag == BM_MEM_TABLE &&
	    c.use == BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE &&
	    bootmem_reservation_receipt_mac(a.secret, &c,
		offsetof(struct bootmem_reservation_receipt, mac), mac) == CB_SUCCESS &&
	    !memcmp(&c, r, sizeof(c)) && equal(mac, c.mac, 32))
		status = CB_SUCCESS;
out:
	if (rv)
		bootmem_reservation_receipt_scrub(r, sizeof(*r));
	if (vv)
		bootmem_reservation_receipt_close(v);
	bootmem_reservation_receipt_scrub(&a, sizeof(a));
	bootmem_reservation_receipt_scrub(&c, sizeof(c));
	bootmem_reservation_receipt_scrub(mac, 32);
	return status;
}
