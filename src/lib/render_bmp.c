/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <boot/coreboot_tables.h>
#include <bootmode.h>
#include <bootsplash.h>
#include <bootstate.h>
#include <console/console.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <symbols.h>

#include "render_bmp.h"

#define MAX_SPLASH_TEXT_WIDTH 32

static struct lb_boot_splash boot_splash_handoff;
static bool boot_splash_handoff_valid;

bool bootsplash_publish_handoff(uintptr_t framebuffer_address,
				uint32_t framebuffer_width,
				uint32_t framebuffer_height,
				uint32_t image_offset_x,
				uint32_t image_offset_y,
				uint32_t image_width,
				uint32_t image_height,
				const void *bmp,
				size_t bmp_size)
{
	if (boot_splash_handoff_valid || !framebuffer_address ||
		!framebuffer_width || !framebuffer_height || !image_width ||
		!image_height || !bmp || !bmp_size || bmp_size > UINT32_MAX ||
		image_offset_x > framebuffer_width ||
		image_offset_y > framebuffer_height ||
		image_width > framebuffer_width - image_offset_x ||
		image_height > framebuffer_height - image_offset_y)
		return false;

	boot_splash_handoff = (struct lb_boot_splash) {
		.tag = LB_TAG_BOOT_SPLASH,
		.size = sizeof(boot_splash_handoff),
		.revision = LB_BOOT_SPLASH_REVISION,
		.flags = LB_BOOT_SPLASH_FLAG_DISPLAYED | LB_BOOT_SPLASH_FLAG_BMP,
		.framebuffer_address = framebuffer_address,
		.image_offset_x = image_offset_x,
		.image_offset_y = image_offset_y,
		.image_width = image_width,
		.image_height = image_height,
		.bmp_address = (uintptr_t)bmp,
		.bmp_size = bmp_size,
	};
	boot_splash_handoff_valid = true;
	return true;
}

bool bootsplash_get_handoff(struct lb_boot_splash *handoff)
{
	if (!handoff || !boot_splash_handoff_valid)
		return false;

	*handoff = boot_splash_handoff;
	return true;
}

/* Validate the complete source layout before allocating or reading any pixels. */
static bool validate_bmp(const struct bmp_image_header *header, size_t logo_size,
	size_t *row_size, size_t *blt_size, size_t *palette_size)
{
	uint64_t row_bytes, pixel_bytes, buffer_bytes;

	if (header->CharB != 'B' || header->CharM != 'M' ||
	    header->Size != logo_size || header->ImageOffset > logo_size ||
	    header->ImageOffset < sizeof(*header) ||
	    header->HeaderSize != sizeof(*header) - offsetof(struct bmp_image_header, HeaderSize) ||
	    header->Planes != 1 || header->CompressionType != 0 ||
	    !header->PixelWidth || !header->PixelHeight ||
	    header->PixelWidth > INT32_MAX || header->PixelHeight > INT32_MAX)
		return false;

	switch (header->BitPerPixel) {
	case 1:
	case 4:
	case 8:
		*palette_size = header->NumberOfColors ? header->NumberOfColors :
			1U << header->BitPerPixel;
		if (*palette_size > (1U << header->BitPerPixel) ||
		    *palette_size > (header->ImageOffset - sizeof(*header)) /
				sizeof(struct bmp_color_map))
			return false;
		break;
	case 24:
	case 32:
		*palette_size = 0;
		break;
	default:
		return false;
	}

	row_bytes = (((uint64_t)header->PixelWidth * header->BitPerPixel + 31) / 32) * 4;
	pixel_bytes = row_bytes * header->PixelHeight;
	buffer_bytes = (uint64_t)header->PixelWidth * header->PixelHeight * sizeof(struct blt_pixel);
	if (row_bytes > SIZE_MAX || buffer_bytes > SIZE_MAX ||
	    pixel_bytes > logo_size - header->ImageOffset ||
	    (header->ImageSize && (header->ImageSize < pixel_bytes ||
				header->ImageSize > logo_size - header->ImageOffset)))
		return false;

	*row_size = row_bytes;
	*blt_size = buffer_bytes;
	return true;
}

/*
 * Visual Representation of the Flipping:
 *
 * Original BMP Image:
 *
 *      0 -----------------------------------------PixelWidth-1
 *      |                                          |
 *      |                                          |
 *      |                                          |
 *      PixelHeight-1------------------------------|
 *
 * Blitting Region:
 *
 *      (x, y) ----------------------------------- (x + blt_width, y)
 *      |                                          |
 *      |                                          |
 *      (x, y + blt_height) -------------------- --(x + blt_width, y + blt_height)
 *
 * Flipped Coordinates:
 *
 * flipped_x:  Represents the horizontal coordinate from the `right` edge of the BMP,
 *             accounting for the blit width.
 *
 * flipped_y:  Represents the vertical coordinate from the `bottom` edge of the BMP,
 *             accounting for the blit height.
 *
 * Example (Orientation: LB_FB_ORIENTATION_BOTTOM_UP):
 *
 * If blt_width is small, flipped_x will be near PixelWidth.
 * If blt_height is small, flipped_y will be near PixelHeight.
 *
 * The blit then starts at (flipped_x, blt_height), effectively flipping the
 * horizontal position and using the original blit height.
 *
 * This allows for blitting from the bottom-left corner of the display panel.
 */
static bool calculate_adj_height_width(const struct bmp_image_header *header, size_t *adjusted_x,
	 size_t *adjusted_y, enum lb_fb_orientation orientation, int blt_width, int blt_height)
{
	size_t flipped_x = header->PixelWidth - blt_width - 1;
	size_t flipped_y = header->PixelHeight - blt_height - 1;

	switch (orientation) {
	case LB_FB_ORIENTATION_LEFT_UP:
		*adjusted_x = flipped_y;
		*adjusted_y = flipped_x;
		break;

	case LB_FB_ORIENTATION_BOTTOM_UP:
		*adjusted_x = flipped_x;
		*adjusted_y = blt_height;
		break;

	case LB_FB_ORIENTATION_RIGHT_UP:
		*adjusted_x = blt_height;
		*adjusted_y = blt_width;
		break;

	case LB_FB_ORIENTATION_NORMAL:
	default:
		*adjusted_x = blt_width;
		*adjusted_y = flipped_y;
		break;
	}

	return true;
}

/*
 * Visual Representation of gop_width and calculate linear offset into the GOP buffer:
 *
 * GOP Display Buffer:
 *
 *      +------------------------------------------------+
 *      | 0                                              |
 *      | |--> gop_width pixels in this row              |
 *      +------------------------------------------------+
 *      | gop_width                                      |
 *      |                                                |
 *      | gop_width                                      |
 *      | ...                                            |
 *      +------------------------------------------------+
 *      | gop_width                                      |
 *      +------------------------------------------------+
 *
 * gop_width: Represents the width of a row in the GOP buffer.
 * - It is determined by the `orientation` of the image.
 * - For `LEFT_UP` and `RIGHT_UP`, the GOP width is the BMP image's `PixelHeight`.
 * - For `BOTTOM_UP` and `NORMAL`, the GOP width is the BMP image's `PixelWidth`.
 *
 * gop_x, gop_y: Coordinates within the GOP buffer where the blit will start.
 * - These are calculated by `calculate_adj_height_width` based on the `orientation`,
 * `blt_width`, and `blt_height`.
 *
 * Pointer to the calculated pixel in the GOP blit buffer, or NULL on error.
 * - Calculate `gop_blt_offset`as `gop_y * gop_width + gop_x` to determine the offset
 *   to access the correct pixel within `gop_blt_buffer`.
 * - `gop_y * gop_width` calculates the offset of the row.
 * - `gop_x` calculates the offset within that row.
 */
static struct blt_pixel *get_gop_blt_pixel(
	struct blt_pixel *gop_blt_buffer, const struct bmp_image_header *header,
	 int blt_width, int blt_height, enum lb_fb_orientation orientation)
{
	size_t gop_x;
	size_t gop_y;
	size_t gop_width;

	switch (orientation) {
	case LB_FB_ORIENTATION_LEFT_UP:
	case LB_FB_ORIENTATION_RIGHT_UP:
		gop_width = header->PixelHeight;
		break;

	case LB_FB_ORIENTATION_BOTTOM_UP:
	case LB_FB_ORIENTATION_NORMAL:
	default:
		gop_width = header->PixelWidth;
		break;
	}

	if (!calculate_adj_height_width(header, &gop_x, &gop_y, orientation,
			 blt_width, blt_height))
		return NULL;

	/*
	 * Calculated pixel in the Graphics Output Protocol (GOP) blit buffer.
	 * This offset represents the starting position where the blitted region will be placed
	 * in the framebuffer.
	 */
	return &gop_blt_buffer[gop_y * gop_width + gop_x];
}

/*
 * Fill BMP image into BLT buffer format with optional orientation
 *
 * +------------------------------------------------------------------+
 * |  BMP Image (header: bmp_image_header)                        |
 * |  (PixelWidth, PixelHeight)                                       |
 * +------------------------------------------------------------------+
 *        |
 *        | (blt_width, blt_height) : Region to blit
 *        V
 * +------------------------------------------------------------------+
 * |  calculate_adj_height_width(header, adjusted_x, adjusted_y,      |
 * |                               orientation, blt_width, blt_height)|
 * +------------------------------------------------------------------+
 *        |
 *        | Calculates adjusted coordinates based on orientation:
 *        |   - flipped_x = header->PixelWidth - blt_width - 1
 *        |   - flipped_y = header->PixelHeight - blt_height - 1
 *        |   - Depending on 'orientation', adjusted_x and adjusted_y are set.
 *        V
 * +-------------------------------------------------------------------+
 * |  get_gop_blt_pixel(gop_blt_buffer, header, blt_width, blt_height, |
 * |                                                  orientation)     |
 * +-------------------------------------------------------------------+
 *        |
 *        | 1. Determines gop_width based on orientation:
 *        |    - LEFT_UP/RIGHT_UP: gop_width = header->PixelHeight,
 *        |    - BOTTOM_UP/NORMAL: gop_width = header->PixelWidth,
 *        | 2. Calls calculate_adj_height_width to get gop_x and gop_y.
 *        | 3. GOP BLT offset: calculates linear offset into the GOP buffer
 *        |    - gop_blt_offset = gop_y * gop_width + gop_x
 *        | 4. GOP BLT offset is used to access the correct regions in:
 *        V
 * +------------------------------------------------------------------+
 * |  GOP Blit Buffer (Framebuffer)                                   |
 * +------------------------------------------------------------------+
 */
static void *fill_blt_buffer(const struct bmp_image_header *header,
	uintptr_t logo, size_t blt_buffer_size, size_t row_size, size_t palette_size,
	enum lb_fb_orientation orientation)
{
	struct blt_pixel *buffer = malloc(blt_buffer_size);
	const struct bmp_color_map *palette = (const void *)(logo + sizeof(*header));

	if (!buffer)
		return NULL;

	for (uint32_t y = 0; y < header->PixelHeight; y++) {
		const uint8_t *row = (const void *)(logo + header->ImageOffset + y * row_size);

		for (uint32_t x = 0; x < header->PixelWidth; x++) {
			struct blt_pixel *pixel = get_gop_blt_pixel(buffer, header, x, y,
								 orientation);
			size_t index = 0;

			switch (header->BitPerPixel) {
			case 1:
				index = (row[x / 8] >> (7 - x % 8)) & 1;
				break;
			case 4:
				index = (row[x / 2] >> (x % 2 ? 0 : 4)) & 0xf;
				break;
			case 8:
				index = row[x];
				break;
			case 24:
			case 32: {
				const uint8_t *source = row + (size_t)x * (header->BitPerPixel / 8);

				*pixel = (struct blt_pixel) {
					.Blue = source[0], .Green = source[1], .Red = source[2],
				};
				continue;
			}
			}
			if (index >= palette_size) {
				free(buffer);
				return NULL;
			}
			*pixel = (struct blt_pixel) {
				.Blue = palette[index].Blue,
				.Green = palette[index].Green,
				.Red = palette[index].Red,
			};
		}
	}

	return buffer;
}

/* Convert a BMP to an owned BGRX buffer. All outputs remain zero on failure. */
bool convert_bmp_to_blt(uintptr_t logo, size_t logo_size,
	uintptr_t *blt, size_t *blt_size, uint32_t *pixel_height, uint32_t *pixel_width,
	enum lb_fb_orientation orientation)
{
	const struct bmp_image_header *header = (const void *)logo;
	size_t row_size, buffer_size, palette_size;
	uintptr_t buffer;
	bool standard_orientation;

	if (blt)
		*blt = 0;
	if (blt_size)
		*blt_size = 0;
	if (pixel_height)
		*pixel_height = 0;
	if (pixel_width)
		*pixel_width = 0;
	if (!blt || !blt_size || !pixel_height || !pixel_width ||
	    !logo || logo_size < sizeof(*header) || logo_size > UINTPTR_MAX - logo ||
	    orientation < LB_FB_ORIENTATION_NORMAL || orientation > LB_FB_ORIENTATION_RIGHT_UP)
		return false;
	if (!validate_bmp(header, logo_size, &row_size, &buffer_size, &palette_size))
		return false;

	buffer = (uintptr_t)fill_blt_buffer(header, logo, buffer_size, row_size,
					  palette_size, orientation);
	if (!buffer)
		return false;

	standard_orientation = orientation == LB_FB_ORIENTATION_NORMAL ||
		orientation == LB_FB_ORIENTATION_BOTTOM_UP;
	*blt = buffer;
	*blt_size = buffer_size;
	*pixel_height = standard_orientation ? header->PixelHeight : header->PixelWidth;
	*pixel_width = standard_orientation ? header->PixelWidth : header->PixelHeight;
	return true;
}

bool load_and_convert_bmp_to_blt(uintptr_t *logo, size_t *logo_size,
	uintptr_t *blt, size_t *blt_size, uint32_t *pixel_height, uint32_t *pixel_width,
	enum lb_fb_orientation orientation)
{
	uintptr_t bmp = 0;
	size_t bmp_size = 0;

	if (logo)
		*logo = 0;
	if (logo_size)
		*logo_size = 0;
	/* Initialize every supplied output even when another output is missing. */
	convert_bmp_to_blt(0, 0, blt, blt_size, pixel_height, pixel_width, orientation);
	if (!logo || !logo_size || !blt || !blt_size || !pixel_height || !pixel_width)
		return false;

	bmp = (uintptr_t)bmp_load_logo(&bmp_size);
	if (!convert_bmp_to_blt(bmp, bmp_size, blt, blt_size,
			       pixel_height, pixel_width, orientation)) {
		bmp_release_logo();
		return false;
	}

	*logo = bmp;
	*logo_size = bmp_size;
	return true;
}

/*
 * Calculates the destination coordinates for the logo based on both horizontal and
 * vertical alignment settings.
 *
 * horizontal_resolution: The horizontal resolution of the display panel.
 * vertical_resolution: The vertical resolution of the display panel.
 * logo_width: The width of the logo bitmap.
 * logo_height: The height of the logo bitmap.
 * halignment: The horizontal alignment setting. Use FW_SPLASH_HALIGNMENT_NONE
 *             if only vertical alignment is specified.
 * valignment: The vertical alignment setting. Use FW_SPLASH_VALIGNMENT_NONE
 *             if only horizontal alignment is specified.
 *
 * Returning `struct logo_coordinates` that contains the calculated x and y coordinates
 * for rendering the logo.
 */
struct logo_coordinates calculate_logo_coordinates(
	uint32_t horizontal_resolution, uint32_t vertical_resolution,
	uint32_t logo_width, uint32_t logo_height,
	enum fw_splash_horizontal_alignment halignment,
	enum fw_splash_vertical_alignment valignment)
{
	struct logo_coordinates coords;

	/* Calculate X coordinate */
	switch (halignment) {
	case FW_SPLASH_HALIGNMENT_LEFT:
		coords.x = 0;
		break;
	case FW_SPLASH_HALIGNMENT_RIGHT:
		coords.x = horizontal_resolution - logo_width;
		break;
	default: /* FW_SPLASH_HALIGNMENT_CENTER (default) */
		coords.x = (horizontal_resolution - logo_width) / 2;
		break;
	}

	/* Calculate Y coordinate */
	switch (valignment) {
	case FW_SPLASH_VALIGNMENT_MIDDLE:
		coords.y = vertical_resolution / 2;
		break;
	case FW_SPLASH_VALIGNMENT_TOP:
		coords.y = 0;
		break;
	case FW_SPLASH_VALIGNMENT_BOTTOM:
		coords.y = vertical_resolution - logo_height;
		break;
	default: /* FW_SPLASH_VALIGNMENT_CENTER (default) */
		coords.y = (vertical_resolution - logo_height) / 2;
		break;
	}

	return coords;
}

/*
 * Copies the logo to the framebuffer.
 *
 * framebuffer_base: The base address of the framebuffer.
 * bytes_per_scanline: The number of bytes per scanline in the framebuffer.
 * logo_buffer: The address of the logo data in BLT format.
 * logo_width: The width of the logo bitmap.
 * logo_height: The height of the logo bitmap.
 * dest_x: The destination x-coordinate in the framebuffer for rendering the logo.
 * dest_y: The destination y-coordinate in the framebuffer for rendering the logo.
 */
static void copy_logo_to_framebuffer(
	uintptr_t framebuffer_base, uint32_t bytes_per_scanline,
	uintptr_t logo_buffer, uint32_t logo_width, uint32_t logo_height,
	uint32_t dest_x, uint32_t dest_y)
{
	size_t pixel_size = sizeof(struct blt_pixel);
	size_t bytes_per_logo_line = (size_t)logo_width * pixel_size;
	uint8_t *framebuffer_offset = (uint8_t *)framebuffer_base + (size_t)dest_y * bytes_per_scanline
					 + (size_t)dest_x * pixel_size;
	uint8_t *dst_row_address = framebuffer_offset;
	uint8_t *src_row_address = (uint8_t *)logo_buffer;
	for (uint32_t i = 0; i < logo_height; i++) {
		memcpy(dst_row_address + (size_t)i * bytes_per_scanline,
		       src_row_address + (size_t)i * bytes_per_logo_line, bytes_per_logo_line);
	}
}

/*
 * Adjust logo layout based on the panel orientation.
 *
 * logo_type: Logo type.
 * config: Logo configuration information.
 * logo_halignment: Resultant horizontal alignment setting.
 * logo_valignment: Resultant vertical alignment setting.
 * logo_bottom_margin: Resultant bottom margin.
 */
static void get_logo_layout(
	enum bootsplash_type logo_type,
	struct logo_config *config,
	enum fw_splash_horizontal_alignment *logo_halignment,
	enum fw_splash_vertical_alignment *logo_valignment,
	uint8_t *logo_bottom_margin
)
{
	if (!config || !logo_halignment || !logo_valignment || !logo_bottom_margin)
		return;

	if (logo_type == BOOTSPLASH_LOW_BATTERY || logo_type == BOOTSPLASH_CENTER ||
			logo_type == BOOTSPLASH_OFF_MODE_CHARGING) {
		*logo_halignment = config->halignment;
		*logo_valignment = config->valignment;
		/* Override logo alignment if the default screen orientation is not normal */
		if (config->panel_orientation != LB_FB_ORIENTATION_NORMAL)
			*logo_valignment = FW_SPLASH_VALIGNMENT_CENTER;
		*logo_bottom_margin = 0;
	} else if (logo_type == BOOTSPLASH_FOOTER) {
		*logo_halignment = FW_SPLASH_HALIGNMENT_CENTER;
		*logo_valignment = FW_SPLASH_VALIGNMENT_CENTER;
		switch (config->panel_orientation) {
		case LB_FB_ORIENTATION_RIGHT_UP:
			*logo_halignment = FW_SPLASH_HALIGNMENT_LEFT;
			break;
		case LB_FB_ORIENTATION_LEFT_UP:
			*logo_halignment = FW_SPLASH_HALIGNMENT_RIGHT;
			break;
		case LB_FB_ORIENTATION_BOTTOM_UP:
			*logo_valignment = FW_SPLASH_VALIGNMENT_TOP;
			break;
		default: /* LB_FB_ORIENTATION_NORMAL (default) */
			*logo_valignment = FW_SPLASH_VALIGNMENT_BOTTOM;
			break;
		}
		*logo_bottom_margin = config->logo_bottom_margin;
	} else { // Default values
		*logo_halignment = FW_SPLASH_HALIGNMENT_CENTER;
		*logo_valignment = FW_SPLASH_VALIGNMENT_CENTER;
		*logo_bottom_margin = 0;
	}
}

/* Validate the accessible BGRX8888 geometry before any native framebuffer write. */
static bool valid_logo_framebuffer(const struct logo_config *config)
{
	uint64_t span;

	if (!config || !config->framebuffer_base || !config->horizontal_resolution ||
	    !config->vertical_resolution || config->bytes_per_scanline % sizeof(struct blt_pixel) ||
	    (uint64_t)config->horizontal_resolution * sizeof(struct blt_pixel) >
		config->bytes_per_scanline ||
	    config->panel_orientation < LB_FB_ORIENTATION_NORMAL ||
	    config->panel_orientation > LB_FB_ORIENTATION_RIGHT_UP)
		return false;
	span = (uint64_t)config->vertical_resolution * config->bytes_per_scanline;
	return span <= SIZE_MAX && span <= UINTPTR_MAX - config->framebuffer_base;
}

/* Load, convert and render one logo; return 0 on success, -1 on failure. */
static int load_and_render_logo_to_framebuffer(
	enum bootsplash_type logo_type,
	struct logo_config *config
)
{
	uintptr_t logo;
	size_t logo_size = 0;
	size_t blt_size = 0;
	uintptr_t blt_buffer = 0;
	int result = -1;
	uint32_t logo_height, logo_width;
	struct logo_coordinates logo_coords;
	enum fw_splash_horizontal_alignment halignment;
	enum fw_splash_vertical_alignment valignment;
	uint8_t logo_bottom_margin;

	if (!valid_logo_framebuffer(config))
		return -1;

	logo = (uintptr_t)bmp_load_logo_by_type(logo_type, &logo_size);

	if (!logo || logo_size < sizeof(struct bmp_image_header)) {
		printk(BIOS_ERR, "%s: BMP image (%zu) is less than expected minimum size (%zu).\n",
				 __func__, logo_size, sizeof(struct bmp_image_header));
		goto out;
	}

	if (!convert_bmp_to_blt(logo, logo_size, &blt_buffer, &blt_size,
			       &logo_height, &logo_width, config->panel_orientation))
		goto out;

	if (!logo_width || !logo_height ||
		logo_width > config->horizontal_resolution ||
		logo_height > config->vertical_resolution) {
		printk(BIOS_ERR, "%s: BMP image (%ux%u) exceeds framebuffer (%ux%u).\n",
		       __func__, logo_width, logo_height,
		       config->horizontal_resolution, config->vertical_resolution);
		goto out;
	}

	get_logo_layout(logo_type, config, &halignment, &valignment, &logo_bottom_margin);

	logo_coords = calculate_logo_coordinates(config->horizontal_resolution,
		 config->vertical_resolution, logo_width, logo_height, halignment, valignment);

	if (logo_bottom_margin) {
		switch (config->panel_orientation) {
		case LB_FB_ORIENTATION_RIGHT_UP:
			logo_coords.x = logo_bottom_margin;
			break;
		case LB_FB_ORIENTATION_LEFT_UP:
			if (logo_bottom_margin > logo_coords.x)
				goto out;
			logo_coords.x -= logo_bottom_margin;
			break;
		case LB_FB_ORIENTATION_BOTTOM_UP:
			logo_coords.y = logo_bottom_margin;
			break;
		default: /* LB_FB_ORIENTATION_NORMAL (default) */
			if (logo_bottom_margin > logo_coords.y)
				goto out;
			logo_coords.y -= logo_bottom_margin;
			break;
		}
	}

	if (logo_coords.x > config->horizontal_resolution ||
		logo_coords.y > config->vertical_resolution ||
		logo_width > config->horizontal_resolution - logo_coords.x ||
		logo_height > config->vertical_resolution - logo_coords.y) {
		printk(BIOS_ERR, "%s: Invalid BMP display rectangle (%u,%u %ux%u).\n",
		       __func__, logo_coords.x, logo_coords.y, logo_width, logo_height);
		goto out;
	}

	copy_logo_to_framebuffer(config->framebuffer_base, config->bytes_per_scanline, blt_buffer,
				 logo_width, logo_height, logo_coords.x, logo_coords.y);
	if (CONFIG(USE_COREBOOT_FOR_BMP_RENDERING) &&
	    logo_type == BOOTSPLASH_CENTER &&
	    bootsplash_publish_handoff(config->framebuffer_base,
				  config->horizontal_resolution, config->vertical_resolution,
				  logo_coords.x, logo_coords.y, logo_width, logo_height,
				  (const void *)logo, logo_size))
		bmp_retain_logo();

	result = 0;
out:
	free((void *)blt_buffer);
	bmp_release_logo();
	return result;
}

/*
 * Loads, converts, and renders a BMP logo to the framebuffer.
 * The logo type (primary or low battery etc.) is seletcted dynamically.
 *
 * config: Logo configuration information.
 *
 */
void render_logo_to_framebuffer(struct logo_config *config)
{
	if (!config)
		return;

	if (config->framebuffer_base == 0) {
		/* Try to load from already populated framebuffer information */
		const struct lb_framebuffer *fb = get_lb_framebuffer();
		/* Exit if framebuffer is still not available */
		/* The native copy path supports BGRX8888, not arbitrary RGB layouts. */
		if (!fb || fb->physical_address > UINTPTR_MAX || fb->bits_per_pixel != 32 ||
		    fb->red_mask_pos != 16 || fb->red_mask_size != 8 ||
		    fb->green_mask_pos != 8 || fb->green_mask_size != 8 ||
		    fb->blue_mask_pos != 0 || fb->blue_mask_size != 8 ||
		    (fb->reserved_mask_size &&
		     (fb->reserved_mask_size != 8 || fb->reserved_mask_pos != 24)))
			return;
		config->framebuffer_base = fb->physical_address;
		config->horizontal_resolution = fb->x_resolution;
		config->vertical_resolution = fb->y_resolution;
		config->bytes_per_scanline = fb->bytes_per_line;
		config->panel_orientation = fb->orientation;
	}

	if (!valid_logo_framebuffer(config)) {
		printk(BIOS_ERR, "CBFS Logo: Invalid stride %u for resolution %ux%u\n",
		       config->bytes_per_scanline,
		       config->horizontal_resolution,
		       config->vertical_resolution);
		return;
	}

	/*
	 * Note: Intel SoC platforms validate their framebuffer geometry earlier
	 * in cb_logo.c. For non-Intel architectures (e.g., ARM/ARM64) using
	 * memlayout, we validate the footprint against the linker region size.
	 */
#if CONFIG(ARCH_ARM) || CONFIG(ARCH_ARM64)
	const uint64_t required_fb_size = (uint64_t)config->vertical_resolution *
					 config->bytes_per_scanline;

	if (REGION_SIZE(framebuffer) > 0 && required_fb_size > REGION_SIZE(framebuffer)) {
		printk(BIOS_ERR,
			"CBFS Logo: Geometry footprint (%llu B) exceeds framebuffer region size (%zu B)\n",
			   required_fb_size, REGION_SIZE(framebuffer));
		return;
	}
#endif
	/*
	 * If the device is in low-battery mode and the low-battery splash screen is being
	 * displayed, prevent further operation and bail out early.
	 */
	if (platform_is_low_battery_shutdown_needed()) {
		if (load_and_render_logo_to_framebuffer(BOOTSPLASH_LOW_BATTERY, config) != 0) {
			printk(BIOS_ERR, "%s: Failed to render low-battery logo.\n", __func__);
		}

		/* Display Text message at the footer of splash screen if supported */
		if (CONFIG(FRAMEBUFFER_SPLASH_TEXT)) {
			char msg[MAX_SPLASH_TEXT_WIDTH];
			if (platform_get_splash_text(BOOTSPLASH_LOW_BATTERY, msg, MAX_SPLASH_TEXT_WIDTH))
				render_text_to_framebuffer(config, msg, BOOTSPLASH_TEXT_FOOTER);
		}
		return;
	}

	/*
	 * If the device has booted due to cable power insertion aka off-mode then display
	 * off-mode charging user notification and bail out early.
	 */
	if (platform_is_off_mode_charging_active()) {
		if (load_and_render_logo_to_framebuffer(BOOTSPLASH_OFF_MODE_CHARGING, config) != 0)
			printk(BIOS_ERR, "%s: Failed to render off-mode charging logo.\n", __func__);

		/* Display Text message at the footer of splash screen if supported */
		if (CONFIG(FRAMEBUFFER_SPLASH_TEXT)) {
			char msg[MAX_SPLASH_TEXT_WIDTH];
			if (platform_get_splash_text(BOOTSPLASH_OFF_MODE_CHARGING, msg,
					 MAX_SPLASH_TEXT_WIDTH))
				render_text_to_framebuffer(config, msg, BOOTSPLASH_TEXT_FOOTER);
		}
		return;
	}

	/* Render the main logo */
	if (load_and_render_logo_to_framebuffer(BOOTSPLASH_CENTER, config) != 0) {
		printk(BIOS_ERR, "%s: Failed to render main splash screen logo.\n", __func__);
	} else if (CONFIG(SPLASH_SCREEN_FOOTER)) {
		/* Render the footer logo */
		if (load_and_render_logo_to_framebuffer(BOOTSPLASH_FOOTER, config) != 0)
			printk(BIOS_ERR, "%s: Failed to render footer logo.\n", __func__);
	}
}

static void release_logo(void *arg_unused)
{
	bmp_release_logo();
}

BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_LOAD, BS_ON_EXIT, release_logo, NULL);
