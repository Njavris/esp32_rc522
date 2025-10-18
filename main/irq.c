#include "irq.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static QueueHandle_t irq_queue = NULL;

static void IRAM_ATTR irq_handler(void* arg)
{
	uint32_t val = 0x69;
	xQueueSendFromISR(irq_queue, &val, NULL);
}

static void irq_bottom_half(void* arg)
{
	struct irq_dev *dev = (struct irq_dev *)arg;
	uint32_t val;
	for (;;) {
		if (xQueueReceive(irq_queue, &val, portMAX_DELAY)) {
			if (dev->irq_bh) {
				dev->irq_bh(dev->bh_data);
			}
		}
	}
}

void irq_en(struct irq_dev *dev) {
	gpio_isr_handler_add(dev->irq_pin, irq_handler, NULL);
}

void irq_dis(struct irq_dev *dev) {
	gpio_isr_handler_remove(dev->irq_pin);
}

void irq_init(struct irq_dev *dev) {
	irq_queue = xQueueCreate(10, sizeof(uint32_t));

	gpio_set_intr_type(dev->irq_pin, GPIO_INTR_ANYEDGE);
	gpio_install_isr_service(0);

	xTaskCreate(irq_bottom_half, "rc522_crq_bh", 2048, dev, 10, NULL);
}
