/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include <console/console.h>
#include <delay.h>
#include <device/i2c_simple.h>
#include <option.h>
#include <string.h>

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

static size_t starlabs_touchpad_encode_command(uint8_t *buf, uint8_t opcode,
					       uint8_t report_type,
					       uint8_t report_id)
{
	size_t length = 0;

	if (report_id < 0x0f) {
		buf[length++] = (report_type << 4) | report_id;
		buf[length++] = opcode;
	} else {
		buf[length++] = (report_type << 4) | 0x0f;
		buf[length++] = opcode;
		buf[length++] = report_id;
	}

	return length;
}

static int starlabs_touchpad_set_report(unsigned int bus, uint16_t cmd_reg,
					uint16_t data_reg, uint8_t report_id,
					const uint8_t *payload,
					size_t payload_len)
{
	uint8_t buf[16];
	size_t cmd_len = 0;
	size_t report_len = sizeof(uint16_t);

	if (report_id)
		report_len++;
	report_len += payload_len;

	starlabs_write_le16(buf, cmd_len, cmd_reg);
	cmd_len += sizeof(uint16_t);
	cmd_len += starlabs_touchpad_encode_command(buf + cmd_len,
						    I2C_HID_OPCODE_SET_REPORT,
						    I2C_HID_REPORT_TYPE_FEATURE,
						    report_id);
	starlabs_write_le16(buf, cmd_len, data_reg);
	cmd_len += sizeof(uint16_t);
	starlabs_write_le16(buf, cmd_len, report_len);
	cmd_len += sizeof(uint16_t);

	if (report_id)
		buf[cmd_len++] = report_id;

	memcpy(buf + cmd_len, payload, payload_len);
	cmd_len += payload_len;

	return i2c_write_raw(bus, STARLABS_TOUCHPAD_I2C_ADDR, buf, cmd_len);
}

static int starlabs_touchpad_get_report(unsigned int bus, uint16_t cmd_reg,
					uint16_t data_reg, uint8_t report_type,
					uint8_t report_id, uint8_t *report,
					size_t report_len)
{
	struct i2c_msg seg[2];
	uint8_t cmd_buf[7];
	uint8_t raw_buf[16];
	size_t cmd_len = 0;
	uint16_t raw_len;
	int ret;

	if (report_len + sizeof(uint16_t) > sizeof(raw_buf))
		return -1;

	starlabs_write_le16(cmd_buf, cmd_len, cmd_reg);
	cmd_len += sizeof(uint16_t);
	cmd_len += starlabs_touchpad_encode_command(cmd_buf + cmd_len,
						    I2C_HID_OPCODE_GET_REPORT,
						    report_type, report_id);
	starlabs_write_le16(cmd_buf, cmd_len, data_reg);
	cmd_len += sizeof(uint16_t);

	seg[0].slave = STARLABS_TOUCHPAD_I2C_ADDR;
	seg[0].flags = 0;
	seg[0].buf = cmd_buf;
	seg[0].len = cmd_len;
	seg[1].slave = STARLABS_TOUCHPAD_I2C_ADDR;
	seg[1].flags = I2C_M_RD;
	seg[1].buf = raw_buf;
	seg[1].len = report_len + sizeof(uint16_t);

	ret = i2c_transfer(bus, seg, ARRAY_SIZE(seg));
	if (ret)
		return ret;

	raw_len = starlabs_read_le16(raw_buf, 0);
	if (raw_len <= sizeof(uint16_t))
		return -1;

	raw_len -= sizeof(uint16_t);
	if (raw_len < report_len)
		return -1;

	memcpy(report, raw_buf + sizeof(uint16_t), report_len);
	if (report[0] != report_id)
		return -1;

	return 0;
}

static int starlabs_touchpad_set_haptics(unsigned int bus, uint16_t cmd_reg,
					 uint16_t data_reg, uint8_t level)
{
	return starlabs_touchpad_set_report(bus, cmd_reg, data_reg,
					    STARLABS_TOUCHPAD_HAPTICS_REPORT_ID,
					    &level, sizeof(level));
}

static int starlabs_touchpad_set_force(unsigned int bus, uint16_t cmd_reg,
				       uint16_t data_reg, uint16_t press,
				       uint16_t release)
{
	uint8_t payload[4];

	starlabs_write_le16(payload, 0, press);
	starlabs_write_le16(payload, 2, release);

	return starlabs_touchpad_set_report(bus, cmd_reg, data_reg,
					    STARLABS_TOUCHPAD_FORCE_REPORT_ID,
					    payload, sizeof(payload));
}

static int starlabs_touchpad_write_user_reg(unsigned int bus, uint16_t cmd_reg,
					    uint16_t data_reg, uint8_t bank,
					    uint8_t addr, uint8_t value)
{
	const uint8_t payload[] = { addr, bank, value };

	return starlabs_touchpad_set_report(bus, cmd_reg, data_reg,
					    STARLABS_TOUCHPAD_USER_REG_REPORT_ID,
					    payload, sizeof(payload));
}

static int starlabs_touchpad_read_user_reg(unsigned int bus, uint16_t cmd_reg,
					   uint16_t data_reg, uint8_t bank,
					   uint8_t addr, uint8_t *value)
{
	uint8_t report[4];
	int ret;

	ret = starlabs_touchpad_write_user_reg(bus, cmd_reg, data_reg,
					       bank | STARLABS_TOUCHPAD_USER_REG_READ_FLAG,
					       addr, 0);
	if (ret)
		return ret;

	mdelay(STARLABS_TOUCHPAD_SETTLE_DELAY_MS);

	ret = starlabs_touchpad_get_report(bus, cmd_reg, data_reg,
					   I2C_HID_REPORT_TYPE_FEATURE,
					   STARLABS_TOUCHPAD_USER_REG_REPORT_ID,
					   report, sizeof(report));
	if (ret)
		return ret;

	*value = report[3];
	return 0;
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
	uint8_t readback = 0;
	int ret;
	struct starlabs_touchpad_op_ctx op_ctx = {
		.bus = STARLABS_TOUCHPAD_I2C_BUS,
		.cmd_reg = STARLABS_TOUCHPAD_FALLBACK_CMD_REG,
		.data_reg = STARLABS_TOUCHPAD_FALLBACK_DATA_REG,
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

	ret = starlabs_touchpad_read_user_reg(op_ctx.bus, op_ctx.cmd_reg,
					      op_ctx.data_reg, op_ctx.bank,
					      op_ctx.addr, &readback);
	if (ret != 0 || readback != op_ctx.value) {
		printk(BIOS_ERR,
		       "Touchpad settings: report rate verify failed via regs 0x%04x/0x%04x: ret=%d read=%u want=%u\n",
		       op_ctx.cmd_reg, op_ctx.data_reg, ret,
		       ret ? 0 : readback, op_ctx.value);
		return;
	}

	printk(BIOS_INFO,
	       "Touchpad settings: verified via regs 0x%04x/0x%04x; haptics=%u click=%u release=%u rate=%u\n",
	       op_ctx.cmd_reg, op_ctx.data_reg,
	       op_ctx.level, op_ctx.press, op_ctx.release, op_ctx.value);
}

BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_BOOT, BS_ON_ENTRY, starlabs_touchpad_apply_settings,
		      NULL);
