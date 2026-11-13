/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <boot/coreboot_tables.h>
#include <bootsplash.h>
#include <console/console.h>
#include <fsp/fsp_gop_blt.h>
#include <stdlib.h>

/* Convert a *.BMP graphics image to a GOP blt buffer */
void fsp_load_and_convert_bmp_to_gop_blt(efi_uintn_t *logo, uint32_t *logo_size,
	efi_uintn_t *blt_ptr, efi_uintn_t *blt_size, uint32_t *pixel_height, uint32_t *pixel_width,
	enum lb_fb_orientation orientation)
{
	uintptr_t bitmap, buffer;
	size_t bitmap_size, buffer_size;
	uint32_t height, width;

	if (!logo || !logo_size || !blt_ptr || !blt_size || !pixel_height || !pixel_width)
		return;
	*logo = 0;
	*logo_size = 0;
	*blt_ptr = 0;
	*blt_size = 0;
	*pixel_height = 0;
	*pixel_width = 0;
	if (!load_and_convert_bmp_to_blt(&bitmap, &bitmap_size, &buffer, &buffer_size,
				       &height, &width, orientation))
		return;
	if ((efi_uintn_t)bitmap != bitmap || (efi_uintn_t)buffer != buffer ||
	    (uint32_t)bitmap_size != bitmap_size || (efi_uintn_t)buffer_size != buffer_size) {
		free((void *)buffer);
		bmp_release_logo();
		return;
	}
	*logo = bitmap;
	*logo_size = bitmap_size;
	*blt_ptr = buffer;
	*blt_size = buffer_size;
	*pixel_height = height;
	*pixel_width = width;
}

/* Convert a *.BMP graphics image (as per input `logo_ptr`) to a GOP blt buffer */
void fsp_convert_bmp_to_gop_blt(uintptr_t logo_ptr, size_t logo_ptr_size,
	efi_uintn_t *blt_ptr, efi_uintn_t *blt_size, uint32_t *pixel_height, uint32_t *pixel_width,
	enum lb_fb_orientation orientation)
{
	uintptr_t buffer;
	size_t buffer_size;
	uint32_t height, width;

	if (!blt_ptr || !blt_size || !pixel_height || !pixel_width)
		return;
	*blt_ptr = 0;
	*blt_size = 0;
	*pixel_height = 0;
	*pixel_width = 0;
	if (!convert_bmp_to_blt(logo_ptr, logo_ptr_size, &buffer, &buffer_size,
			      &height, &width, orientation))
		return;
	if ((efi_uintn_t)buffer != buffer || (efi_uintn_t)buffer_size != buffer_size) {
		free((void *)buffer);
		return;
	}
	*blt_ptr = buffer;
	*blt_size = buffer_size;
	*pixel_height = height;
	*pixel_width = width;
}
