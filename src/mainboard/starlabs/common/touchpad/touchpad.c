/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include <console/console.h>
#include <delay.h>
#include <device/i2c_simple.h>
#include <option.h>

#include <common/touchpad.h>

static uint16_t starlabs_read_le16(const uint8_t *buf, size_t offset)
{
	return (uint16_t)buf[offset] | ((uint16_t)buf[offset + 1] << 8);
}

static void starlabs_write_le16(uint8_t *buf, size_t offset, uint16_t value)
{
	buf[offset] = value & 0xff;
	buf[offset + 1] = value >> 8;
}

static int starlabs_touchpad_read_desc(unsigned int bus, uint8_t *desc)
{
	return i2c_2ba_read_bytes(bus, STARLABS_TOUCHPAD_I2C_ADDR,
				  STARLABS_TOUCHPAD_HID_DESC_REG,
				  desc, TOUCHPAD_DESC_LENGTH);
}

static int starlabs_touchpad_set_haptics(unsigned int bus, uint16_t cmd_reg,
					 uint16_t data_reg, uint8_t level)
{
	uint8_t buf[10];

	starlabs_write_le16(buf, 0, cmd_reg);
	buf[2] = (I2C_HID_REPORT_TYPE_FEATURE << 4) |
		 STARLABS_TOUCHPAD_HAPTICS_REPORT_ID;
	buf[3] = I2C_HID_OPCODE_SET_REPORT;
	starlabs_write_le16(buf, 4, data_reg);
	starlabs_write_le16(buf, 6, 4);
	buf[8] = STARLABS_TOUCHPAD_HAPTICS_REPORT_ID;
	buf[9] = level;

	return i2c_write_raw(bus, STARLABS_TOUCHPAD_I2C_ADDR, buf, sizeof(buf));
}

static int starlabs_touchpad_set_force(unsigned int bus, uint16_t cmd_reg,
				       uint16_t data_reg, uint16_t press,
				       uint16_t release)
{
	uint8_t buf[13];

	starlabs_write_le16(buf, 0, cmd_reg);
	buf[2] = (I2C_HID_REPORT_TYPE_FEATURE << 4) |
		 STARLABS_TOUCHPAD_FORCE_REPORT_ID;
	buf[3] = I2C_HID_OPCODE_SET_REPORT;
	starlabs_write_le16(buf, 4, data_reg);
	starlabs_write_le16(buf, 6, 7);
	buf[8] = STARLABS_TOUCHPAD_FORCE_REPORT_ID;
	starlabs_write_le16(buf, 9, press);
	starlabs_write_le16(buf, 11, release);

	return i2c_write_raw(bus, STARLABS_TOUCHPAD_I2C_ADDR, buf, sizeof(buf));
}

static int starlabs_touchpad_write_user_reg(unsigned int bus, uint16_t cmd_reg,
					    uint16_t data_reg, uint8_t bank,
					    uint8_t addr, uint8_t value)
{
	uint8_t buf[12];

	starlabs_write_le16(buf, 0, cmd_reg);
	buf[2] = (I2C_HID_REPORT_TYPE_FEATURE << 4) |
		 STARLABS_TOUCHPAD_USER_REG_REPORT_ID;
	buf[3] = I2C_HID_OPCODE_SET_REPORT;
	starlabs_write_le16(buf, 4, data_reg);
	starlabs_write_le16(buf, 6, 6);
	buf[8] = STARLABS_TOUCHPAD_USER_REG_REPORT_ID;
	buf[9] = addr;
	buf[10] = bank;
	buf[11] = value;

	return i2c_write_raw(bus, STARLABS_TOUCHPAD_I2C_ADDR, buf, sizeof(buf));
}

static int starlabs_touchpad_set_haptics_op(void *arg)
{
	const struct starlabs_touchpad_op_ctx *ctx = arg;

	return starlabs_touchpad_set_haptics(ctx->bus, ctx->cmd_reg,
					     ctx->data_reg, ctx->level);
}

static int starlabs_touchpad_set_force_op(void *arg)
{
	const struct starlabs_touchpad_op_ctx *ctx = arg;

	return starlabs_touchpad_set_force(ctx->bus, ctx->cmd_reg,
					   ctx->data_reg,
					   ctx->press, ctx->release);
}

static int starlabs_touchpad_write_user_reg_op(void *arg)
{
	const struct starlabs_touchpad_op_ctx *ctx = arg;

	return starlabs_touchpad_write_user_reg(ctx->bus, ctx->cmd_reg,
						ctx->data_reg, ctx->bank,
						ctx->addr, ctx->value);
}

static int starlabs_touchpad_retry(int (*op)(void *arg), void *arg)
{
	int ret;
	int attempt;

	for (attempt = 0; attempt < STARLABS_TOUCHPAD_RETRIES; attempt++) {
		ret = op(arg);
		if (ret == 0)
			return 0;
		mdelay(STARLABS_TOUCHPAD_RETRY_DELAY_MS);
	}

	return ret;
}

static void starlabs_touchpad_apply_settings(void *arg)
{
	uint8_t desc[TOUCHPAD_DESC_LENGTH];
	uint16_t cmd_reg = STARLABS_TOUCHPAD_FALLBACK_CMD_REG;
	uint16_t data_reg = STARLABS_TOUCHPAD_FALLBACK_DATA_REG;
	uint16_t desc_cmd_reg;
	uint16_t desc_data_reg;
	int ret;
	int attempt;
	struct starlabs_touchpad_op_ctx op_ctx = {
		.bus = STARLABS_TOUCHPAD_I2C_BUS,
		.level = get_uint_option("touchpad_haptics",
					 STARLABS_TOUCHPAD_HAPTICS_DEFAULT),
		.press = get_uint_option("touchpad_force_press",
					 STARLABS_TOUCHPAD_PRESS_FORCE_DEFAULT),
		.release = get_uint_option("touchpad_force_release",
					   STARLABS_TOUCHPAD_RELEASE_FORCE_DEFAULT),
		.bank = STARLABS_TOUCHPAD_USER_REG_BANK,
		.addr = STARLABS_TOUCHPAD_USER_REG_ADDR_RATE,
		.value = get_uint_option("touchpad_report_rate",
					 STARLABS_TOUCHPAD_REPORT_RATE_DEFAULT),
	};

	(void)arg;

	for (attempt = 0; attempt < STARLABS_TOUCHPAD_RETRIES; attempt++) {
		ret = starlabs_touchpad_read_desc(STARLABS_TOUCHPAD_I2C_BUS, desc);
		if (ret == 0) {
			desc_cmd_reg = starlabs_read_le16(desc, TOUCHPAD_DESC_CMD_REG);
			desc_data_reg = starlabs_read_le16(desc, TOUCHPAD_DESC_DATA_REG);
			if (desc_cmd_reg != 0 || desc_data_reg != 0) {
				cmd_reg = desc_cmd_reg;
				data_reg = desc_data_reg;
				break;
			}
		}
		mdelay(STARLABS_TOUCHPAD_RETRY_DELAY_MS);
	}
	if (ret != 0) {
		printk(BIOS_ERR, "Touchpad settings: HID descriptor read failed on i2c%u\n",
		       STARLABS_TOUCHPAD_I2C_BUS);
		return;
	}

	op_ctx.cmd_reg = cmd_reg;
	op_ctx.data_reg = data_reg;

	ret = starlabs_touchpad_retry(starlabs_touchpad_set_haptics_op, &op_ctx);
	if (ret != 0) {
		printk(BIOS_ERR, "Touchpad settings: failed to set haptics level %u: %d\n",
		       op_ctx.level, ret);
		return;
	}

	ret = starlabs_touchpad_retry(starlabs_touchpad_set_force_op, &op_ctx);
	if (ret != 0) {
		printk(BIOS_ERR,
		       "Touchpad settings: failed to set force thresholds %u/%u: %d\n",
		       op_ctx.press, op_ctx.release, ret);
		return;
	}

	ret = starlabs_touchpad_retry(starlabs_touchpad_write_user_reg_op, &op_ctx);
	if (ret != 0) {
		printk(BIOS_ERR, "Touchpad settings: failed to set report rate %u: %d\n",
		       op_ctx.value, ret);
		return;
	}

	printk(BIOS_INFO,
	       "Touchpad settings: applied haptics=%u click=%u release=%u rate=%u\n",
	       op_ctx.level, op_ctx.press, op_ctx.release, op_ctx.value);
}

BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_BOOT, BS_ON_ENTRY, starlabs_touchpad_apply_settings,
		      NULL);
