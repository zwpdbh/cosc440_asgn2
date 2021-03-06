/**
 * File: asgn2.c
 * Date: 13/03/2011
 * Author: Your Name
 * Version: 0.1
 *
 * This is a module which serves as a virtual ramdisk which disk size is
 * limited by the amount of memory available and serves as the requirement for
 * COSC440 assignment 1 in 2012.
 *
 * Note: multiple devices and concurrent modules are not supported in this
 *       version.
 */

/* This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/sched.h>

#define MYDEV_NAME "asgn2"
#define MYIOC_TYPE 'k'
#define CHECK_SIZE 100

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhao Wei");
MODULE_DESCRIPTION("COSC440 asgn2");


/*=== start Define GPIO pins for the dummy device ===*/
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/delay.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 3, 0)
#include <asm/switch_to.h>
#else
#include <asm/system.h>
#endif
#include <mach/platform.h>

static u32 gpio_dummy_base;

/* Define GPIO pins for the dummy device */
static struct gpio gpio_dummy[] = {
    { 7, GPIOF_IN, "GPIO7" },
    { 8, GPIOF_OUT_INIT_HIGH, "GPIO8" },
    { 17, GPIOF_IN, "GPIO17" },
    { 18, GPIOF_OUT_INIT_HIGH, "GPIO18" },
    { 22, GPIOF_IN, "GPIO22" },
    { 23, GPIOF_OUT_INIT_HIGH, "GPIO23" },
    { 24, GPIOF_IN, "GPIO24" },
    { 25, GPIOF_OUT_INIT_HIGH, "GPIO25" },
    { 4, GPIOF_OUT_INIT_LOW, "GPIO4" },
    { 27, GPIOF_IN, "GPIO27" },
};


static int dummy_irq;

extern irqreturn_t dummyport_interrupt(int irq, void *dev_id);

static inline u32
gpio_inw(u32 addr)
{
    u32 data;
    asm volatile("ldr %0,[%1]" : "=r"(data) : "r"(addr));
    return data;
}

static inline void gpio_outw(u32 addr, u32 data) {
    asm volatile("str %1,[%0]" : : "r"(addr), "r"(data));
}

void setgpidfunc(u32 func, u32 alt) {
    u32 sel, data, shift;
    
    if (func > 53) {
        return;
    }
    sel = 0;
    while (func > 0) {
        func = func - 10;
        sel++;
    }
    sel = (sel << 2) + gpio_dummy_base;
    data = gpio_inw(sel);
    shift = func + (func << 1);
    data &= ~(7 << shift);
    data |= alt << shift;
    gpio_outw(sel, data);
}

u8 read_half_byte(void) {
    u32 c;
    u8 r;
    r = 0;
    c = gpio_inw(gpio_dummy_base + 0x34);
    if (c & (1 << 7)) r |= 1;
    if (c & (1 << 17)) r |= 2;
    if (c & (1 << 22)) r |= 4;
    if (c & (1 << 24)) r |= 8;
    return r;
}

static void write_to_gpio(char c) {
    volatile unsigned *gpio_set, *gpio_clear;
    gpio_set = (unsigned *)((char *)gpio_dummy_base + 0x1c);
    gpio_clear = (unsigned *)((char *)gpio_dummy_base + 0x28);
    if(c & 1) *gpio_set = 1 << 8;
    else *gpio_clear = 1 << 8;
    udelay(1);
    if(c & 2) *gpio_set = 1 << 18;
    else *gpio_clear = 1 << 18;
    udelay(1);
    if(c & 4) *gpio_set = 1 << 23;
    else *gpio_clear = 1 << 23;
    udelay(1);
    if(c & 8) *gpio_set = 1 << 25;
    else *gpio_clear = 1 << 25;
    udelay(1);
}


/*=== end Defination for GPIO ===*/

typedef struct asgn2_dev_t {
    dev_t dev;            /* the device */
    struct cdev *cdev;
    struct list_head mem_list;
    int num_pages;        /* number of memory pages this module currently holds */
    size_t data_size;     /* total data size in this module */
    atomic_t nprocs;      /* number of processes accessing this device */
    atomic_t max_nprocs;  /* max number of processes accessing this device */
    struct kmem_cache *cache;      /* cache memory */
    struct class *class;     /* the udev class */
    struct device *device;   /* the udev device node */
    unsigned long head;
    unsigned long tail;
    unsigned long total_size;
} asgn2_dev;

asgn2_dev asgn2_device;

typedef struct page_node_rec {
    struct list_head list;
    struct page *page;
} page_node;

/**
 * Helper function which add a page and set the data_size
 */
void add_pages(int num) {
    int i;
    
    for (i = 0; i < num; i++) {
        page_node *pg;
        pg = kmalloc(sizeof(struct page_node_rec), GFP_KERNEL);
        pg->page = alloc_page(GFP_KERNEL);
        INIT_LIST_HEAD(&pg->list);
        list_add_tail(&pg->list, &asgn2_device.mem_list);
        asgn2_device.num_pages += 1;
    }
    
}


/*===============================================*/
typedef struct q_item *q_item;
typedef struct queue *queue;

/* length: the length of file */
struct q_item {
    unsigned long length;
    bool complete;
    q_item next;
};


struct queue {
    q_item first;
    q_item last;
    int size;
};

queue q;

queue queue_new(void) {
    queue q = kmalloc(sizeof(*q), GFP_KERNEL);
    q->first = NULL;
    q->last = NULL;
    q->size = 0;
    
    return q;
}

void enqueue(queue q) {
    if (q->size == 0) {
        q->first = kmalloc(sizeof(struct q_item), GFP_KERNEL);
        q->first->next = NULL;
        q->last = q->first;
        
        q->first->length = 0;
        q->first->complete = false;
        
    } else {
        printk(KERN_WARNING "before enqueue, set previous file record as complete \n");
        q->last->complete = true;
        q->last->next = kmalloc(sizeof(struct q_item), GFP_KERNEL);
        q->last = q->last->next;
        q->last->length = 0;
        q->last->complete = false;
        q->last->next = NULL;
    }
    
    q->size += 1;
}

void dequeue(queue q) {
    
    q_item tmp;
    
    if (q->size > 0) {
        tmp = q->first;
        q->first = q->first->next;
        kfree(tmp);
        q->size -= 1;
    }
}

void queue_free(queue q) {
    q_item each;
    while (q->first != NULL) {
        each = q->first;
        q->first = q->first->next;
        kfree(each);
    }
    kfree(q);
}

/*==============================================*/

DECLARE_WAIT_QUEUE_HEAD(wq);

DECLARE_WAIT_QUEUE_HEAD(access_wq);


/*===============================================*/

/*define my circular buffer*/
typedef struct circular_buffer *buffer;
struct circular_buffer {
    struct page *page;
    int head;
    int tail;
};

buffer bf;

/*helper function on my buffer*/
buffer circular_buffer_new(void) {
    buffer bf;
    bf = kmalloc(sizeof(struct circular_buffer), GFP_KERNEL);
    bf->page = alloc_page(GFP_KERNEL);
    bf->head = 0;
    bf->tail = 0;
    
    return bf;
}

/*helper function write one byte*/
void circular_buffer_write(char data) {
    char *a;
    
    
    if((bf->head+1) % PAGE_SIZE == bf->tail) {
        printk(KERN_WARNING "circular buffer is full, just return\n");
        return;
    }
    
    
    
    a=(char*)(page_address(bf->page)+bf->head);
    
    *a = data;
    
    bf->head += 1;
    bf->head = bf->head%PAGE_SIZE;
}

/*helper function read one byte*/
char circular_buffer_read(void){
    char *c;
    
    c = (char *)(page_address(bf->page)+bf->tail);
    bf->tail += 1;
    bf->tail = bf->tail % PAGE_SIZE;
    return *c;
}

/*release my buffer*/
void circular_buffer_free(buffer bf) {
    __free_page(bf->page);
    kfree(bf);
}

/*print out the content in the buffer*/
void circular_buffer_print(void) {
    int count;
    int index;
    char *c;
    index = bf->tail;
    for (count = 0; count < PAGE_SIZE - 1; count++) {
        if(index == bf->head)
            break;
        c = (char *) (page_address(bf->page)+index);
        printk(KERN_WARNING "index: %d, char *c = %c.\n", index, *c);
        index++;
        index = index % PAGE_SIZE;
    }
}


/* bottom half: tasklet handler need to
 * read data from circular buff
 * and write it into the circular page pool
 */
void my_tasklet_handler(unsigned long tasklet_data) {
    char c;
    char *a;
    int page_index;
    int offset;
    int index;
    
    page_node *p;
    
    if(bf->tail == bf->head) {
        /* becuase bf->tail == bf->head, circular buffer is empty, just return,
         * no schedule tasklet */
        printk(KERN_WARNING "at bottom half: becuase bf->tail == bf->head, circular buffer is empty, just retur\n");
        return;
    }
    
    c =  circular_buffer_read();
    
    /* prepare to write c into circular page */
    if ((asgn2_device.head + 1) % asgn2_device.total_size == asgn2_device.tail) {
        printk(KERN_WARNING "asgn2_device.head = %lu, asgn2_device.tail = %lu\n", asgn2_device.head, asgn2_device.tail);
        printk(KERN_WARNING "page pool is full\n");
        return;
    }
    
    page_index = (asgn2_device.head % asgn2_device.total_size) / PAGE_SIZE;
    offset = (asgn2_device.head % asgn2_device.total_size) % PAGE_SIZE;
    
    /*loop to that position and write c in page*/
    /*later change it to use a page to hold the specific page if it is too slow*/
    index = 0;
    
    list_for_each_entry(p, &asgn2_device.mem_list, list) {
        if (index == page_index) {
            a = (char *)(page_address(p->page) + offset);
            *a = c;
            
            asgn2_device.head += 1;
            asgn2_device.head = asgn2_device.head % asgn2_device.total_size;
            asgn2_device.data_size += 1;
            break;
        }
        index += 1;
    }
    /*update queue record*/
    q->last->length += 1;
    
    /* if the accumulative data is big enough, wake the sleeping process */
    if (q->first->length % CHECK_SIZE == 0 || q->first->complete == true ) {
        printk(KERN_WARNING "In bottom half:\n");
        printk(KERN_DEBUG "q->first->length = %lu\n", q->first->length);
        printk(KERN_DEBUG "q->first->complete == %d\n", q->first->complete);
        printk(KERN_DEBUG "process %i (%s) awakening the readers...\n",
               current->pid, current->comm);
        printk(KERN_WARNING "\n");
        wake_up_interruptible(&wq);
    }
}

DECLARE_TASKLET(my_tasklet, my_tasklet_handler, 0);

u8 msbit=0;

int odd=1;

/* top half */
irqreturn_t dummyport_interrupt(int irq, void *dev_id) {
    u8 half=read_half_byte();
    if(odd){
        msbit=half;
    }else{
        char ascii = (char)msbit<<4|half;
        if (ascii) {
            circular_buffer_write(ascii);
            tasklet_schedule(&my_tasklet);
        } else {
            /*when it meet the end of file*/
            enqueue(q);
            /*meet one end of file, need to wake up*/
            printk(KERN_WARNING "In the top hafl, meet one end of file, need to wake up\n");
            printk(KERN_DEBUG "process %i (%s) awakening the readers...\n", current->pid, current->comm);
            printk(KERN_WARNING "\n");
            wake_up_interruptible(&wq);
        }
        
    }
    odd=!odd;
    return IRQ_HANDLED;
}

/*==========================================================================*/
/*==========================================================================*/
/*==========================================================================*/


int asgn2_major = 0;                      /* major number of module */
int asgn2_minor = 0;                      /* minor number of module */
int asgn2_dev_count = 1;                  /* number of devices */

struct proc_dir_entry *proc;                /*for proc entry*/


void free_memory_pages(struct asgn2_dev_t *dev, int free_num_of_pages, long data_size) {
    struct page_node_rec *page_node_current_ptr, *page_node_next_ptr;
    int count = 0;
    if (!list_empty(&dev->mem_list)) {
        list_for_each_entry_safe(page_node_current_ptr, page_node_next_ptr, &dev->mem_list, list) {
            __free_page(page_node_current_ptr->page);
            list_del(&page_node_current_ptr->list);
            kfree(page_node_current_ptr);
            
            count += 1;
            printk(KERN_WARNING "Have freed %d pages\n", count);
            if (count == free_num_of_pages) {
                break;
            }
        }
        dev->num_pages -= free_num_of_pages;
        dev->data_size -= data_size;
    }
}

/**
 * This function opens the virtual disk, if it is opened in the write-only
 * mode, all memory pages will be freed.
 */
/**
 * From book: should perform the following tasks:
 * 1. check for device-specific erros
 * 2. initialize the device if it is being opened for the first time
 * 3. update the f_op pointer, if necessary
 * 4. allocate and fill any data structure to be put in filp->private_data
 */
int asgn2_open(struct inode *inode, struct file *filp) {
    
    filp->private_data = &asgn2_device;
    
    /*only allow read operation*/
    if ((filp->f_flags & O_ACCMODE) != O_RDONLY) {
        return -1;
    }
    
    if (atomic_read(&asgn2_device.nprocs) == 1) {
        printk(KERN_WARNING "only one process can read at a time, put current access process into sleep\n");
        wait_event_interruptible_exclusive(access_wq, atomic_read(&asgn2_device.nprocs) == 0);
    }
    
    atomic_inc(&asgn2_device.nprocs);
    return 0; /* success */
}


/**
 * This function releases the virtual disk, but nothing needs to be done
 * in this case.
 */
int asgn2_release (struct inode *inode, struct file *filp) {
    /* COMPLETE ME */
    /**
     * decrement process count
     */
    if(atomic_read(&asgn2_device.nprocs) > 0) {
        atomic_sub(1, &asgn2_device.nprocs);
    }
    wake_up_interruptible(&access_wq);
    return 0;
}



/**
 * This function reads contents of the virtual disk and writes to the user
 */
ssize_t asgn2_read(struct file *filp, char __user *buf, size_t count,
                   loff_t *f_pos) {
    /*record offset in current page*/
    size_t offset;
    
    size_t unfinished;
    size_t result;
    size_t finished;
    size_t total_finished;
    
    int page_index;
    int index;
    
    page_node *p;
    
    int processing_count;
    
    total_finished = 0;
    processing_count = 1;
    
    printk(KERN_WARNING "\n\n\n");
    printk(KERN_WARNING "======READING========\n");
    
    if (q->first->complete == false && q->first->length < CHECK_SIZE) {
        printk(KERN_WARNING "No data available right now, put the current process into sleep\n");
        printk(KERN_DEBUG "process %i (%s) going to sleep\n", current->pid, current->comm);
        printk(KERN_DEBUG "\n");
        wait_event_interruptible(wq, (q->first->complete == true || q->first->length > CHECK_SIZE));
        printk(KERN_WARNING "==after waking from sleep==\n");
    }
    
    
    printk(KERN_WARNING "*f_pos = %ld\n", (long int) *f_pos);
    printk(KERN_WARNING "q->first->length = %lu\n", q->first->length);
    printk(KERN_WARNING "asgn2_device.data_size = %lu\n", (unsigned long)asgn2_device.data_size);
    printk(KERN_WARNING "asgn2_device.head = %lu\n", asgn2_device.head);
    printk(KERN_WARNING "asgn2_device.tail = %lu\n", asgn2_device.tail);
    
    if (*f_pos + count > q->first->length) {
        unfinished = q->first->length;
    } else {
        unfinished = count;
    }
    printk(KERN_WARNING "unfinished = %d\n", unfinished);
    printk(KERN_WARNING "\n");
    
    /*when there is some data to read*/
    if (q->first->length > 0) {
        printk(KERN_WARNING "===begin processing ===\n");
        printk(KERN_DEBUG "unfinished = %d\n", unfinished);
        
        do {
            index = 0;
            finished = 0;
            printk(KERN_WARNING "==processing_count = %d==\n", processing_count);
            printk(KERN_DEBUG "*f_pos = %ld\n", (long int) *f_pos);
            
            page_index = ((asgn2_device.tail) % asgn2_device.total_size) / PAGE_SIZE;
            offset = ((asgn2_device.tail) % asgn2_device.total_size) % PAGE_SIZE;
            
            list_for_each_entry(p, &asgn2_device.mem_list, list) {
                if (index == page_index) {
                    if (unfinished > PAGE_SIZE - offset) {
                        result = copy_to_user(buf + total_finished, (page_address(p->page) + offset), PAGE_SIZE - offset);
                        finished = PAGE_SIZE - offset -result;
                        printk(KERN_WARNING "finished (PAGE_SIZE - offset) = %lu\n", (unsigned long) finished);
                    } else {
                        result = copy_to_user(buf + total_finished, (page_address(p->page) + offset), unfinished);
                        finished = unfinished - result;
                        printk(KERN_WARNING "finished (unfinished) = %lu\n", (unsigned long) finished);
                    }
                    break;
                }
                index += 1;
            }
            
            if (result < 0) {
                break;
            }
            
            processing_count += 1;
            unfinished -= finished;
            total_finished += finished;
            *f_pos += finished;
            q->first->length -= finished;
            
            asgn2_device.tail += finished;
            asgn2_device.tail = asgn2_device.tail % asgn2_device.total_size;
            printk(KERN_WARNING "asgn2_device.tail = %lu\n", asgn2_device.tail);
            asgn2_device.data_size -= finished;
            printk(KERN_WARNING "asgn2_device.data_size = %lu\n", (unsigned long)asgn2_device.data_size);
            
            printk(KERN_DEBUG "unfinished = %d\n", unfinished);
            printk(KERN_DEBUG "total_finished = %d\n", total_finished);
            printk(KERN_DEBUG "q->first->length = %lu\n", q->first->length);
            printk(KERN_DEBUG "\n");
            
        } while (unfinished > 0);
        
        printk(KERN_WARNING "===END of READING, return %d===\n", total_finished);
        printk(KERN_WARNING "\n\n\n");
        return total_finished;
    } else {
        printk(KERN_WARNING "because q->first->length <= 0, no further reading\n");
    }
    
    if (q->first->length == 0 && q->first->complete == true) {
        dequeue(q);
    }
    
    printk(KERN_WARNING "===END of READING, return %d===\n", total_finished);
    printk(KERN_DEBUG "\n\n\n");
    return 0;
}



#define SET_NPROC_OP 1
#define TEM_SET_NPROC _IOW(MYIOC_TYPE, SET_NPROC_OP, int)

/**
 * The ioctl function, which nothing needs to be done in this case.
 *
 */
long asgn2_ioctl (struct file *filp, unsigned cmd, unsigned long arg) {
    int nr;
    int new_nprocs;
    int result;
    
    /* COMPLETE ME */
    /**
     * check whether cmd is for our device, if not for us, return -EINVAL
     *
     * get command, and if command is SET_NPROC_OP, then get the data, and
     set max_nprocs accordingly, don't forget to check validity of the
     value before setting max_nprocs
     */
    if(_IOC_TYPE(cmd) != MYIOC_TYPE) {
        return -EINVAL;
    }
    nr = _IOC_NR(cmd);
    
    switch (nr) {
        case SET_NPROC_OP:
            result=copy_from_user((int*)&new_nprocs,(int*)arg,sizeof(int));
            
            if(result!=0) return -EINVAL;
            if(new_nprocs > atomic_read(&asgn2_device.nprocs)){
                atomic_set(&asgn2_device.max_nprocs, new_nprocs);
                return 0;
            }
            break;
            
        default:
            break;
    }
    return -ENOTTY;
}

struct file_operations asgn2_fops = {
    .owner = THIS_MODULE,
    .read = asgn2_read,
    /*    .write = asgn2_write, */
    .unlocked_ioctl = asgn2_ioctl,
    .open = asgn2_open,
    .release = asgn2_release,
};


static void *my_seq_start(struct seq_file *s, loff_t *pos)
{
    if(*pos >= 1) return NULL;
    else return &asgn2_dev_count + *pos;
}

static void *my_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
    (*pos)++;
    if(*pos >= 1) return NULL;
    else return &asgn2_dev_count + *pos;
}

static void my_seq_stop(struct seq_file *s, void *v)
{
    /* There's nothing to do here! */
}

int my_seq_show(struct seq_file *s, void *v) {
    /* COMPLETE ME */
    /**
     * use seq_printf to print some info to s
     */
    q_item each;
    int i;
    
    seq_printf(s, "dev->nprocs = %d\n", atomic_read(&asgn2_device.nprocs));
    seq_printf(s, "dev->max_nprocs = %d\n", atomic_read(&asgn2_device.max_nprocs));
    seq_printf(s, "dev->num_pages = %d\n", asgn2_device.num_pages);
    seq_printf(s, "dev->data_size = %d\n", asgn2_device.data_size);
    seq_printf(s, "dev->head = %lu\n", asgn2_device.head);
    seq_printf(s, "dev->tail = %lu\n", asgn2_device.tail);
    
    i = 0;
    each = q->first;
    
    while (each != NULL) {
        seq_printf(s, "index: %d, length: %lu, complete: %d\n", i, each->length, each->complete);
        each = each->next;
        i += 1;
    }
    
    return 0;
}


static struct seq_operations my_seq_ops = {
    .start = my_seq_start,
    .next = my_seq_next,
    .stop = my_seq_stop,
    .show = my_seq_show
};


static int my_proc_open(struct inode *inode, struct file *filp)
{
    return seq_open(filp, &my_seq_ops);
}



struct file_operations asgn2_proc_ops = {
    .owner = THIS_MODULE,
    .open = my_proc_open,
    .llseek = seq_lseek,
    .read = seq_read,
    .release = seq_release,
};


int gpio_dummy_init(void)
{
    int ret;
    
    gpio_dummy_base = (u32)ioremap_nocache(BCM2708_PERI_BASE + 0x200000, 4096);
    printk(KERN_WARNING "The gpio base is mapped to %x\n", gpio_dummy_base);
    ret = gpio_request_array(gpio_dummy, ARRAY_SIZE(gpio_dummy));
    
    if (ret) {
        printk(KERN_ERR "Unable to request GPIOs for the dummy device: %d\n", ret);
        goto fail2;
    }
    ret = gpio_to_irq(gpio_dummy[ARRAY_SIZE(gpio_dummy)-1].gpio);
    if(ret < 0) {
        printk(KERN_ERR "Unable to request IRQ for gpio %d: %d\n", gpio_dummy[ARRAY_SIZE(gpio_dummy)-1].gpio, ret);
        goto fail1;
    }
    dummy_irq = ret;
    printk(KERN_WARNING "Successfully requested IRQ# %d for %s\n", dummy_irq, gpio_dummy[ARRAY_SIZE(gpio_dummy)-1].label);
    
    ret = request_irq(dummy_irq, dummyport_interrupt, IRQF_TRIGGER_RISING | IRQF_ONESHOT, "gpio27", NULL);
    
    if(ret) {
        printk(KERN_ERR "Unable to request IRQ for dummy device: %d\n", ret);
        goto fail1;
    }
    write_to_gpio(15);
    
    bf = circular_buffer_new();
    q = queue_new();
    enqueue(q);
    
    return 0;
    
fail1:
    gpio_free_array(gpio_dummy, ARRAY_SIZE(gpio_dummy));
fail2:
    iounmap((void *)gpio_dummy_base);
    return ret;
}

void gpio_dummy_exit(void)
{
    free_irq(dummy_irq, NULL);
    gpio_free_array(gpio_dummy, ARRAY_SIZE(gpio_dummy));
    iounmap((void *)gpio_dummy_base);
}

/**
 * Initialise the module and create the master device
 */
int __init asgn2_init_module(void){
    int rv;
    dev_t devno = MKDEV(asgn2_major, 0);
    
    if (asgn2_major) {
        rv = register_chrdev_region(devno, 1, MYDEV_NAME);
        if (rv < 0) {
            printk(KERN_WARNING "Can't use the major number %d; try atomatic allocation...\n", asgn2_major);
            rv = alloc_chrdev_region(&devno, 0, 1, MYDEV_NAME);
            asgn2_major = MAJOR(devno);
        }
    } else {
        rv = alloc_chrdev_region(&devno, 0, 1, MYDEV_NAME);
        asgn2_major = MAJOR(devno);
    }
    
    if (rv < 0) {
        return rv;
    }
    /*Initialize each fields of asgn2_device*/
    memset(&asgn2_device, 0, sizeof(struct asgn2_dev_t));
    asgn2_device.cdev = kmalloc(sizeof(struct cdev), GFP_KERNEL);
    cdev_init(asgn2_device.cdev, &asgn2_fops);
    asgn2_device.cdev->owner = THIS_MODULE;
    INIT_LIST_HEAD(&asgn2_device.mem_list);
    asgn2_device.num_pages = 0;
    asgn2_device.data_size = 0;
    asgn2_device.head = 0;
    asgn2_device.tail = 0;
    asgn2_device.total_size = 0;
    atomic_set(&asgn2_device.nprocs, 0);
    atomic_set(&asgn2_device.max_nprocs, 1);
    
    rv = cdev_add(asgn2_device.cdev, devno, 1);
    if (rv) {
        printk(KERN_WARNING "Error %d adding device %s", rv, MYDEV_NAME);
    }
    
    asgn2_device.class = class_create(THIS_MODULE, MYDEV_NAME);
    if (IS_ERR(asgn2_device.class)) {
        cdev_del(asgn2_device.cdev);
        unregister_chrdev_region(devno, 1);
        printk(KERN_WARNING "%s: can't create udev class\n", MYDEV_NAME);
        rv = -ENOMEM;
        return rv;
    }
    
    asgn2_device.device = device_create(asgn2_device.class, NULL, MKDEV(asgn2_major, 0), "%s", MYDEV_NAME);
    if (IS_ERR(asgn2_device.device)) {
        class_destroy(asgn2_device.class);
        cdev_del(asgn2_device.cdev);
        unregister_chrdev_region(devno, 1);
        printk(KERN_WARNING "%s: can't create udev device\n", MYDEV_NAME);
        rv = -ENOMEM;
        return rv;
    }
    
    /*create proc entry*/
    proc = proc_create_data(MYDEV_NAME, 0, NULL, &asgn2_proc_ops, NULL);
    
    /*initialize GPIO device*/
    printk(KERN_WARNING "creating GPIO device\n");
    printk(KERN_WARNING "initializing 10 pages\n");
    add_pages(10);
    asgn2_device.total_size = asgn2_device.num_pages * PAGE_SIZE;
    printk(KERN_WARNING "\n\n\n");
    printk(KERN_WARNING "===Create %s driver succeed.===\n", MYDEV_NAME);
    
    return gpio_dummy_init();
}


/**
 * Finalise the module
 */
void __exit asgn2_exit_module(void){
    device_destroy(asgn2_device.class, MKDEV(asgn2_major, 0));
    class_destroy(asgn2_device.class);
    free_memory_pages(&asgn2_device, asgn2_device.num_pages, asgn2_device.data_size);
    
    cdev_del(asgn2_device.cdev);
    kfree(asgn2_device.cdev);
    
    remove_proc_entry(MYDEV_NAME, NULL);
    unregister_chrdev_region(MKDEV(asgn2_major, 0), 1);
    printk(KERN_WARNING "===release GPIO device\n");
    gpio_dummy_exit();
    circular_buffer_free(bf);
    queue_free(q);
    printk(KERN_WARNING "===GOOD BYE from %s===\n", MYDEV_NAME);
    
}





module_init(asgn2_init_module);
module_exit(asgn2_exit_module);

