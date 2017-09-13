#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/init.h>

typedef struct simp_t {
	int i;
	int j;
} simp;

static simp t_data;
