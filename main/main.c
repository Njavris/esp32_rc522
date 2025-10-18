#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rc522.h"

void app_main(void) {
	struct rc522_dev dev;
	memset(&dev, 0, sizeof(struct rc522_dev));

	rc522_init(&dev);

	setvbuf(stdin, NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);

	uint8_t key[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	while (1) {
		uint8_t uid[5];
		uint8_t atqa[2];
		uint8_t data[16 * 4];
		uint8_t sak;

		if (rc522_send_reqa(&dev, atqa))
			goto skip;

		printf("ReqA success ATQA: %02x %02x\n", atqa[0], atqa[1]);

		if (rc522_anticol_cl1(&dev, uid))
			goto skip;

		rc522_select_cl1(&dev, uid, &sak);
		if (sak & 0x4) {
			uint8_t uid_cl2[4];
			uint8_t sak_cl2;
			if (!rc522_anticol_cl2(&dev, uid_cl2)) {
				rc522_select_cl2(&dev, uid_cl2, &sak_cl2);
			}
		}

		if (rc522_sector_read(&dev, 0, true, key, uid, data))
			goto skip;

		for (int i = 0; i < 4; i++) {
			printf("%02x: ", i);
			for (int j = 0; j < 16; j++)
				printf("%02x ", data[i * 16 + j]);
			printf("\n");
		}

		rc522_send_halt(&dev);
skip:
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}
