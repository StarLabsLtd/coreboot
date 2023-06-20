/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/mmio.h>
#include <boot/coreboot_tables.h>
#include <boot/upl_fdt_table.h>
#include <console/console.h>
#include <console/uart.h>
#include <drivers/uart/pl011.h>
#include <stdlib.h>

void uart_init(unsigned int idx)
{
	struct pl011_uart *regs = uart_platform_baseptr(idx);
	uint32_t tmp;

	if (!regs)
		return;

	/* Disable UART */
	tmp = read32(&regs->cr);
	tmp &= ~PL011_UARTCR_UARTEN;
	write32(&regs->cr, tmp);

	/*
	 * Program Divisor
	 * As per: PL011 Technical reference manual:
	 * BAUDDIV = (Fuartclk / (16 * baud_rate))
	 * Considering 6 bits(64) for UARTFBRD
	 * BAUDDIV = (Fuartclk * 4 / baud_rate)
	 */
	tmp = uart_platform_refclk() * 4 / get_uart_baudrate();

	write32(&regs->ibrd, tmp >> 6);
	write32(&regs->fbrd, tmp & 0x3f);

	/* Program LINE Control 8n1, FIFO enable */
	tmp = read32(&regs->lcr_h);
	tmp |= PL011_LINE_CONTROL;
	write32(&regs->lcr_h, tmp);

	/* Enable UART */
	tmp = read32(&regs->cr);
	tmp |= PL011_UARTCR_UARTEN | PL011_UARTCR_RXE | PL011_UARTCR_TXE;
	write32(&regs->cr, tmp);
}

void uart_tx_byte(unsigned int idx, unsigned char data)
{
	struct pl011_uart *regs = uart_platform_baseptr(idx);

	write8(&regs->dr, data);
	uart_tx_flush(idx);
}

void uart_tx_flush(unsigned int idx)
{
	struct pl011_uart *regs = uart_platform_baseptr(idx);

	/* FIXME: add a timeout */
	while (!(read32(&regs->fr) & PL011_UARTFR_TXFE))
		;
}

unsigned char uart_rx_byte(unsigned int idx)
{
	struct pl011_uart *regs = uart_platform_baseptr(idx);

	while (read32(&regs->fr) & PL011_UARTFR_RXFE)
		;
	return read8(&regs->dr);
}

enum cb_err fill_lb_serial(struct lb_serial *serial)
{
	serial->type = LB_SERIAL_TYPE_MEMORY_MAPPED;
	serial->baseaddr = uart_platform_base(CONFIG_UART_FOR_CONSOLE);
	serial->baud = get_uart_baudrate();
	serial->regwidth = sizeof(uint32_t);
	serial->input_hertz = uart_platform_refclk();

	return CB_SUCCESS;
}

const char *upl_fdt_add_serial(struct device_tree_node *parent_node)
{
	static const char *serial_path[] = { "serial", NULL };
	u32 addr_cells = 0, size_cells = 0;
	struct device_tree_node *uart_node = dt_find_node(parent_node, serial_path, &addr_cells, &size_cells, 1);
	if (!uart_node) {
		printk(BIOS_ERR, "%s(): could not add serial node\n", __func__);
		return NULL;
	}

	u64 reg_addrs[] = { uart_platform_base(CONFIG_UART_FOR_CONSOLE) };
	u64 reg_sizes[] = { 0x1000 };
	dt_add_reg_prop(uart_node, reg_addrs, reg_sizes, 1, addr_cells, size_cells);
	dt_add_string_prop(uart_node, "compatible", "arm,pl011");
	dt_add_u32_prop(uart_node, "clock-frequency", uart_platform_refclk());
	dt_add_u32_prop(uart_node, "current-speed", get_uart_baudrate());
	dt_add_u32_prop(uart_node, "reg-io-width", sizeof(uint32_t));

	return "/serial";
}
