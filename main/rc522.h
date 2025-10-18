#ifndef __RC522_H__
#define __RC522_H__

#include "spi.h"
#include "irq.h"

enum rc522_result_e {
	rc522_res_ok = 0,
	rc522_res_timeout = -1,
	rc522_res_error = -2,
	rc522_res_nak = -3,
};

typedef enum rc522_result_e rc522_result_t;

struct rc522_dev {
	int rst;
	bool rst_pol;
	struct spi_dev spidev;
	struct irq_dev irqdev;
};

void rc522_init(struct rc522_dev *dev);
void rc522_antena_en(struct rc522_dev *dev, bool en);

rc522_result_t rc522_send_reqa(struct rc522_dev *dev, uint8_t atqa[2]);
rc522_result_t rc522_send_halt(struct rc522_dev *dev);
rc522_result_t rc522_anticol_cl1(struct rc522_dev *dev, uint8_t ret[5]);
rc522_result_t rc522_select_cl1(struct rc522_dev *dev, uint8_t uid[5], uint8_t *sak);
rc522_result_t rc522_anticol_cl2(struct rc522_dev *dev, uint8_t ret[4]);
rc522_result_t rc522_select_cl2(struct rc522_dev *dev, const uint8_t uid[4],
								uint8_t *sak);

rc522_result_t rc522_block_read(struct rc522_dev *dev, uint8_t block, bool keyA,
		const uint8_t key[6], const uint8_t uid[4], uint8_t ret[16]);
rc522_result_t rc522_block_write(struct rc522_dev *dev, uint8_t block, bool keyA,
		const uint8_t key[6], const uint8_t uid[4], const uint8_t data[16]);

rc522_result_t rc522_sector_read(struct rc522_dev *dev, uint8_t sector,
	bool keyA, const uint8_t key[6], const uint8_t uid[4], uint8_t *ret);
rc522_result_t rc522_sector_write(struct rc522_dev *dev, uint8_t sector,
	bool keyA, const uint8_t key[6], const uint8_t uid[4], const uint8_t *data);

#endif // __RC522_H__
