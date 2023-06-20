/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <boot/coreboot_tables.h>
#include <boot/upl_fdt_table.h>
#include <commonlib/bsd/helpers.h>
#include <console/console.h>
#include <console/uart.h>
#include <commonlib/device_tree.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "uart8250reg.h"

/* Should support 8250, 16450, 16550, 16550A type UARTs */

/* Expected character delay at 1200bps is 9ms for a working UART
 * and no flow-control. Assume UART as stuck if shift register
 * or FIFO takes more than 50ms per character to appear empty.
 *
 * Estimated that inb() from UART takes 1 microsecond.
 */
#define SINGLE_CHAR_TIMEOUT	(50 * 1000)
#define FIFO_TIMEOUT		(16 * SINGLE_CHAR_TIMEOUT)

static int uart8250_can_tx_byte(unsigned int base_port)
{
	return inb(base_port + UART8250_LSR) & UART8250_LSR_THRE;
}

static void uart8250_tx_byte(unsigned int base_port, unsigned char data)
{
	unsigned long i = SINGLE_CHAR_TIMEOUT;
	while (i-- && !uart8250_can_tx_byte(base_port));
	outb(data, base_port + UART8250_TBR);
}

static void uart8250_tx_flush(unsigned int base_port)
{
	unsigned long i = FIFO_TIMEOUT;
	while (i-- && !(inb(base_port + UART8250_LSR) & UART8250_LSR_TEMT));
}

static int uart8250_can_rx_byte(unsigned int base_port)
{
	return inb(base_port + UART8250_LSR) & UART8250_LSR_DR;
}

static unsigned char uart8250_rx_byte(unsigned int base_port)
{
	unsigned long i = SINGLE_CHAR_TIMEOUT;
	while (i && !uart8250_can_rx_byte(base_port))
		i--;

	if (i)
		return inb(base_port + UART8250_RBR);
	else
		return 0x0;
}

static void uart8250_init(unsigned int base_port, unsigned int divisor)
{
	/* Disable interrupts */
	outb(0x0, base_port + UART8250_IER);
	/* Enable FIFOs */
	outb(UART8250_FCR_FIFO_EN, base_port + UART8250_FCR);

	/* assert DTR and RTS so the other end is happy */
	outb(UART8250_MCR_DTR | UART8250_MCR_RTS, base_port + UART8250_MCR);

	/* DLAB on */
	outb(UART8250_LCR_DLAB | CONFIG_TTYS0_LCS, base_port + UART8250_LCR);

	/* Set Baud Rate Divisor. 12 ==> 9600 Baud */
	outb(divisor & 0xFF,   base_port + UART8250_DLL);
	outb((divisor >> 8) & 0xFF,    base_port + UART8250_DLM);

	/* Set to 3 for 8N1 */
	outb(CONFIG_TTYS0_LCS, base_port + UART8250_LCR);
}

static const unsigned int bases[] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };

uintptr_t uart_platform_base(unsigned int idx)
{
	if (idx < ARRAY_SIZE(bases))
		return bases[idx];
	return 0;
}

void uart_init(unsigned int idx)
{
	if (!CONFIG(DRIVERS_UART_8250IO_SKIP_INIT)) {
		unsigned int div = uart_get_baudrate_divisor();
		uart8250_init(uart_platform_base(idx), div);
	}
}

void uart_tx_byte(unsigned int idx, unsigned char data)
{
	uart8250_tx_byte(uart_platform_base(idx), data);
}

unsigned char uart_rx_byte(unsigned int idx)
{
	return uart8250_rx_byte(uart_platform_base(idx));
}

void uart_tx_flush(unsigned int idx)
{
	uart8250_tx_flush(uart_platform_base(idx));
}

enum cb_err fill_lb_serial(struct lb_serial *serial)
{
	serial->type = LB_SERIAL_TYPE_IO_MAPPED;
	serial->baseaddr = uart_platform_base(CONFIG_UART_FOR_CONSOLE);
	serial->baud = get_uart_baudrate();
	serial->regwidth = 1;
	serial->input_hertz = uart_platform_refclk();

	return CB_SUCCESS;
}

const char *upl_fdt_add_serial(struct device_tree_node *parent_node)
{
	static const char *serial_path[] = { "isa", "serial", NULL };
	u32 addr_cells = 0, size_cells = 0;
	struct device_tree_node *uart_node = dt_find_node(parent_node, serial_path, &addr_cells, &size_cells, 1);
	if (!uart_node) {
		printk(BIOS_ERR, "%s(): could not add serial node\n", __func__);
		return NULL;
	}

#define FDT_IORESOURCE_FLAG 0x100000000
	u64 reg_addrs[] = { uart_platform_base(CONFIG_UART_FOR_CONSOLE) | FDT_IORESOURCE_FLAG };
	u64 reg_sizes[] = { 8 };
	dt_add_reg_prop(uart_node, reg_addrs, reg_sizes, 1, addr_cells, size_cells);
	dt_add_string_prop(uart_node, "compatible", "ns8250");
	dt_add_u32_prop(uart_node, "clock-frequency", uart_platform_refclk());
	dt_add_u32_prop(uart_node, "current-speed", get_uart_baudrate());
	dt_add_u32_prop(uart_node, "reg-io-width", sizeof(uint8_t));

	return "/isa/serial";
}
