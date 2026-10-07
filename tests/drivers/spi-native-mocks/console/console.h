/* SPDX-License-Identifier: GPL-2.0-only */
#define BIOS_INFO 1
#define BIOS_ERR 2
#define BIOS_DEBUG 3
int printk(int level, const char *format, ...);
