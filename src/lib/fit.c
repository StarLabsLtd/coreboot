/* Taken from depthcharge: src/boot/fit.c */
/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <assert.h>
#include <boardid.h>
#include <bootmem.h>
#include <console/console.h>
#include <ctype.h>
#include <endian.h>
#include <fit.h>
#include <identity.h>
#include <memrange.h>
#include <program_loading.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <types.h>

static struct list_node image_nodes;
static struct list_node config_nodes;
static struct list_node compat_strings;

struct compat_string_entry {
	const char *compat_string;
	struct list_node list_node;
};

/* Convert string to lowercase and replace '_' and spaces with '-'. */
static char *clean_compat_string(char *str)
{
	for (size_t i = 0; i < strlen(str); i++) {
		str[i] = tolower(str[i]);
		if (str[i] == '_' || str[i] == ' ')
			str[i] = '-';
	}

	return str;
}

static void fit_add_default_compat_strings(void)
{
	char compat_string[80] = {};

	if ((board_id() != UNDEFINED_STRAPPING_ID) &&
	    (sku_id() != UNDEFINED_STRAPPING_ID)) {
		snprintf(compat_string, sizeof(compat_string),
			 "%s,%s-rev%u-sku%u", mainboard_vendor, mainboard_part_number,
			 board_id(), sku_id());

		fit_add_compat_string(compat_string);
	}

	if (board_id() != UNDEFINED_STRAPPING_ID) {
		snprintf(compat_string, sizeof(compat_string), "%s,%s-rev%u",
			 mainboard_vendor, mainboard_part_number, board_id());

		fit_add_compat_string(compat_string);
	}

	if (sku_id() != UNDEFINED_STRAPPING_ID) {
		snprintf(compat_string, sizeof(compat_string), "%s,%s-sku%u",
			 mainboard_vendor, mainboard_part_number, sku_id());

		fit_add_compat_string(compat_string);
	}

	snprintf(compat_string, sizeof(compat_string), "%s,%s",
		 mainboard_vendor, mainboard_part_number);

	fit_add_compat_string(compat_string);
}

static struct fit_image_node *find_image(const char *name)
{
	struct fit_image_node *image;
	list_for_each(image, image_nodes, list_node) {
		if (!strcmp(image->name, name))
			return image;
	}
	printk(BIOS_ERR, "Cannot find image node %s!\n", name);
	return NULL;
}

static struct fit_image_node *find_image_with_overlays(const char *name,
	int bytes, struct list_node *prev)
{
	struct fit_image_node *base = find_image(name);
	if (!base)
		return NULL;

	int len = strnlen(name, bytes) + 1;
	bytes -= len;
	name += len;
	while (bytes > 0) {
		struct fit_image_chain *next = xzalloc(sizeof(*next));
		next->image = find_image(name);
		if (!next->image)
			return NULL;
		list_insert_after(&next->list_node, prev);
		prev = &next->list_node;
		len = strnlen(name, bytes) + 1;
		bytes -= len;
		name += len;
	}

	return base;
}

static void image_node(struct device_tree_node *node, u32 addr_cells, void *fit_bin_base)
{
	/* External data starts after the four-byte-aligned FIT header. */
	size_t fit_fdt_size =
		ALIGN_UP(be32_to_cpu(((const struct fdt_header *)fit_bin_base)->totalsize), 4);
	struct fit_image_node *image = xzalloc(sizeof(*image));

	image->compression = CBFS_COMPRESS_NONE;
	image->name = node->name;

	struct device_tree_property *prop;
	list_for_each(prop, node->properties, list_node) {
		if (!strcmp("data-offset", prop->prop.name)) {
			u64 offset = fdt_read_int_prop(&prop->prop, 0);

			if (fit_fdt_size > UINTPTR_MAX - (uintptr_t)fit_bin_base ||
			    offset > UINTPTR_MAX - (uintptr_t)fit_bin_base - fit_fdt_size)
				image->data = NULL;
			else
				image->data = (void *)(uintptr_t)((uintptr_t)fit_bin_base +
							  fit_fdt_size + offset);
		} else if (!strcmp("data-size", prop->prop.name)) {
			image->size = fdt_read_int_prop(&prop->prop, 0);
		} else if (!strcmp("data", prop->prop.name)) {
			image->data = prop->prop.data;
			image->size = prop->prop.size;
		} else if (!strcmp("compression", prop->prop.name)) {
			if (!strcmp("none", prop->prop.data))
				image->compression = CBFS_COMPRESS_NONE;
			else if (!strcmp("lzma", prop->prop.data))
				image->compression = CBFS_COMPRESS_LZMA;
			else if (!strcmp("lz4", prop->prop.data))
				image->compression = CBFS_COMPRESS_LZ4;
			else
				image->compression = -1;
		} else if (!strcmp("uncomp-size", prop->prop.name)) {
			image->uncompressed_size = fdt_read_int_prop(&prop->prop, 1);
		} else if (!strcmp("load", prop->prop.name)) {
			image->load_address = fdt_read_int_prop(&prop->prop, addr_cells);
		} else if (!strcmp("entry", prop->prop.name)) {
			image->entrypoint_address = fdt_read_int_prop(&prop->prop, addr_cells);
		}
	}

	list_insert_after(&image->list_node, &image_nodes);
}

static void config_node(struct device_tree_node *node)
{
	struct fit_config_node *config = xzalloc(sizeof(*config));
	config->name = node->name;

	struct device_tree_property *prop;
	list_for_each(prop, node->properties, list_node) {
		if (!strcmp("firmware", prop->prop.name))
			config->firmware = find_image(prop->prop.data);
		else if (!strcmp("kernel", prop->prop.name))
			config->kernel = find_image(prop->prop.data);
		else if (!strcmp("fdt", prop->prop.name))
			config->fdt = find_image_with_overlays(prop->prop.data,
				prop->prop.size, &config->overlays);
		else if (!strcmp("ramdisk", prop->prop.name))
			config->ramdisk = find_image(prop->prop.data);
		else if (!strcmp("compatible", prop->prop.name))
			config->compat = prop->prop;
	}

	list_insert_after(&config->list_node, &config_nodes);
}

static struct device_tree *fit_unpack(void *fit, const char **default_config)
{
	struct device_tree *tree = fdt_unflatten(fit);
	if (!tree)
		return NULL;

	u32 addr_cells;
	struct device_tree_node *child;
	struct device_tree_node *images = dt_find_node_by_path(tree, "/images",
							       &addr_cells, NULL, 0);
	if (images)
		list_for_each(child, images->children, list_node)
			image_node(child, addr_cells, fit);

	struct device_tree_node *configs = dt_find_node_by_path(tree,
		"/configurations", NULL, NULL, 0);
	if (configs) {
		*default_config = dt_find_string_prop(configs, "default");
		list_for_each(child, configs->children, list_node)
			config_node(child);
	}

	return tree;
}

static int fdt_find_compat(const void *blob, uint32_t start_offset,
			   struct fdt_property *prop)
{
	int offset = start_offset;
	int size;

	size = fdt_next_node_name(blob, offset, NULL);
	if (!size)
		return -1;
	offset += size;

	while ((size = fdt_next_property(blob, offset, prop))) {
		if (!strcmp("compatible", prop->name))
			return 0;

		offset += size;
	}

	prop->name = NULL;
	return -1;
}

static int fit_check_compat(struct fdt_property *compat_prop,
			    const char *compat_name)
{
	int bytes = compat_prop->size;
	const char *compat_str = compat_prop->data;

	for (int pos = 0; bytes && compat_str[0]; pos++) {
		if (!strncmp(compat_str, compat_name, bytes))
			return pos;
		int len = strlen(compat_str) + 1;
		compat_str += len;
		bytes -= len;
	}
	return -1;
}

void fit_update_chosen(struct device_tree *tree, const char *cmd_line)
{
	const char *path[] = { "chosen", NULL };
	struct device_tree_node *node;
	node = dt_find_node(tree->root, path, NULL, NULL, 1);

	dt_add_string_prop(node, "bootargs", cmd_line);
}

void fit_add_ramdisk(struct device_tree *tree, void *ramdisk_addr,
		     size_t ramdisk_size)
{
	const char *path[] = { "chosen", NULL };
	struct device_tree_node *node;
	node = dt_find_node(tree->root, path, NULL, NULL, 1);

	u64 start = (uintptr_t)ramdisk_addr;
	u64 end = start + ramdisk_size;

	dt_add_u64_prop(node, "linux,initrd-start", start);
	dt_add_u64_prop(node, "linux,initrd-end", end);
}

static void update_reserve_map(uint64_t start, uint64_t end,
			       struct device_tree *tree)
{
	struct device_tree_reserve_map_entry *entry = xzalloc(sizeof(*entry));

	entry->start = start;
	entry->size = end - start;

	list_insert_after(&entry->list_node, &tree->reserve_map);
}

struct entry_params {
	unsigned int addr_cells;
	unsigned int size_cells;
	void *data;
};

static uint64_t max_range(unsigned int size_cells)
{
	/*
	 * Split up ranges who's sizes are too large to fit in #size-cells.
	 * The largest value we can store isn't a power of two, so we'll round
	 * down to make the math easier.
	 */
	return 0x1ULL << (size_cells * 32 - 1);
}

static void update_mem_property(u64 start, u64 end, struct entry_params *params)
{
	u8 *data = (u8 *)params->data;
	u64 full_size = end - start;
	while (full_size) {
		const u64 max_size = max_range(params->size_cells);
		const u64 size = MIN(max_size, full_size);

		dt_write_int(data, start, params->addr_cells * sizeof(u32));
		data += params->addr_cells * sizeof(uint32_t);
		start += size;

		dt_write_int(data, size, params->size_cells * sizeof(u32));
		data += params->size_cells * sizeof(uint32_t);
		full_size -= size;
	}
	params->data = data;
}

struct mem_map {
	struct memranges mem;
	struct memranges reserved;
};

static bool walk_memory_table(const struct range_entry *r, void *arg)
{
	struct mem_map *arg_map = arg;
	struct memranges *ranges;
	enum bootmem_type tag;

	ranges = range_entry_tag(r) == BM_MEM_RAM ? &arg_map->mem : &arg_map->reserved;
	tag = range_entry_tag(r) == BM_MEM_RAM ? BM_MEM_RAM : BM_MEM_RESERVED;
	memranges_insert(ranges, range_entry_base(r), range_entry_size(r), tag);
	return true;
}

void fit_add_compat_string(const char *str)
{
	struct compat_string_entry *compat_node;

	compat_node = xzalloc(sizeof(*compat_node));
	compat_node->compat_string = strdup(str);

	clean_compat_string((char *)compat_node->compat_string);

	list_insert_after(&compat_node->list_node, &compat_strings);
}

void fit_update_memory(struct device_tree *tree)
{
	const struct range_entry *r;
	struct device_tree_node *node;
	u32 addr_cells = 1, size_cells = 1;
	struct mem_map map;

	printk(BIOS_INFO, "FIT: Updating devicetree memory entries\n");

	dt_read_cell_props(tree->root, &addr_cells, &size_cells);

	/*
	 * First remove all existing device_type="memory" nodes, then add ours.
	 */
	list_for_each(node, tree->root->children, list_node) {
		const char *devtype = dt_find_string_prop(node, "device_type");
		if (devtype && !strcmp(devtype, "memory"))
			list_remove(&node->list_node);
	}

	node = xzalloc(sizeof(*node));

	node->name = "memory";
	list_insert_after(&node->list_node, &tree->root->children);
	dt_add_string_prop(node, "device_type", (char *)"memory");

	memranges_init_empty(&map.mem, NULL, 0);
	memranges_init_empty(&map.reserved, NULL, 0);

	bootmem_walk_os_mem(walk_memory_table, &map);

	/* CBMEM regions are both carved out and explicitly reserved. */
	memranges_each_entry(r, &map.reserved) {
		update_reserve_map(range_entry_base(r), range_entry_end(r),
				   tree);
	}

	/*
	 * Count the amount of 'reg' entries we need (account for size limits).
	 */
	size_t count = 0;
	memranges_each_entry(r, &map.mem) {
		uint64_t size = range_entry_size(r);
		uint64_t max_size = max_range(size_cells);
		count += DIV_ROUND_UP(size, max_size);
	}

	/* Allocate the right amount of space and fill up the entries. */
	size_t length = count * (addr_cells + size_cells) * sizeof(u32);

	void *data = xzalloc(length);

	struct entry_params add_params = { addr_cells, size_cells, data };
	memranges_each_entry(r, &map.mem) {
		update_mem_property(range_entry_base(r), range_entry_end(r),
				    &add_params);
	}
	assert(add_params.data - data == length);

	/* Assemble the final property and add it to the device tree. */
	dt_add_bin_prop(node, "reg", data, length);

	memranges_teardown(&map.mem);
	memranges_teardown(&map.reserved);
}

/*
 * Finds a compat string and updates the compat position and rank.
 * @param config The current config node to operate on
 * @return 0 if compat updated, -1 if this FDT cannot be used.
 */
static int fit_update_compat(struct fit_config_node *config)
{
	/* If there was no "compatible" property in config node, this is a
	   legacy FIT image. Must extract compat prop from FDT itself. */
	if (!config->compat.name) {
		if (!config->fdt) {
			printk(BIOS_WARNING, "config %s has no FDT. Only the default "
			       "config can be considered without FDT compat.\n",
			       config->name);
			return -1;
		}

		void *fdt_blob = config->fdt->data;
		const struct fdt_header *fdt_header = fdt_blob;
		uint32_t fdt_offset = be32_to_cpu(fdt_header->structure_offset);

		if (config->fdt->compression != CBFS_COMPRESS_NONE) {
			printk(BIOS_ERR, "config %s has a compressed FDT without "
			       "external compatible property, skipping.\n",
			       config->name);
			return -1;
		}

		/* FDT overlays are not supported in legacy FIT images. */
		if (!list_is_empty(&config->overlays)) {
			printk(BIOS_ERR, "config %s has overlay but no compat!\n",
			       config->name);
			return -1;
		}

		if (fdt_find_compat(fdt_blob, fdt_offset, &config->compat)) {
			printk(BIOS_ERR, "Can't find compat string in FDT %s "
			       "for config %s, skipping.\n",
			       config->fdt->name, config->name);
			return -1;
		}
	}

	config->compat_pos = -1;
	config->compat_rank = -1;
	size_t i = 0;
	struct compat_string_entry *compat_node;
	list_for_each(compat_node, compat_strings, list_node) {
		int pos = fit_check_compat(&config->compat,
					   compat_node->compat_string);
		if (pos >= 0) {
			config->compat_pos = pos;
			config->compat_rank = i;
			config->compat_string =
				compat_node->compat_string;
		}
		i++;
	}

	return 0;
}

struct fit_config_node *fit_load(void *fit)
{
	struct fit_image_node *image;
	struct fit_config_node *config;
	struct compat_string_entry *compat_node;
	struct fit_image_chain *overlay_chain;

	printk(BIOS_DEBUG, "FIT: Loading FIT from %p\n", fit);

	const char *default_config_name = NULL;

	struct device_tree *tree = fit_unpack(fit, &default_config_name);
	if (!tree) {
		printk(BIOS_ERR, "Failed to unflatten FIT image!\n");
		return NULL;
	}

	struct fit_config_node *default_config = NULL;
	struct fit_config_node *compat_config = NULL;

	/* List the images we found. */
	list_for_each(image, image_nodes, list_node)
		printk(BIOS_DEBUG, "FIT: Image %s has %d bytes.\n", image->name,
		       image->size);

	fit_add_default_compat_strings();

	printk(BIOS_DEBUG, "FIT: Compat preference "
	       "(lowest to highest priority) :");

	list_for_each(compat_node, compat_strings, list_node) {
		printk(BIOS_DEBUG, " %s", compat_node->compat_string);
	}
	printk(BIOS_DEBUG, "\n");
	/* Process and list the configs. */
	list_for_each(config, config_nodes, list_node) {
		if (!config->kernel && !config->firmware) {
			printk(BIOS_ERR, "config %s has no kernel or firmware, skipping.\n",
			       config->name);
			continue;
		}
		if (config->kernel && !config->fdt) {
			printk(BIOS_ERR, "config %s has no FDT, skipping. "
			       "This is required by a kernel.\n", config->name);
			continue;
		}

		if (config->ramdisk &&
		    config->ramdisk->compression < 0) {
			printk(BIOS_WARNING, "Ramdisk is compressed with "
			       "an unsupported algorithm, discarding config %s."
			       "\n", config->name);
			continue;
		}

		if (default_config_name && !strcmp(config->name, default_config_name))
			default_config = config;

		/* The default config is a fallback. It can be considered without compat strings,
		   which means that an FDT is not required. */
		if (fit_update_compat(config) && config != default_config)
			continue;

		printk(BIOS_DEBUG, "FIT: config %s", config->name);
		if (config == default_config)
			printk(BIOS_DEBUG, " (default)");
		if (config->firmware) {
			printk(BIOS_DEBUG, ", firmware %s", config->firmware->name);

			/* Insert firmware's secondary images. */
			struct fit_image_chain *next;
			list_for_each(image, image_nodes, list_node) {
				if (strcmp(image->name, config->firmware->name) != 0) {
					next = xzalloc(sizeof(*next));
					next->image = image;
					list_insert_after(&next->list_node, &config->secondary_images);
				}
			}
		}
		if (config->kernel)
			printk(BIOS_DEBUG, ", kernel %s", config->kernel->name);
		if (config->fdt)
			printk(BIOS_DEBUG, ", fdt %s", config->fdt->name);
		list_for_each(overlay_chain, config->overlays, list_node)
			printk(BIOS_DEBUG, " %s", overlay_chain->image->name);
		if (config->ramdisk)
			printk(BIOS_DEBUG, ", ramdisk %s",
			       config->ramdisk->name);
		if (config->compat.name) {
			printk(BIOS_DEBUG, ", compat");
			int bytes = config->compat.size;
			const char *compat_str = config->compat.data;
			for (int pos = 0; bytes && compat_str[0]; pos++) {
				printk(BIOS_DEBUG, " %s", compat_str);
				if (pos == config->compat_pos)
					printk(BIOS_DEBUG, " (match)");
				int len = strlen(compat_str) + 1;
				compat_str += len;
				bytes -= len;
			}

			if (config->compat_rank >= 0 && (!compat_config ||
			    config->compat_rank > compat_config->compat_rank))
				compat_config = config;
		}
		printk(BIOS_DEBUG, "\n");
	}

	struct fit_config_node *to_boot = NULL;
	if (compat_config) {
		to_boot = compat_config;
		printk(BIOS_INFO, "FIT: Choosing best match %s for compat "
		       "%s.\n", to_boot->name, to_boot->compat_string);
	} else if (default_config) {
		to_boot = default_config;
		printk(BIOS_INFO, "FIT: No match, choosing default %s.\n",
		       to_boot->name);
	} else {
		printk(BIOS_ERR, "FIT: No compatible or default configs. "
		       "Giving up.\n");
		return NULL;
	}

	return to_boot;
}
