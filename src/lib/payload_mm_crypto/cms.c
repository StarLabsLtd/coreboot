/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_cms.h"

#include "crypto.h"

#include <commonlib/helpers.h>
#include <mbedtls/platform_util.h>

struct der_cursor {
	const uint8_t *data;
	size_t size;
};

struct der_object {
	struct payload_mm_crypto_span full;
	struct payload_mm_crypto_span value;
	uint8_t tag;
};

struct certificate_identity {
	struct payload_mm_crypto_span issuer;
	struct payload_mm_crypto_span serial;
};

static const uint8_t signed_data_oid[] = {
	0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x07, 0x02,
};
static const uint8_t data_oid[] = {
	0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x07, 0x01,
};
static const uint8_t sha256_oid[] = {
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
};
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
static const uint8_t sha384_oid[] = {
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02,
};
static const uint8_t sha512_oid[] = {
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03,
};
#endif
static const uint8_t rsa_oid[] = {
	0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01,
};
static const uint8_t content_type_oid[] = {
	0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x09, 0x03,
};
static const uint8_t message_digest_oid[] = {
	0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x09, 0x04,
};
static const uint8_t signing_time_oid[] = {
	0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x09, 0x05,
};
static const uint8_t smime_capabilities_oid[] = {
	0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x09, 0x0f,
};
static const uint8_t pkcs7_guid[] = {
	0x9d, 0xd2, 0xaf, 0x4a, 0xdf, 0x68, 0xee, 0x49,
	0x8a, 0xa9, 0x34, 0x7d, 0x37, 0x56, 0x65, 0xa7,
};

static bool bytes_equal(const void *left, const void *right, size_t size)
{
	const uint8_t *a = left;
	const uint8_t *b = right;
	uint8_t difference = 0U;
	size_t index;

	for (index = 0U; index < size; index++)
		difference |= a[index] ^ b[index];
	return difference == 0U;
}

static bool span_equal(const struct payload_mm_crypto_span *left,
	const struct payload_mm_crypto_span *right)
{
	return left->size == right->size &&
		bytes_equal(left->data, right->data, left->size);
}

static bool der_read(struct der_cursor *cursor, struct der_object *object)
{
	const uint8_t *start;
	size_t header = 2U;
	size_t length;
	size_t octets;
	size_t index;

	if (cursor == NULL || object == NULL || cursor->size < 2U)
		return false;
	start = cursor->data;
	if ((start[0] & 0x1fU) == 0x1fU)
		return false;
	length = start[1];
	if ((length & 0x80U) != 0U) {
		octets = length & 0x7fU;
		if (octets == 0U || octets > sizeof(size_t) ||
		    cursor->size < 2U + octets || start[2] == 0U)
			return false;
		length = 0U;
		for (index = 0U; index < octets; index++) {
			if (length > (SIZE_MAX - start[2U + index]) / 256U)
				return false;
			length = length * 256U + start[2U + index];
		}
		if (length < 128U)
			return false;
		header += octets;
	}
	if (length > cursor->size - header)
		return false;
	object->tag = start[0];
	object->full.data = start;
	object->full.size = header + length;
	object->value.data = start + header;
	object->value.size = length;
	cursor->data += header + length;
	cursor->size -= header + length;
	return true;
}

static bool der_expect(struct der_cursor *cursor, uint8_t tag,
	struct der_object *object)
{
	return der_read(cursor, object) && object->tag == tag;
}

static bool der_only(const struct der_object *object, uint8_t tag,
	struct der_cursor *contents)
{
	struct der_cursor cursor = { object->full.data, object->full.size };
	struct der_object parsed;

	if (!der_expect(&cursor, tag, &parsed) || cursor.size != 0U)
		return false;
	contents->data = parsed.value.data;
	contents->size = parsed.value.size;
	return true;
}

static bool oid_is(const struct der_object *object, const uint8_t *oid,
	size_t oid_size)
{
	return object->tag == 0x06U && object->value.size == oid_size &&
		bytes_equal(object->value.data, oid, oid_size);
}

static bool integer_is_one(const struct der_object *object)
{
	return object->tag == 0x02U && object->value.size == 1U &&
		object->value.data[0] == 1U;
}

static bool integer_canonical(const struct der_object *object)
{
	if (object->tag != 0x02U || object->value.size == 0U ||
	    (object->value.data[0] & 0x80U) != 0U)
		return false;
	return object->value.size == 1U || object->value.data[0] != 0U ||
		(object->value.data[1] & 0x80U) != 0U;
}

static bool der_integer_canonical(const struct der_object *object)
{
	if (object->value.size == 0U)
		return false;
	if (object->value.size == 1U)
		return true;
	return !(object->value.data[0] == 0U &&
		 (object->value.data[1] & 0x80U) == 0U) &&
		!(object->value.data[0] == 0xffU &&
		  (object->value.data[1] & 0x80U) != 0U);
}

static bool der_oid_canonical(const struct der_object *object)
{
	bool first_octet = true;
	size_t index;

	if (object->value.size == 0U)
		return false;
	for (index = 0U; index < object->value.size; index++) {
		if (first_octet && object->value.data[index] == 0x80U)
			return false;
		first_octet = (object->value.data[index] & 0x80U) == 0U;
	}
	return first_octet;
}

static unsigned int decimal_pair(const uint8_t *digits)
{
	return (unsigned int)(digits[0] - '0') * 10U + digits[1] - '0';
}

static bool decimal_digits(const uint8_t *digits, size_t count)
{
	size_t index;

	for (index = 0U; index < count; index++) {
		if (digits[index] < '0' || digits[index] > '9')
			return false;
	}
	return true;
}

static bool der_time_canonical(const struct der_object *object)
{
	static const uint8_t month_days[] = {
		31U, 28U, 31U, 30U, 31U, 30U,
		31U, 31U, 30U, 31U, 30U, 31U,
	};
	const uint8_t *digits = object->value.data;
	size_t digit_count;
	unsigned int year;
	unsigned int month;
	unsigned int day;
	unsigned int days;

	if (object->tag == 0x17U) {
		digit_count = 12U;
		if (object->value.size != digit_count + 1U)
			return false;
		year = decimal_pair(digits);
		year += year >= 50U ? 1900U : 2000U;
		digits += 2U;
	} else {
		digit_count = 14U;
		if (object->tag != 0x18U ||
		    object->value.size != digit_count + 1U)
			return false;
		year = decimal_pair(digits) * 100U + decimal_pair(digits + 2U);
		digits += 4U;
		if (year == 0U || (year >= 1950U && year <= 2049U))
			return false;
	}
	if (!decimal_digits(object->value.data, digit_count) ||
	    object->value.data[digit_count] != 'Z')
		return false;
	month = decimal_pair(digits);
	day = decimal_pair(digits + 2U);
	if (month == 0U || month > 12U)
		return false;
	days = month_days[month - 1U];
	if (month == 2U && (year % 4U == 0U) &&
	    (year % 100U != 0U || year % 400U == 0U))
		days++;
	return day != 0U && day <= days && decimal_pair(digits + 4U) <= 23U &&
		decimal_pair(digits + 6U) <= 59U &&
		decimal_pair(digits + 8U) <= 59U;
}

#ifdef PAYLOAD_MM_AUTH_TEST
bool payload_mm_auth_test_time_is_canonical(uint8_t tag,
	const uint8_t *value, size_t value_size)
{
	struct der_object object = {
		.tag = tag,
		.value = { value, value_size },
	};

	return (value_size == 0U || value != NULL) && der_time_canonical(&object);
}
#endif

static bool der_primitive_canonical(const struct der_object *object)
{
	size_t unused;

	switch (object->tag) {
	case 0x01U:
		return object->value.size == 1U &&
			(object->value.data[0] == 0U ||
			 object->value.data[0] == 0xffU);
	case 0x02U:
		return der_integer_canonical(object);
	case 0x03U:
		if (object->value.size == 0U || object->value.data[0] > 7U)
			return false;
		unused = object->value.data[0];
		return (object->value.size != 1U || unused == 0U) &&
			(unused == 0U ||
			 (object->value.data[object->value.size - 1U] &
			  ((1U << unused) - 1U)) == 0U);
	case 0x05U:
		return object->value.size == 0U;
	case 0x06U:
		return der_oid_canonical(object);
	case 0x10U:
	case 0x11U:
		return false;
	case 0x17U:
	case 0x18U:
		return der_time_canonical(object);
	default:
		return object->tag != 0U;
	}
}

static bool algorithm_is(struct der_object *object, const uint8_t *oid,
	size_t oid_size)
{
	struct der_cursor cursor;
	struct der_object field;

	if (!der_only(object, 0x30U, &cursor) ||
	    !der_expect(&cursor, 0x06U, &field) || !oid_is(&field, oid, oid_size))
		return false;
	if (cursor.size == 0U)
		return true;
	return der_expect(&cursor, 0x05U, &field) && field.value.size == 0U &&
		cursor.size == 0U;
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
static bool hash_algorithm(const struct der_object *object,
	enum payload_mm_hash_algorithm *algorithm)
{
	struct der_object copy;

	if (!algorithm)
		return false;
	copy = *object;
	if (algorithm_is(&copy, sha256_oid, sizeof(sha256_oid)))
		*algorithm = PAYLOAD_MM_HASH_SHA256;
	else {
		copy = *object;
		if (algorithm_is(&copy, sha384_oid, sizeof(sha384_oid)))
			*algorithm = PAYLOAD_MM_HASH_SHA384;
		else {
			copy = *object;
			if (!algorithm_is(&copy, sha512_oid, sizeof(sha512_oid)))
				return false;
			*algorithm = PAYLOAD_MM_HASH_SHA512;
		}
	}
	return true;
}
#endif

static int span_order(const struct payload_mm_crypto_span *left,
	const struct payload_mm_crypto_span *right)
{
	size_t common = left->size < right->size ? left->size : right->size;
	size_t index;

	for (index = 0U; index < common; index++) {
		if (left->data[index] != right->data[index])
			return left->data[index] < right->data[index] ? -1 : 1;
	}
	if (left->size == right->size)
		return 0;
	return left->size < right->size ? -1 : 1;
}

static bool attribute_size_valid(size_t size)
{
	return size <= PAYLOAD_MM_MAX_CMS_ATTRIBUTE_SIZE;
}

static bool der_validate(const struct der_object *object, size_t depth,
	size_t *objects)
{
	struct payload_mm_crypto_span previous = { 0 };
	struct der_cursor cursor;
	struct der_object child;

	if (++*objects > PAYLOAD_MM_MAX_DER_OBJECTS)
		return false;
	if ((object->tag & 0xc0U) == 0U) {
		if ((object->tag & 0x20U) != 0U && object->tag != 0x30U &&
		    object->tag != 0x31U)
			return false;
		if ((object->tag & 0x20U) == 0U)
			return der_primitive_canonical(object);
	} else if ((object->tag & 0x20U) == 0U) {
		return true;
	}
	if (depth == PAYLOAD_MM_MAX_DER_DEPTH)
		return false;
	cursor.data = object->value.data;
	cursor.size = object->value.size;
	while (cursor.size != 0U) {
		if (!der_read(&cursor, &child) ||
		    (object->tag == 0x31U && previous.data != NULL &&
		     span_order(&previous, &child.full) > 0) ||
		    !der_validate(&child, depth + 1U, objects))
			return false;
		previous = child.full;
	}
	return true;
}

#ifdef PAYLOAD_MM_AUTH_TEST
bool payload_mm_auth_test_der_is_canonical(const uint8_t *der,
	size_t der_size)
{
	struct der_cursor cursor = { der, der_size };
	struct der_object object;
	size_t objects = 0U;

	return der != NULL && der_read(&cursor, &object) && cursor.size == 0U &&
		der_validate(&object, 0U, &objects);
}

bool payload_mm_auth_test_attribute_size_is_valid(size_t size)
{
	return attribute_size_valid(size);
}
#endif

static bool parse_certificate_identity(
	const struct payload_mm_crypto_span *certificate,
	struct certificate_identity *identity)
{
	struct der_cursor cursor = { certificate->data, certificate->size };
	struct der_cursor fields;
	struct der_object object;

	if (!der_expect(&cursor, 0x30U, &object) || cursor.size != 0U ||
	    !der_only(&object, 0x30U, &fields) ||
	    !der_expect(&fields, 0x30U, &object))
		return false;
	fields.data = object.value.data;
	fields.size = object.value.size;
	if (!der_read(&fields, &object))
		return false;
	if (object.tag == 0xa0U && !der_read(&fields, &object))
		return false;
	if (!integer_canonical(&object))
		return false;
	identity->serial = object.value;
	if (!der_expect(&fields, 0x30U, &object) ||
	    !der_expect(&fields, 0x30U, &object))
		return false;
	identity->issuer = object.full;
	return true;
}

static enum payload_mm_verify_status parse_trust_xdr(
	const struct payload_mm_crypto_span *xdr,
	struct payload_mm_crypto_span *anchors, size_t *count)
{
	size_t offset = 0U;
	size_t padding;
	size_t length;
	size_t index;

	if (xdr == NULL || xdr->data == NULL || xdr->size == 0U ||
	    xdr->size > PAYLOAD_MM_MAX_TRUST_XDR_SIZE)
		return PAYLOAD_MM_VERIFY_INVALID;
	*count = 0U;
	while (offset < xdr->size) {
		if (xdr->size - offset < 4U ||
		    *count == PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES)
			return PAYLOAD_MM_VERIFY_MALFORMED;
		length = ((size_t)xdr->data[offset] << 24) |
			((size_t)xdr->data[offset + 1U] << 16) |
			((size_t)xdr->data[offset + 2U] << 8) |
			xdr->data[offset + 3U];
		offset += 4U;
		if (length == 0U ||
		    length > PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE ||
		    length > xdr->size - offset)
			return PAYLOAD_MM_VERIFY_MALFORMED;
		anchors[*count].data = xdr->data + offset;
		anchors[*count].size = length;
		(*count)++;
		offset += length;
		padding = (4U - (offset & 3U)) & 3U;
		if (offset == xdr->size)
			break;
		if (padding > xdr->size - offset)
			return PAYLOAD_MM_VERIFY_MALFORMED;
		for (index = 0U; index < padding; index++) {
			if (xdr->data[offset + index] != 0U)
				return PAYLOAD_MM_VERIFY_MALFORMED;
		}
		offset += padding;
	}
	return *count == 0U ? PAYLOAD_MM_VERIFY_MALFORMED : PAYLOAD_MM_VERIFY_OK;
}

enum signed_attributes_result {
	SIGNED_ATTRIBUTES_FORMAT,
	SIGNED_ATTRIBUTES_AUTHENTICATED,
	SIGNED_ATTRIBUTES_VALID,
};

#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
struct cms_content {
	const uint8_t *digest;
	size_t digest_size;
	const struct payload_mm_crypto_span *spans;
	size_t span_count;
};
#endif

static enum signed_attributes_result parse_signed_attributes(
	const struct der_object *attributes,
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	const uint8_t *content_digest, size_t content_digest_size)
#else
	const uint8_t content_digest[PAYLOAD_MM_SHA256_SIZE])
#endif
{
	struct der_cursor cursor = { attributes->value.data,
		attributes->value.size };
	struct payload_mm_crypto_span previous = { 0 };
	struct der_cursor attribute;
	struct der_cursor values;
	struct der_object object;
	struct der_object oid;
	struct der_object value_set;
	bool content_type = false;
	bool message_digest = false;
	bool signing_time = false;
	bool smime_capabilities = false;
	size_t count = 0U;
	size_t objects = 0U;

	while (cursor.size != 0U) {
		if (++count > PAYLOAD_MM_MAX_CMS_ATTRIBUTES ||
		    !der_expect(&cursor, 0x30U, &object) ||
		    !attribute_size_valid(object.full.size) ||
		    (previous.data != NULL && span_order(&previous, &object.full) >= 0))
			return SIGNED_ATTRIBUTES_FORMAT;
		previous = object.full;
		attribute.data = object.value.data;
		attribute.size = object.value.size;
		if (!der_expect(&attribute, 0x06U, &oid) ||
		    oid.value.size > PAYLOAD_MM_MAX_CMS_OID_SIZE ||
		    !der_expect(&attribute, 0x31U, &value_set) ||
		    attribute.size != 0U)
			return SIGNED_ATTRIBUTES_FORMAT;
		if (!der_validate(&value_set, 0U, &objects))
			return SIGNED_ATTRIBUTES_FORMAT;
		values.data = value_set.value.data;
		values.size = value_set.value.size;
		if (oid_is(&oid, content_type_oid, sizeof(content_type_oid))) {
			if (content_type || !der_expect(&values, 0x06U, &object) ||
			    !oid_is(&object, data_oid, sizeof(data_oid)) ||
			    values.size != 0U)
				return SIGNED_ATTRIBUTES_FORMAT;
			content_type = true;
		} else if (oid_is(&oid, message_digest_oid,
			sizeof(message_digest_oid))) {
			if (message_digest || !der_expect(&values, 0x04U, &object) ||
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
			    object.value.size != content_digest_size ||
#else
			    object.value.size != PAYLOAD_MM_SHA256_SIZE ||
#endif
			    values.size != 0U)
				return SIGNED_ATTRIBUTES_FORMAT;
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
			if (!bytes_equal(object.value.data, content_digest,
				content_digest_size))
#else
			if (!bytes_equal(object.value.data, content_digest,
				PAYLOAD_MM_SHA256_SIZE))
#endif
				return SIGNED_ATTRIBUTES_AUTHENTICATED;
			message_digest = true;
		} else if (oid_is(&oid, signing_time_oid,
			sizeof(signing_time_oid))) {
			if (signing_time || !der_read(&values, &object) ||
			    (object.tag != 0x17U && object.tag != 0x18U) ||
			    !der_time_canonical(&object) || values.size != 0U)
				return SIGNED_ATTRIBUTES_FORMAT;
			signing_time = true;
		} else if (oid_is(&oid, smime_capabilities_oid,
			sizeof(smime_capabilities_oid))) {
			if (smime_capabilities || !der_expect(&values, 0x30U, &object) ||
			    values.size != 0U)
				return SIGNED_ATTRIBUTES_FORMAT;
			smime_capabilities = true;
		} else {
			return SIGNED_ATTRIBUTES_FORMAT;
		}
	}
	return content_type && message_digest ? SIGNED_ATTRIBUTES_VALID :
		SIGNED_ATTRIBUTES_FORMAT;
}

static enum payload_mm_verify_status parse_cms(
	const struct payload_mm_crypto_span *signed_data,
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	const struct cms_content *content,
#else
	const uint8_t content_digest[PAYLOAD_MM_SHA256_SIZE],
#endif
	struct payload_mm_crypto_span *certificates,
	size_t *certificate_count, size_t *signer_index,
	struct payload_mm_crypto_span *signed_attributes,
	struct payload_mm_crypto_span *signature,
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	enum payload_mm_hash_algorithm *digest_algorithm,
#endif
	enum payload_mm_crypto_failure_source *failure_source)
{
	struct certificate_identity identities[PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES];
	struct payload_mm_crypto_span signer_issuer;
	struct payload_mm_crypto_span signer_serial;
	struct der_cursor cursor = { signed_data->data, signed_data->size };
	struct der_cursor contents;
	struct der_cursor fields;
	struct der_object object;
	struct der_object algorithm;
	struct der_object first;
	struct payload_mm_crypto_span certificate;
	struct certificate_identity identity;
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	uint8_t computed_digest[PAYLOAD_MM_MAX_DIGEST_SIZE];
	const uint8_t *content_digest;
	size_t content_digest_size;
#endif
	size_t matches = 0U;
	size_t objects = 0U;
	size_t index;
	int order;
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	enum payload_mm_hash_algorithm signer_algorithm;
	enum payload_mm_verify_status status;
#endif
	enum signed_attributes_result attributes_result;

	*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_CAPSULE_FORMAT;

	if (!der_expect(&cursor, 0x30U, &object) || cursor.size != 0U ||
	    !der_validate(&object, 0U, &objects) ||
	    !der_only(&object, 0x30U, &contents))
		return PAYLOAD_MM_VERIFY_MALFORMED;
	cursor = contents;
	if (!der_read(&cursor, &first))
		return PAYLOAD_MM_VERIFY_MALFORMED;
	if (oid_is(&first, signed_data_oid, sizeof(signed_data_oid))) {
		if (!der_expect(&cursor, 0xa0U, &object) || cursor.size != 0U ||
		    !der_only(&object, 0xa0U, &contents) ||
		    !der_expect(&contents, 0x30U, &object) || contents.size != 0U)
			return PAYLOAD_MM_VERIFY_MALFORMED;
		fields.data = object.value.data;
		fields.size = object.value.size;
	} else {
		fields = contents;
	}
	if (!der_expect(&fields, 0x02U, &object) || !integer_is_one(&object) ||
	    !der_expect(&fields, 0x31U, &object))
		return PAYLOAD_MM_VERIFY_MALFORMED;
	contents.data = object.value.data;
	contents.size = object.value.size;
	if (!der_expect(&contents, 0x30U, &algorithm) || contents.size != 0U)
		return PAYLOAD_MM_VERIFY_MALFORMED;
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	if (!hash_algorithm(&algorithm, digest_algorithm))
		return PAYLOAD_MM_VERIFY_UNSUPPORTED;
	content_digest_size = payload_mm_hash_digest_size(*digest_algorithm);
	if (content->span_count) {
		if (content->digest || content->digest_size)
			return PAYLOAD_MM_VERIFY_INVALID;
		status = payload_mm_hash_spans(*digest_algorithm, content->spans,
			content->span_count, computed_digest);
		if (status != PAYLOAD_MM_VERIFY_OK)
			return status;
		content_digest = computed_digest;
	} else {
		if (content->spans || !content->digest ||
		    content->digest_size != content_digest_size)
			return PAYLOAD_MM_VERIFY_UNSUPPORTED;
		content_digest = content->digest;
	}
#else
	if (!algorithm_is(&algorithm, sha256_oid, sizeof(sha256_oid)))
		return PAYLOAD_MM_VERIFY_UNSUPPORTED;
#endif
	if (!der_expect(&fields, 0x30U, &object))
		return PAYLOAD_MM_VERIFY_MALFORMED;
	contents.data = object.value.data;
	contents.size = object.value.size;
	if (!der_expect(&contents, 0x06U, &object) ||
	    !oid_is(&object, data_oid, sizeof(data_oid)) || contents.size != 0U)
		return PAYLOAD_MM_VERIFY_UNSUPPORTED;
	if (!der_expect(&fields, 0xa0U, &object))
		return PAYLOAD_MM_VERIFY_MALFORMED;
	contents.data = object.value.data;
	contents.size = object.value.size;
	*certificate_count = 0U;
	while (contents.size != 0U) {
		if (*certificate_count ==
		    PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES ||
		    !der_expect(&contents, 0x30U, &object))
			return PAYLOAD_MM_VERIFY_MALFORMED;
		certificate = object.full;
		if (!parse_certificate_identity(&certificate, &identity))
			return PAYLOAD_MM_VERIFY_MALFORMED;
		index = *certificate_count;
		while (index != 0U) {
			order = span_order(&certificate, &certificates[index - 1U]);
			if (order == 0)
				return PAYLOAD_MM_VERIFY_MALFORMED;
			if (order > 0)
				break;
			certificates[index] = certificates[index - 1U];
			identities[index] = identities[index - 1U];
			index--;
		}
		certificates[index] = certificate;
		identities[index] = identity;
		(*certificate_count)++;
	}
	if (*certificate_count == 0U || !der_expect(&fields, 0x31U, &object) ||
	    fields.size != 0U)
		return PAYLOAD_MM_VERIFY_MALFORMED;
	contents.data = object.value.data;
	contents.size = object.value.size;
	if (!der_expect(&contents, 0x30U, &object) || contents.size != 0U)
		return PAYLOAD_MM_VERIFY_UNSUPPORTED;
	fields.data = object.value.data;
	fields.size = object.value.size;
	if (!der_expect(&fields, 0x02U, &object) || !integer_is_one(&object) ||
	    !der_expect(&fields, 0x30U, &object))
		return PAYLOAD_MM_VERIFY_MALFORMED;
	contents.data = object.value.data;
	contents.size = object.value.size;
	if (!der_expect(&contents, 0x30U, &object))
		return PAYLOAD_MM_VERIFY_MALFORMED;
	signer_issuer = object.full;
	if (!der_expect(&contents, 0x02U, &object) ||
	    !integer_canonical(&object) || contents.size != 0U)
		return PAYLOAD_MM_VERIFY_MALFORMED;
	signer_serial = object.value;
	if (!der_expect(&fields, 0x30U, &algorithm))
		return PAYLOAD_MM_VERIFY_MALFORMED;
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	if (!hash_algorithm(&algorithm, &signer_algorithm) ||
	    signer_algorithm != *digest_algorithm)
#else
	if (!algorithm_is(&algorithm, sha256_oid, sizeof(sha256_oid)))
#endif
		return PAYLOAD_MM_VERIFY_UNSUPPORTED;
	if (!der_expect(&fields, 0xa0U, &object))
		return PAYLOAD_MM_VERIFY_MALFORMED;
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	attributes_result = parse_signed_attributes(&object, content_digest,
		content_digest_size);
#else
	attributes_result = parse_signed_attributes(&object, content_digest);
#endif
	if (attributes_result != SIGNED_ATTRIBUTES_VALID) {
		if (attributes_result == SIGNED_ATTRIBUTES_AUTHENTICATED)
			*failure_source =
				PAYLOAD_MM_CRYPTO_FAILURE_CAPSULE_SIGNATURE;
		return attributes_result == SIGNED_ATTRIBUTES_AUTHENTICATED ?
			PAYLOAD_MM_VERIFY_REJECTED : PAYLOAD_MM_VERIFY_MALFORMED;
	}
	*signed_attributes = object.full;
	if (!der_expect(&fields, 0x30U, &algorithm))
		return PAYLOAD_MM_VERIFY_MALFORMED;
	if (!algorithm_is(&algorithm, rsa_oid, sizeof(rsa_oid)))
		return PAYLOAD_MM_VERIFY_UNSUPPORTED;
	if (!der_expect(&fields, 0x04U, &object) || fields.size != 0U ||
	    object.value.size == 0U ||
	    object.value.size > PAYLOAD_MM_CRYPTO_MAX_SIGNATURE_SIZE)
		return PAYLOAD_MM_VERIFY_MALFORMED;
	*signature = object.value;
	for (index = 0U; index < *certificate_count; index++) {
		if (span_equal(&identities[index].issuer, &signer_issuer) &&
		    span_equal(&identities[index].serial, &signer_serial)) {
			*signer_index = index;
			matches++;
		}
	}
	if (matches != 1U) {
		*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_CAPSULE_SIGNATURE;
		return PAYLOAD_MM_VERIFY_REJECTED;
	}
	return PAYLOAD_MM_VERIFY_OK;
}

static enum payload_mm_verify_status cms_verify_detailed(
	const struct payload_mm_crypto_span *signed_data,
	const uint8_t content_digest[PAYLOAD_MM_SHA256_SIZE],
	const struct payload_mm_crypto_span *trust_xdr,
	enum payload_mm_crypto_failure_source *failure_source)
{
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	const struct cms_content content = {
		.digest = content_digest,
		.digest_size = PAYLOAD_MM_SHA256_SIZE,
	};
#endif
	struct payload_mm_crypto_span certificates[PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES];
	struct payload_mm_crypto_span intermediates[PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES];
	struct payload_mm_crypto_span anchors[PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES];
	struct payload_mm_crypto_span attributes;
	struct payload_mm_crypto_span signature;
	struct payload_mm_crypto_span hash_spans[2];
	uint8_t attribute_tag = 0x31U;
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	uint8_t attribute_digest[PAYLOAD_MM_MAX_DIGEST_SIZE];
	enum payload_mm_hash_algorithm digest_algorithm;
#else
	uint8_t attribute_digest[PAYLOAD_MM_SHA256_SIZE];
#endif
	size_t certificate_count;
	size_t intermediate_count = 0U;
	size_t anchor_count;
	size_t signer;
	size_t index;
	enum payload_mm_verify_status status;

	if (failure_source == NULL)
		return PAYLOAD_MM_VERIFY_INVALID;
	*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_TRANSIENT;
	if (signed_data == NULL || signed_data->data == NULL ||
	    signed_data->size == 0U ||
	    signed_data->size > PAYLOAD_MM_MAX_CMS_SIZE ||
	    content_digest == NULL)
		return PAYLOAD_MM_VERIFY_INVALID;
	status = parse_trust_xdr(trust_xdr, anchors, &anchor_count);
	if (status != PAYLOAD_MM_VERIFY_OK) {
		*failure_source = PAYLOAD_MM_CRYPTO_FAILURE_TRUST;
		return status;
	}
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	status = parse_cms(signed_data, &content, certificates,
		&certificate_count, &signer, &attributes, &signature,
		&digest_algorithm, failure_source);
#else
	status = parse_cms(signed_data, content_digest, certificates,
		&certificate_count, &signer, &attributes, &signature,
		failure_source);
#endif
	if (status != PAYLOAD_MM_VERIFY_OK) {
		return status;
	}
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	if (digest_algorithm != PAYLOAD_MM_HASH_SHA256)
		return PAYLOAD_MM_VERIFY_UNSUPPORTED;
#endif
	for (index = 0U; index < certificate_count; index++) {
		if (index != signer)
			intermediates[intermediate_count++] = certificates[index];
	}
	status = payload_mm_x509_chain_verify_detailed(&certificates[signer],
		intermediates, intermediate_count, anchors,
		anchor_count, failure_source);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
	hash_spans[0].data = &attribute_tag;
	hash_spans[0].size = 1U;
	hash_spans[1].data = attributes.data + 1U;
	hash_spans[1].size = attributes.size - 1U;
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	status = payload_mm_hash_spans(digest_algorithm, hash_spans, 2U,
		attribute_digest);
#else
	status = payload_mm_sha256_spans(hash_spans, 2U, attribute_digest);
#endif
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
	status = payload_mm_rsa_verify(&certificates[signer], digest_algorithm,
		attribute_digest, payload_mm_hash_digest_size(digest_algorithm),
		&signature);
#else
	status = payload_mm_rsa_sha256_verify(&certificates[signer],
		attribute_digest, &signature);
#endif
	if (status == PAYLOAD_MM_VERIFY_MALFORMED || status == PAYLOAD_MM_VERIFY_UNSUPPORTED ||
	    status == PAYLOAD_MM_VERIFY_REJECTED)
		*failure_source = status == PAYLOAD_MM_VERIFY_REJECTED ?
			PAYLOAD_MM_CRYPTO_FAILURE_CAPSULE_SIGNATURE :
			PAYLOAD_MM_CRYPTO_FAILURE_CAPSULE_FORMAT;
	return status;
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_CMS_VERIFY)
static bool address_range_valid(const void *data, size_t size)
{
	return size == 0U || (data && (uintptr_t)data <= UINTPTR_MAX - size);
}

static bool address_ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_address = (uintptr_t)left;
	uintptr_t right_address = (uintptr_t)right;

	if (!address_range_valid(left, left_size) ||
	    !address_range_valid(right, right_size))
		return true;
	if (left_size == 0U || right_size == 0U)
		return false;
	return left_address < right_address + right_size &&
		right_address < left_address + left_size;
}

static bool authvar_cms_buffers_disjoint(
	const struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content, size_t content_count,
	const struct payload_mm_cms_verified_signer *verified)
{
	const void *mutable_data[] = { owner, verified };
	const size_t mutable_sizes[] = { sizeof(*owner), sizeof(*verified) };
	const void *immutable_data[PAYLOAD_MM_HASH_MAX_SPANS + 3U];
	size_t immutable_sizes[PAYLOAD_MM_HASH_MAX_SPANS + 3U];
	size_t immutable_count = 0U;
	size_t left;
	size_t right;

	if (!address_range_valid(content, content_count * sizeof(*content)))
		return false;
	immutable_data[immutable_count] = signed_data;
	immutable_sizes[immutable_count++] = sizeof(*signed_data);
	immutable_data[immutable_count] = signed_data->data;
	immutable_sizes[immutable_count++] = signed_data->size;
	immutable_data[immutable_count] = content;
	immutable_sizes[immutable_count++] = content_count * sizeof(*content);
	for (left = 0U; left < content_count; left++) {
		immutable_data[immutable_count] = content[left].data;
		immutable_sizes[immutable_count++] = content[left].size;
	}
	for (left = 0U; left < ARRAY_SIZE(mutable_data); left++) {
		if (!address_range_valid(mutable_data[left], mutable_sizes[left]))
			return false;
		for (right = left + 1U; right < ARRAY_SIZE(mutable_data); right++)
			if (address_ranges_overlap(mutable_data[left], mutable_sizes[left],
				mutable_data[right], mutable_sizes[right]))
				return false;
		for (right = 0U; right < immutable_count; right++)
			if (address_ranges_overlap(mutable_data[left], mutable_sizes[left],
				immutable_data[right], immutable_sizes[right]))
				return false;
	}
	for (left = 0U; left < immutable_count; left++)
		if (!address_range_valid(immutable_data[left], immutable_sizes[left]))
			return false;
	return true;
}

enum payload_mm_verify_status payload_mm_cms_verify_detached_untrusted(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content_spans, size_t content_count,
	struct payload_mm_cms_verified_signer *verified)
{
	const struct cms_content content = {
		.spans = content_spans,
		.span_count = content_count,
	};
	struct payload_mm_cms_verified_signer published = { 0 };
	struct payload_mm_crypto_span attributes;
	struct payload_mm_crypto_span signature;
	struct payload_mm_crypto_span hash_spans[2];
	enum payload_mm_crypto_failure_source failure_source;
	enum payload_mm_verify_status status;
	uint8_t attribute_tag = 0x31U;
	uint8_t attribute_digest[PAYLOAD_MM_MAX_DIGEST_SIZE];
	size_t signer;

	if (!owner || !verified || !signed_data || !content_spans ||
	    !address_range_valid(owner, sizeof(*owner)) ||
	    !address_range_valid(verified, sizeof(*verified)) ||
	    !address_range_valid(signed_data, sizeof(*signed_data)) ||
	    (uintptr_t)owner % _Alignof(*owner) ||
	    (uintptr_t)verified % _Alignof(*verified) ||
	    (uintptr_t)signed_data % _Alignof(*signed_data) ||
	    (uintptr_t)content_spans % _Alignof(*content_spans) ||
	    !signed_data->data ||
	    !signed_data->size || signed_data->size > PAYLOAD_MM_MAX_CMS_SIZE ||
	    !content_count || content_count > PAYLOAD_MM_HASH_MAX_SPANS ||
	    !authvar_cms_buffers_disjoint(owner, signed_data, content_spans,
		content_count, verified))
		return PAYLOAD_MM_VERIFY_INVALID;
	status = payload_mm_crypto_begin(owner);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
	status = parse_cms(signed_data, &content, published.certificates,
		&published.certificate_count, &signer, &attributes, &signature,
		&published.digest_algorithm, &failure_source);
	if (status == PAYLOAD_MM_VERIFY_OK) {
		hash_spans[0].data = &attribute_tag;
		hash_spans[0].size = 1U;
		hash_spans[1].data = attributes.data + 1U;
		hash_spans[1].size = attributes.size - 1U;
		status = payload_mm_hash_spans(published.digest_algorithm,
			hash_spans, 2U, attribute_digest);
	}
	if (status == PAYLOAD_MM_VERIFY_OK)
		status = payload_mm_rsa_verify(&published.certificates[signer],
			published.digest_algorithm, attribute_digest,
			payload_mm_hash_digest_size(published.digest_algorithm),
			&signature);
	if (status == PAYLOAD_MM_VERIFY_OK)
		published.signer_certificate = published.certificates[signer];
	status = payload_mm_crypto_end(owner, status);
	mbedtls_platform_zeroize(attribute_digest, sizeof(attribute_digest));
	if (status == PAYLOAD_MM_VERIFY_OK)
		*verified = published;
	return status;
}
#endif

#ifdef PAYLOAD_MM_AUTH_TEST
enum payload_mm_verify_status payload_mm_cms_verify(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const uint8_t content_digest[PAYLOAD_MM_SHA256_SIZE],
	const struct payload_mm_crypto_span *trust_xdr)
{
	enum payload_mm_crypto_failure_source failure_source;
	enum payload_mm_verify_status status;

	status = payload_mm_crypto_begin(owner);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
	status = cms_verify_detailed(signed_data, content_digest, trust_xdr,
		&failure_source);
	return payload_mm_crypto_end(owner, status);
}
#endif

static uint16_t read_le16(const uint8_t *bytes)
{
	return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t read_le64(const uint8_t *bytes)
{
	return (uint64_t)read_le32(bytes) |
		((uint64_t)read_le32(bytes + 4U) << 32);
}

enum payload_mm_verify_status payload_mm_authenticate_image(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *image,
	const struct payload_mm_crypto_span *trust_xdr,
	struct payload_mm_authenticated_image *authenticated)
{
	struct payload_mm_crypto_span hash_spans[2];
	struct payload_mm_crypto_span signed_data;
	uint8_t digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t verified_digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t cms_digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t verified_cms_digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t trust_digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t verified_trust_digest[PAYLOAD_MM_SHA256_SIZE];
	uint32_t certificate_length;
	size_t payload_offset;
	enum payload_mm_crypto_failure_source failure_source;
	enum payload_mm_verify_status verify_status;
	enum payload_mm_verify_status status;

	if (authenticated == NULL)
		return PAYLOAD_MM_VERIFY_INVALID;
	*authenticated = (struct payload_mm_authenticated_image){
		.failure_source = PAYLOAD_MM_AUTH_FAILURE_TRANSIENT,
	};
	status = payload_mm_crypto_begin(owner);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
	if (image == NULL || image->data == NULL) {
		status = PAYLOAD_MM_VERIFY_INVALID;
		goto out;
	}
	if (trust_xdr == NULL || trust_xdr->data == NULL || trust_xdr->size == 0U ||
	    trust_xdr->size > PAYLOAD_MM_MAX_TRUST_XDR_SIZE) {
		authenticated->failure_source = PAYLOAD_MM_AUTH_FAILURE_TRUST;
		status = PAYLOAD_MM_VERIFY_INVALID;
		goto out;
	}
	if (image->size < PAYLOAD_MM_AUTH_HEADER_SIZE) {
		authenticated->failure_source = PAYLOAD_MM_AUTH_FAILURE_CAPSULE_FORMAT;
		status = PAYLOAD_MM_VERIFY_INVALID;
		goto out;
	}
	certificate_length = read_le32(image->data + 8U);
	if (certificate_length <= 24U || certificate_length > image->size - 8U ||
	    certificate_length - 24U > PAYLOAD_MM_MAX_CMS_SIZE ||
	    read_le16(image->data + 12U) != 0x0200U ||
	    read_le16(image->data + 14U) != 0x0ef1U) {
		authenticated->failure_source =
			PAYLOAD_MM_AUTH_FAILURE_CAPSULE_FORMAT;
		status = PAYLOAD_MM_VERIFY_INVALID;
		goto out;
	}
	if (!bytes_equal(image->data + 16U, pkcs7_guid, sizeof(pkcs7_guid))) {
		authenticated->failure_source =
			PAYLOAD_MM_AUTH_FAILURE_CAPSULE_FORMAT;
		status = PAYLOAD_MM_VERIFY_UNSUPPORTED;
		goto out;
	}
	payload_offset = 8U + certificate_length;
	if (payload_offset >= image->size) {
		authenticated->failure_source =
			PAYLOAD_MM_AUTH_FAILURE_CAPSULE_FORMAT;
		status = PAYLOAD_MM_VERIFY_INVALID;
		goto out;
	}
	if (image->size - payload_offset > PAYLOAD_MM_MAX_SIGNED_BODY_SIZE) {
		authenticated->failure_source =
			PAYLOAD_MM_AUTH_FAILURE_CAPSULE_FORMAT;
		status = PAYLOAD_MM_VERIFY_INVALID;
		goto out;
	}
	signed_data.data = image->data + PAYLOAD_MM_AUTH_HEADER_SIZE;
	signed_data.size = certificate_length - 24U;
	hash_spans[0].data = image->data + payload_offset;
	hash_spans[0].size = image->size - payload_offset;
	hash_spans[1].data = image->data;
	hash_spans[1].size = 8U;
	status = payload_mm_sha256_spans(hash_spans, 2U, digest);
	if (status != PAYLOAD_MM_VERIFY_OK)
		goto out;
	status = payload_mm_sha256(signed_data.data, signed_data.size,
		cms_digest);
	if (status != PAYLOAD_MM_VERIFY_OK)
		goto out;
	status = payload_mm_sha256(trust_xdr->data, trust_xdr->size,
		trust_digest);
	if (status != PAYLOAD_MM_VERIFY_OK)
		goto out;
	verify_status = cms_verify_detailed(&signed_data, digest, trust_xdr,
		&failure_source);
	status = payload_mm_sha256_spans(hash_spans, 2U, verified_digest);
	if (status != PAYLOAD_MM_VERIFY_OK)
		goto out;
	status = payload_mm_sha256(signed_data.data, signed_data.size,
		verified_cms_digest);
	if (status != PAYLOAD_MM_VERIFY_OK)
		goto out;
	status = payload_mm_sha256(trust_xdr->data, trust_xdr->size,
		verified_trust_digest);
	if (status != PAYLOAD_MM_VERIFY_OK)
		goto out;
	if (!bytes_equal(digest, verified_digest, sizeof(digest)) ||
	    !bytes_equal(cms_digest, verified_cms_digest, sizeof(cms_digest)) ||
	    !bytes_equal(trust_digest, verified_trust_digest,
		sizeof(trust_digest))) {
		status = PAYLOAD_MM_VERIFY_CHANGED;
		goto out;
	}
	if (owner->allocation_failed) {
		authenticated->failure_source = PAYLOAD_MM_AUTH_FAILURE_TRANSIENT;
		status = PAYLOAD_MM_VERIFY_NO_MEMORY;
		goto out;
	}
	if (verify_status != PAYLOAD_MM_VERIFY_OK) {
		if (failure_source ==
		    PAYLOAD_MM_CRYPTO_FAILURE_CAPSULE_FORMAT)
			authenticated->failure_source =
				PAYLOAD_MM_AUTH_FAILURE_CAPSULE_FORMAT;
		else if (failure_source ==
			 PAYLOAD_MM_CRYPTO_FAILURE_CAPSULE_SIGNATURE)
			authenticated->failure_source =
				PAYLOAD_MM_AUTH_FAILURE_CAPSULE_SIGNATURE;
		else if (failure_source == PAYLOAD_MM_CRYPTO_FAILURE_TRUST)
			authenticated->failure_source =
				PAYLOAD_MM_AUTH_FAILURE_TRUST;
		else
			authenticated->failure_source =
				PAYLOAD_MM_AUTH_FAILURE_TRANSIENT;
		status = verify_status;
		goto out;
	}
	authenticated->payload = hash_spans[0];
	authenticated->monotonic_count = read_le64(image->data);
	authenticated->failure_source = PAYLOAD_MM_AUTH_FAILURE_NONE;
	status = PAYLOAD_MM_VERIFY_OK;
out:
	return payload_mm_crypto_end(owner, status);
}
