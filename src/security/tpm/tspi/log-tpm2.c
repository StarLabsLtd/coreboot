/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Unlike log.c this implements TPM log according to TPM2.0 specification
 * rather then using coreboot-specific log format.
 *
 * First entry is in TPM1.2 format and serves as a header, the rest are in
 * a newer (agile) format which supports SHA256 and multiple hashes, but we
 * store only one hash.
 *
 * This is defined in "TCG EFI Protocol Specification".
 */

#include <endian.h>
#include <console/console.h>
#include <security/tpm/tspi.h>
#include <security/tpm/tspi/crtm.h>
#include <security/tpm/tspi/logs.h>
#include <region_file.h>
#include <stddef.h>
#include <string.h>
#include <symbols.h>
#include <cbmem.h>
#include <vb2_sha.h>

static size_t tpm2_log_entry_header_size(void)
{
	return offsetof(struct tpm_2_log_entry, data);
}

static uint32_t tpm2_log_read_le32(const uint8_t *entry, size_t offset)
{
	uint32_t value;

	memcpy(&value, entry + offset, sizeof(value));
	return le32toh(value);
}

static void tpm2_log_write_le16(uint8_t *entry, size_t offset, uint16_t value)
{
	value = htole16(value);
	memcpy(entry + offset, &value, sizeof(value));
}

static void tpm2_log_write_le32(uint8_t *entry, size_t offset, uint32_t value)
{
	value = htole32(value);
	memcpy(entry + offset, &value, sizeof(value));
}

static uint32_t tpm2_log_entry_data_length(const uint8_t *entry)
{
	return tpm2_log_read_le32(entry, offsetof(struct tpm_2_log_entry, data_length));
}

static const uint8_t *tpm2_log_entry_digest(const uint8_t *entry)
{
	return entry + offsetof(struct tpm_2_log_entry, digest);
}

static const uint8_t *tpm2_log_entry_data(const uint8_t *entry)
{
	return entry + offsetof(struct tpm_2_log_entry, data);
}

static size_t tpm2_log_entry_size(const uint8_t *entry)
{
	uint32_t data_length = tpm2_log_entry_data_length(entry);

	if (data_length > TPM_20_LOG_DATA_MAX_LENGTH)
		return 0;

	return tpm2_log_entry_header_size() + data_length;
}

static size_t tpm2_log_capacity(const struct tpm_2_log_table *tclt)
{
	return le16toh(tclt->vendor.max_entries) * sizeof(struct tpm_2_log_entry);
}

static const uint8_t *tpm2_log_entry_offset(const struct tpm_2_log_table *tclt,
					    uint16_t entry_idx)
{
	uint16_t i;
	const uint8_t *entry = (const uint8_t *)tclt->entries;
	const uint8_t *end = entry + tpm2_log_capacity(tclt);

	for (i = 0; i < entry_idx; i++) {
		size_t remaining = end - entry;
		size_t entry_size;

		if (remaining < tpm2_log_entry_header_size())
			return NULL;

		entry_size = tpm2_log_entry_size(entry);
		if (!entry_size || entry_size > remaining)
			return NULL;

		entry += entry_size;
	}

	return entry;
}

static const uint8_t *tpm2_log_entry_at(const struct tpm_2_log_table *tclt, uint16_t entry_idx)
{
	const uint8_t *entry = tpm2_log_entry_offset(tclt, entry_idx);
	const uint8_t *end = (const uint8_t *)tclt->entries + tpm2_log_capacity(tclt);
	size_t remaining;
	size_t entry_size;

	if (!entry)
		return NULL;

	remaining = end - entry;
	if (remaining < tpm2_log_entry_header_size())
		return NULL;

	entry_size = tpm2_log_entry_size(entry);
	if (!entry_size || entry_size > remaining)
		return NULL;

	return entry;
}

static size_t tpm2_log_used_size(const struct tpm_2_log_table *tclt)
{
	uint16_t entries = le16toh(tclt->vendor.num_entries);
	const uint8_t *start = (const uint8_t *)tclt->entries;
	const uint8_t *next = tpm2_log_entry_offset(tclt, entries);

	if (!next)
		return tpm2_log_capacity(tclt) + 1;

	return (const uint8_t *)next - start;
}

static int tpm2_log_uses_packed_entries(const struct tpm_2_log_table *tclt)
{
	return le32toh(tclt->vendor.entry_size) == 0;
}

static int tpm2_log_uses_fixed_entries(const struct tpm_2_log_table *tclt)
{
	return le32toh(tclt->vendor.entry_size) == sizeof(struct tpm_2_log_entry);
}

static const uint8_t *tpm2_log_get_entry_at(const struct tpm_2_log_table *tclt,
					    uint16_t entry_idx)
{
	if (tpm2_log_uses_fixed_entries(tclt)) {
		if (entry_idx >= le16toh(tclt->vendor.max_entries))
			return NULL;

		return (const uint8_t *)&tclt->entries[entry_idx];
	}

	if (!ENV_HAS_CBMEM || !tpm2_log_uses_packed_entries(tclt))
		return NULL;

	return tpm2_log_entry_at(tclt, entry_idx);
}

static uint16_t tpmalg_from_vb2_hash(enum vb2_hash_algorithm hash_type)
{
	switch (hash_type) {
	case VB2_HASH_SHA1:
		return TPM2_ALG_SHA1;
	case VB2_HASH_SHA256:
		return TPM2_ALG_SHA256;
	case VB2_HASH_SHA384:
		return TPM2_ALG_SHA384;
	case VB2_HASH_SHA512:
		return TPM2_ALG_SHA512;

	default:
		return 0xFF;
	}
}

static int tpm2_log_event_is_valid(enum vb2_hash_algorithm digest_algo, const uint8_t *digest,
				   const size_t digest_len, const uint8_t *data,
				   const size_t data_len)
{
	if (!digest) {
		printk(BIOS_WARNING, "TPM LOG: event digest not set\n");
		return 0;
	}

	if (digest_algo != TPM_MEASURE_ALGO) {
		printk(BIOS_WARNING, "TPM LOG: digest is of unsupported type: %s\n",
		       vb2_get_hash_algorithm_name(digest_algo));
		return 0;
	}

	if (digest_len != vb2_digest_size(TPM_MEASURE_ALGO)) {
		printk(BIOS_WARNING, "TPM LOG: digest has invalid length: %d\n",
		       (int)digest_len);
		return 0;
	}

	if (!data) {
		printk(BIOS_WARNING, "TPM LOG: event data not set\n");
		return 0;
	}

	if (data_len > TPM_20_LOG_DATA_MAX_LENGTH) {
		printk(BIOS_WARNING, "TPM LOG: event data too long\n");
		return 0;
	}

	return 1;
}

static void tpm2_log_write_event(uint8_t *entry, const uint32_t pcr, const uint32_t event_type,
				 enum vb2_hash_algorithm digest_algo, const uint8_t *digest,
				 const size_t digest_len, const uint8_t *data,
				 const size_t data_len)
{
	tpm2_log_write_le32(entry, offsetof(struct tpm_2_log_entry, pcr), pcr);
	tpm2_log_write_le32(entry, offsetof(struct tpm_2_log_entry, event_type), event_type);
	tpm2_log_write_le32(entry, offsetof(struct tpm_2_log_entry, digest_count), 1);
	tpm2_log_write_le16(entry, offsetof(struct tpm_2_log_entry, digest_type),
			    tpmalg_from_vb2_hash(digest_algo));
	tpm2_log_write_le32(entry, offsetof(struct tpm_2_log_entry, data_length),
			    (uint32_t)data_len);

	memcpy(entry + offsetof(struct tpm_2_log_entry, digest), digest, digest_len);
	memcpy(entry + offsetof(struct tpm_2_log_entry, data), data, data_len);
}

static int tpm2_log_append_fixed_event(struct tpm_2_log_table *tclt, const uint32_t pcr,
				       const uint32_t event_type,
				       enum vb2_hash_algorithm digest_algo,
				       const uint8_t *digest, const size_t digest_len,
				       const char *name, const size_t name_len)
{
	struct tpm_2_log_entry *entry;
	size_t data_len = name_len + 1;

	if (!tpm2_log_event_is_valid(digest_algo, digest, digest_len, (const uint8_t *)name,
				     data_len))
		return 0;

	if (le16toh(tclt->vendor.num_entries) >= le16toh(tclt->vendor.max_entries)) {
		printk(BIOS_WARNING, "TPM LOG: log table is full\n");
		return 0;
	}

	entry = &tclt->entries[le16toh(tclt->vendor.num_entries)];
	tclt->vendor.num_entries = htole16(le16toh(tclt->vendor.num_entries) + 1);

	memset(entry, 0, sizeof(*entry));

	entry->pcr = htole32(pcr);
	entry->event_type = htole32(event_type);
	entry->digest_count = htole32(1);
	entry->digest_type = htole16(tpmalg_from_vb2_hash(digest_algo));
	memcpy(entry->digest, digest, digest_len);
	entry->data_length = htole32(data_len);
	memcpy(entry->data, name, name_len);
	entry->data[name_len] = '\0';

	return 1;
}

static int tpm2_log_append_packed_event(struct tpm_2_log_table *tclt, const uint32_t pcr,
					const uint32_t event_type,
					enum vb2_hash_algorithm digest_algo,
					const uint8_t *digest, const size_t digest_len,
					const uint8_t *data, const size_t data_len)
{
	uint8_t *entry;
	size_t new_entry_size;
	size_t used_size;
	size_t capacity;

	if (!tpm2_log_event_is_valid(digest_algo, digest, digest_len, data, data_len))
		return 0;

	if (le16toh(tclt->vendor.num_entries) >= le16toh(tclt->vendor.max_entries)) {
		printk(BIOS_WARNING, "TPM LOG: log table is full\n");
		return 0;
	}

	new_entry_size = tpm2_log_entry_header_size() + data_len;
	used_size = tpm2_log_used_size(tclt);
	capacity = tpm2_log_capacity(tclt);
	if (used_size > capacity || new_entry_size > capacity - used_size) {
		printk(BIOS_WARNING, "TPM LOG: log table is full\n");
		return 0;
	}

	entry = (uint8_t *)tclt->entries + used_size;
	tclt->vendor.num_entries = htole16(le16toh(tclt->vendor.num_entries) + 1);

	tpm2_log_write_event(entry, pcr, event_type, digest_algo, digest, digest_len, data,
			     data_len);

	return 1;
}

void *tpm2_log_cbmem_init(void)
{
	static struct tpm_2_log_table *tclt;
	if (tclt)
		return tclt;

	if (ENV_HAS_CBMEM) {
		size_t tpm_log_len;
		struct tcg_efi_spec_id_event *hdr;

		tclt = cbmem_find(CBMEM_ID_TPM2_TCG_LOG);
		if (tclt)
			return tclt;

		tpm_log_len = sizeof(struct tpm_2_log_table) +
			      MAX_TPM_LOG_ENTRIES * sizeof(struct tpm_2_log_entry);
		tclt = cbmem_add(CBMEM_ID_TPM2_TCG_LOG, tpm_log_len);
		if (!tclt)
			return NULL;

		memset(tclt, 0, tpm_log_len);
		hdr = &tclt->header;

		hdr->event_type = htole32(EV_NO_ACTION);
		hdr->event_size = htole32(33 + sizeof(tclt->vendor));
		strcpy((char *)hdr->signature, TPM_20_SPEC_ID_EVENT_SIGNATURE);
		hdr->platform_class = htole32(0x00); // client platform
		hdr->spec_version_minor = 0x00;
		hdr->spec_version_major = 0x02;
		hdr->spec_errata = 0x00;
		hdr->uintn_size = 0x02; // 64-bit UINT
		hdr->num_of_algorithms = htole32(1);
		hdr->digest_sizes[0].alg_id = htole16(tpmalg_from_vb2_hash(TPM_MEASURE_ALGO));
		hdr->digest_sizes[0].digest_size = htole16(vb2_digest_size(TPM_MEASURE_ALGO));

		tclt->vendor_info_size = sizeof(tclt->vendor);
		tclt->vendor.reserved = 0;
		tclt->vendor.version_major = TPM_20_LOG_VI_MAJOR;
		tclt->vendor.version_minor = TPM_20_LOG_VI_MINOR;
		tclt->vendor.magic = htole32(TPM_20_LOG_VI_MAGIC);
		tclt->vendor.max_entries = htole16(MAX_TPM_LOG_ENTRIES);
		tclt->vendor.num_entries = htole16(0);
		/*
		 * Exported logs are a packed TCG_PCR_EVENT2 stream.
		 * Consumers walk entries by EventSize.
		 */
		tclt->vendor.entry_size = htole32(0);
	}

	return tclt;
}

void tpm2_log_dump(void)
{
	int i, j;
	struct tpm_2_log_table *tclt;
	int hash_size;
	const char *alg_name;

	tclt = tpm_log_init();
	if (!tclt)
		return;

	hash_size = vb2_digest_size(TPM_MEASURE_ALGO);
	alg_name = vb2_get_hash_algorithm_name(TPM_MEASURE_ALGO);

	printk(BIOS_INFO, "coreboot TPM 2.0 measurements:\n\n");
	for (i = 0; i < le16toh(tclt->vendor.num_entries); i++) {
		const uint8_t *entry = tpm2_log_get_entry_at(tclt, i);
		uint32_t data_length;

		if (!entry) {
			printk(BIOS_WARNING, "TPM LOG: malformed entry\n");
			break;
		}

		data_length = tpm2_log_entry_data_length(entry);
		printk(BIOS_INFO, " PCR-%u ",
		       tpm2_log_read_le32(entry, offsetof(struct tpm_2_log_entry, pcr)));

		for (j = 0; j < hash_size; j++)
			printk(BIOS_INFO, "%02x", tpm2_log_entry_digest(entry)[j]);

		printk(BIOS_INFO, " %s [%.*s]\n", alg_name, (int)data_length,
		       (const char *)tpm2_log_entry_data(entry));
	}
	printk(BIOS_INFO, "\n");
}

void tpm2_log_add_table_entry(const char *name, const uint32_t pcr,
			      enum vb2_hash_algorithm digest_algo, const uint8_t *digest,
			      const size_t digest_len)
{
	struct tpm_2_log_table *tclt;
	size_t name_len;

	if (!name) {
		printk(BIOS_WARNING, "TPM LOG: entry name not set\n");
		return;
	}

	tclt = tpm_log_init();
	if (!tclt) {
		printk(BIOS_WARNING, "TPM LOG: non-existent!\n");
		return;
	}

	name_len = strnlen(name, TPM_20_LOG_DATA_MAX_LENGTH - 1);

	if (tpm2_log_uses_fixed_entries(tclt))
		tpm2_log_append_fixed_event(tclt, pcr, EV_ACTION, digest_algo, digest,
					    digest_len, name, name_len);
	else if (ENV_HAS_CBMEM && tpm2_log_uses_packed_entries(tclt)) {
		uint8_t event_name[TPM_20_LOG_DATA_MAX_LENGTH];

		memcpy(event_name, name, name_len);
		event_name[name_len++] = '\0';
		tpm2_log_append_packed_event(tclt, pcr, EV_ACTION, digest_algo, digest,
					     digest_len, event_name, name_len);
	} else {
		printk(BIOS_WARNING, "TPM LOG: unknown entry layout\n");
	}
}

int tpm2_log_get(int entry_idx, int *pcr, const uint8_t **digest_data,
		 enum vb2_hash_algorithm *digest_algo, const char **event_name)
{
	struct tpm_2_log_table *tclt;
	const uint8_t *entry;

	tclt = tpm_log_init();
	if (!tclt)
		return 1;

	if (entry_idx < 0 || entry_idx >= le16toh(tclt->vendor.num_entries))
		return 1;

	entry = tpm2_log_get_entry_at(tclt, entry_idx);
	if (!entry)
		return 1;

	*pcr = tpm2_log_read_le32(entry, offsetof(struct tpm_2_log_entry, pcr));
	*digest_data = tpm2_log_entry_digest(entry);
	*digest_algo = TPM_MEASURE_ALGO; /* We validate algorithm on addition */
	*event_name = (const char *)tpm2_log_entry_data(entry);
	return 0;
}

uint16_t tpm2_log_get_size(const void *log_table)
{
	const struct tpm_2_log_table *tclt = log_table;
	return le16toh(tclt->vendor.num_entries);
}

void tpm2_preram_log_clear(void)
{
	printk(BIOS_INFO, "TPM LOG: clearing the log\n");
	/*
	 * Pre-RAM keeps fixed slots in CAR/SRAM. Entries are serialized into the
	 * packed CBMEM log when CBMEM is created.
	 */
	struct tpm_2_log_table *tclt = (struct tpm_2_log_table *)_tpm_log;
	tclt->vendor.max_entries = htole16(MAX_PRERAM_TPM_LOG_ENTRIES);
	tclt->vendor.num_entries = htole16(0);
	tclt->vendor.entry_size = htole32(sizeof(struct tpm_2_log_entry));
}

void tpm2_log_copy_entries(const void *from, void *to)
{
	const struct tpm_2_log_table *from_log = from;
	struct tpm_2_log_table *to_log = to;
	int i;

	if (!ENV_HAS_CBMEM)
		return;

	for (i = 0; i < le16toh(from_log->vendor.num_entries); i++) {
		const uint8_t *from_entry = tpm2_log_get_entry_at(from_log, i);
		uint32_t data_length;
		uint32_t event_type;
		uint32_t pcr;

		if (!from_entry) {
			printk(BIOS_WARNING, "TPM LOG: malformed entry\n");
			return;
		}

		pcr = tpm2_log_read_le32(from_entry, offsetof(struct tpm_2_log_entry, pcr));
		event_type = tpm2_log_read_le32(from_entry,
						offsetof(struct tpm_2_log_entry, event_type));
		data_length = tpm2_log_entry_data_length(from_entry);

		if (!tpm2_log_append_packed_event(to_log, pcr, event_type, TPM_MEASURE_ALGO,
						  tpm2_log_entry_digest(from_entry),
						  vb2_digest_size(TPM_MEASURE_ALGO),
						  tpm2_log_entry_data(from_entry), data_length))
			return;
	}
}
