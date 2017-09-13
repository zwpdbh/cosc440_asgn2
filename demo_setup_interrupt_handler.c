#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#define SHARED_IRQ 19
static int irq = SHARED_IRQ;
module_param(irq, int, S_IRUGO);

/* default delay time in top half -- try 10 to get results */
static int delay = 0;
module_param(delay, int, S_IRUGO);

static atomic_t counter_bh, counter_th;
struct my_dat {
	unsigned long jiffies;
	struct tasklet_struct tsk;
	struct work_struct work;
};
static struct my_dat my_data;

static irqreturn_t my_interrupt(int irq, void *dev_id);
static int __init my_neneric_init(void) {
	atomic_set(&counter_bh, 0);
	atomic_set(&counter_th, 0);

	/*use my_data for dev_id*/
	if(request_irq(irq, my_interrupt, IRQF_SHARED, "my_init", &my_data))
		return -1;

	printk(KERN_INFO "successfully loaded\n");
	return 0;
}

static void __exit my_generic_exit(void) {
	synchronize_irq(irq);
	free_irq(irq, &my_data);
	printk(KERN_INFO "counter_th = %d, counter_bh = %d\n", atomic_read(&counter_th), atomic_read(&counter_bh));
	printk(KERN_INFO "Successfully unloaded\n");
}
