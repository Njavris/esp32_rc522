#ifndef __IRQ_H__
#define __IRQ_H__

struct irq_dev {
	int irq_pin;
	void (*irq_bh)(void * data);
	void *bh_data;
};

void irq_init(struct irq_dev *dev);
void irq_en(struct irq_dev *dev);
void irq_dis(struct irq_dev *dev);


#endif // __IRQ_H__
