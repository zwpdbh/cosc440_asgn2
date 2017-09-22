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

/**
 * The node structure for the memory page linked list.
 */
typedef struct page_node_rec {
    struct list_head list;
    struct page *page;
} page_node;

/*define my circular buffer*/
typedef struct circular_buffer *buffer;
struct circular_buffer {
    struct page *page;
    int head;
    int tail;
};

buffer bf;


/*===PAGE POOL===*/
typedef struct q_item *q_item;
typedef struct queue *queue;

struct q_item {
    struct list_head mem_list;
    int num_pages;        /* number of memory pages this item currently holds */
    size_t data_size;
    q_item next;
};

struct queue {
    q_item first;
    q_item last;
    int length;
};

queue q;
bool end_of_file = true;

queue queue_new(void) {
    queue q = kmalloc(sizeof(*q), GFP_KERNEL);
    q->first = NULL;
    q->last = NULL;
    q->length = 0;
    return q;
}

void enqueue(queue q) {
    if (q->length == 0) {
        q->first = kmalloc(sizeof(struct q_item), GFP_KERNEL);
        q->first->next = NULL;
        q->last = q->first;
    } else {
        q->last->next = kmalloc(sizeof(struct q_item), GFP_KERNEL);
        q->last = q->last->next;
        q->last->next = NULL;
    }
    
    INIT_LIST_HEAD(&q->last->mem_list);
    q->first->num_pages = 0;
    q->first->data_size = 0;
    q->length += 1;
}

struct list_head dequeue(queue q) {
    struct list_head dequeued_list_head;
    q_item tmp;
    
    if (q->length > 0) {
        dequeued_list_head = q->first->mem_list;
        tmp = q->first;
        q->first = q->first->next;
        
        // do I need to free list_head?
        //        free(&q->first->mem_list);
        kfree(tmp);
        q->length -= 1;
    }
    
    return dequeued_list_head;
}

void queue_print(queue q) {
    int i;
    q_item each;
    
    i = 0;
    each = q->first;
    
    printk(KERN_WARNING "summary of current queue:\n");
    while (each != NULL) {
        printk(KERN_WARNING "queue[%d], => %d page, %d data_size\n", i, each->num_pages, each->data_size);
        printk(KERN_WARNING "\n");
        each = each->next;
        i += 1;
    }
    printk(KERN_WARNING "end of summary of current queue\n");
    printk(KERN_WARNING "\n");
}

int queue_size(queue q) {
    return q->length;
}


//struct q_item {
//    struct list_head mem_list;
//    int num_pages;        /* number of memory pages this item currently holds */
//    size_t data_size;
//    q_item next;
//};

//void add_pages(int num) {
//    int i;
//
//    for (i = 0; i < num; i++) {
//        page_node *pg;
//        pg = kmalloc(sizeof(struct page_node_rec), GFP_KERNEL);
//        pg->page = alloc_page(GFP_KERNEL);
//        INIT_LIST_HEAD(&pg->list);
//        printk(KERN_WARNING "before adding new page, num_pages = %d\n", asgn2_device.num_pages);
//        list_add_tail(&pg->list, &asgn2_device.mem_list);
//        asgn2_device.num_pages += 1;
//        printk(KERN_WARNING "after adding new page, num_pages = %d\n", asgn2_device.num_pages);
//    }
//
//}



void queue_read(queue q, char c) {
    struct q_item *curr;
    page_node *pg;
    
    char *a;
    int offset;
    
    curr = q->first;
    
    if (curr->num_pages == 0 || curr->data_size % PAGE_SIZE == 0) {
        pg = kmalloc(sizeof(struct page_node_rec), GFP_KERNEL);
        pg->page = alloc_page(GFP_KERNEL);
        INIT_LIST_HEAD(&pg->list);
        printk(KERN_WARNING "before adding new page, num_pages = %d\n", curr->num_pages);
        list_add_tail(&pg->list, &curr->mem_list);
        curr->num_pages += 1;
        printk(KERN_WARNING "after adding new page, num_pages = %d\n", curr->num_pages);
    }
    
    offset = curr->data_size % PAGE_SIZE;
    //list_for_each_entry_reverse(pos, head, member)
    list_for_each_entry_reverse(pg, &curr->mem_list, list) {
        a = (char *)(page_address(pg->page) + offset);
        *a = c;
        curr->data_size += 1;
        break;
    }
    //
    //    /*helper function write one byte*/
    //    void circular_buffer_write(char data) {
    //        char *a;
    //        if((bf->head+1) % PAGE_SIZE == bf->tail) {
    //            printk(KERN_WARNING "circular buffer is full, just return\n");
    //            return;
    //        }
    //
    //
    //        a=(char*)(page_address(bf->page)+bf->head);
    //
    //        *a = data;
    //
    //        bf->head += 1;
    //        bf->head = bf->head%PAGE_SIZE;
    //    }
    
}


/*free all in queue*/
void queue_free(queue q) {
    q_item each;
    struct page_node_rec *pnode, *pnode_next;
    int count;
    int index;
    
    index = 0;
    printk(KERN_WARNING "===free all q_items in queues===\n");
    while (q->first != NULL) {
        each = q->first;
        q->first = q->first->next;
        // free each mem_list
        printk(KERN_WARNING "=free %dth q_item in queue=\n", index);
        count = 0;
        if (!list_empty(&each->mem_list)) {
            list_for_each_entry_safe(pnode, pnode_next, &each->mem_list, list) {
                __free_page(pnode->page);
                list_del(&pnode->list);
                kfree(pnode);
                printk(KERN_WARNING "freed %d: page\n", count);
                count += 1;
            }
        }
        kfree(each);
        
        printk(KERN_WARNING "=end of free %dth q_item in queue=\n", index);
        index += 1;
    }
    kfree(q);
    printk(KERN_WARNING "===end of free all q_items in queue===\n");
}

/*======*/

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

/*define my tasklet and tasklet handler*/
void my_tasklet_handler(unsigned long tasklet_data) {
    
    char c;
    printk(KERN_WARNING "\n");
    printk(KERN_WARNING "=executing tasklet handler...=\n");
    printk(KERN_WARNING "bf->head = %d, bf->tail = %d\n", bf->head, bf->tail);
    if(bf->tail == bf->head) {
        printk(KERN_WARNING "becuase bf->tail == bf->head, circular buffer is empty, just return, no schedule tasklet\n");
        return;
    } else {
        c =  circular_buffer_read();
        
        if (c) {
            printk(KERN_WARNING "read out c = %c from circular buffer\n", c);
            printk(KERN_WARNING "after reading, bf->head = %d, bf->tail = %d\n", bf->head, bf->tail);
            // at bottom half, read data from circular buffer into page pool
            
            queue_read(q, c);
            
            printk(KERN_WARNING "=next, read data from page pool=\n");
            printk(KERN_WARNING "\n");
        } else {
            printk(KERN_WARNING "\n meet the end of file\n");
            printk(KERN_WARNING "need to create a new mem_list in queue\n");
            enqueue(q);
            printk(KERN_WARNING "after enqueue, the length of the queue is %d\n\n", q->length);
            queue_print(q);
        }
    }
}

DECLARE_TASKLET(my_tasklet, my_tasklet_handler, 0);

u8 msbit=0;

int odd=1;

irqreturn_t dummyport_interrupt(int irq, void *dev_id) {
    u8 half=read_half_byte();
    if(odd){
        msbit=half;
    }else{
        char ascii=(char)msbit<<4|half;
        printk(KERN_WARNING "try to write char: %c into circular_buffer\n",ascii);
        circular_buffer_write(ascii);
        
        tasklet_schedule(&my_tasklet);
    }
    odd=!odd;
    return IRQ_HANDLED;
}

/*==========================================================================*/
/*==========================================================================*/
/*==========================================================================*/

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
} asgn2_dev;

asgn2_dev asgn2_device;


int asgn2_major = 0;                      /* major number of module */
int asgn2_minor = 0;                      /* minor number of module */
int asgn2_dev_count = 1;                  /* number of devices */

struct proc_dir_entry *proc;                /*for proc entry*/

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
        printk(KERN_WARNING "before adding new page, num_pages = %d\n", asgn2_device.num_pages);
        list_add_tail(&pg->list, &asgn2_device.mem_list);
        asgn2_device.num_pages += 1;
        printk(KERN_WARNING "after adding new page, num_pages = %d\n", asgn2_device.num_pages);
    }
    
}


/**
 * This function frees all memory pages held by the module.
 */
void free_memory_pages(struct asgn2_dev_t *dev) {
    
    struct page_node_rec *page_node_current_ptr, *page_node_next_ptr;
    int count = 1;
    if (!list_empty(&dev->mem_list)) {
        list_for_each_entry_safe(page_node_current_ptr, page_node_next_ptr, &dev->mem_list, list) {
            __free_page(page_node_current_ptr->page);
            list_del(&page_node_current_ptr->list);
            kfree(page_node_current_ptr);
            
            printk(KERN_WARNING "freed %d: page\n", count);
            count += 1;
        }
        dev->num_pages = 0;
        dev->data_size = 0;
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
    /* COMPLETE ME */
    /**
     * Increment process count, if exceeds max_nprocs, return -EBUSY
     *
     * if opened in write-only mode, free all memory pages
     *
     */
    //    struct asgn2_dev_t *dev;
    int nprocs;
    int max_nprocs;
    
    //    dev = container_of(inode->i_cdev, struct asgn2_dev_t, cdev);
    //    filp->private_data = dev;
    filp->private_data = &asgn2_device;
    
    
    atomic_inc(&asgn2_device.nprocs);
    
    nprocs = atomic_read(&asgn2_device.nprocs);
    max_nprocs = atomic_read(&asgn2_device.max_nprocs);
    if (nprocs > max_nprocs) {
        return -EBUSY;
    }
    
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
        free_memory_pages(&asgn2_device);
    }
    
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
    int curr_page_index;
    
    struct list_head *ptr;
    struct page_node_rec *page_ptr;
    int processing_count;
    
    //dev = filp->private_data;
    total_finished = 0;
    processing_count = 0;
    
    printk(KERN_WARNING "======READING========\n");
    printk(KERN_WARNING "*f_pos = %ld\n", (long) *f_pos);
    
    /*if the seeking position is bigger than the data_size, return 0*/
    if (*f_pos >= asgn2_device.data_size) {
        return 0;
    }
    
    page_index = *f_pos / PAGE_SIZE;
    offset = *f_pos % PAGE_SIZE;
    curr_page_index = 0;
    
    /*check the limit of amount of work needed to be done*/
    if (*f_pos + count > asgn2_device.data_size) {
        unfinished = asgn2_device.data_size - *f_pos;
    } else {
        unfinished = count;
    }
    
    ptr = asgn2_device.mem_list.next;
    /*make sure the current operating page is the page computed from *f_pos / PAGE_SIZE*/
    while (curr_page_index < page_index) {
        ptr = ptr->next;
        curr_page_index += 1;
    }
    
    
    printk(KERN_WARNING "unfinished = %d\n", unfinished);
    
    do {
        page_index = *f_pos / PAGE_SIZE;
        offset = *f_pos % PAGE_SIZE;
        
        printk(KERN_WARNING "curr_page_index = %d\n", curr_page_index);
        printk(KERN_WARNING "page_index = %d\n", page_index);
        printk(KERN_WARNING "offset = %d\n", offset);
        
        if (page_index != curr_page_index) {
            printk(KERN_WARNING "curr_page_index = %d, *f_pos / PAGE_SIZE = page_index = %d", curr_page_index, page_index);
            ptr = ptr->next;
            curr_page_index += 1;
            printk(KERN_WARNING "go to next page: %d\n", curr_page_index);
        }
        
        page_ptr = list_entry(ptr, page_node, list);
        if (unfinished > PAGE_SIZE - offset) {
            printk(KERN_WARNING "processing %ld amout of data(PAGE_SIZE - offset)\n", (long)(PAGE_SIZE - offset));
            result = copy_to_user(buf + total_finished, (page_address(page_ptr->page) + offset), PAGE_SIZE - offset);
            finished = PAGE_SIZE - offset -result;
        } else {
            printk(KERN_WARNING "processing %ld amout of data(unfinished)\n", (long int)unfinished);
            result = copy_to_user(buf + total_finished, (page_address(page_ptr->page) + offset), unfinished);
            finished = unfinished - result;
        }
        
        if (result < 0) {
            break;
        }
        
        processing_count += 1;
        unfinished -= finished;
        total_finished += finished;
        *f_pos += finished;
        
        printk(KERN_WARNING "===processing_count = %d===\n", processing_count);
        printk(KERN_WARNING "finished = %d\n", finished);
        printk(KERN_WARNING "unfinished = %d\n", unfinished);
        printk(KERN_WARNING "total_finished = %d\n", total_finished);
        printk(KERN_WARNING "\n");
    } while (unfinished >0);
    
    printk(KERN_WARNING "===END of READING, return %d===\n", total_finished);
    printk(KERN_WARNING "\n\n\n");
    return total_finished;
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
    
    seq_printf(s, "dev->nprocs = %d\n", atomic_read(&asgn2_device.nprocs));
    seq_printf(s, "dev->max_nprocs = %d\n", atomic_read(&asgn2_device.max_nprocs));
    seq_printf(s, "dev->num_pages = %d\n", asgn2_device.num_pages);
    seq_printf(s, "dev->data_size = %d\n", asgn2_device.data_size);
    
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
    atomic_set(&asgn2_device.nprocs, 0);
    atomic_set(&asgn2_device.max_nprocs, 5);
    
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
    free_memory_pages(&asgn2_device);
    
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
