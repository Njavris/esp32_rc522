#include "rc522.h"

#include "driver/gpio.h"
#include "sdkconfig.h"
#include "rc522_reg.h"
#include <string.h>


#define RC522_DELAY		5

#define RC522_AUTH_TIMEOUT	500
#define RC522_CRC_TIMEOUT	500
#define RC522_RTX_TIMEOUT	500

//#define DEBUG

#ifdef DEBUG
#define DBG_LOG(...)	printf(__VA_ARGS__);
#else
#define DBG_LOG(...)
#endif


static void rc522_setup_rst(struct rc522_dev *dev) {
	gpio_config_t io_conf = {};
    	io_conf.pin_bit_mask = (1ULL << dev->rst);
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
	io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
	gpio_config(&io_conf);
}

static void rc522_rst(struct rc522_dev *dev) {
	gpio_set_level(dev->rst, dev->rst_pol);
	vTaskDelay(100 / portTICK_PERIOD_MS);
	gpio_set_level(dev->rst, !dev->rst_pol);
	vTaskDelay(100 / portTICK_PERIOD_MS);
}

static uint8_t rc522_reg_read(struct rc522_dev *dev, uint8_t addr) {
	uint8_t wr[2] = { ((addr << 1) & 0x7e) | (1 << 7), 0 };
	uint8_t rd[2] = { 0 };
	dev->spidev.tx_rx(&dev->spidev, wr, rd, 2);
	return rd[1];
}

static void rc522_reg_write(struct rc522_dev *dev, uint8_t addr, uint8_t val) {
	uint8_t wr[2] = { ((addr << 1) & 0x7e), val };
	dev->spidev.tx_rx(&dev->spidev, wr, NULL, 2);
}

static void rc522_reg_mod(struct rc522_dev *dev, uint8_t addr, uint8_t clr,
		uint8_t set) {
	uint8_t reg_val = rc522_reg_read(dev, addr);
	reg_val &= ~(clr);
	reg_val |= set;
	rc522_reg_write(dev, addr, reg_val);
}

static rc522_result_t rc522_transceive(struct rc522_dev *dev, const uint8_t *tx_buf,
		int tx_len, uint8_t *rx_buf, int *rx_len, uint8_t last_bits ) {
	int i;
	rc522_result_t ret = rc522_res_ok;
	uint8_t reg_val, cnt;

	rc522_reg_write(dev, CollReg, 0x80);
	rc522_reg_write(dev, CommandReg, CMD_IDLE);
	rc522_reg_write(dev, FIFOLevelReg, 0x80); // Flush fifo
	rc522_reg_write(dev, CommIrqReg, 0x7f); // clear irqs

	DBG_LOG("sending %d bytes: ", tx_len);
	for (i = 0; i < tx_len; i++) {
		rc522_reg_write(dev, FIFODataReg, tx_buf[i]);
		DBG_LOG("%02x ", tx_buf[i]);
	}
	DBG_LOG("\n");

	rc522_reg_mod(dev, BitFramingReg, 0x7, last_bits & 0x7);

	rc522_reg_write(dev, CommandReg, CMD_TRX);
	rc522_reg_mod(dev, BitFramingReg, 0x0, 0x80);

	for (i = 0; i <= RC522_RTX_TIMEOUT / RC522_DELAY; i++) {
		reg_val = rc522_reg_read(dev, CommIrqReg);
		if (reg_val & 0x30)
			break;
		if (reg_val & 1 || i == RC522_RTX_TIMEOUT / RC522_DELAY) {
			ret = rc522_res_timeout;
			goto timeout;
		}

		vTaskDelay(RC522_DELAY / portTICK_PERIOD_MS);
	}
	rc522_reg_mod(dev, BitFramingReg, 0x80, 0x0);

	reg_val = rc522_reg_read(dev, ErrorReg);
	if (reg_val & 0x13)
		goto fail;

	if (!tx_len || !rx_buf)
		goto out;
	
	cnt = rc522_reg_read(dev, FIFOLevelReg);
	cnt = cnt < *rx_len ? cnt : *rx_len;
	for (i = 0; i < cnt; i++) {
		rx_buf[i] = rc522_reg_read(dev, FIFODataReg);
	}
	*rx_len = cnt;

	DBG_LOG("RX: %d bytes: ", cnt);
	for (i = 0; i < cnt; i++)
		DBG_LOG(" %02x", rx_buf[i]);
	DBG_LOG("\n");

	goto out;
fail:
	ret = rc522_res_error;
	DBG_LOG("%s: fail [CommIrqReg]:%02x [ErrorReg]:%02x\n",
			__func__,
			rc522_reg_read(dev, CommIrqReg),
			rc522_reg_read(dev, ErrorReg));
timeout:
	if (rx_len)
		*rx_len = 0;
	rc522_reg_mod(dev, BitFramingReg, 0x80, 0x0);
out:
	rc522_reg_write(dev, CommIrqReg, 0x7f);
	rc522_reg_write(dev, CommandReg, CMD_IDLE);
	return ret;
}

static rc522_result_t rc522_calc_crc(struct rc522_dev *dev, uint8_t *buf,
						int len, uint8_t *res) {
	int i;
	rc522_reg_write(dev, CommandReg, CMD_IDLE);
	rc522_reg_write(dev, DivIrqReg, 0x04);
	rc522_reg_write(dev, FIFOLevelReg, 0x80);
	for (i = 0; i < len; i++)
		rc522_reg_write(dev, FIFODataReg, buf[i]);
	rc522_reg_write(dev, CommandReg, CMD_CALC_CRC);

	for (i = 0; i < RC522_CRC_TIMEOUT / RC522_DELAY; i++) {
		uint8_t reg_val = rc522_reg_read(dev, DivIrqReg);
		if (reg_val & 0x4) {
			res[0] = rc522_reg_read(dev, CRCResultRegL);
			res[1] = rc522_reg_read(dev, CRCResultRegH);
			rc522_reg_write(dev, CommandReg, CMD_IDLE);
			return rc522_res_ok;
		}
		vTaskDelay(RC522_DELAY/ portTICK_PERIOD_MS);
	}

	rc522_reg_write(dev, CommandReg, CMD_IDLE);
	return rc522_res_timeout;
}

rc522_result_t rc522_send_reqa(struct rc522_dev *dev, uint8_t atqa[2]) {
	rc522_result_t res;
	uint8_t rx[2], tx[] = { 0x26 };
	int rx_len = sizeof(rx) / sizeof(rx[0]);

	rc522_reg_write(dev, TxModeReg, 0);
	rc522_reg_write(dev, RxModeReg, 0);
	rc522_reg_write(dev, ModWidthReg, 0x26);

	rc522_reg_mod(dev, CollReg, 0x80, 0x0);
	res = rc522_transceive(dev, tx, 1, rx, &rx_len, 0x7);
	if (res)
		return res;

	if (rx_len == 2) {
		if (atqa)
			memcpy(atqa, rx, rx_len);
		DBG_LOG("ATQA: %02x %02x\n", rx[0], rx[1]);
		return rc522_res_ok;
	}

	return rc522_res_error;
}

rc522_result_t rc522_send_halt(struct rc522_dev *dev) {
	uint8_t tx[4] = { 0x50, 0x00 };
	rc522_result_t res = rc522_res_ok;

	res = rc522_calc_crc(dev, tx, 2, &tx[2]);
	if (res)
		goto out;
	rc522_transceive(dev, tx, 4, NULL, NULL, 0x0);
out:
	rc522_reg_write(dev, CommandReg, CMD_IDLE);
	rc522_reg_mod(dev, Status2Reg, 0x08, 0x00);
	rc522_reg_write(dev, FIFOLevelReg, 0x80);
	rc522_reg_write(dev, CommIrqReg, 0x7f);
	rc522_reg_mod(dev, BitFramingReg, 0x7, 0);
	return res;
}

rc522_result_t rc522_anticol_cl1(struct rc522_dev *dev, uint8_t ret[5]) {
	rc522_result_t res;
	uint8_t tx[2] = { 0x93, 0x20 };
	int rx_len = 5;

	res =rc522_transceive(dev, tx, sizeof(tx) / sizeof(tx[0]), ret, &rx_len, 0x0);
	if (res)
		return res;

	if (rx_len == 5 ) {
		int i;
		uint8_t crc = ret[0] ^ ret[1] ^ ret[2] ^ ret[3];
		DBG_LOG("CL1 UID:");
		for (i = 0; i < 4; i++)
			DBG_LOG(" %02x", ret[i]);
		DBG_LOG("\n");
		if (crc != ret[4]) {
			DBG_LOG("Invalid crc\n");
			return rc522_res_error;
		}
		return rc522_res_ok;
	}

	return rc522_res_error;
}

rc522_result_t rc522_select_cl1(struct rc522_dev *dev, uint8_t uid[5], uint8_t *sak) {
	rc522_result_t res;
	uint8_t tx[9] = { 0x93, 0x70 };
	uint8_t rx[8];
	int rx_len = sizeof(rx) / sizeof(rx[0]);
	memcpy(&tx[2], uid, 5);
	res = rc522_calc_crc(dev, tx, 7, &tx[7]);
	if (res)
		return res;
	res = rc522_transceive(dev, tx, sizeof(tx) / sizeof(tx[0]), rx, &rx_len, 0x0);
	if (res)
		return res;

	if (rx_len <= 0)
		return rc522_res_error;

	if (sak)
		*sak = rx[0];

	return rc522_res_ok;
}

rc522_result_t rc522_anticol_cl2(struct rc522_dev *dev, uint8_t ret[4]) {
	rc522_result_t res;
	uint8_t tx[2] = { 0x95, 0x20 };
	int rx_len = 4;

	res = rc522_transceive(dev, tx, sizeof(tx) / sizeof(tx[0]), ret, &rx_len, 0x0);
	if (res)
		return res;

	if (rx_len == 5 ) {
		int i;
		uint8_t crc = ret[0] ^ ret[1] ^ ret[2];
		DBG_LOG("CL2 UID:");
		for (i = 0; i < 4; i++)
			DBG_LOG(" %02x", ret[i]);
		DBG_LOG("\n");
		if (crc != ret[3]) {
			DBG_LOG("Invalid crc\n");
			return rc522_res_error;
		}
		return rc522_res_ok;
	}

	return rc522_res_error;
}

rc522_result_t rc522_select_cl2(struct rc522_dev *dev, const uint8_t uid[4],
								uint8_t *sak) {
	rc522_result_t res;
	uint8_t tx[9] = { 0x95, 0x70 };
	uint8_t rx[8];
	int rx_len = sizeof(rx) / sizeof(rx[0]);
	memcpy(&tx[2], uid, 4);
	res = rc522_calc_crc(dev, tx, 7, &tx[7]);
	if (res)
		return res;
	res = rc522_transceive(dev, tx, sizeof(tx) / sizeof(tx[0]), rx, &rx_len, 0x0);
	if (res)
		return res;

	if (rx_len <= 0)
		return rc522_res_error;

	*sak = rx[0];

	return rc522_res_ok;
}

static rc522_result_t rc522_block_auth(struct rc522_dev *dev, uint8_t block,
			bool keyA, const uint8_t key[6], const uint8_t uid[4]) {	
	int i, ret = 0;
	uint8_t reg_val;
	uint8_t tx[12];
	int tx_len = sizeof(tx) / sizeof(tx[0]);

	tx[0] = keyA ? 0x60 : 0x61;
	tx[1] = block;
	memcpy(&tx[2], key, 6);
	memcpy(&tx[8], uid, 4);

	rc522_reg_write(dev, CollReg, 0x80);
	rc522_reg_write(dev, CommandReg, CMD_IDLE);
	rc522_reg_write(dev, FIFOLevelReg, 0x80); // Flush fifo
	rc522_reg_write(dev, CommIrqReg, 0x7f); // clear irqs

	DBG_LOG("AUTH sending %d bytes: ", tx_len);
	for (i = 0; i < tx_len; i++) {
		rc522_reg_write(dev, FIFODataReg, tx[i]);
		DBG_LOG("%02x ", tx[i]);
	}
	DBG_LOG("\n");

	rc522_reg_write(dev, CommandReg, CMD_MFAUTH);

	for (i = 0; i < RC522_AUTH_TIMEOUT / RC522_DELAY; i++) {
		reg_val = rc522_reg_read(dev, CommIrqReg);
		if (reg_val & 0x10)
			break;
		if (reg_val & 1)
			break;

		vTaskDelay(RC522_DELAY / portTICK_PERIOD_MS);
	}

	reg_val = rc522_reg_read(dev, ErrorReg);
	if (reg_val & 0x1b)
		goto fail;

	goto out;
fail:
	ret = rc522_res_error;
	DBG_LOG("%s: fail [CommIrqReg]:%02x [ErrorReg]:%02x\n",
			__func__,
			rc522_reg_read(dev, CommIrqReg),
			rc522_reg_read(dev, ErrorReg));
out:
	rc522_reg_write(dev, CommandReg, CMD_IDLE);
	rc522_reg_write(dev, CommIrqReg, 0x7f);
	return ret;
}

rc522_result_t rc522_block_read(struct rc522_dev *dev, uint8_t block, bool keyA,
		const uint8_t key[6], const uint8_t uid[4], uint8_t ret[16]) {	
	rc522_result_t res;
	uint8_t tx[4] = { 0x30 };
	uint8_t rx[18];
	int rx_len = 18;

	res = rc522_block_auth(dev, block, keyA, key, uid);
	if (res)
		return res;

	tx[1] = block;

	res = rc522_calc_crc(dev, tx, 2, &tx[2]);
	if (res)
		return res;

	res = rc522_transceive(dev, tx, sizeof(tx) / sizeof(tx[0]), rx, &rx_len, 0);
	if (res)
		return res;

	if (rx_len == 1 && rx[0] == 0x4)
		return rc522_res_nak;	

	if (rx_len != 18)
		return rc522_res_error;

	memcpy(ret, rx, 16);

	return rc522_res_ok;
}

rc522_result_t rc522_block_write(struct rc522_dev *dev, uint8_t block, bool keyA,
		const uint8_t key[6], const uint8_t uid[4], const uint8_t data[16]) {	
	rc522_result_t res;
	uint8_t tx[18] = { 0xa0 };

	res = rc522_block_auth(dev, block, keyA, key, uid);
	if (res)
		return res;

	tx[1] = block;
	res = rc522_calc_crc(dev, tx, 2, &tx[2]);
	if (res)
		return res;

	res = rc522_transceive(dev, tx, 4, NULL, NULL, 0);
	if (res)
		return res;

	memcpy(tx, data, 16);
	res = rc522_calc_crc(dev, tx, 16, &tx[16]);
	if (res)
		return res;

	res = rc522_transceive(dev, tx, 18, NULL, NULL, 0);
	if (res)
		return res;

	return rc522_res_ok;
}

#define BLOCK_SZ	16
#define SECTOR_SZ	(4 * BLOCK_SZ)

rc522_result_t rc522_sector_read(struct rc522_dev *dev, uint8_t sector,
		bool keyA, const uint8_t key[6], const uint8_t uid[4], uint8_t *ret) {
	rc522_result_t res;
	int i;
	uint8_t base_blk_id = sector * 4;

	for (i = 0; i < 4; i++) {
		uint8_t blk_id = base_blk_id + i;
		uint8_t *block = &ret[blk_id * BLOCK_SZ];
		res = rc522_block_read(dev, blk_id, keyA, key, uid, block);
		if (res)
			return res;
	}
	return rc522_res_ok;
}

rc522_result_t rc522_sector_write(struct rc522_dev *dev, uint8_t sector,
			bool keyA, const uint8_t key[6], const uint8_t uid[4],
								const uint8_t *data) {
	rc522_result_t res;
	int i;
	uint8_t base_blk_id = sector * 4;

	for (i = 0; i < 4; i++) {
		uint8_t blk_id = base_blk_id + i;
		uint8_t *block = &data[blk_id * BLOCK_SZ];
		res = rc522_block_write(dev, blk_id, keyA, key, uid, block);
		if (res)
			return res;
	}
	return rc522_res_ok;
}

static void rc522_irq_bh(void *data) {
	struct rc522_dev *dev = (struct rc522_dev *)data;
	uint8_t reg_val;

	reg_val = rc522_reg_read(dev, Status1Reg);
	if (reg_val & (1 << 4)) {
		reg_val = rc522_reg_read(dev, CommIrqReg);
		printf("ComIRQ: %02x\n", reg_val);
		rc522_reg_write(dev, Status1Reg, 0);
	}
}

void rc522_antena_en(struct rc522_dev *dev, bool en) {
	uint8_t reg_val;
	DBG_LOG("%s: antena %s\n", __func__, en ? "enabled" : "disabled");
	reg_val = rc522_reg_read(dev, TxControlReg);
	if (!en)
		rc522_reg_mod(dev, TxControlReg, 0x3, 0x0);
	else if ((reg_val & 0x3) != 0x3)
		rc522_reg_mod(dev, TxControlReg, 0x0, 0x3);
}

void rc522_init(struct rc522_dev *dev) {
	dev->spidev.cs = CONFIG_RC522_SPI_CS_PIN;
	dev->spidev.sclk = CONFIG_RC522_SPI_SCLK_PIN;
	dev->spidev.mosi = CONFIG_RC522_SPI_MOSI_PIN;
	dev->spidev.miso = CONFIG_RC522_SPI_MISO_PIN;
	dev->irqdev.irq_pin = CONFIG_RC522_IRQ_PIN;
	dev->rst = CONFIG_RC522_RST_PIN;

	DBG_LOG("cs: %d\n", dev->spidev.cs);
	DBG_LOG("sclk: %d\n", dev->spidev.sclk);
	DBG_LOG("mosi: %d\n", dev->spidev.mosi);
	DBG_LOG("miso: %d\n", dev->spidev.miso);
	DBG_LOG("irq: %d\n", dev->irqdev.irq_pin);
#ifdef CONFIG_RC522_NRST
	dev->rst_pol = true;
#else
	dev->rst_pol = false;
#endif
	DBG_LOG("rst: %c%d\n", dev->rst_pol ? '!' : ' ', dev->rst);
	dev->irqdev.irq_bh = &rc522_irq_bh;
	dev->irqdev.bh_data = (void *)dev;

	rc522_setup_rst(dev);
	spi_init(&dev->spidev);
	irq_init(&dev->irqdev);


	rc522_rst(dev);
//	irq_en(&dev->irqdev);
	rc522_reg_write(dev, CommandReg, CMD_RST);
	while(rc522_reg_read(dev, 0x01) & (1 << 4)) {
		vTaskDelay(50 / portTICK_PERIOD_MS);
	}

	// Reset rates
	rc522_reg_write(dev, TxModeReg, 0);
	rc522_reg_write(dev, RxModeReg, 0);

	// Modulation width
	rc522_reg_write(dev, ModWidthReg, 0x26);

	rc522_reg_write(dev, TModeReg, 0x80);
	rc522_reg_write(dev, TPrescalerReg, 0xa9);

	rc522_reg_write(dev, TReloadRegH, 0x3);
	rc522_reg_write(dev, TReloadRegL, 0xe8);

	rc522_reg_write(dev, TxASKReg, 0x40); // 100 % ASK
	rc522_reg_write(dev, ModeReg, 0x3d);

	rc522_reg_mod(dev, RFCfgReg, 0, 0x70);
	rc522_antena_en(dev, true);
}
