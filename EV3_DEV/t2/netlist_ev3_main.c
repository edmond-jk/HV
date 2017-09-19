#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fcntl.h>  /* O_ACCMODE */
#include <linux/hdreg.h>  /* HDIO_GETGEO */
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/unistd.h>
#include <linux/pci_regs.h>
#include <linux/aer.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/wait.h>

//#define DISABLE_DESCRIPTOR_DEBUG

// init_MUTEX is not in 2.6.37 and later kernels
#ifndef init_MUTEX
#define init_MUTEX(_m) sema_init(_m,1) 
#endif

#include "netlist_ev3_ioctl.h"
#include "netlist_ev3_main.h"

#include <asm/uaccess.h>
#include <asm/io.h>

#if defined(__powerpc__)
#if defined(__powerpc64__)	/* 64bit version */
#define rdtscll(val)					\
	do {								\
		__asm__ __volatile__ ("mfspr %0, 268" : "=r" (val));	\
	} while(0)
#else	/*__powerpc__ 32bit version */
#define rdtscll(val)							\
	 do {								\
		uint32_t tbhi, tblo ;					\
		__asm__ __volatile__ ("mftbl %0" : "=r" (tblo));	\
		val = 1000 * ((uint64_t) tbhi << 32) | tblo;		\
	} while(0)
#endif
#endif

/* Version Information */
#define DRIVER_REV 1
#define DRIVER_VER 14
#define DRIVER_VERSION "v1.test"
#define DRIVER_AUTHOR "Netlist"
#define DRIVER_DESC "Netlist EXPRESSvault 3 PCIe Card Module Driver"
#define DRIVER_DATE "02/21/2017"

#define NUM_STACK_VECS 32
#define EV_ACTIVATE_LIMIT 100   // For IOCTL based READ/WRITE activate only.
#define AUTO_ERASE_DELAY_MS 500 // 1000 works 125 is too low, 250 works - I doubled it.

#define ABORT_CODE (-EIO)

// Defined by JK
DECLARE_WAIT_QUEUE_HEAD(WaitQueue_Producer); 
DECLARE_WAIT_QUEUE_HEAD(WaitQueue_Consumer); 
static struct task_struct *netlist_req_producer; 
static struct task_struct *netlist_req_consumer; 

/* Command Line arguments */
static int max_devices = EV_MAXCARDS;
module_param(max_devices, int, 0);
MODULE_PARM_DESC(max_devices, "Maximum number of cards");

//static int max_sgl = DEFAULT_MAX_OUTSTANDING_DMAS;
static int max_sgl = 1;
module_param(max_sgl, int, 0);
MODULE_PARM_DESC(max_sgl, "Maximum Outstanding DMAs");

static int max_dmas = DEFAULT_MAX_DMAS_QUEUED_TO_HW;
module_param(max_dmas, int, 0);
MODULE_PARM_DESC(max_dmas, "Maximum queued DMAs");

static int max_descriptors = DEFAULT_MAX_DESCRIPTORS_QUEUED_TO_HW;
module_param(max_descriptors, int, 0);
MODULE_PARM_DESC(max_descriptors, "Maximum queued descriptors");

static int auto_save = DEFAULT_AUTO_SAVE;
module_param(auto_save, int, 0);
MODULE_PARM_DESC(auto_save, "Auto Save");

static int auto_restore = DEFAULT_AUTO_RESTORE;
module_param(auto_restore, int, 0);
MODULE_PARM_DESC(auto_restore, "Auto Restore");

static int auto_erase = DEFAULT_AUTO_ERASE;
module_param(auto_erase, int, 0);
MODULE_PARM_DESC(auto_erase, "Auto Erase");

static int enable_ecc = DEFAULT_ENABLE_ECC; // Will enable ECC if and only if the FPGA version is capable of reporting it.
module_param(enable_ecc, int, 0);
MODULE_PARM_DESC(enable_ecc, "Enable ECC");

static int skip_sectors = 100; // Some non-zero non-one value to mean it has not been explicitely set at load time.
module_param(skip_sectors, int, 0);
MODULE_PARM_DESC(skip_sectors, "Skip Sectors");

static int capture_stats = DEFAULT_CAPTURE_STATS;  // Use with caution - this reads the high performance counter on each IO. 
module_param(capture_stats, int, 0);
MODULE_PARM_DESC(capture_stats, "Capture Stats");

// On a link up this is the number of lanes which is expected.
// Set to 0 to avoid any action is the expected number of lanes does not match
// the actual number of lanes.
static int lanes_expected = DEFAULT_NUM_EXPECTED_LANES; 
module_param(lanes_expected, int, 0); 
MODULE_PARM_DESC(lanes_expected, "PCIe Lanes Expected");

#ifdef USING_SGIO
// These are used by the IOCTL based test IO path.
static int max_queued_sgio = 2048;
module_param(max_queued_sgio, int, 0);
MODULE_PARM_DESC(max_queued_sgio, "Maximum number of queued SgVec");

static int min_avail_sgio = 128;
module_param(min_avail_sgio, int, 0);
MODULE_PARM_DESC(min_avail_sgio, "Minimum number of free SgVec before waking up process");
#endif

int poll_cycle_time_ns = DEFAULT_POLLS_DELAY_NS;  // Used only if polled mode is set. Is also used as timeout for coalescing.
module_param(poll_cycle_time_ns, int, 0); 
MODULE_PARM_DESC(poll_cycle_time_ns, "Poll delay in nanoseconds");

int polling_mode = DEFAULT_ENABLE_POLLING;  
module_param(polling_mode, int, 0); 
MODULE_PARM_DESC(polling_mode, "TRUE means polled operation, FALSE means interrupt driven");

int dma_int_factor = DEFAULT_DMA_INTERRUPT_FACTOR; // Interrupt coalescing  
module_param(dma_int_factor, int, 0); 
MODULE_PARM_DESC(dma_int_factor, "Generate an interrupt every nth DMA request");

int data_logger_sample_time_secs = DEFAULT_DATA_LOGGER_SAMPLE_TIME_SECONDS; // Capture time
module_param(data_logger_sample_time_secs, int, 0); 
MODULE_PARM_DESC(data_logger_sample_time_secs, "Time between data logger samples");

int data_logger_sample_length = DEFAULT_DATA_LOGGER_SAMPLES; // Number of latest samples to capture.
module_param(data_logger_sample_length, int, 0); 
MODULE_PARM_DESC(data_logger_sample_length, "Number of samples to capture");

#ifdef COMPILE_IOSTAT
int iostat_enable = DEFAULT_IO_STAT_SUPPORT; // Enable/disable IOSTAT support
module_param(iostat_enable, int, 0); 
MODULE_PARM_DESC(iostat_enable, "IOSTAT support");
#endif

int max_partitions = EV_MINORS; // Number of maximum partitions plus 1. Extra entry is needed. 
module_param(max_partitions, int, 0); 
MODULE_PARM_DESC(max_partitions, "Maximum number of block driver partitions plus one");

int max_loop_counter = DEFAULT_LOOP_COUNTER; // Number of completions before tasklet or worker thread relinquishes CPU
module_param(max_loop_counter, int, 0); 
MODULE_PARM_DESC(max_loop_counter, "Maximum completions loop counter");

#ifdef USE_WORK_QUEUES
int num_workqueues = DEFAULT_NUM_WORKQUEUES; // USE_WORK_QUEUES only: Number of worker threads used for completions
module_param(num_workqueues, int, 0); 
MODULE_PARM_DESC(num_workqueues, "Number of work queue threads");
#endif

// Provide support for some older kernels
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
/**
 * ktime_get - get the monotonic time in ktime_t format
 *
 * returns the time in ktime_t format
 */
static ktime_t ktime_get(void)
{
        struct timespec now;

        ktime_get_ts(&now);

        return timespec_to_ktime(now);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
// This name changed somewhere between 2.6.18 and 2.6.28.1 
// I have not found exactly which version yet - TBD.
#define HRTIMER_MODE_REL HRTIMER_REL
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
// This was added sometime between 2.6.18 and 2.6.28.1
/**
 * list_splice_tail - join two lists, each list being a queue
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static inline void list_splice_tail(struct list_head *list,
                                struct list_head *head)
{
        if (!list_empty(list))
                __list_splice(list, head->prev);
}
#endif


// Globals
static int b_major_nr; // Major number for block devices
static int c_major_nr; // Major number for character devices
static struct cardinfo *cards = NULL; // Pointer to cardinfo structure array.
static int num_cards = 0; // Number of cards detected

static int pio_read_write(struct cardinfo *card, int read_write, ev_buf_t* buf);
static int netlist_ioctl(struct inode *i, struct file *f,    unsigned int cmd, unsigned long arg);
#if defined(USING_SGIO)
#if defined(HAVE_UNLOCKED_IOCTL) || defined(HAVE_COMPAT_IOCTL)
static long netlist_unlocked_ioctl(struct file *filp, u_int iocmd,unsigned long arg);
#endif
#endif
//JK
static int activate_ev_request(struct cardinfo* card, ev_request_t* pRequest);
static int issue_ev_request(struct cardinfo* card);
static int enqueue_ev_request(struct cardinfo *card);
static int initialize_ev_request(ev_request_t *ev_req);
static int process_interrupt_handler(struct cardinfo *card);
static void netlist_process_finalization(struct cardinfo* card);

static int ev_activate(struct cardinfo *card);
static int ev_start_io(struct cardinfo *card);
static int netlist_cleanup_body(void);
#ifdef TBD
static int ev_fill_ram(struct cardinfo *card, unsigned long long pattern);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
static int __devinit netlist_pci_probe(struct pci_dev *dev, const struct pci_device_id *id);
#else
static int netlist_pci_probe(struct pci_dev *dev, const struct pci_device_id *id);
#endif
static void netlist_pci_remove(struct pci_dev *dev);
static int netlist_pci_remove_body(struct cardinfo *card);
static void init_dma_timer(struct cardinfo *card);
void del_dma_timer(struct cardinfo *card, int sgl_no);
static void ev_timeout_check_dma(unsigned long context);
static void ev_polling_timer(unsigned long context);
static irqreturn_t process_interrupt(struct cardinfo *card);
static int chip_reset(struct cardinfo *card);
static void netlist_abort_with_error(struct cardinfo *card);
static void netlist_abort_io(struct cardinfo *card, int status_code); 

/* OS Interface functions */
#if defined(USING_BIO)
static int netlist_blk_open(struct block_device *dev, fmode_t mode);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)
static int netlist_blk_release(struct gendisk *disk, fmode_t mode);
#else
static void netlist_blk_release(struct gendisk *disk, fmode_t mode);
#endif
static int netlist_blk_ioctl(struct block_device *dev, fmode_t mode, unsigned cmd, unsigned long arg);
static int netlist_blk_get_geo(struct block_device *bdev, struct hd_geometry *geo);
static int netlist_blk_media_changed(struct gendisk *disk);
static int netlist_blk_revalidate(struct gendisk *disk);

// The device operations structure for the block device driver
static struct block_device_operations netlist_blk_ops = 
{
    .owner              = THIS_MODULE,
    .open               = netlist_blk_open,
    .release            = netlist_blk_release,
    .ioctl              = netlist_blk_ioctl,
    .getgeo             = netlist_blk_get_geo,
    .media_changed      = netlist_blk_media_changed,
    .revalidate_disk    = netlist_blk_revalidate,
};

// Other BIO related routines.
static int add_block_disk(struct cardinfo *card);
static int add_bio(struct cardinfo *card);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)
static int ev_make_request(struct request_queue *q, ev_bio *bio);
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
static void ev_make_request(struct request_queue *q, ev_bio *bio);
#else
blk_qc_t ev_make_request(struct request_queue *q, ev_bio *bio);
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
static void ev_unplug_device(struct request_queue *q);
#endif
#endif

#if defined(USING_SGIO) 
static int netlist_chr_open(struct inode *inode, struct file *filp);
static int netlist_chr_release(struct inode *inode, struct file *filp);
static int netlist_chr_mmap(struct file *filp, struct vm_area_struct *vma);
static ssize_t netlist_chr_read(struct file *filp, char __user *buf, size_t count, loff_t *offp);
static ssize_t netlist_chr_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp);
static loff_t netlist_chr_llseek(struct file *filp, loff_t off, int whence);

static struct file_operations netlist_chr_ops = 
{
    .owner = THIS_MODULE,
    .open = netlist_chr_open,
    .release = netlist_chr_release,
#if defined(HAVE_UNLOCKED_IOCTL) || defined(HAVE_COMPAT_IOCTL)
    .unlocked_ioctl = netlist_unlocked_ioctl,
#else
    .ioctl = netlist_ioctl,
#endif
    .mmap = netlist_chr_mmap,
    .read = netlist_chr_read,
    .write = netlist_chr_write,
    .llseek = netlist_chr_llseek,
};

// Other SGIO related routines.
static inline int ev_do_sgio_write(struct cardinfo *card, struct EvIoReq *req, int sync);
static int ev_do_sgio_read(struct cardinfo *card, struct EvIoReq *req, int sync);
static inline int ev_chr_init_kiocb(struct mm_kiocb *iocb, uint32_t nvec, void *cookie);
static inline int ev_chr_init_sgio(struct sgio *io, mm_sgio_op op, struct mm_kiocb *iocb, struct SgVec *vec, int kernel_page, int sync);
static int mm_unmap_user_buf(struct sgio *io);
static inline int mm_do_zero_copy(uint64_t len);
static int mm_map_user_buf(struct sgio *io, uint64_t uaddr, unsigned long len, int write_to_vm);
static int netlist_ioc_process_io_req(struct cardinfo *card, struct EvIoReq *req, mm_sgio_op op, int sync);
static int mm_iocb_completion(struct mm_kiocb *iocb, struct cardinfo *card);
static int ev_get_io_events(struct cardinfo *card, unsigned long arg);

static int add_sgio(struct cardinfo *card);
static int add_mapped_io(struct cardinfo *card); // Use DMA
static int sgl_map_user_pages(const unsigned int max_pages, unsigned long uaddr, size_t count, int rw, struct page ***mapped_pages, int *page_offset);
static void sgl_unmap_user_pages(const unsigned int num_pages, struct page ***mapped_pages);
#endif

// Structures used by the PCI device are next
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
static const struct pci_device_id __devinitdata netlist_pci_ids[] = 
#else
static const struct pci_device_id netlist_pci_ids[] = 
#endif
{
    {
        .vendor = EV_VENDOR_ID,
        .device = EV_DEVICE_ID_BAR32_WINDOW_32M_4GB,
        .subvendor = PCI_ANY_ID,
        .subdevice = PCI_ANY_ID,
    },
    {
        .vendor = EV_VENDOR_ID,
        .device = EV_DEVICE_ID_BAR32_WINDOW_32M_8GB,
        .subvendor = PCI_ANY_ID,
        .subdevice = PCI_ANY_ID,
    },
    {
        .vendor = EV_VENDOR_ID,
        .device = EV_DEVICE_ID_BAR32_WINDOW_32M_16GB,
        .subvendor = PCI_ANY_ID,
        .subdevice = PCI_ANY_ID,
    },
    { 
        // end: all zeroes
        .vendor = 0,
        .device = 0,
        .subvendor = 0,
        .subdevice = 0,
    }
};

MODULE_DEVICE_TABLE(pci, netlist_pci_ids);

static void print_ev_request_pool(struct cardinfo* card)
{
    struct ev_dma_desc* desc;
    int index;
    ev_request_t* p;

    EV_DEBUG_ALWAYS("nEnqueued:%d, nIssued:%d, nProcessed:%d, nCompleted:%d\n", 
                        card->nEnqueued, card->nIssued, card->nProcessed, card->nCompleted);
    for (index = 0; index < MAX_EV_REQUESTS; index++)
    {
        p = &card->ev_request_pool[index];
        EV_DEBUG_ALWAYS("I%d> ready:%d, page_dma:%llx, cnt:ox%X, iotype:%d\n", 
                index, p->ready, p->page_dma, p->cnt, p->io_type);
        if (p->desc != NULL)
        {
            desc = &p->desc[p->headcnt];

            EV_DEBUG_ALWAYS("---- transfer size: 0x%X, control bits:0x%X, data_dma_handle:0x%llX\n", 
                    desc->transfer_size, desc->control_bits, desc->data_dma_handle);
        }
        else
        {
            EV_DEBUG_ALWAYS("---- desc NULL\n");
        }
    }
}

static int producer_fn(void *data)
{
    struct cardinfo* card = data;

    allow_signal(SIGKILL);

    while (!kthread_should_stop())
    {
        while (card->nEnqueued != card->nIssued)
        {
            spin_lock(&card->IssueLock);
            issue_ev_request(card);
            spin_unlock(&card->IssueLock);
        }
#if 0 
        //XXX MUST BE REMOVED --- low performance...
        spin_lock(&card->EnqueueLock);
        if (card->bio != NULL)
        {
            enqueue_ev_request(card);
        }
        spin_unlock(&card->EnqueueLock);
#endif    
        card->producerStatus = 0;
        wait_event_interruptible(WaitQueue_Producer, card->producerStatus != 0);

        if (signal_pending(current))             
            break;                                             
    } 
   
    do_exit(0); 
  
    return 0;  
}

static int consumer_fn(void *data)
{
    struct cardinfo* card = data;
    int retval;

    allow_signal(SIGKILL);

    while (!kthread_should_stop())
    {
        card->consumerStatus = 0;
        wait_event_interruptible(WaitQueue_Consumer, card->consumerStatus != 0);
        
        if (signal_pending(current))             
            break;                                             
       
        spin_lock(&card->PostprocessingLock);
        retval = process_interrupt_handler(card);
        spin_unlock(&card->PostprocessingLock);
        
    }
    
    do_exit(0); 
  
    return 0;  
}

static int finalizer_fn(void *data)
{
    ev_finalizer_data_t* finalizer_data = (ev_finalizer_data_t*) data;
    struct cardinfo* card = finalizer_data->card;

    allow_signal(SIGKILL);

    while (!kthread_should_stop())
    {
        finalizer_data->status = 0;
        wait_event_interruptible(finalizer_data->ev_finalizer_waitQ, finalizer_data->status != 0);
        
        if (signal_pending(current))             
            break;                                             
       
        netlist_process_finalization(card); 
    }
    
    do_exit(0); 
  
    return 0;  
}


pci_ers_result_t pci_slot_reset(struct pci_dev *dev)
{
    EV_DEBUG_ALWAYS("AER: Inside pci_slot_reset\n");

    return 0;
}

pci_ers_result_t pci_link_reset(struct pci_dev *dev)
{
    EV_DEBUG_ALWAYS("AER: Inside pci_link_reset\n");

    return 0;

}

pci_ers_result_t pci_error_detected(struct pci_dev *dev, enum pci_channel_state error)
{
    EV_DEBUG_ALWAYS("AER: Inside pci_error_detected and the error number is 0x%x\n", error);

    return 0;
}

void pci_resume(struct pci_dev *dev)
{
    EV_DEBUG_ALWAYS("AER: Inside pci_resume\n");
}

pci_ers_result_t pci_mmio_enabled(struct pci_dev *dev)
{
    EV_DEBUG_ALWAYS("AER: Inside pci_mmio_enabled\n");
    
    return 0;
}

struct pci_error_handlers pci_err_handler =
{
    .error_detected = pci_error_detected,
    .link_reset     = pci_link_reset,
    .slot_reset     = pci_slot_reset,
    .mmio_enabled   = pci_mmio_enabled,
    .resume         = pci_resume,
};

static struct pci_driver netlist_pci_driver = 
{
    .name           = PCI_DRIVER_NAME,
    .id_table       = netlist_pci_ids,
    .probe          = netlist_pci_probe,
    .remove         = netlist_pci_remove,
    .err_handler    = &pci_err_handler, // This is a structure
};

// Debug related
static void show_descriptor(int index, struct ev_dma_desc *desc);
static void show_sgl(struct cardinfo *card, int index);
static void show_card_state(struct cardinfo *card);

#if defined(DEBUG_STATS) && defined(USING_BIO)
static inline void show_bio(struct bio *bio, struct pci_dev *dev);
// Use this to capture any event into the capture buffer
#endif

#if defined(DEBUG_STATS_CORE)
static void dbg_capture(struct cardinfo *card, unsigned long long event)
{
    // Exit when buffer is full on the second cycle
    //if ((card->stats_dbg.dbg_head == (DBG_BUF_SIZE-1)) && (card->stats_dbg.dbg_cycles == 1))
    //{
    //    return;
    //}

    // Exit when buffer is full and 84h has been seen
    //if ((card->stats_dbg.dbg_head == (DBG_BUF_SIZE-1)) && (card->stats_dbg.dbg_histogram[0x84] > 0))
    //{
    //    return;
    //}

    // Stop capturing after any of certain events has been seen
    //if ((card->stats_dbg.dbg_histogram[0xf1]>0) ||
    //    (card->stats_dbg.dbg_histogram[0x85]>0) ||
    //    (card->stats_dbg.dbg_histogram[0xe0]>0))
    //{
    //    return;
    //}

    card->stats_dbg.dbg_buffer[card->stats_dbg.dbg_head] = event;
    card->stats_dbg.dbg_histogram[event&0xff]++;
    card->stats_dbg.dbg_valid_entries++;


    card->stats_dbg.dbg_head++;
    if (card->stats_dbg.dbg_head == DBG_BUF_SIZE)
    {
        card->stats_dbg.dbg_head = 0;
        card->stats_dbg.dbg_cycles++;
    }

    if (card->stats_dbg.dbg_head == card->stats_dbg.dbg_tail)
    {
        card->stats_dbg.dbg_tail++;
        card->stats_dbg.dbg_valid_entries--;
        if (card->stats_dbg.dbg_tail == DBG_BUF_SIZE)
        {
            card->stats_dbg.dbg_tail = 0;
        }
    }
}
#endif

#if defined(USING_SGIO)
static int netlist_chr_open(struct inode *inode, struct file *filp)
{
    int minor, card_number;

    minor       = MINOR(inode->i_rdev);
    card_number = (minor >> EV_SHIFT);

    filp->private_data = (void *)&cards[card_number];
    return 0;
}

static int netlist_chr_release(struct inode *inode, struct file *filp)
{
    EV_DEBUG_FASTPATH("release");
    return 0;
}

// TBD - see if SKIP area can be masked off from the user application's view.
static int netlist_chr_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long offset, req_size;
    struct cardinfo *card = filp->private_data; 

    EV_DEBUG("vma=%p", vma);
    if (num_cards == 0) 
    {
        EV_DEBUG("%s card is not present", CARD_NAME);
        return -ENODEV;
    }

    offset = (vma->vm_pgoff << PAGE_SHIFT);

    if (offset & (PAGE_SIZE-1)) 
    {
        EV_DEBUG("offset is not page-aligned\n");
        return -ENXIO;
    }

    req_size = vma->vm_end - vma->vm_start;

    if (offset + req_size > card->mem_len) 
    {
        EV_DEBUG("request size is out of range");
        return -EINVAL;
    }
    offset += (unsigned long)card->mem_base;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
    vma->vm_flags |= (VM_IO | VM_RESERVED);
#else
    // May need to use ( VM_DONTEXPAND | VM_DONTDUMP ) if VM_IO by itself
    // does not work.
    vma->vm_flags |= (VM_IO);
#endif


    /* the following is borrowed from drivers/char/mem.c */
#ifdef pgprot_noncached
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#else /* ! pgprot_noncached */
#if defined(__i386__) || defined(__x86_64__)
    /* On PPro and successors, PCD alone doesn't always mean
     * uncached because of interactions with the MTRRs. PCD | PWT
     * means definitely uncached. */
    if (boot_cpu_data.x86 > 3)
        pgprot_val(vma->vm_page_prot) |= _PAGE_PCD | _PAGE_PWT;
#elif defined(__powerpc__)
    pgprot_val(vma->vm_page_prot) |= _PAGE_NO_CACHE | _PAGE_GUARDED;
#elif defined(__mc68000__)
#ifdef SUN3_PAGE_NOCACHE
    if (MMU_IS_SUN3)
        pgprot_val(vma->vm_page_prot) |= SUN3_PAGE_NOCACHE;
    else
#endif /* SUN3_PAGE_NOCACHE */
    if (MMU_IS_851 || MMU_IS_030)
        pgprot_val(vma->vm_page_prot) |= _PAGE_NOCACHE030;
    /* Use no-cache mode, serialized */
    else if (MMU_IS_040 || MMU_IS_060)
        pgprot_val(vma->vm_page_prot) =
            (pgprot_val(vma->vm_page_prot) & _CACHEMASK040)
            | _PAGE_NOCACHE_S;
#endif /* __mc68000__ */
#endif /* ! pgprot_noncached */

#if defined(io_remap_pfn_range) || defined(GET_IOSPACE)
    if (io_remap_pfn_range(vma, vma->vm_start, offset >> PAGE_SHIFT,
                req_size, vma->vm_page_prot)) {
        return -EAGAIN;
    }
#else /* ! io_remap_pfn_range && ! GET_IOSPACE */
/*
 * The five argument version of remap_page_range and the symbol VM_HUGETLB
 * were both backported to Linux 2.4 from Linux 2.5 in some Redhat
 * kernels, so guess that seeing VM_HUGETLB means that we need to call
 * the five argument version of remap_page_range.
 */
#if defined(VM_HUGETLB)
    if (io_remap_page_range(vma, vma->vm_start, offset, req_size,
                vma->vm_page_prot)) {
        return -EAGAIN;
    }
#else
    if (io_remap_page_range(vma->vm_start, offset, req_size,
                vma->vm_page_prot)) {
        return -EAGAIN;
    }
#endif
#endif /* ! GET_IOSPACE */

    return 0;
}

// Auxilliary routine used for both READ and WRITE paths.
static ssize_t netlist_chr_aux(struct file *filp, const char __user *buf, size_t count, loff_t *offp, int rw)
{
    struct cardinfo *card = filp->private_data; 
    ssize_t retval = 0;
    unsigned long flags;
    int i=0;
    // size_in_sectors already takes skip_sectors into account
    loff_t available_size = card->size_in_sectors * EV_HARDSECT;
    loff_t ddr_offset = *offp + (card->skip_sectors * EV_HARDSECT); // Skip over invalid DDR area.;
    int activate_success = -1; // 0 is success
    struct page **mapped_pages=NULL; // Temporary holding place until we can get a lock
    int page_offset=0; // Temporary holding place until we can get a lock

    // Next are related to blocking and waiting for event.
    LIST_HEAD(compl_list);
    wait_queue_t wait;
    int err = 0;

    // Start the performance time as soon as we get indications of a first IO
    // since the latest reset_stats.
    if (card->capture_stats)
    {
        spin_lock_irqsave(&card->lock, flags);
        if (card->stats_perf.start_time == 0)
        {
            rdtscll(card->stats_perf.start_time);
        }
        spin_unlock_irqrestore(&card->lock, flags);
    }

    if (rw == WRITE)
    {
        EV_DEBUG_FASTPATH("Write");
    }
    else
    {
        EV_DEBUG_FASTPATH("Read");
    }


     EV_DEBUG_FASTPATH("buf=%p count=0x%lx *offp=0x%llx ddr_offset=0x%llx rw=%d\n", buf, count, *offp, ddr_offset, rw);

#if defined(DEBUG_STATS)
    spin_lock_irqsave(&card->lock, flags);
    dbg_capture(card, 0x1a); // Number of character IO requests made by the OS total
    spin_unlock_irqrestore(&card->lock, flags);
#endif

    // We currently only support MMAP_MODE_DMA
    if (card->mmap_mode == MMAP_MODE_DMA)
    {
        // Avoid the classic lost wakeup problem.
        set_current_state(TASK_INTERRUPTIBLE);

        // Do some checks - we do want to use offp as it is zero-based (not ddr_offset)
        if (*offp < available_size)
        {
            if (*offp + count > available_size)
                count = available_size - *offp;

            *offp+=count;

            // We cannot get a lock yet due to kmallocs
            i = sgl_map_user_pages(MAX_DESC_PER_DMA, (unsigned long)buf, count, rw, &mapped_pages, &page_offset);

            spin_lock_irqsave(&card->lock, flags);

#if defined(DEBUG_STATS)
            card->stats_dbg.ios_rcvd++; // Stats
            if ((card->stats_dbg.ios_rcvd-card->stats_dbg.ios_completed) > card->stats_dbg.ios_max_outstanding)
            {
                card->stats_dbg.ios_max_outstanding = (card->stats_dbg.ios_rcvd-card->stats_dbg.ios_completed); // Stats 
            }
            dbg_capture(card, 0x11); // Number of character IO requests made by the OS
#endif

            if (i>0)
            {
                card->mapped_pages = mapped_pages;
                card->num_mapped_pages = i;
                card->page_offset = page_offset;
                
                // Due to DMA limitations of source and destination addresses having to be on an
                // 8-byte boundary and the byte count for DMA having to be a multiple of 8 bytes.
                // Work is in progress to address these limitations.

                card->offset = ddr_offset;
                card->count = retval = count;
                card->rw = rw;

                EV_DEBUG_FASTPATH("Number of pages is %d offset=%llx count=%lx rw=%x",i, card->offset, card->count, card->rw);

                // Add the mapped pages to the SGL.
                EV_DEBUG_FASTPATH("Buf =%p : %x %x %x %x", buf, buf[0], buf[1], buf[2], buf[3]);

                // If ev_activate returns a non-zero that means the IO could not be queued, retry until it gets queued or
                // exit if after so many loops it fails. This is most likely to happen when the block driver is running
                // simultaneously with the character driver.

                // We do not expect this loop to be used more than since the block driver reserves an SGL location for the 
                // character driver when compiled for both.

                // As soon as we activate we could very well complete the IO at the hardware level. Therefore, we should
                // already be on the event_waiters queue ready to receive the wakeup. Otherwise we will miss it.

                // Add thread to the queue
                init_waitqueue_entry(&wait, current);
                current->state = TASK_INTERRUPTIBLE;
                add_wait_queue_exclusive(&card->event_waiters, &wait);

                activate_success = ev_activate(card); 

                if (activate_success !=0)
                {
#if defined(DEBUG_STATS)
                    dbg_capture(card, 0xF0); // Could not queue character driver IO - will retry
#endif
                    remove_wait_queue(&card->event_waiters, &wait);
                    spin_unlock_irqrestore(&card->lock, flags);
                    sgl_unmap_user_pages(i, &mapped_pages);
                    set_current_state(TASK_RUNNING);
                    return -EAGAIN;
                }

                spin_unlock_irqrestore(&card->lock, flags);

                // Block until completion
                for (;;) 
                {
                    if (signal_pending(current)) 
                    {
                        err = -EINTR;
                        EV_DEBUG_ALWAYS("SIGNAL ALREADY PENDING");  // TBD - remove
                        break;
                    }

                    EV_DEBUG_FASTPATH("BLOCKED");
                    // Timeout time is in jiffies. 
                    schedule_timeout(HZ * DMA_TIMEOUT_VAL);    // Block until signal is received or timeout happens
                    // TBD - find out if a timeout happenned or not.
                    EV_DEBUG_FASTPATH("UNBLOCKED");
                    break;
                }

                spin_lock_irqsave(&card->lock, flags);
                remove_wait_queue(&card->event_waiters, &wait);
                spin_unlock_irqrestore(&card->lock, flags);
                set_current_state(TASK_RUNNING);
            }
            else
            {
                spin_unlock_irqrestore(&card->lock, flags);
                EV_DEBUG_FASTPATH("Error mapping user pages =%d", i);
            }
        }
    }

    return retval;
}



static ssize_t netlist_chr_read(struct file *filp, char __user *buf, size_t count, loff_t *offp)
{
    ssize_t retval=0;

    retval = netlist_chr_aux(filp, buf, count, offp, READ);

    return retval;
}

static ssize_t netlist_chr_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp)
{
    ssize_t retval=0;

    retval = netlist_chr_aux(filp, buf, count, offp, WRITE);

    return retval;
}

static loff_t netlist_chr_llseek(struct file *filp, loff_t off, int whence)
{
    struct cardinfo *card = filp->private_data; 
    loff_t newpos;  

    EV_DEBUG_FASTPATH("llseek offset=0x%llx whence=0x%x", off, whence);

    //lock_kernel();
    switch(whence) 
    {    
        case SEEK_SET: // Relative to beginning of file
            newpos = off;      
            break;    
        case SEEK_CUR: // Relative to the current file position
            newpos = filp->f_pos + off;      

            if (newpos > ((card->size_in_sectors * EV_HARDSECT) + off))
            {
                newpos = (card->size_in_sectors * EV_HARDSECT) + off;      
            }

            break;    
        case SEEK_END: // Relative to the end of the file
            newpos = (card->size_in_sectors * EV_HARDSECT) + off;      
            break;    
        default: /* can't happen */      
            //unlock_kernel();
            return -EINVAL;  
    }  
    
    if (newpos < 0) 
        return -EINVAL;  

    filp->f_pos = newpos;  

    //unlock_kernel();
    return newpos;    
}

#endif

#if defined(USING_BIO)
static int netlist_blk_open(struct block_device *dev, fmode_t mode)
{
    struct cardinfo *card = dev->bd_disk->private_data; 
    spin_lock_bh(&card->lock);
    card->users++;
#if defined(DEBUG_STATS)
    dbg_capture(card, 0x30 | (card->users<<8)); // Block driver opens - includes for IOCTL calls.
#endif
    spin_unlock_bh(&card->lock);
    return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)
static int netlist_blk_release(struct gendisk *disk, fmode_t mode)
#else
static void netlist_blk_release(struct gendisk *disk, fmode_t mode)
#endif
{
    struct cardinfo *card = disk->private_data; 

    spin_lock_bh(&card->lock);
    card->users--;
#if defined(DEBUG_STATS)
    dbg_capture(card, 0x32 | (card->users<<8));    // Block driver closes - includes for IOCTL calls.
#endif
    spin_unlock_bh(&card->lock);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)
    return 0;
#endif
}

static int netlist_blk_ioctl(struct block_device *dev, fmode_t mode, unsigned cmd, unsigned long arg)
{
    int retVal = 0;
    struct file *f = NULL;

    retVal = netlist_ioctl(dev->bd_inode, f, cmd, arg);

    return retVal;
}

static int netlist_blk_get_geo(struct block_device *bdev, struct hd_geometry *geo)
{
    struct cardinfo *card = bdev->bd_disk->private_data; 

    geo->heads = 64;
    geo->sectors = 32;
    geo->cylinders = (u16)((int)(card->size_in_sectors) / ((int)geo->heads * geo->sectors)); // geo->sectors is sectors per cylinder

    geo->start = get_start_sect(bdev);
    EV_DEBUG(" heads=%d sectors=%d cylinders=%d start=%ld total sectors=%ld", geo->heads, geo->sectors, geo->cylinders, geo->start, card->size_in_sectors);

    return 0;    
}

static int netlist_blk_media_changed(struct gendisk *disk)
{
    //struct cardinfo *card = disk->private_data;
    // We do not currently support media changing.    

    return 0;
}

static int netlist_blk_revalidate(struct gendisk *disk)
{
    //struct cardinfo *card = disk->private_data;
    
    return 0;
}

#endif


/**********************************************************************************
*
* init_dma_timer - Timer initialization for DMA timeout timer
*
* Arguments: card: pointer to the cardinfo structure.
*
* RETURNS: None.
**********************************************************************************/
static void init_dma_timer(struct cardinfo *card)
{
    int sgl_no = card->active_sgl;

    init_timer(&card->dma_timer[sgl_no]);
    card->dma_timer[sgl_no].function = ev_timeout_check_dma;
    // Encode the card and sgl index into context.
    card->dma_timer[sgl_no].data = (card->card_number) | (sgl_no<<8); 
    card->dma_timer[sgl_no].expires = jiffies + (HZ * DMA_TIMEOUT_VAL);
    add_timer(&card->dma_timer[sgl_no]);
}

void del_dma_timer(struct cardinfo *card, int sgl_no)
{
    (void) del_timer(&card->dma_timer[sgl_no]);
}

static void ev_timeout_check_dma(unsigned long context)
{
    int card_idx = (context & 0xff); // Low byte has card info
    int sgl_number = ((context>>8) & 0xff); // Second byte has sgl number
    struct cardinfo *card = &cards[card_idx];
    unsigned int dma_status;

    // Mark it as a timed out IO
  //  card->ev_sgls[sgl_number].io_status = IO_STATUS_TIMEOUT;

#if defined(DEBUG_STATS)
    card->stats_dbg.dmas_timed_out++; // Stats
    dbg_capture(card, 0xF1 | (sgl_number<<8)); // DMA timeout event - second byte is SGL index of IO
#endif

    EV_DEBUG_ALWAYS("Error: DMA timeout error processing SGL : 0x%x Card index=%d io_type=%d\n", 
                    sgl_number, card_idx, card->ev_sgls[sgl_number].io_type);
    EV_DEBUG_ALWAYS("Latest system address used for DMA : %llx\n", card->last_accessed_system_address);

    // TBD - get context
    dma_status = readq(card->int_status_address + card->current_address);
    if (dma_status)
    {
        // The status came in after the timeout. 
        process_interrupt(card);
    }
    else
    {
        // Complete the IO to the OS with an error status.
        netlist_abort_io(card, ABORT_CODE);
    }
}

static void capture_data_logger_entry (struct cardinfo *card, uint64_t value)
{
    card->data_logger.data_log_buffer[card->data_logger.head] = value;
    card->data_logger.head++;
    if (card->data_logger.head >= card->data_logger.wraparound_index)
    {
        card->data_logger.head = 0;
    }
    if (card->data_logger.head == card->data_logger.tail)
    {
        // We must bump up the tail by the number of samples so that the display routines 
        // always have a full set of measurements
        card->data_logger.tail += card->data_logger.sample_count;
        if (card->data_logger.tail >= card->data_logger.wraparound_index)
        {
            //card->data_logger.tail -= card->data_logger.wraparound_index; // May not be 0 may be more than 0???
            card->data_logger.tail = 0; // Assumption here is that wraparound is a multiple of the number of measurements.
        }
    }
}

static void ev_polling_timer(unsigned long context)
{
    //struct cardinfo *card = &cards[context]; <---WANT, however there is no context available.
    struct cardinfo *card;
    int i;
    uint64_t issued_to_hw = 0;
    unsigned int temp_dword_1 = 0;
    unsigned int temp_dword_2 = 0;
    unsigned int temp_dword_3 = 0;


	if (polling_mode)
	{
        for (i=0;i<num_cards;i++)
        {
            card = &cards[i];
            // The context contains the card index.
            // Run the ISR in polled mode
            process_interrupt(card);
        }
    }
    else
    {
        for (i=0;i<num_cards;i++)
        {
            card = &cards[i];

            if (card->dma_int_factor > 1)
            {
                disable_irq(card->irq);
                issued_to_hw = card->num_start_io - card->num_sgl_processed;
                // This must be a coalescing timer.

                // If no activity for two consecutive timer invokationa AMD there are outstanding DMAs then poll
                if (issued_to_hw != 0)
                {
                    if (card->last_num_start_io == card->num_start_io)
                    {
                        // The number of DMAs started has not changed in two consecutive cycles.
                        process_interrupt(card);
                    } 
                }
                card->last_num_start_io = card->num_start_io;  // Get the current value.
                enable_irq(card->irq);

            }
        }
    }

    // Data logger routine
    for (i=0;i<num_cards;i++)
    {
        card = &cards[i];

        card->data_logger_elapsed_ns += poll_cycle_time_ns;

        if ((card->data_logger_elapsed_ns / 1000000000) > card->data_logger.sample_time)
        {
            card->data_logger_elapsed_ns = 0;

            // We should not need a lock to read these registers atomically.
            // Get the temperatures
            temp_dword_1 = readl(card->reg_remap + EV3_CARD_TEMP);
            temp_dword_2 = readl(card->reg_remap + EV3_FPGA_TEMP);
            temp_dword_3 = readl(card->reg_remap + CURRENT_PMU_TEMP);

            EV_DEBUG_ALWAYS("Data logger capture card %d   card temp=%d fpga temp=%d pmu temp=%d \n",i, temp_dword_1, temp_dword_2, temp_dword_3);

            capture_data_logger_entry (card, DATA_LOGGER_CARD_TEMPERATURE |  temp_dword_1);
            capture_data_logger_entry (card, DATA_LOGGER_FPGA_TEMPERATURE |  temp_dword_2);
            capture_data_logger_entry (card, DATA_LOGGER_PMU_TEMPERATURE |  temp_dword_3);
        }
    }
}

enum hrtimer_restart my_hrtimer_callback( struct hrtimer *timer )
{
    ktime_t timer_now = ktime_set(0,0);
    unsigned long misses;
    struct cardinfo *card = &cards[0];
#if 0 
    if (card->nEnqueued != card->nIssued)
    {
        card->producerStatus = 1;
        wake_up(&WaitQueue_Producer);
    }
#endif
   
    if ((card->consumerStatus == 0) && (card->nIssued != card->nProcessed))
    {
        card->consumerStatus = 1;
        wake_up(&WaitQueue_Consumer);

    }
  
    card->nDebugLoop++;

#if 0
    if (card->nDebugLoop > 20000)
    {
        EV_DEBUG_ALWAYS("nE:%d, nI:%d, nP:%d, nC:%d\n", 
                card->nEnqueued, card->nIssued, card->nProcessed, card->nCompleted);
        EV_DEBUG_ALWAYS("nInterrupts:%d\n", card->nInterrupts);

        card->nDebugLoop = 0;
    }
#endif
       // This is needed to cycle.
  
    poll_cycle_time_ns = 200000;
    card->polling_ktime = ktime_set( 0, poll_cycle_time_ns );
    timer_now = ktime_get();
    misses = hrtimer_forward(timer, timer_now, card->polling_ktime);

    return HRTIMER_RESTART;
}

// Using High resolution timers
static void init_polling_timer(struct cardinfo *card, int isFast)
{
	EV_DEBUG_ALWAYS("Initializing polling timer, mode is %s\n", isFast ? "FAST" : "SLOW");

    card->polling_timer_is_active = TRUE;

	if (isFast)
	{
        card->polling_ktime = ktime_set( 0, poll_cycle_time_ns );

        hrtimer_init( &card->polling_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );
  
        card->polling_timer.function = &my_hrtimer_callback;

        hrtimer_start( &card->polling_timer, card->polling_ktime, HRTIMER_MODE_REL );
	}
	else
	{
        // In slow mode we want the timer to catch interrupts which were not generated but posted as flags 
        // Set new value for poll_cycle_time_ns, it is used for restarting the timer.
        //poll_cycle_time_ns = 500000000; // 500mS  = 10 IOPS
        // poll_cycle_time_ns = 500000; // 500nS     = 10.4 KIOPS
        // poll_cycle_time_ns = 500; // 0.5nS           = 330 KIOPS
        card->polling_ktime = ktime_set( 0, poll_cycle_time_ns );  

        hrtimer_init( &card->polling_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );
  
        card->polling_timer.function = &my_hrtimer_callback;

        hrtimer_start( &card->polling_timer, card->polling_ktime, HRTIMER_MODE_REL );
	}
}

static int ev_start_io(struct cardinfo *card)
{
    struct ev_dma_desc *desc;
    struct ev_sgl *sgl;
    int offset;
    int num_ios_outstanding;

    init_dma_timer(card);

    sgl = &card->ev_sgls[card->active_sgl];

#ifdef COMPILE_IOSTAT
    if (iostat_enable)
    {
        ev_iostat_start(sgl->bio, &(sgl->start));
    }
#endif

    /* make the last descriptor end the chain */
    desc = &sgl->desc[sgl->cnt - 1];

    // TBD - Support interrupt coalescing.
    // Currently supported are interrupt driven and polled mode.
    desc->control_bits |= DMASGL_DESCRIPTOR_CHAIN_END;

#if 0
    if (!polling_mode)
    {
        if (card->active_sgl > card->processed_sgl)
        {
            num_ios_outstanding = card->active_sgl - card->processed_sgl;
        }
        else
        {
            num_ios_outstanding = card->processed_sgl - card->active_sgl;
        }

        // Coalesce if dma_int_factor > 1
        // Do not coalesce if there are no DMAs outstanding, otherwise single-threaded
        // DMA will be as slow as the (slow timer * 2) per DMA. 
        // active_sgl already got incremented for the current DMA therefore num_ios_outstanding must be 
        // greater than 1 to justify coalescing 
        if (card->dma_int_factor > 1)
        {
            if (((card->num_start_io % card->dma_int_factor) == 0) || (num_ios_outstanding == 1))
            {
                desc->control_bits |= DMASGL_CHAIN_END_INTERRUPT_ENABLE;
            }
        }
        else
        {
            // No coalescing dma_int_factor == 1
            desc->control_bits |= DMASGL_CHAIN_END_INTERRUPT_ENABLE;
        }
    }
#endif

    card->num_start_io++;
    card->last_accessed_system_address = desc->data_dma_handle;
    desc = &sgl->desc[sgl->headcnt];
    offset = ((char*)desc) - ((char*)sgl->desc);

    // Track descriptors - TBD - This is obsolescent
    if (card->enable_descriptor_throttle)
    {
        card->active_descriptors += sgl->cnt;
    }

#if 1
    /* Print descriptor Contents */
    EV_DEBUG2("ev_start_io: Starting descriptor:");
    EV_DEBUG2("System Address data_dma_handle: 0x%llX", desc->data_dma_handle);
    EV_DEBUG2("DDR Address : 0x%llX", (u64)(((u64)desc->ddr_mem_addr_hi<<32) | desc->ddr_mem_addr_lo)); 
    EV_DEBUG2("Transfer length transfer_size: 0x%X", desc->transfer_size);
    EV_DEBUG2("Control Bits control_bits: 0x%X", desc->control_bits);
    EV_DEBUG2("SGL count : 0x%X", sgl->cnt);
    EV_DEBUG2("REQ_ADDR page_dma = 0x%llX, offset = 0x%X,  Writing 0x%llX to REQ_ADDR\n", sgl->page_dma, offset, (uint64_t)(sgl->page_dma + offset));
#endif

    writel((uint32_t)(sgl->page_dma + offset), card->reg_remap + EV_FPGA_REQ_ADDR_REG_L);
    // TBD - Look into this - shift is greater than size of the type - might as well write 0.
    // Look into a proper fix.
    writel((uint32_t)((sgl->page_dma + offset) >> 32), card->reg_remap + EV_FPGA_REQ_ADDR_REG_H);
    writel(sgl->cnt, card->reg_remap + EV_FPGA_REQ_LEN_REG);

#if defined(DEBUG_STATS)
    card->stats_dbg.dmas_started++; // Stats
    
    if ((card->stats_dbg.dmas_started-card->stats_dbg.dmas_completed) > card->stats_dbg.dmas_max_outstanding)
    {
        card->stats_dbg.dmas_max_outstanding = (card->stats_dbg.dmas_started-card->stats_dbg.dmas_completed);
    }

    // Save value of number of outstanding dmas for later calculation of average outstanding
    card->stats_dbg.dmas_num_outstanding += (card->stats_dbg.dmas_started-card->stats_dbg.dmas_completed);

    // Track max descriptors to the hardware
    // Track descriptors - TBD - This is obsolescent
    if (card->enable_descriptor_throttle)
    {
        if (card->active_descriptors > card->stats_dbg.descriptors_max_outstanding)
        {
            card->stats_dbg.descriptors_max_outstanding = card->active_descriptors;
        }
    }
#endif
    return 0;
}

// This will return the number of IOs that can be queued to the SGL array. If zero or less then cannot queue any.
static inline int ev_ok_to_add_io(struct cardinfo *card)
{
    int retval = 0; 

    if (!card->stop_io)
    {
        if (card->processed_sgl >= card->empty_sgl)
        {
            retval = card->processed_sgl - card->empty_sgl - 1; // Must leave room for one empty always
        }
        else
        {
            retval = (card->max_sgl - 1) - card->empty_sgl + card->processed_sgl;
        }
    }

    return(retval);    
}

// This will return the number of IOs that can be queued to the hardware. If zero then cannot queue any.
static inline int ev_ok_to_start_io(struct cardinfo *card)
{
    int retval = FALSE; 
    uint64_t issued_to_hw = card->num_start_io - card->num_sgl_processed;

    retval = (card->max_dmas_to_hw - issued_to_hw) > 0;

    // Track descriptors - TBD - This is obsolescent
    if (card->enable_descriptor_throttle)
    {
        if (retval)
        {
            if ((card->active_descriptors+card->ev_sgls[card->next_active_sgl].cnt) > card->max_descriptors_to_hw)
            {
                retval = FALSE;
            }
        }
    }

    return(retval);    
}

static int enqueue_ev_request(struct cardinfo *card)
{
    ev_request_t* pRequest;
    int retVal = 0;
    ev_bio* bio;
    unsigned long long ddr_mem_addr;
    int temp_length;

    do {

    if (card->nCompleted > card->nEnqueued)
    {
        if (card->nCompleted - card->nEnqueued <= 1)
        {
#if 0
            EV_DEBUG_ALWAYS("1. No available EV Request -- nEnqueued:%d, nIssued:%d, nProcessed:%d, nCompleted:%d\n", 
                card->nEnqueued, card->nIssued, card->nProcessed, card->nCompleted);
#endif
            return 0;
        }
    }
    else
    {
        // TODO......
        if (MAX_EV_REQUESTS +  card->nCompleted <= card->nEnqueued + 1)
        {
#if 0
            EV_DEBUG_ALWAYS("2. No available EV Request -- nEnqueued:%d, nIssued:%d, nProcessed:%d, nCompleted:%d\n", 
                card->nEnqueued, card->nIssued, card->nProcessed, card->nCompleted);
#endif
            return 0;
        }
    }

    pRequest = &card->ev_request_pool[card->nEnqueued];
    if (pRequest->ready == 0)    {
        EV_DEBUG_ALWAYS("Not Ready\n");
        
        return 0;
    }
    
    bio = card->currentbio;
    if(!bio && card->bio)
    {
        bio = card->currentbio = card->bio;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
        card->current_idx = card->bio->bi_idx;
        card->current_sector = card->bio->bi_sector+card->skip_sectors;
#else
        card->current_idx = card->bio->bi_iter.bi_idx;
        card->current_sector = card->bio->bi_iter.bi_sector+card->skip_sectors;
#endif
        card->bio = ev_bio_next(bio);
        if (card->bio == NULL) 
            card->biotail = &card->bio;
      
        ev_bio_next(card->currentbio) = NULL; 
    }
    if (bio == NULL)
    {
     //   EV_DEBUG_ALWAYS("bio is NULL \n");
        
        return 0;
    }
   
    
    if (pRequest->cnt >= MAX_DESC_PER_DMA)
    {
        EV_DEBUG_ALWAYS("pRequest->cnt >= MAX_DESC_PER_DMA\n");

        return 0;
    }

    // At this poitn we have th ebio and the descriptor array we want to  add the bio data to 
    {
		struct bvec_iter segno;
		struct bio_vec bvec;
        int rw;
        dma_addr_t dma_handle;
        struct ev_dma_desc *desc;

        rw = bio_data_dir(bio);
        pRequest->io_type = IO_BIO;
        pRequest->io_context = (void *)bio;

	    if (bio->bi_iter.bi_size > 0)
        {
            bio_for_each_segment(bvec, bio, segno) 
            {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0) // ARbitrary switch-over to dma_map 
				bvec = bio_iter_iovec(bio, segno);
				dma_handle = dma_map_page(&(card->dev->dev), bvec.bv_page, bvec.bv_offset, bvec.bv_len, (rw != WRITE) ?    DMA_FROM_DEVICE : DMA_TO_DEVICE);			    
				temp_length = bvec.bv_len;
                if (dma_mapping_error(&(card->dev->dev), dma_handle))
                {
                    EV_DEBUG_ALWAYS("ERROR: dma map error");
                }
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
                bvec = bio_iovec_idx(bio, segno);
                dma_handle = pci_map_page(card->dev, bvec->bv_page, bvec->bv_offset, bvec->bv_len, (rw != WRITE) ?    PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
                temp_length = bvec->bv_len;
#else
		        bvec = bio_iter_iovec(bio, segno);
		        dma_handle = pci_map_page(card->dev, bvec.bv_page, bvec.bv_offset, bvec.bv_len, (rw != WRITE) ?    PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);			    
                temp_length = bvec.bv_len;
#endif
#endif
                if (pRequest->bio == NULL)
                    pRequest->idx = card->current_idx;

                if (pRequest->biotail != &ev_bio_next(bio)) 
                {
                    *(pRequest->biotail) = bio;
                    pRequest->biotail = &ev_bio_next(bio);
                    ev_bio_next(bio) = NULL;
                }

                while (temp_length > 0)
                {
                    // Fill in the next empty descriptor
                    desc = &pRequest->desc[pRequest->cnt];

                    desc->data_dma_handle = (uint64_t)dma_handle;
                
                    ddr_mem_addr  = (u64)(card->current_sector << 9);
                    desc->ddr_mem_addr_lo = (u32)(ddr_mem_addr & 0xFFFFFFFF);   // Card address low
                    desc->ddr_mem_addr_hi = (u8)((ddr_mem_addr>>32) & 0xFF);    // Card address high

                    // Limit descriptors to 32K in size
                    if (temp_length > 0x8000)
                    {
                        desc->transfer_size = 0x8000;
                        temp_length -= 0x8000;
                    }
                    else
                    { 
                        desc->transfer_size = temp_length;
                        temp_length = 0;
                    }

                    if (rw == WRITE)
                    {
                        desc->control_bits = 0x01; // This may be a WRITE or SWRITE(maybe - not sure)
                    }
                    else
                    {
                        desc->control_bits = 0x00; // This may be a READ or a READA (read-ahead)
                    }

                    card->current_sector += (desc->transfer_size >> 9);
                    card->current_idx++; // TBD - current_idx and p->cnt seem to track each other - can I use only p->cnt?
                    pRequest->cnt++;
                }
            }
        }
        else
        {
            // Special case seen in kernels 2.6.33.12 thru 2.6.36.4. During "mke2fs" the driver receives a BIO of 
            // length 0. mke2fs will hang if we do not process this.
            // Do a dummy DMA - This is a BIO of zero length. This is seen on some kernel versions and is
            // likely some marker for the OS or a guarantee that the hardware has been flushed. We want to
            // preserve the order so we issue it just like any IO. We read DDR address 0 into a scratch buffer
            // and throw away the data.
            rw = READ;

            if (pRequest->bio == NULL)
                pRequest->idx = card->current_idx;

            if (pRequest->biotail != &ev_bio_next(bio)) 
            {
                *(pRequest->biotail) = bio;
                pRequest->biotail = &ev_bio_next(bio);
                ev_bio_next(bio) = NULL;
            }

            // Fill in the next empty descriptor
            desc = &pRequest->desc[pRequest->cnt];
            // Make sure the alignment is correct.
            desc->data_dma_handle = (uint64_t)((card->scratch_dma_bus_addr+EV_DMA_DESCRIPTOR_ALIGNMENT-1) & 0xfffffffffffffff8ULL);
            desc->ddr_mem_addr_lo = (u32)(0); // Address 0
            desc->ddr_mem_addr_hi = (u8)(0); // Address 0
            desc->transfer_size = EV_HARDSECT;
            desc->control_bits = 0x00; // This is a READ
            card->current_idx++; // TBD - current_idx and p->cnt seem to track each other - can I use only p->cnt?
            pRequest->cnt++;
        }
    }

    if (card->current_idx >= bio->bi_vcnt)
    {
        // We are done  with this IO current_index is same as IO's last vector index
        card->currentbio = NULL;
    
        retVal=1;
    }
    
    card->nEnqueued++;
    if (card->nEnqueued == MAX_EV_REQUESTS)
        card->nEnqueued = 0;

    } while (card->bio != NULL);
#if 0
    EV_DEBUG_ALWAYS("nEnqueued:%d, nIssued:%d, nProcessed:%d, nCompleted:%d\n", 
                card->nEnqueued, card->nIssued, card->nProcessed, card->nCompleted);
#endif

    return retVal;
}

static int activate_ev_request(struct cardinfo* card, ev_request_t* pRequest)
{
    struct ev_dma_desc* desc;
    int offset;
    
    desc = &pRequest->desc[pRequest->cnt - 1];

    desc->control_bits |= DMASGL_DESCRIPTOR_CHAIN_END;

    desc->control_bits |= DMASGL_CHAIN_END_INTERRUPT_ENABLE;

    card->last_accessed_system_address = desc->data_dma_handle;
    desc = &pRequest->desc[pRequest->headcnt];
    offset = ((char*)desc) - ((char*)pRequest->desc);

#if 0
    EV_DEBUG_ALWAYS("REQ_ADDR page_dma = 0x%llX, offset = 0x%X,  Writing 0x%llX\n", 
            pRequest->page_dma, offset, (uint64_t)(pRequest->page_dma + offset));
#endif
    writel((uint32_t)(pRequest->page_dma + offset), card->reg_remap + EV_FPGA_REQ_ADDR_REG_L);
    // TBD - Look into this - shift is greater than size of the type - might as well write 0.
    // Look into a proper fix.
    writel((uint32_t)((pRequest->page_dma + offset) >> 32), card->reg_remap + EV_FPGA_REQ_ADDR_REG_H);
    writel(pRequest->cnt, card->reg_remap + EV_FPGA_REQ_LEN_REG);

    return 0;
}

static int issue_ev_request(struct cardinfo* card)
{
    ev_request_t*   pRequest;

//    while (card->nEnqueued != card->nIssued)
    if (card->nEnqueued != card->nIssued)
    {
        if (card->nIssued >= card->nProcessed) 
        {
            if ((card->nIssued - card->nProcessed) >= card->max_dmas_to_hw)
            {
                return 0;
            }
        }
        else
        {
            if ((card->nIssued + MAX_EV_REQUESTS - card->nProcessed) >= card->max_dmas_to_hw)
            {
                return 0;
            }
        }
        
        pRequest = &card->ev_request_pool[card->nIssued];
        
        pRequest->ready = 0;

        activate_ev_request(card, pRequest);
       
        card->nIssued++;
        if (card->nIssued == MAX_EV_REQUESTS)
            card->nIssued = 0;
#if 0 
        EV_DEBUG_ALWAYS("nE:%d, nI:%d, nP:%d, nC:%d\n", 
                card->nEnqueued, card->nIssued, card->nProcessed, card->nCompleted);
#endif   
        
    } 
#if 0 
    if (card->consumerStatus == 0)
    {
        card->consumerStatus = 1;
        wake_up(&WaitQueue_Consumer);
    }
#endif

    
    return 0;
}

static int ev_activate(struct cardinfo *card)
{
    int retval = 0; // 0 is success - meaning it was queued and maybe even started.
    int is_okay = FALSE; 
    int ok_to_add; // Number to add to SGL array
    int ok_to_start; // Number to start
    int total_added = 0;
    
    ok_to_add = ev_ok_to_add_io(card);

    printk("!!!!!!!!\n");

#if defined(USING_BIO)
#if defined(USING_SGIO) && defined(USING_BIO)
    if (ok_to_add>1) // We must reserve a place for the character driver to avoid blocking without having added an entry
#else
    if (ok_to_add>0) // We just need one place per add_bio
#endif
    {
        // is_okay in this case means all the descriptors for a single OS IO have been received and are ready
        // for DMA.

        is_okay = add_bio(card);  // Block driver make_request interface
        EV_DEBUG2("after add_bio is_okay=%d",is_okay);
        if (is_okay)
        {
            total_added++;
            ok_to_add--;
            if (card->empty_sgl >= (card->max_sgl - 1))
                card->empty_sgl = 0;
            else
                card->empty_sgl++;
        }

    }
#endif

#ifdef USING_SGIO

    if (ok_to_add)
    {
        is_okay = add_sgio(card); // Character driver IOCTL interface
        EV_DEBUG2("after add_sgio is_okay=%d",is_okay);
        if (is_okay)
        {
            total_added++;
            ok_to_add--;
            if (card->empty_sgl >= (card->max_sgl - 1))
                card->empty_sgl = 0;
            else
                card->empty_sgl++;
        }
    }

    if (ok_to_add)
    {
        is_okay = add_mapped_io(card); // Character driver READ/WRITE interface 
        EV_DEBUG2("after add_mapped_io is_okay=%d",is_okay);

        if (is_okay)
        {
            total_added++;
            ok_to_add--;
            if (card->empty_sgl >= (card->max_sgl - 1))
                card->empty_sgl = 0;
            else
                card->empty_sgl++;
        }
    }
#endif

    if (total_added == 0)
    {
        retval = -1; // This value is important to the character driver.
    }

    // Always check to see if there are any more entries in the SGL that need to get started.
    {
        // Process the SGL only when the SGL is ready and desc count is > 0
        // Whether or not we added any IO's there may be some waiting to get started. Go ahead and start.
        ok_to_start = ev_ok_to_start_io(card);
        if ((card->ev_sgls[card->next_active_sgl].ready) && (card->ev_sgls[card->next_active_sgl].cnt > 0) && ok_to_start) 
        {
            // Make the SGL not ready until it is made ready in the interrupt handler
            card->ev_sgls[card->next_active_sgl].ready = 0;    
            card->active_sgl = card->next_active_sgl;

            // Track next after active_sgl
            card->next_active_sgl++; 
            if (card->next_active_sgl >= card->max_sgl)
            {
                card->next_active_sgl = 0;
            }

            // Start as many as possible at a time.
            //for (i=0;i<ok_to_start;i++)

            if (ok_to_start)
            {
           //     ev_start_io(card);
            }
        }
    }


    // retval is number added to SGL list - not number started.
    return(retval);
}

/**********************************************************************************
*
* reset_sgl - Reset the used / new SGL.
*
* Arguments: sgl: pointer to the struct ev_sgl
*
* RETURNS: None.
**********************************************************************************/
static inline int reset_sgl(struct ev_sgl *sgl)
{
    sgl->cnt = 0;
    sgl->headcnt = 0;
    sgl->io_type = IO_NONE;
    sgl->io_context = NULL;
    sgl->io_status = IO_STATUS_GOOD;
    sgl->ready = 1;
#if defined(USING_BIO)
    sgl->bio = NULL;
    sgl->biotail = &sgl->bio;
#endif

#if defined(USING_SGIO)
    INIT_LIST_HEAD(&sgl->sgio);
    sgl->current_sgio = NULL;
    sgl->mapped_pages = NULL;  // Be careful to free these first.
#endif
    sgl->sync = TRUE; 

    return 0;
}
static void reset_debug_stats(struct cardinfo *card)
{
    // Some stats are always compiled in
    card->stats_perf.ios_completed = 0;
    card->stats_perf.start_time = 0;
    card->stats_perf.end_time = 0;
    card->stats_perf.bytes_transferred = 0;
    card->stats_perf.total_interrupts = 0;
    card->stats_perf.completion_interrupts = 0;

#if defined(DEBUG_STATS_CORE)
    card->stats_dbg.ios_rcvd = 0;
    card->stats_dbg.dmas_queued = 0;
    card->stats_dbg.dmas_started = 0;
    card->stats_dbg.dmas_completed = 0;
    card->stats_dbg.ios_completed = 0;
    card->stats_dbg.dmas_timed_out = 0;
    card->stats_dbg.dmas_errored = 0;
    card->stats_dbg.dma_completion_ints = 0;
    card->stats_dbg.dmas_max_outstanding = 0;
    card->stats_dbg.dmas_num_outstanding = 0;
    card->stats_dbg.descriptors_max_outstanding = 0;
    card->stats_dbg.ios_max_outstanding = 0;
    card->stats_dbg.num_read_bios = 0;
    card->stats_dbg.num_write_bios = 0;
    card->stats_dbg.num_unplug_fn_called = 0;
    card->stats_dbg.num_dbg_1 = 0;
    card->stats_dbg.num_dbg_2 = 0;
    card->stats_dbg.num_dbg_3 = 0;
    card->stats_dbg.num_dbg_4 = 0;
    card->stats_dbg.num_dbg_5 = 0;
    card->stats_dbg.num_dbg_6 = 0;
    card->stats_dbg.num_dbg_7 = 0;

    card->stats_dbg.dbg_head = 0;
    card->stats_dbg.dbg_tail = 0;
    card->stats_dbg.dbg_cycles = 0;
    card->stats_dbg.dbg_valid_entries = 0;
    memset(card->stats_dbg.dbg_buffer, 0, sizeof(card->stats_dbg.dbg_buffer));
    memset(card->stats_dbg.dbg_histogram, 0, sizeof(card->stats_dbg.dbg_histogram));
#endif
}                              

static int add_block_disk(struct cardinfo *card)
{
    int retVal=0;

    EV_DEBUG2("add_block_disk\n");
    reset_debug_stats(card);
    /* Initialize the queue for Device Operations using BIO structure */
    card->bio = NULL;
    card->biotail = &card->bio;

    card->users = 0;
    card->queue = blk_alloc_queue(GFP_KERNEL);
    if (!card->queue) 
    {
        EV_DEBUG("blk_alloc_queue FAILED");
        retVal = -ENOMEM;
    }
    else
    {
#ifdef COMPILE_IOSTAT
        if (iostat_enable)
        {
            queue_flag_set(QUEUE_FLAG_IO_STAT, card->queue);
        }
#endif
        blk_queue_make_request(card->queue, ev_make_request);

        card->queue->queue_lock = &card->lock;
        card->queue->queuedata = card;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)

        card->queue->unplug_fn = ev_unplug_device;

#endif

        blk_queue_dma_alignment(card->queue, EV_DMA_DATA_ALIGNMENT-1);  // TBD  - is this data or descriptor alignment???
        blk_queue_bounce_limit(card->queue, BLK_BOUNCE_ANY);

        EV_DEBUG("Sector size=%d Max descriptors per SGL=%d Max bytes per descriptor=%d Max sectors per IO=%d\n", 
                   EV_HARDSECT, MAX_DESC_PER_DMA, MAX_TRANSFER_BYTES_PER_DESCRIPTOR, MAX_SECTORS_PER_IO);

        blk_queue_max_segment_size(card->queue, MAX_TRANSFER_BYTES_PER_DESCRIPTOR);
    
        EV_DEBUG_ALWAYS("MAX_DESC_PER_DMA=%d  MAX_SECTORS_PER_IO=%d\n", MAX_DESC_PER_DMA, MAX_SECTORS_PER_IO);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32) // TBD - find out when these were removed.
        // These were eliminated in later kernel versions.
        blk_queue_hardsect_size(card->queue, EV_HARDSECT); 
        blk_queue_max_hw_segments(card->queue, MAX_DESC_PER_DMA);
        blk_queue_max_phys_segments(card->queue, MAX_DESC_PER_DMA); 
        blk_queue_max_sectors(card->queue, MAX_SECTORS_PER_IO);
#else
        blk_queue_max_segments(card->queue, MAX_DESC_PER_DMA);      // 256 - No diff as far as # of descriptors sent.
        blk_queue_max_hw_sectors(card->queue, MAX_SECTORS_PER_IO);  // This makes /sys/block/ev3mema/queue/max_hw_sectors_kb = 16256 = max decriptors outstanding to the hardware.. 
        card->queue->limits.max_sectors = (MAX_SECTORS_PER_IO);     //  Set maximum number of sectors per DMA. This makes /sys/block/ev3mema/queue/max_sectors_kb = 1024, which allows 256 descriptors. 
#endif

        /* Add the DDR Memory as a disk to the device */
        EV_DEBUG_ALWAYS("max_partitions=%d\n", max_partitions);
        card->disk = alloc_disk(max_partitions);  // Defaults to EV_MINORS, can be changed during load-time.
        if (! card->disk) 
        {
            EV_DEBUG("alloc_disk FAILED");
            retVal = -ENOMEM;
        }
        else
        {
            EV_DEBUG2("after alloc_disk\n");
            snprintf (card->disk->disk_name, 32, "%s%c", BLOCK_DRIVER_NAME, card->card_number + 'a');
            card->disk->major = b_major_nr;
            card->disk->first_minor = card->card_number << EV_SHIFT;
            card->disk->fops = &netlist_blk_ops;
            card->disk->queue = card->queue;
            card->disk->private_data = (void *) card;
            // Avoid potential hangs.
            EV_DEBUG2("gendisk info: name=%s major=%d first_minor=%d fops=%p queue=%p private_data=%p capacity in sectors=%ld\n",
                        card->disk->disk_name, card->disk->major, card->disk->first_minor, card->disk->fops,
                        card->disk->queue, card->disk->private_data, card->size_in_sectors);
            set_capacity(card->disk, 0);
            add_disk(card->disk);
            set_capacity(card->disk, card->size_in_sectors);

            EV_DEBUG2("after add_disk\n");

        }
    }

    return retVal;
}


/**********************************************************************************
*
* ev_make_request - This function is called whenever there is a request
*                                       for data transfer from kernel.
*
* Arguments: q: pointer to the structure request_queue
*                        bio: Pointer to bio structure which contains the data buffer.
*
* Return: Zero if Success
*                 Non Zero value in case of error.
*
**********************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)
static int ev_make_request(struct request_queue *q, ev_bio *bio)
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
static void ev_make_request(struct request_queue *q, ev_bio *bio)
#else
blk_qc_t ev_make_request(struct request_queue *q, ev_bio *bio)
#endif
#endif
{
    struct cardinfo *card = q->queuedata;

    // Start the performance time as soon as we get indications of a first IO
    // since the latest reset_stats.
    if (card->capture_stats)
    {
        if (card->stats_perf.start_time == 0)
        {
            rdtscll(card->stats_perf.start_time);
        }
    }

    EV_DEBUG_FASTPATH("bio=%p q=%p",bio,q);

    spin_lock(&card->EnqueueLock);

#if defined(DEBUG_STATS)
    {
        card->stats_dbg.ios_rcvd++; // Stats
        if ((card->stats_dbg.ios_rcvd-card->stats_dbg.ios_completed) > card->stats_dbg.ios_max_outstanding)
        {
            card->stats_dbg.ios_max_outstanding = (card->stats_dbg.ios_rcvd-card->stats_dbg.ios_completed); // Stats 
        }
        dbg_capture(card, 0x1); // Number of Block IO requests made by the OS
    }
#endif

    ev_bio_next(bio) = NULL;
    *card->biotail = bio;
    card->biotail = &ev_bio_next(bio);
    card->num_make_request++;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
    blk_plug_device(q);
 //   ev_activate(card);
#else
//    ev_activate(card);
#endif
    enqueue_ev_request(card);

   // spin_unlock_irq(&card->lock);
    spin_unlock(&card->EnqueueLock);
   
    spin_lock(&card->IssueLock);
    issue_ev_request(card);
    spin_unlock(&card->IssueLock);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)
    return 0;
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
    return BLK_QC_T_NONE;
#endif
#endif
    // No return value for other kernels
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
/**********************************************************************************
*
* ev_unplug_device - Method to unplug the block device
*
* Arguments: q: pointer to the struct request_queue
*
* RETURNS: None.
**********************************************************************************/
static void ev_unplug_device(struct request_queue *q)
{
#ifdef ENABLE_UNPLUG // out by default
    struct cardinfo *card = q->queuedata;
    unsigned long flags;

    spin_lock_irqsave(&card->lock, flags);
    
#if defined(DEBUG_STATS)
    card->stats_dbg.num_unplug_fn_called++; // Stats
    dbg_capture(card, 0x2);    // BIO unplug called
#endif

#ifdef WIP_FIND_OPTIMIZED_METHOD_FOR_THIS
    if (blk_remove_plug(q))
#endif
    {
#if defined(DEBUG_STATS)
        dbg_capture(card, 0x9);    // blk_remove_plug is non-zero - activate
#endif

        ev_activate(card);
    }

    spin_unlock_irqrestore(&card->lock, flags);
#endif
}
#endif


#ifndef DISABLE_DESCRIPTOR_DEBUG
static int largest_pcnt =0;
static unsigned long long largest_xfer_bytes = 0LL;
static unsigned long long smallest_xfer_bytes = 0xffffffffLL;
#endif
/**********************************************************************************
*
* add_bio - Get the buffer information to tranfer to/from the device and 
*            update the SGLs.
*
* Arguments: card: pointer to the cardinfo structure.
*
* Return: Zero if Success
*          Non Zero value in case of error.
**********************************************************************************/
static int add_bio(struct cardinfo *card)
{
    int retVal=0;
    struct ev_sgl *p;
    ev_bio *bio;
    int sgl_idx = card->empty_sgl;
    unsigned long long ddr_mem_addr;

#if defined(DEBUG_STATS)
    int num_sectors = 0;
#endif
    int temp_length;  

    

    bio = card->currentbio;
    if (!bio && card->bio) 
    {
        bio = card->currentbio = card->bio;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
        card->current_idx = card->bio->bi_idx;
        card->current_sector = card->bio->bi_sector+card->skip_sectors;
#else
	    card->current_idx = card->bio->bi_iter.bi_idx;
	    card->current_sector = card->bio->bi_iter.bi_sector+card->skip_sectors;
#endif
        card->bio = ev_bio_next(bio);
        if (card->bio == NULL)
            card->biotail = &card->bio;
        ev_bio_next(card->currentbio) = NULL;
    }

    if (!bio)
    {
#if defined(DEBUG_STATS)
        dbg_capture(card, 0x60);  // add_bio called with no IOs to process
#endif
        return 0;
    }

    p = &card->ev_sgls[sgl_idx];

    if (!p->ready) 
    {
#if defined(DEBUG_STATS)
        dbg_capture(card, 0xC6); // SGL is not ready during an add_bio
#endif
        return 0;
    }

    if (p->cnt >= MAX_DESC_PER_DMA)
    {
#if defined(DEBUG_STATS)
        dbg_capture(card, 0xC7); // BIO count exceeds the Descriptors per page
#endif
        return 0;
    }

    // At this point we have the bio and the descriptor array we want to add the bio data to
    {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)    	
        int segno;
        struct bio_vec *bvec;
#else
		struct bvec_iter segno;
		struct bio_vec bvec;
#endif
        int rw;
        dma_addr_t dma_handle;
        struct ev_dma_desc *desc;

        rw = bio_data_dir(bio);
        p->io_type = IO_BIO;
        p->io_context = (void *)bio;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
        if (bio->bi_size > 0)
#else
	    if (bio->bi_iter.bi_size > 0)
#endif
        {
            bio_for_each_segment(bvec, bio, segno) 
            {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0) // ARbitrary switch-over to dma_map 
				bvec = bio_iter_iovec(bio, segno);
				dma_handle = dma_map_page(&(card->dev->dev), bvec.bv_page, bvec.bv_offset, bvec.bv_len, (rw != WRITE) ?    DMA_FROM_DEVICE : DMA_TO_DEVICE);			    
				temp_length = bvec.bv_len;
                if (dma_mapping_error(&(card->dev->dev), dma_handle))
                {
                    EV_DEBUG_ALWAYS("ERROR: dma map error");
                }
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
                bvec = bio_iovec_idx(bio, segno);
                dma_handle = pci_map_page(card->dev, bvec->bv_page, bvec->bv_offset, bvec->bv_len, (rw != WRITE) ?    PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
                temp_length = bvec->bv_len;
#else
		        bvec = bio_iter_iovec(bio, segno);
		        dma_handle = pci_map_page(card->dev, bvec.bv_page, bvec.bv_offset, bvec.bv_len, (rw != WRITE) ?    PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);			    
                temp_length = bvec.bv_len;
#endif
#endif
                if (p->bio == NULL)
                    p->idx = card->current_idx;

                if (p->biotail != &ev_bio_next(bio)) 
                {
                    *(p->biotail) = bio;
                    p->biotail = &ev_bio_next(bio);
                    ev_bio_next(bio) = NULL;
                }



                while (temp_length > 0)
                {
                    // Fill in the next empty descriptor
                    desc = &p->desc[p->cnt];

#ifndef DISABLE_DESCRIPTOR_DEBUG
                    if (p->cnt > largest_pcnt)
                    {
                        largest_pcnt = p->cnt;
                    }
#endif

                    desc->data_dma_handle = (uint64_t)dma_handle;
                
                    ddr_mem_addr  = (u64)(card->current_sector << 9);
                    desc->ddr_mem_addr_lo = (u32)(ddr_mem_addr & 0xFFFFFFFF);   // Card address low
                    desc->ddr_mem_addr_hi = (u8)((ddr_mem_addr>>32) & 0xFF);    // Card address high

                    // Limit descriptors to 32K in size
                    if (temp_length > 0x8000)
                    {
                        desc->transfer_size = 0x8000;
                        temp_length -= 0x8000;
                    }
                    else
                    { 
                        desc->transfer_size = temp_length;
                        temp_length = 0;
                    }

#ifndef DISABLE_DESCRIPTOR_DEBUG
                    if (desc->transfer_size < smallest_xfer_bytes)
                    {
                        smallest_xfer_bytes = desc->transfer_size;
                    }

                    if (desc->transfer_size > largest_xfer_bytes)
                    {
                        largest_xfer_bytes = desc->transfer_size;
                    }
#endif

                    if (rw == WRITE)
                    {
                        desc->control_bits = 0x01; // This may be a WRITE or SWRITE(maybe - not sure)
#if defined(DEBUG_STATS)
                        if (p->cnt == 0)
                        {
                            card->stats_dbg.num_write_bios++;
                        }
#endif
                    }
                    else
                    {
                        desc->control_bits = 0x00; // This may be a READ or a READA (read-ahead)
#if defined(DEBUG_STATS)
                        if (p->cnt == 0)
                        {
                            card->stats_dbg.num_read_bios++;
                        }
#endif
                    }

                    card->current_sector += (desc->transfer_size >> 9);
                    card->current_idx++; // TBD - current_idx and p->cnt seem to track each other - can I use only p->cnt?
                    p->cnt++;
                }
            }
        }
        else
        {
            // Special case seen in kernels 2.6.33.12 thru 2.6.36.4. During "mke2fs" the driver receives a BIO of 
            // length 0. mke2fs will hang if we do not process this.
            // Do a dummy DMA - This is a BIO of zero length. This is seen on some kernel versions and is
            // likely some marker for the OS or a guarantee that the hardware has been flushed. We want to
            // preserve the order so we issue it just like any IO. We read DDR address 0 into a scratch buffer
            // and throw away the data.
            rw = READ;

            if (p->bio == NULL)
                p->idx = card->current_idx;

            if (p->biotail != &ev_bio_next(bio)) 
            {
                *(p->biotail) = bio;
                p->biotail = &ev_bio_next(bio);
                ev_bio_next(bio) = NULL;
            }

            // Fill in the next empty descriptor
            desc = &p->desc[p->cnt];
            // Make sure the alignment is correct.
            desc->data_dma_handle = (uint64_t)((card->scratch_dma_bus_addr+EV_DMA_DESCRIPTOR_ALIGNMENT-1) & 0xfffffffffffffff8ULL);
            desc->ddr_mem_addr_lo = (u32)(0); // Address 0
            desc->ddr_mem_addr_hi = (u8)(0); // Address 0
            desc->transfer_size = EV_HARDSECT;
            desc->control_bits = 0x00; // This is a READ
            card->current_idx++; // TBD - current_idx and p->cnt seem to track each other - can I use only p->cnt?
            p->cnt++;
        }
    }

    if (card->current_idx >= bio->bi_vcnt)
    {
        // We are done  with this IO current_index is same as IO's last vector index
        card->currentbio = NULL;
        retVal=1;
    }

#if defined(DEBUG_STATS)
    card->stats_dbg.dmas_queued++; // Stats

    num_sectors = bio_sectors(bio);    
    dbg_capture(card, 0x5 | (num_sectors<<8)); // Block SGL is queued 
#endif

    return retVal;
}

#ifdef USING_SGIO
/**********************************************************************************
*
* add_sgio - Get the buffer information to tranfer to/from the device and 
*            update the SGLs.
*
* Arguments: card: pointer to the cardinfo structure.
*
* Return: Zero if Success
*          Non Zero value in case of error.
**********************************************************************************/
static int add_sgio(struct cardinfo *card)
{
    struct ev_sgl *p;
    struct ev_dma_desc *desc;
    dma_addr_t dma_handle;
    struct sgio *io;
    mm_sgio_op rw;
    uint16_t len;
    uint64_t dev_addr;
    int sgl_idx = card->empty_sgl;

    io = card->current_sgio;
    if (!io && !list_empty(&card->sgio)) 
    {
        io = card->current_sgio = list_first_entry(&card->sgio, struct sgio, list);
        card->sgio_cnt--;
        list_del(&io->list);
    }

    if (!io)    
    {
        // This is expected if multiple drivers are queueing IOs and running simultaneously
        EV_DEBUG2("IO is NULL");
        return 0;
    }

    p = &card->ev_sgls[sgl_idx];
    if (!p->ready) 
    {
        EV_DEBUG2("SGL Not Ready");
        return 0;
    }

    EV_DEBUG2("card->empty_sgl %d p->cnt %d", card->empty_sgl, p->cnt);
    if (p->cnt >= MAX_DESC_PER_DMA) {
        EV_DEBUG2("ready page is full cnt %d max %lu", p->cnt, MAX_DESC_PER_DMA);
        return 0;
    }
    desc = &p->desc[p->cnt];

    rw = mm_sgio_rw(io);
    p->io_type = IO_SGIO;
    p->io_context = (void *)io;
    p->sync = io->sync;
    
    if (io->pvcnt == 0) {
        /* small request that doesn't use zero-copying */
        len = mm_sgio_len(io);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0) // ARbitrary switch-over to dma_map 
        dma_handle = dma_map_single(&(card->dev->dev), io->kbuf, len, (rw != MM_SGIO_WRITE) ? DMA_FROM_DEVICE : DMA_TO_DEVICE);
        if (dma_mapping_error(&(card->dev->dev), dma_handle))
        {
            EV_DEBUG_ALWAYS("ERROR: dma mapping");
        }
#else
        dma_handle = pci_map_single(card->dev, io->kbuf, len, (rw != MM_SGIO_WRITE) ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
#endif
        dev_addr = io->vec.dev_addr;
    } else {
        /* request that uses zero-copying */
        struct sgio_page_vec *pv = &io->page_vec[io->add_pvidx];
        if (io->add_pvidx >= MM_MAX_SGVEC_PAGES) {
            /* FIXME: should fail early so we can return error to user */
            panic("too many pages");
        }
        len = pv->pv_len;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0) // ARbitrary switch-over to dma_map 
        dma_handle = dma_map_page(&(card->dev->dev), pv->pv_page, pv->pv_offset, len, (rw != MM_SGIO_WRITE) ? DMA_FROM_DEVICE : DMA_TO_DEVICE);
        if (dma_mapping_error(&(card->dev->dev), dma_handle))
        {
            EV_DEBUG_ALWAYS("ERROR: dma mapping");
        }
#else
        dma_handle = pci_map_page(card->dev, pv->pv_page, pv->pv_offset, len, (rw != MM_SGIO_WRITE) ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
#endif
        dev_addr = pv->pv_devaddr;
    }

    p->cnt++;
    
    /* Update the descriptor */
    desc->data_dma_handle = (uint64_t)dma_handle;
    desc->ddr_mem_addr_lo = dev_addr;
    desc->ddr_mem_addr_hi = (dev_addr>>32);
    desc->transfer_size = len;

    if (rw == WRITE)
        desc->control_bits = 0x01;
    else
        desc->control_bits = 0x00;

    /* Print descriptor Contents */
    EV_DEBUG2("System Address : 0x%X", desc->data_dma_handle);
    EV_DEBUG2("DDR Address : 0x%llX", (u64)(((u64)desc->ddr_mem_addr_hi<<32) | desc->ddr_mem_addr_lo)); 
    EV_DEBUG2("Transfer length : 0x%X", desc->transfer_size);
    EV_DEBUG2("Control Bits : 0x%X", desc->control_bits);

    //    EV_DEBUG("0x%llX - 0x%X - 0x%X", desc->data_dma_handle, desc->ddr_mem_addr, desc->transfer_size);
    if (p->current_sgio != io) {
        p->current_sgio = io;
        list_add_tail(&io->page_list[sgl_idx], &p->sgio);
    }

    io->add_pvidx++;
    BUG_ON(io->add_pvidx >= MM_MAX_SGVEC_PAGES);

    /* This also apply to non-zero-copy SG io which has io->pvcnt 0 */
    if (io->add_pvidx >= io->pvcnt) {
        io->add_compl = 1;
        card->current_sgio = NULL;
    }

#if defined(DEBUG_STATS)
    card->stats_dbg.dmas_queued++; // Stats

    dbg_capture(card, 0x25); // IOCTL SGL is queued 
#endif

    return 1;
}

// This takes the IO mapped by the field "card->mapped_pages" and fills in the SGL.
static int add_mapped_io(struct cardinfo *card)
{
    struct ev_sgl *p;
    struct ev_dma_desc *desc;
    int retval = FALSE;
    int i;
    dma_addr_t dma_handle;
    unsigned short page_offset;
    size_t count = 0;

    mm_sgio_op rw;
    uint16_t len;
    uint64_t dev_addr;
    int sgl_idx = card->empty_sgl;

    // If there is nothing to do then exit.
    if (card->mapped_pages == NULL)
    {
        return retval;
    }

    p = &card->ev_sgls[sgl_idx];
    if (!p->ready) 
    {
        EV_DEBUG_ALWAYS("SGL Not Ready");
        return retval;
    }

    EV_DEBUG_FASTPATH("card->empty_sgl %d p->cnt %d", card->empty_sgl, p->cnt);
    if (p->cnt >= MAX_DESC_PER_DMA) 
    {
        EV_DEBUG_ALWAYS("ERROR: ready page is full cnt %d max %d", p->cnt, MAX_DESC_PER_DMA);
        return retval;
    }


    i = 0;
    p->io_type = IO_MAPPED;
    p->io_context = (void *)NULL;  // There is no IO context, the delivery thread will block.

    dev_addr = card->offset;
    page_offset = card->page_offset;
    count = card->count;


    while((i<card->num_mapped_pages) && (p->cnt<MAX_DESC_PER_DMA))
    {
        size_t bytes = PAGE_SIZE - page_offset;

        desc = &p->desc[p->cnt];
        rw = card->rw;
        len = min(bytes,count);
        count -= len;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0) // ARbitrary switch-over to dma_map 
        dma_handle = dma_map_page(&(card->dev->dev), card->mapped_pages[i], page_offset, len, (card->rw == READ) ?  DMA_FROM_DEVICE : DMA_TO_DEVICE);
        if (dma_mapping_error(&(card->dev->dev), dma_handle))
        {
            EV_DEBUG_ALWAYS("ERROR: dma mapping");
        }
#else
        dma_handle = pci_map_page(card->dev, card->mapped_pages[i], page_offset, len, (card->rw == READ) ?  PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
#endif

        EV_DEBUG2("idx=%x p->cnt=%x phys_addr=%llx dma_handle=%llx page_offset=%x len=%x count=%lx", 
                    i, p->cnt, page_to_phys(card->mapped_pages[i]), dma_handle, card->page_offset, len, count); 


        /* Update the descriptor */
        //desc->data_dma_handle = (uint64_t)page_to_phys(card->mapped_pages[i] + page_offset);
        desc->data_dma_handle = dma_handle;
        desc->ddr_mem_addr_lo = dev_addr;
        desc->ddr_mem_addr_hi = (dev_addr>>32);
        desc->transfer_size = len;

        if (rw == WRITE)
            desc->control_bits = 0x01;
        else
            desc->control_bits = 0x00;

#if 1
        /* Print descriptor Contents */
        EV_DEBUG2("System Address : 0x%llX", desc->data_dma_handle);
        EV_DEBUG2("DDR Address : 0x%llX", (u64)(((u64)desc->ddr_mem_addr_hi<<32) | desc->ddr_mem_addr_lo)); 
        EV_DEBUG2("Transfer length : 0x%X", desc->transfer_size);
        EV_DEBUG2("Control Bits : 0x%X", desc->control_bits);
#endif
        dev_addr += bytes;
        p->cnt++;
        i++;
        page_offset = 0; // It's always 0 after that first page is done.
    }
    
    retval = TRUE;

    // Save the mapped_pages information for the completion routine to release.
    p->mapped_pages = card->mapped_pages;

    // Reset the fields used. These should not be used on the completion side.
    card->mapped_pages = NULL;
    card->num_mapped_pages = 0;
    card->offset = 0;
    card->rw = READ;
    card->page_offset = 0;

#if defined(DEBUG_STATS)
    card->stats_dbg.dmas_queued++; // Stats
    dbg_capture(card, 0x15); // Character driver SGL is queued 
#endif

    return retval;
}
#endif // USING_SGIO

#define sgio_list_first_entry(ptr, idx)  (struct sgio *)((char *)(ptr)->next - (char *)&(((struct sgio *)0)->page_list[idx]))


/**********************************************************************************
*
* netlist_process_one_completion - This will terminate and complete a single IO 
&                                  and is normally called from the "bottom half"
*                                  tasklet or from a timeout case.
*
* Arguments: data : contains struct cardinfo information 
*
* Returns: None
*
**********************************************************************************/

// Complete one IO with the status code. 
static void netlist_abort_io(struct cardinfo *card, int status_code)
{
    struct ev_sgl *sgl;
    struct ev_dma_desc *desc;
    int temp_idx;

#if defined(USING_SGIO)
    unsigned long long ddr_mem_addr;
    struct sgio *io;
    int do_wakeup = 0;
    uint16_t len = 0;
    uint16_t control;
#endif

#if defined(USING_BIO)
    ev_bio *bio;
    ev_bio *return_bio = NULL;
#endif

    EV_DEBUG_ALWAYS("EV%d: Completing I/O with status 0x%x\n", card->card_number, status_code);
    // Complete the next IO
    spin_lock_bh(&card->lock);

    // Since the interrupt did not happen we must increment the control variable to process a single IO
    card->process_req_sgl++;  // This counter is cumulative since power-up.

    if (card->num_sgl_processed >= card->process_req_sgl)
    {
#if defined(DEBUG_STATS)
        dbg_capture(card, 0x61); // Bottom-half tasklet scheduled with no work to do
#endif
        spin_unlock_bh(&card->lock);
        return;
    }

    card->num_sgl_processed++;

    temp_idx = card->processed_sgl;  // Save the previously processed SGL index
    if (card->processed_sgl >= (card->max_sgl - 1))
    {
        card->processed_sgl = 0;
    }
    else
    {
        card->processed_sgl++;        
    }

#if defined(DEBUG_STATS)
    dbg_capture(card, (card->processed_sgl<<16) | 0x4); // An IO completion has been processed
#endif

    del_dma_timer(card, card->processed_sgl);
    sgl = &card->ev_sgls[card->processed_sgl];

    // Completion based on which flow generated the original IO
    switch(sgl->io_type)
    {
        case IO_BIO:
#if defined(USING_BIO)
            bio = sgl->bio;
            {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
                int segno;
                struct bio_vec *bvec;
#else
		        struct bvec_iter segno;
		        struct bio_vec bvec;
#endif

                int rw;

                rw = bio_data_dir(bio);
                bio_for_each_segment(bvec, bio, segno) 
                {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0) // ARbitrary switch-over to dma_map 
		            bvec = bio_iter_iovec(bio, segno);
		            desc = &sgl->desc[segno.bi_idx];
		            dma_unmap_page(&(card->dev->dev), desc->data_dma_handle, bvec.bv_len, (rw != WRITE) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);	    
		            card->stats_perf.bytes_transferred += bvec.bv_len;
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
                    bvec = bio_iovec_idx(bio, segno);
                    desc = &sgl->desc[segno];
                    pci_unmap_page(card->dev, desc->data_dma_handle, bvec->bv_len, (rw != WRITE) ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
                    card->stats_perf.bytes_transferred += bvec->bv_len;
#else
		            bvec = bio_iter_iovec(bio, segno);
		            desc = &sgl->desc[segno.bi_idx];
		            pci_unmap_page(card->dev, desc->data_dma_handle, bvec.bv_len, (rw != WRITE) ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);	    
		            card->stats_perf.bytes_transferred += bvec.bv_len;
#endif
#endif

                }

                // Increment this value before a call to activate is possible due to the start_io throttle being tested during
                // the activate path.
#if defined(DEBUG_STATS)
                card->stats_dbg.dmas_completed++;
#endif

                if (card->ev_sgls[card->processed_sgl].io_status == IO_STATUS_GOOD)
                {
                    ev_bio_uptodate(bio, 1); /* Good completion status */
                }
                else
                {
                    ev_bio_uptodate(bio, 0); /* Bad completion status */
                    //Same as clear_bit(BIO_UPTODATE, &bio->bi_flags);
                    EV_DEBUG_ALWAYS("EV%d: I/O error on SGL 0x%x\n", card->card_number, card->processed_sgl);
                }

#ifdef COMPILE_IOSTAT
                if (iostat_enable)
                {
                    ev_iostat_end(bio, sgl->start);
                }
#endif

                reset_sgl(sgl);
                return_bio = bio;

                // We only need to activate the next one if there is a next one to activate or start
                // Note that next_active and empty_sgl will not track each other
                // when multiple drivers are running simultaneously.
                if ((card->currentbio!=NULL) || (card->bio!=NULL) || (card->next_active_sgl != card->empty_sgl)) 
                {
                    ev_activate(card);
                }

                spin_unlock_bh(&card->lock);

                if (return_bio) 
                {
                    bio = return_bio;

                    return_bio = ev_bio_next(bio);
                    ev_bio_next(bio) = NULL;
                    //EV_DEBUG3("Completing BIO %p\n", bio);

#if defined(DEBUG_STATS)
                    card->stats_dbg.ios_completed++; // Stats
#endif
                    card->stats_perf.ios_completed++;
                    // Might want to use ev_bio_endio to handle kernel differences.
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
                    bio_endio(bio, 0, status_code); // Somewhere between 2.6.18 and 2.6.28.1 the number of paarmeters changed - TBD - find out which 
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
                    bio_endio(bio, status_code);
#else
                    bio_endio(bio);
#endif
#endif
                }
            }
#endif
            break;
        case IO_SGIO:
#if defined(USING_SGIO)
            while (sgl->headcnt < sgl->cnt)
            {
                int deleted;
                BUG_ON(list_empty(&sgl->sgio));
                io = sgio_list_first_entry(&sgl->sgio, card->processed_sgl);
                BUG_ON(io->deleted);

                desc = &sgl->desc[sgl->headcnt];
                control = desc->control_bits & 0x01;
                deleted = 0;

                if (io->pvcnt == 0) 
                {
                    len = io->kbuf_bytes;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0) // ARbitrary switch-over to dma_map 
                    dma_unmap_single(&(card->dev->dev), desc->data_dma_handle, len, (control)? DMA_TO_DEVICE : DMA_FROM_DEVICE);
#else
                    pci_unmap_single(card->dev, desc->data_dma_handle, len, (control)? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
#endif
                    list_del(&io->page_list[card->processed_sgl]);
                     deleted = 1;
                } 
                else 
                {
                    struct sgio_page_vec *pv = &io->page_vec[io->compl_pvidx];
                    len = pv->pv_len;
                    if (!io->kernel_page) 
                    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0) // ARbitrary switch-over to dma_map 
                        dma_unmap_page(&(card->dev->dev), desc->data_dma_handle, len, (control) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
#else
                        pci_unmap_page(card->dev, desc->data_dma_handle, len, (control) ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
#endif
                    }

                    if (io->add_pvidx == io->pvcnt && io->add_compl != 1) 
                    {
                        panic("add_comp != 1: pvcnt %u compl_pvidx %u add_pvidx %u add_compl %u "
                              "io->deleted %d deleted %d\n",
                              io->pvcnt, io->compl_pvidx, io->add_pvidx, io->add_compl, io->deleted,
                              deleted);
                    }

                    if (++io->compl_pvidx == io->pvcnt) 
                    {
                        if (io->add_compl != 1) 
                        {
                            panic("completed but add_comp != 1: pvcnt %u compl_pvidx %u "
                                  "add_pvidx %u add_compl %u io->deleted %d deleted %d\n",
                                  io->pvcnt, io->compl_pvidx, io->add_pvidx, io->add_compl,
                                  io->deleted, deleted);
                        }

                        list_del(&io->page_list[card->processed_sgl]);
                        deleted = 1;
                        io->deleted = 1;
                    }

                    if (io->compl_pvidx > io->add_pvidx) 
                    {
                        struct mm_kiocb *iocb = io->kiocb;
                        panic("compl_pvidx > add_pvidx: pvcnt %u compl_pvidx %u add_pvidx %u "
                              "add_compl %u io->deleted %d deleted %d ncompld %d nvec %d\n",
                            io->pvcnt, io->compl_pvidx, io->add_pvidx, io->add_compl, io->deleted, deleted,
                            iocb->ncompld, iocb->nvec);
                    }
                }

                if (deleted) 
                {
                    struct mm_kiocb *iocb = io->kiocb;

                    list_add_tail(&io->list, &iocb->compld);
                    if (++iocb->ncompld == iocb->nvec) 
                    {
                        list_del(&iocb->list);

                        if (sgl->sync)
                        {
                            // Only add synchronous IO to the compld_iocb list.
                            list_add_tail(&iocb->list, &card->compld_iocb);
                            do_wakeup = 1;
                        }
                    }
                }

                /*
                 * If we have completed a combined DMA request from multiple DMA requests, we
                 * need to split the results and apply it on the sub-requests.
                 */

                if (desc->transfer_size == len) 
                {
                    sgl->headcnt++;
                } 
                else 
                {
                    panic("desc len (%u) != len (%u)\n", desc->transfer_size, len);
                    EV_DEBUG("MM: process merge requests desc %p io %p len %d",
                         desc, io, len);
                    desc->transfer_size -= len;
            
                    ddr_mem_addr = ((unsigned long long)desc->ddr_mem_addr_hi<<32) | desc->ddr_mem_addr_lo;
                    ddr_mem_addr += len;
                    desc->ddr_mem_addr_lo = ddr_mem_addr & 0xffffffff;
                    desc->ddr_mem_addr_hi = (uint8_t)((ddr_mem_addr >> 32) & 0xff);
                    desc->data_dma_handle += len;
                }

                card->stats_perf.bytes_transferred += desc->transfer_size; 
            } // End of while loop

            // Increment this value before a call to activate is possible due to the start_io throttle being tested during
            // the activate path.
#if defined(DEBUG_STATS)
            card->stats_dbg.dmas_completed++;
#endif

            if (sgl->headcnt >= sgl->cnt) 
            {
#if defined(DEBUG_STATS)
                card->stats_dbg.ios_completed++; // Stats
#endif
                card->stats_perf.ios_completed++;
                reset_sgl(sgl);

                // We only need to activate the next one iff there is a next one
                if (!(list_empty(&card->pending_iocb)))
                {
                    ev_activate(card);
                }
            } 
            else 
            {
                EV_DEBUG("headcnt=%d < cnt=%d\n", sgl->headcnt, sgl->cnt);
                /* haven't finished with this one yet */
            }

            if (do_wakeup) 
            {
                if (waitqueue_active(&card->event_waiters))
                    wake_up_interruptible(&card->event_waiters);
            }
#endif
            spin_unlock_bh(&card->lock);
            break;
        case IO_MAPPED: // The character driver READ/WRITE path
#if defined(USING_SGIO)
            while (sgl->headcnt < sgl->cnt)
            {
                desc = &sgl->desc[sgl->headcnt];
                control = desc->control_bits & 0x01;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0) // ARbitrary switch-over to dma_map 
                dma_unmap_page(&(card->dev->dev), desc->data_dma_handle, desc->transfer_size, (control) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
#else
                pci_unmap_page(card->dev, desc->data_dma_handle, desc->transfer_size, (control) ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
#endif
                if (control == 0) // READ
                {
                    // Mark the page as dirty
                    SetPageDirty(sgl->mapped_pages[sgl->headcnt]);
                }
                // TBD May need cache flush for rw==READ

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
                page_cache_release(sgl->mapped_pages[sgl->headcnt]);
#else
                put_page(sgl->mapped_pages[sgl->headcnt]);
#endif

                sgl->headcnt++;

                card->stats_perf.bytes_transferred += desc->transfer_size; 

            } // End of while loop
            kfree(sgl->mapped_pages);

            // Increment this value before a call to activate is possible due to the start_io throttle being tested during
            // the activate path.
#if defined(DEBUG_STATS)
            card->stats_dbg.dmas_completed++;
#endif
#if defined(DEBUG_STATS)
            card->stats_dbg.ios_completed++; // Stats
#endif
            card->stats_perf.ios_completed++;
            reset_sgl(sgl);

            // Send signal to complete the synchronous IO.
            do_wakeup = 1;
            if (do_wakeup) 
            {
#if defined(DEBUG_STATS)
                dbg_capture(card, 0x16); // Number of wakeups to chr threads
#endif
                if (waitqueue_active(&card->event_waiters))
                {
                    wake_up_interruptible(&card->event_waiters);
#if defined(DEBUG_STATS)
                    dbg_capture(card, 0x17); // Number of wakeups to chr threads with active waiters
#endif
                }
            }

            spin_unlock_bh(&card->lock);
#endif
            break;
        case IO_NONE: // Fall through
        default:
            // Error case
            spin_unlock_bh(&card->lock);
            break;
    }

    if (card->capture_stats)
    {
        spin_lock_bh(&card->lock);
        rdtscll(card->stats_perf.end_time);
        spin_unlock_bh(&card->lock);
    }
}

static void netlist_process_finalization(struct cardinfo* card)
{
    struct ev_dma_desc *desc;
    ev_request_t* pRequest = NULL;
    ev_bio *bio;
    ev_bio *return_bio = NULL;

    do
    {
        spin_lock_bh(&card->CompleteLock);
        if (card->nCompleted == card->nProcessed)
        {
            spin_unlock_bh(&card->CompleteLock);
            
            return;
        }
        pRequest = &card->ev_request_pool[card->nCompleted];
        card->nCompleted++;
        if (card->nCompleted == MAX_EV_REQUESTS)
            card->nCompleted = 0;
#if 0 
        EV_DEBUG_ALWAYS("nE:%d, nI:%d, nP:%d, nC:%d\n", 
                card->nEnqueued, card->nIssued, card->nProcessed, card->nCompleted);
#endif
        spin_unlock_bh(&card->CompleteLock);

        if (pRequest->done_buffer != pRequest->page_dma)
        {
            if (card->ooo_count<64)
            {
                card->ooo_count++;
                EV_DEBUG_ALWAYS("process_completion: out-of-order found s/b 0x%llX but is 0x%llx\n", 
                        pRequest->page_dma, pRequest->done_buffer);
                EV_DEBUG_ALWAYS("^^nEnqueued:%d, nIssued:%d, nProcessed:%d, nCompleted:%d\n", 
                        card->nEnqueued, card->nIssued, card->nProcessed, card->nCompleted);
               
                if (card->ooo_count==64)
                {
                    EV_DEBUG_ALWAYS("process_completion: STOPPING out-of-order messages\n");
                }
            }
                
        }

        // Completion based on which flow generated the original IO
        switch(pRequest->io_type)
        {
            case IO_BIO:
#if defined(USING_BIO)
                bio = pRequest->bio;
                {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
                    int segno;
                    struct bio_vec *bvec;
#else
		            struct bvec_iter segno;
		            struct bio_vec bvec;
#endif                    
                    int rw;

                    rw = bio_data_dir(bio);

                    bio_for_each_segment(bvec, bio, segno) 
                    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0) // ARbitrary switch-over to dma_map 
						bvec = bio_iter_iovec(bio, segno);
						desc = &pRequest->desc[segno.bi_idx];
						dma_unmap_page(&(card->dev->dev), desc->data_dma_handle, bvec.bv_len, (rw != WRITE) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);		
						card->stats_perf.bytes_transferred += bvec.bv_len;
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
                        bvec = bio_iovec_idx(bio, segno);
                        desc = &pRequest->desc[segno];
                        pci_unmap_page(card->dev, desc->data_dma_handle, bvec->bv_len, (rw != WRITE) ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
                        card->stats_perf.bytes_transferred += bvec->bv_len;
#else
						bvec = bio_iter_iovec(bio, segno);
						desc = &pRequest->desc[segno.bi_idx];
						pci_unmap_page(card->dev, desc->data_dma_handle, bvec.bv_len, (rw != WRITE) ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);		
						card->stats_perf.bytes_transferred += bvec.bv_len;
#endif
#endif

                    }

                    // Increment this value before a call to activate is possible due to the start_io throttle being tested during
                    // the activate path.
#if defined(DEBUG_STATS)
                    card->stats_dbg.dmas_completed++;
#endif

                    if (pRequest->io_status == IO_STATUS_GOOD)
                    {
                        ev_bio_uptodate(bio, 1); /* Good completion status */
                    }
                    else
                    {
                        ev_bio_uptodate(bio, 0); /* Bad completion status */
                        //Same as clear_bit(BIO_UPTODATE, &bio->bi_flags);
                        EV_DEBUG_ALWAYS("EV%d: I/O error on SGL 0x%x\n", card->card_number, card->processed_sgl);
                    }

                    initialize_ev_request(pRequest);

                    return_bio = bio;


                    if (return_bio) 
                    {
                        bio = return_bio;

                        return_bio = ev_bio_next(bio);
                        ev_bio_next(bio) = NULL;
                        //EV_DEBUG3("Completing BIO %p\n", bio);

#if defined(DEBUG_STATS)
                        card->stats_dbg.ios_completed++; // Stats
#endif
                        card->stats_perf.ios_completed++;

                        // Might want to use ev_bio_endio to handle kernel differences.
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
// Somewhere between 2.6.18 and 2.6.28.1 the number of parameters changed - TBD - find out which 
                    	bio_endio(bio, bio->bi_size, 0);
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
                        bio_endio(bio, 0);
#else
                        bio_endio(bio);
#endif
#endif
                    }
                }
#endif
                break;
            case IO_NONE: // Fall through
            default:
                break;
        }

        if (card->capture_stats)
        {
            spin_lock_bh(&card->lock);
            rdtscll(card->stats_perf.end_time);
            spin_unlock_bh(&card->lock);
        }

#if 0
#ifndef USE_WORK_QUEUES
        if (loop_counter == 0)
        {
            tasklet_schedule(&card->tasklet[tasklet_index]);
        }
#else
        // Use the last setting of tasklet_index to select the work context.
        // num_dmas_to_complete is not currently used
        if (loop_counter == 0)
	    {
            //card->tasklet_context[tasklet_index].num_dmas_to_complete = num_dmas_to_complete; // Not implemented
       	    queue_work(card->wq, &card->tasklet_context[tasklet_index].ws);
        }

#endif
#endif
        if (card->nCompleted == card->nProcessed)
        {
       	  //  queue_work(card->wq, &card->tasklet_context[tasklet_index].ws);
            break;
        }

    } while (1);
}

/**********************************************************************************
*
* netlist_process_completions - This is the completion interrupt's "bottom-half"
* tasklet.
*
* Arguments: data : contains struct cardinfo information 
*
* Returns: None
*
**********************************************************************************/
static void netlist_process_completions(unsigned long data)
{
    struct ev_tasklet_data *tasklet_context = (struct ev_tasklet_data *)data;
    struct cardinfo *card = tasklet_context->card;
    struct ev_dma_desc *desc;
    ev_request_t* pRequest = NULL;
    ev_bio *bio;
    ev_bio *return_bio = NULL;

    // Stay in here until the same count IO interrupt count matches the processed count.
    // The plan is to implement this differently by having the interrupt_status array get cleaned up
    // by the tasklet processing.
    do
    {
        card->producerStatus = 1;
        wake_up_interruptible(&WaitQueue_Producer);
      
        spin_lock_bh(&card->CompleteLock);
        if (card->nCompleted == card->nProcessed)
        {
            spin_unlock_bh(&card->CompleteLock);
            
            return;
        }
        pRequest = &card->ev_request_pool[card->nCompleted];
        card->nCompleted++;
        if (card->nCompleted == MAX_EV_REQUESTS)
            card->nCompleted = 0;
#if 0 
        EV_DEBUG_ALWAYS("nE:%d, nI:%d, nP:%d, nC:%d\n", 
                card->nEnqueued, card->nIssued, card->nProcessed, card->nCompleted);
#endif
        spin_unlock_bh(&card->CompleteLock);

        if (pRequest->done_buffer != pRequest->page_dma)
        {
            if (card->ooo_count<64)
            {
                card->ooo_count++;
                EV_DEBUG_ALWAYS("process_completion: out-of-order found s/b 0x%llX but is 0x%llx\n", 
                        pRequest->page_dma, pRequest->done_buffer);
                EV_DEBUG_ALWAYS("^^nEnqueued:%d, nIssued:%d, nProcessed:%d, nCompleted:%d\n", 
                        card->nEnqueued, card->nIssued, card->nProcessed, card->nCompleted);
               
                if (card->ooo_count==64)
                {
                    EV_DEBUG_ALWAYS("process_completion: STOPPING out-of-order messages\n");
                }
            }
                
        }

        // Completion based on which flow generated the original IO
        switch(pRequest->io_type)
        {
            case IO_BIO:
#if defined(USING_BIO)
                bio = pRequest->bio;
                {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
                    int segno;
                    struct bio_vec *bvec;
#else
		            struct bvec_iter segno;
		            struct bio_vec bvec;
#endif                    
                    int rw;

                    rw = bio_data_dir(bio);

                    bio_for_each_segment(bvec, bio, segno) 
                    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0) // ARbitrary switch-over to dma_map 
						bvec = bio_iter_iovec(bio, segno);
						desc = &pRequest->desc[segno.bi_idx];
						dma_unmap_page(&(card->dev->dev), desc->data_dma_handle, bvec.bv_len, (rw != WRITE) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);		
						card->stats_perf.bytes_transferred += bvec.bv_len;
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
                        bvec = bio_iovec_idx(bio, segno);
                        desc = &pRequest->desc[segno];
                        pci_unmap_page(card->dev, desc->data_dma_handle, bvec->bv_len, (rw != WRITE) ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
                        card->stats_perf.bytes_transferred += bvec->bv_len;
#else
						bvec = bio_iter_iovec(bio, segno);
						desc = &pRequest->desc[segno.bi_idx];
						pci_unmap_page(card->dev, desc->data_dma_handle, bvec.bv_len, (rw != WRITE) ? PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);		
						card->stats_perf.bytes_transferred += bvec.bv_len;
#endif
#endif

                    }

                    // Increment this value before a call to activate is possible due to the start_io throttle being tested during
                    // the activate path.
#if defined(DEBUG_STATS)
                    card->stats_dbg.dmas_completed++;
#endif

                    if (pRequest->io_status == IO_STATUS_GOOD)
                    {
                        ev_bio_uptodate(bio, 1); /* Good completion status */
                    }
                    else
                    {
                        ev_bio_uptodate(bio, 0); /* Bad completion status */
                        //Same as clear_bit(BIO_UPTODATE, &bio->bi_flags);
                        EV_DEBUG_ALWAYS("EV%d: I/O error on SGL 0x%x\n", card->card_number, card->processed_sgl);
                    }

                    initialize_ev_request(pRequest);

                    return_bio = bio;


                    if (return_bio) 
                    {
                        bio = return_bio;

                        return_bio = ev_bio_next(bio);
                        ev_bio_next(bio) = NULL;
                        //EV_DEBUG3("Completing BIO %p\n", bio);

#if defined(DEBUG_STATS)
                        card->stats_dbg.ios_completed++; // Stats
#endif
                        card->stats_perf.ios_completed++;

                        // Might want to use ev_bio_endio to handle kernel differences.
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
// Somewhere between 2.6.18 and 2.6.28.1 the number of parameters changed - TBD - find out which 
                    	bio_endio(bio, bio->bi_size, 0);
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
                        bio_endio(bio, 0);
#else
                        bio_endio(bio);
#endif
#endif
                    }
                }
#endif
                break;
            case IO_NONE: // Fall through
            default:
                break;
        }

        if (card->capture_stats)
        {
            spin_lock_bh(&card->lock);
            rdtscll(card->stats_perf.end_time);
            spin_unlock_bh(&card->lock);
        }

#if 0
#ifndef USE_WORK_QUEUES
        if (loop_counter == 0)
        {
            tasklet_schedule(&card->tasklet[tasklet_index]);
        }
#else
        // Use the last setting of tasklet_index to select the work context.
        // num_dmas_to_complete is not currently used
        if (loop_counter == 0)
	    {
            //card->tasklet_context[tasklet_index].num_dmas_to_complete = num_dmas_to_complete; // Not implemented
       	    queue_work(card->wq, &card->tasklet_context[tasklet_index].ws);
        }

#endif
#endif
        if (card->nCompleted == card->nProcessed)
        {
       	  //  queue_work(card->wq, &card->tasklet_context[tasklet_index].ws);
            break;
        }

    } while (1);
}

#ifdef USE_WORK_QUEUES
static void workq_func(struct work_struct *work)
{
    struct ev_tasklet_data *data = (struct ev_tasklet_data *)work; 
    netlist_process_completions((unsigned long)data);
}
#endif

//#define MAX_INT_LOOP 64  // Handle one IO per physical interrupt - 1 slows down.
#define MAX_INT_LOOP 256  // Handle one IO per physical interrupt - 1 slows down.

static irqreturn_t process_interrupt(struct cardinfo *card)
{
    irqreturn_t ret_val = IRQ_HANDLED;

    if (card->consumerStatus == 0)
    {
        card->consumerStatus = 1;
        wake_up(&WaitQueue_Consumer);
    }
//    card->nInterrupts++;

    return ret_val;
}

static int process_interrupt_handler(struct cardinfo *card)
{
    unsigned int dma_status;
    unsigned int temp_dword_1 = 0;
    unsigned int temp_dword_2 = 0;
    unsigned int temp_dword_3 = 0;
    int tasklet_index = 0;
    ev_request_t*   pRequest;
    int nLoop = 0;

    while (card->nProcessed != card->nIssued)    
    {
        dma_status = readq(card->int_status_address + card->current_address);

        if (dma_status)
        {
            writeq(0, card->int_status_address + card->current_address);

            // A DMA error could mean that the EV_INT_DESC_PROCESS may not be set due to overloaded bit field. 
            if (dma_status & ~(EV_INT_NVL_STATUS_CHANGE))
            {
                pRequest = &card->ev_request_pool[card->nProcessed];
                pRequest->done_buffer = dma_status & ~(EV_INT_DMA_COMPLETION | EV_INT_DMA_ERROR);
#if 0 
                EV_DEBUG_ALWAYS("INT RX: nProcessed:%d, dma_status=%x, page_dma=%llx, loop_count=%d\n", 
               card->nProcessed, dma_status, pRequest->page_dma, loop_count);
#endif                
#if 0
                if (card->enable_descriptor_throttle)
                {
                    card->active_descriptors -= pRequest->cnt;
                }
#endif
                if (dma_status & EV_INT_DMA_ERROR)
                {
                    pRequest->io_status = IO_STATUS_ECC_MULTIBIT_ERROR;
                }
                
                card->nProcessed++;
                if (card->nProcessed == MAX_EV_REQUESTS)
                    card->nProcessed = 0;
#if 0 
                EV_DEBUG_ALWAYS("nE:%d, nI:%d, nP:%d, nC:%d\n", 
                        card->nEnqueued, card->nIssued, card->nProcessed, card->nCompleted);
#endif   

 //               nLoop++;
 //               card->nInterrupts--;
              
           //     tasklet_index = (card->current_address%num_workqueues); // Odd/even  - high numbers
           //     queue_work(card->wq, &card->tasklet_context[tasklet_index].ws);
       
                card->finalizer_data[card->current_finalizer].status = 1;
                wake_up_interruptible(&card->finalizer_data[card->current_finalizer].ev_finalizer_waitQ); 
              
                card->producerStatus = 1;
                wake_up_interruptible(&WaitQueue_Producer);
             
                card->current_finalizer++;
                if (card->current_finalizer == NUM_EV_FINALIZERS)
                    card->current_finalizer = 0;
            }
            else
            {
                // Asynchronous interrupts
                if (dma_status & EV_INT_NVL_STATUS_CHANGE)
                {
                    // NV status change

                    temp_dword_1 = readl(card->reg_remap + HOST_INTERRUPT_STATUS_REG);
                    temp_dword_2 = readl(card->reg_remap + HOST_INTERRUPT_MASK_REG);
                    temp_dword_3 = readl(card->reg_remap + ERROR_REG);
                    EV_DEBUG_ALWAYS("NV status change INT_STATUS=0x%.8X INT_MASK=0x%.8X ERROR_REG=0x%.8X", 
                            temp_dword_1, temp_dword_2, temp_dword_3);

                    // Clear the HOST_INTERRUPT_STATUS_REG right away.
                    writel(temp_dword_1, card->reg_remap + HOST_INTERRUPT_STATUS_REG);                                
                }
                else
                {
                    // Unknmown interrupt this is not our interrupt.
                //    exit_loop = TRUE;
                    return 0;
                }
            }

            card->current_address++;
            if (card->current_address == EV_NUM_INT_STATUS_ENTRIES)
            {
                card->current_address = 0;
            }
#if 0
            if (readq(card->int_status_address + card->current_address) == 0)
            {
                exit_loop = TRUE;
            }
#endif
        }
        else
        {
            // No more interrupts
         //   exit_loop = TRUE;
          
            return 0;
        }

    } // end of while loop

    return 0;
}

/**********************************************************************************
*
* ev_interrupt - This function is called whenever an interrupt is generated.
*
* Arguments: Irq: Irq number
*             card: Pointer to struct cardinfo
*
* Return: Zero if Success
*          Non Zero value in case of error.
*
**********************************************************************************/
static irqreturn_t ev_interrupt(int irq, void *__card)
{
    struct cardinfo *card = (struct cardinfo *) __card;

    EV_DEBUG("ev_interrupt():\n");
    card->msi_interrupt_count++;
    return process_interrupt(card);
}

static int init_beacon_timer(struct cardinfo *card);

static void beacon_toggle(unsigned long context)
{
    int card_idx = (context & 0xff); // Low byte has card info
    struct cardinfo *card = &cards[card_idx];
    unsigned int led_value=0;

    if (card->beacon_state)
    {
        led_value = (LED_LINK_CONTROL |  
                     LED_ARMED_CONTROL |   
                     LED_BACKUP_CONTROL | 
                     LED_RESTORE_CONTROL |
                     LED_PMU_CONTROL |
                     LED_ERROR_CONTROL |   
                     LED_DCAL_CONTROL);

        writel(led_value, card->reg_remap + LED_CONTROL); // LEDs OFF
    }
    else
    {
        led_value = (LED_LINK_CONTROL |  LED_LINK_STATE |
                     LED_ARMED_CONTROL | LED_ARMED_STATE |  
                     LED_BACKUP_CONTROL | LED_BACKUP_STATE |
                     LED_RESTORE_CONTROL | LED_RESTORE_STATE | 
                     LED_PMU_CONTROL | LED_PMU_STATE |
                     LED_ERROR_CONTROL | LED_ERROR_STATE |   
                     LED_DCAL_CONTROL | LED_DCAL_STATE);

        writel(led_value, card->reg_remap + LED_CONTROL); // LEDs ON
    }

    card->beacon_state = !card->beacon_state;
    dbg_capture(card, 0x22); // BEACON timer expired

    // Linux timers are not cyclic we need to restart the timer for each cycle
    // for as long as the control variable is set to TRUE.
    if (card->beacon)
    {
        init_beacon_timer(card);
    }
}

static int init_beacon_timer(struct cardinfo *card)
{
    init_timer(&card->beacon_timer);
    card->beacon_timer.function = beacon_toggle;
    card->beacon_timer.data = card->card_number;
    card->beacon_timer.expires  = jiffies + (HZ / 8); // Blink times per second
    add_timer(&card->beacon_timer);
    return 0;
}

#ifdef TBD
static int ev_fill_ram(struct cardinfo *card, unsigned long long pattern)
{
    int ret_val = 0;
    uint64_t temp_addr = 0; 
    unsigned int temp_dword = 0;
    unsigned int window_number = 0;
    int count;
    uint64_t i; // Current address

    EV_DEBUG_ALWAYS("Fill pattern is = 0x%llx", pattern);

    // Fill the entire DDR space regardless of the setting of skip_sectors.
    for (i=0;i<((card->size_in_sectors + card->skip_sectors)*EV_HARDSECT);i+=sizeof(unsigned long long))
    {
        // Check to see if this is a windowed version. If so ensure the proper window has been selected.
        if (card->num_windows>1)
        {
            window_number = (i / card->window_size);

            // Get the offset into the window.
            temp_addr = (i % card->window_size); 

            // See if proper window is set
            if (card->cur_window != window_number)
            {
                card->cur_window = window_number;

                EV_DEBUG2("New window selected 0x%x, offset = 0x%llx", card->cur_window, temp_addr);

                window_number |= EV_WIN_SEL_CHANGE_START;
                writel(window_number, card->reg_remap + EV_FPGA_WINDOW_SELECT);
                
                count=0;    
                do
                {
                    temp_dword = readl(card->reg_remap + EV_FPGA_WINDOW_SELECT);
                    temp_dword &= EV_WIN_SEL_CHANGE_START;

                    count++; // A way out of this loop.
                } while ((temp_dword!=0) && (count<100000));

                if (count==10000000)
                {
                    EV_DEBUG_ALWAYS("ERROR: window change timed out");
                }
                else
                {
                    EV_DEBUG2("window change loop iterations = %d", count);
                }
            }
        }
        else
        {
            temp_addr = i; // It is linear otherwise
        }

        writeq(pattern, card->mem_remap + temp_addr); 
    }

    // This will guarantee that on exit of this function, the data has reached the DDR and is not queued
    // somewhere in the hardware. This is a flush.
    i = readq(card->mem_remap + temp_addr); 
    return(ret_val);
}
#endif


#define INVALID_I2C_VALUE (0xFFFFFFFF)

uint32_t pmu_spd_access(struct cardinfo *card, uint8_t spd_data, uint8_t spd_addr, uint8_t spdread)
{
    uint32_t ret_val = 0;
	uint64_t value = 0;	
	uint32_t pmuspdi;

	udelay(1000*1);
	value = card->passcode;
    writel(value, card->reg_remap + MAR_AM); 
	udelay(1000*10);

	if( spdread != 1 )
	{
	    // access is write	
		// register
		value = spd_addr;
        writel(value, card->reg_remap + PMU_SPD_ADDRESS); 

		// data
		udelay(1000*10);
		value = spd_data;
        writel(value, card->reg_remap + PMU_SPD_DATA); 
	}
	else
	{
	    // access is read
		// register
		value = spd_addr;
        writel(value, card->reg_remap + PMU_SPD_ADDRESS); 
	}

    value = readl(card->reg_remap + CONTROL_REG); 
	if( spdread != 1 )
	{
		value |= (1<<9);
	}
	else
	{
		value &= ~(1<<9);	
	}
    writel(value, card->reg_remap + CONTROL_REG); 
	udelay(1000*5);

	// write 1 to reg 0x200.2
    value = readl(card->reg_remap + COMMAND_REG); 
	//printf ("Register \t(0x%.3x) = 0x%8X\n", 0x200, (int)(value ));
	value = value | 0x04;
	
	udelay(1000*5);
    writel(value, card->reg_remap + COMMAND_REG); 

	//udelay(1000*10);
	pmuspdi = 0;
	do
	{	
		udelay(1000*1);
		pmuspdi++;
		if (pmuspdi > 1000)
		{
            EV_DEBUG_ALWAYS("ERROR: PMU SPD access timeout");
            ret_val = INVALID_I2C_VALUE;
		}
        value = readl(card->reg_remap + COMMAND_REG); 
	} while(((value & 0x04) != 0) && (ret_val != INVALID_I2C_VALUE));

    if (ret_val != INVALID_I2C_VALUE)
    {
	    // read data from spd
	    if( spdread == 1 )
	    {
            value = readl(card->reg_remap + PMU_SPD_DATA); 

            if (value != 0xdeaddead)
            {
                ret_val = (uint32_t)(value & 0xff);
            }
            else
            {
                ret_val = INVALID_I2C_VALUE;
            }
		    udelay(1000*5);
	    }
    }

	value = 0x0;
    writel(value, card->reg_remap + MAR_AM); 
	return ret_val;
}
		
static int ev_hw_access(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;
    mem_addr_size_t *access_request_ptr;
    uint64_t temp_addr = 0; 
    unsigned char temp_byte = 0;
    unsigned short temp_word = 0;
    unsigned int temp_dword = 0;
    unsigned int window_number = 0;
    int count;
    uint32_t i2c_result = 0;

    temp_arg = (ioctl_arg_t *)arg;
    access_request_ptr = &temp_arg->ioctl_union.mem_addr_size;

    switch(access_request_ptr->space)
    {
        case SPACE_PCI: // PCI Config Space registers
            if (access_request_ptr->is_read)
            {
                switch(access_request_ptr->access_size)
                {
                    case 1:
                        ret_val = pci_read_config_byte(card->dev, access_request_ptr->addr, &temp_byte);
                        access_request_ptr->val = temp_byte;
                        break;
                    case 2:
                        ret_val = pci_read_config_word(card->dev, access_request_ptr->addr, &temp_word);
                        access_request_ptr->val = temp_word;
                        break;
                    case 4:
                        ret_val = pci_read_config_dword(card->dev, access_request_ptr->addr, &temp_dword);
                        access_request_ptr->val = temp_dword;
                        break;
                    case 8:
                        ret_val = -EINVAL; /* Not supported */
                        break;
                    default:
                        ret_val = -EINVAL;
                }
            }
            else
            {
                switch(access_request_ptr->access_size)
                {
                    case 1:
                        temp_byte = (unsigned char)(access_request_ptr->val & 0xff);
                        ret_val = pci_write_config_byte(card->dev, access_request_ptr->addr, temp_byte);
                        break;
                    case 2:
                        temp_word = (unsigned short)(access_request_ptr->val & 0xffff);
                        ret_val = pci_write_config_byte(card->dev, access_request_ptr->addr, temp_word);
                        break;
                    case 4:
                        temp_dword = (unsigned long)(access_request_ptr->val & 0xffffffff);
                        ret_val = pci_write_config_byte(card->dev, access_request_ptr->addr, temp_dword);
                        break;
                    case 8:
                        ret_val = -EINVAL; /* Not supported */
                        break;
                    default:
                        ret_val = -EINVAL;
                }
            }
            break;
        case SPACE_REG:
            /* Memory-mapped registers */
            if (access_request_ptr->is_read)
            {
                switch(access_request_ptr->access_size)
                {
                    case 1:
                        access_request_ptr->val = readb(card->reg_remap + access_request_ptr->addr);
                        break;
                    case 2:
                        access_request_ptr->val = readw(card->reg_remap + access_request_ptr->addr);
                        break;
                    case 4:
                        access_request_ptr->val = readl(card->reg_remap + access_request_ptr->addr);
                        break;
                    case 8:
                        access_request_ptr->val = readq(card->reg_remap + access_request_ptr->addr);
                        break;
                    default:
                        ret_val = -EINVAL;
                }
            }
            else
            {
                switch(access_request_ptr->access_size)
                {
                    case 1:
                        writeb(access_request_ptr->val, card->reg_remap + access_request_ptr->addr);
                        break;
                    case 2:
                        writew(access_request_ptr->val, card->reg_remap + access_request_ptr->addr);
                        break;
                    case 4:
                        writel(access_request_ptr->val, card->reg_remap + access_request_ptr->addr);
                        break;
                    case 8:
                        writeq(access_request_ptr->val, card->reg_remap + access_request_ptr->addr);
                        break;
                    default:
                        ret_val = -EINVAL;
                }
            }
            break;
        case SPACE_PIO:
            // Check to see if this is a windowed version. If so ensure the proper window has been selected.
            if (card->num_windows>1)
            {
                window_number = (access_request_ptr->addr / card->window_size);

                // Adjust the offset into the window.
                temp_addr = access_request_ptr->addr; // Save the original as we will be changing it.
                access_request_ptr->addr = (access_request_ptr->addr % card->window_size); 

                // See if proper window is set
                if (card->cur_window != window_number)
                {
                    card->cur_window = window_number;

                    EV_DEBUG_ALWAYS("New window selected 0x%x, offset = 0x%llx", 
                                    card->cur_window, access_request_ptr->addr);

                    window_number |= EV_WIN_SEL_CHANGE_START;
                    writel(window_number, card->reg_remap + EV_FPGA_WINDOW_SELECT);
                    
                    count=0;    
                    do
                    {
                        temp_dword = readl(card->reg_remap + EV_FPGA_WINDOW_SELECT);
                        temp_dword &= EV_WIN_SEL_CHANGE_START;

                        count++; // A way out of this loop.
                    } while ((temp_dword!=0) && (count<100000));

                    if (count==10000000)
                    {
                        EV_DEBUG_ALWAYS("ERROR: window change timed out");
                    }
                    else
                    {
                        EV_DEBUG_ALWAYS("window change loop iterations = %d", count);
                    }
                }
            }

            /* Memory-mapped PIO access using BAR1 */
            if (access_request_ptr->is_read)
            {
//#define USE_POINTERS
#ifndef USE_POINTERS
                switch(access_request_ptr->access_size)
                {
                    case 1:
                        access_request_ptr->val = readb(card->mem_remap + access_request_ptr->addr);
                        break;
                    case 2:
                        access_request_ptr->val = readw(card->mem_remap + access_request_ptr->addr);
                        break;
                    case 4:
                        access_request_ptr->val = readl(card->mem_remap + access_request_ptr->addr);
                        break;
                    case 8:
                        access_request_ptr->val = readq(card->mem_remap + access_request_ptr->addr);
                        break;
                    default:
                        ret_val = -EINVAL;
                }
#else
                {
                    unsigned char b;
                    unsigned char *b_ptr;
                    unsigned short w;
                    unsigned short *w_ptr;
                    unsigned int dw;
                    unsigned int *dw_ptr;
                    unsigned long long qw;
                    unsigned long long *qw_ptr;


                    switch(access_request_ptr->access_size)
                    {
                        case 1:
                            b_ptr = (unsigned char *)(card->mem_remap + access_request_ptr->addr);
                            b = *b_ptr;
                            access_request_ptr->val = b;
                            break;
                        case 2:
                            w_ptr = (unsigned short *)(card->mem_remap + access_request_ptr->addr);
                            w = *w_ptr;
                            access_request_ptr->val = w;
                            break;
                        case 4:
                            dw_ptr = (unsigned int *)(card->mem_remap + access_request_ptr->addr);
                            dw = *dw_ptr;
                            access_request_ptr->val = dw;
                            break;
                        case 8:
                            qw_ptr = (unsigned long long *)(card->mem_remap + access_request_ptr->addr);
                            qw = *qw_ptr;
                            access_request_ptr->val = qw;
                            break;
                        default:
                            ret_val = -EINVAL;
                    }
                }
#endif
            }
            else
            {
#ifndef USE_POINTERS
                switch(access_request_ptr->access_size)
                {
                    case 1:
                        writeb(access_request_ptr->val, card->mem_remap + access_request_ptr->addr);
                        break;
                    case 2:
                        writew(access_request_ptr->val, card->mem_remap + access_request_ptr->addr);
                        break;
                    case 4:
                        writel(access_request_ptr->val, card->mem_remap + access_request_ptr->addr);
                        break;
                    case 8:
                        writeq(access_request_ptr->val, card->mem_remap + access_request_ptr->addr);
                        break;
                    default:
                        ret_val = -EINVAL;
                }
#else
                {
                    unsigned char b;
                    unsigned char *b_ptr;
                    unsigned short w;
                    unsigned short *w_ptr;
                    unsigned int dw;
                    unsigned int *dw_ptr;
                    unsigned long long qw;
                    unsigned long long *qw_ptr;


                    switch(access_request_ptr->access_size)
                    {
                        case 1:
                            b_ptr = (unsigned char *)(card->mem_remap + access_request_ptr->addr);
                            b  = (unsigned char *)(access_request_ptr->val & 0xff);
                            *b_ptr = b;
                            break;
                        case 2:
                            w_ptr = (unsigned short *)(card->mem_remap + access_request_ptr->addr);
                            w  = (unsigned short *)(access_request_ptr->val & 0xffff);
                            *w_ptr = w;
                            break;
                        case 4:
                            dw_ptr = (unsigned int *)(card->mem_remap + access_request_ptr->addr);
                            dw  = (unsigned int *)(access_request_ptr->val & 0xffffffff);
                            *dw_ptr = dw;
                            break;
                        case 8:
                            qw_ptr = (unsigned long long *)(card->mem_remap + access_request_ptr->addr);
                            qw  = (unsigned long long *)(access_request_ptr->val);
                            *qw_ptr = qw;
                            break;
                        default:
                            ret_val = -EINVAL;
                    }
                }
#endif
            }

            if (card->num_windows>1)
            {
                access_request_ptr->addr = temp_addr; // Restore the saved value so we do not have side effects at the app.
            }
            break;
        case SPACE_I2C:
            i2c_result = pmu_spd_access(card, (unsigned char)(access_request_ptr->val & 0xff), (unsigned char)(access_request_ptr->addr & 0xff),  (unsigned char)(access_request_ptr->is_read));
            if (i2c_result != INVALID_I2C_VALUE)
            {
                if (access_request_ptr->is_read)
                {
                    access_request_ptr->val = i2c_result;
                }
            }
            else
            {
                ret_val = -EFAULT;
            }
            break;
        default:
            break;
    }

    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}

static int ev_get_set_capture_stats(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;
    get_set_val_t *request_ptr;

    temp_arg = (ioctl_arg_t *)arg;
    request_ptr = &temp_arg->ioctl_union.get_set_val;

    // Make sure it is within range.
    if (request_ptr->is_read)
    {
        request_ptr->value = card->capture_stats;
    }
    else
    {
        if ((request_ptr->value>=0) && (request_ptr->value<=1))
        {
            EV_DEBUG_ALWAYS("Setting capture stats to %lld", request_ptr->value);
            card->capture_stats = request_ptr->value;
        }
        else
        {
            EV_DEBUG3("Invalid value");
            ret_val = -EFAULT;
        }
    }

    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}

static int ev_get_perf_stats(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;

    temp_arg = (ioctl_arg_t *)arg;
    
    card->stats_perf.is_enabled = card->capture_stats; // Pass enabled/disabled info.

    temp_arg->ioctl_union.performance_stats = card->stats_perf;    
    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}

static int ev_set_max_outstanding_dmas(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;
    get_set_val_t *request_ptr;

    temp_arg = (ioctl_arg_t *)arg;
    request_ptr = &temp_arg->ioctl_union.get_set_val;

    // Make sure it is within range.
    if (request_ptr->is_read)
    {
        request_ptr->value = card->max_dmas_to_hw;
    }
    else
    {
        if ((request_ptr->value>0) && (request_ptr->value<card->max_sgl))
        {
            card->max_dmas_to_hw = request_ptr->value;
        }
        else
        {
            EV_DEBUG3("Invalid value");
            ret_val = -EFAULT;
        }
    }

    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}


static int ev_set_max_outstanding_descriptors(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;
    get_set_val_t *request_ptr;

    temp_arg = (ioctl_arg_t *)arg;
    request_ptr = &temp_arg->ioctl_union.get_set_val;

    // Make sure it is within range.
    if (request_ptr->is_read)
    {
        request_ptr->value = card->max_descriptors_to_hw;
    }
    else
    {
        if ((request_ptr->value>0) && (request_ptr->value<(MAX_DESC_PER_DMA*DEFAULT_MAX_DMAS_QUEUED_TO_HW)))
        {
            card->max_descriptors_to_hw = request_ptr->value;
        }
        else
        {
            EV_DEBUG3("Invalid value");
            ret_val = -EFAULT;
        }
    }

    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}


static int ev_set_beacon(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;
    get_set_val_t *request_ptr;
    unsigned int val = 0;

    temp_arg = (ioctl_arg_t *)arg;
    request_ptr = &temp_arg->ioctl_union.get_set_val;

    // Make sure it is within range.
    if (request_ptr->is_read)
    {
        request_ptr->value = card->beacon;
    }
    else
    {
        if ((request_ptr->value>=0) && (request_ptr->value<=1))
        {
            if ((card->beacon) && (!request_ptr->value))
            {
                // Turning the beacon off
                EV_DEBUG_ALWAYS("BEACON OFF");
                val = 0;
                writel(val, card->reg_remap + LED_CONTROL);
                (void) del_timer_sync(&card->beacon_timer);
            }
            else
            {
                if ((!card->beacon) && (request_ptr->value))
                {                                 
                    // Turning the beacon on
                    EV_DEBUG_ALWAYS("BEACON ON");
                    init_beacon_timer(card);
                }
            }
            card->beacon = request_ptr->value;
        }
        else
        {
            EV_DEBUG3("Invalid value");
            ret_val = -EFAULT;
        }
    }

    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}

static int ev_set_passcode(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;
    get_set_val_t *request_ptr;

    temp_arg = (ioctl_arg_t *)arg;
    request_ptr = &temp_arg->ioctl_union.get_set_val;

    // Make sure it is within range.
    if (request_ptr->is_read)
    {
        // We will allow reading this user-entered value
        request_ptr->value = card->passcode;
    }
    else
    {
        if ((request_ptr->value>=0) && (request_ptr->value<=0xffffffff))
        {
            // WRITE the value to the hardware
            EV_DEBUG_ALWAYS("PASSCODE %llx has been stored", request_ptr->value);
            card->passcode = request_ptr->value; // Save for later I2C register accesses
        }
        else
        {
            EV_DEBUG3("Invalid value");
            ret_val = -EFAULT;
        }
    }                        

    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}

static int ev_set_memory_window(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;
    get_set_val_t *request_ptr;
    uint32_t temp_dword;
    uint32_t window_number = 0;
    int count = 0;

    temp_arg = (ioctl_arg_t *)arg;
    request_ptr = &temp_arg->ioctl_union.get_set_val;

    // Make sure it is within range.
    if (request_ptr->is_read)
    {
        request_ptr->value = card->cur_window;
    }
    else
    {
        if ((request_ptr->value>=0) && (request_ptr->value<card->num_windows))
        {
            if (card->cur_window != request_ptr->value)
            {
                card->cur_window = window_number = request_ptr->value;

                // TBD - remove - this is debug only - do not show all window changes
                EV_DEBUG("New window selected 0x%x", card->cur_window);

                window_number |= EV_WIN_SEL_CHANGE_START;
                writel(window_number, card->reg_remap + EV_FPGA_WINDOW_SELECT);
                
                count=0;    
                do
                {
                    temp_dword = readl(card->reg_remap + EV_FPGA_WINDOW_SELECT);
                    temp_dword &= EV_WIN_SEL_CHANGE_START;

                    count++; // A way out of this loop.    This usually takes only 1 cycle.
                } while ((temp_dword!=0) && (count<100000));

                if (count==10000000)
                {
                    EV_DEBUG_ALWAYS("ERROR: window change timed out");
                }
                else
                {
                    EV_DEBUG("window change loop iterations = %d", count);
                }
            }
        }
        else
        {
            EV_DEBUG3("Invalid value");
            ret_val = -EFAULT;
        }
    }

    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}

static int nl_ecc_reset(struct cardinfo *card)
{
    int ret_val = 0;
    uint32_t temp_dword;

    if (card->ecc_is_enabled)
    {
        EV_DEBUG_ALWAYS("ECC Counters have been reset, passcode=%x", card->passcode);

        temp_dword = readl(card->reg_remap + EV_FPGA_ECC_CTRL);
        temp_dword |= CLEAR_ECC_ERROR;
        writel(temp_dword, card->reg_remap + EV_FPGA_ECC_CTRL);
    }
    else
    {
        EV_DEBUG_ALWAYS("ERROR: ECC is not enabled");
        ret_val = -EFAULT;
    }

    return ret_val;
}

static int ev_ecc_reset(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;

    temp_arg = (ioctl_arg_t *)arg;

    ret_val = nl_ecc_reset(card);

    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}

static int ev_ecc_status(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;
    ecc_status_t *request_ptr;
    uint32_t temp_dword;
    uint8_t temp_byte;

    temp_arg = (ioctl_arg_t *)arg;
    request_ptr = &temp_arg->ioctl_union.ecc_status;

    if (card->ecc_is_enabled)
    {
        temp_byte = readb(card->reg_remap + EV_FPGA_ECC_STATUS);
        request_ptr->is_sbe = ((temp_byte & SBE) == SBE);
        request_ptr->is_mbe = ((temp_byte & MBE) == MBE);

        temp_byte = readb(card->reg_remap + EV_FPGA_ECC_CTRL);
        request_ptr->is_auto_corr_enabled = ((temp_byte & ENABLE_AUTO_CORR) == ENABLE_AUTO_CORR);

        temp_byte = readb(card->reg_remap + EV_FPGA_SBE_COUNT);
        request_ptr->num_ddr_single_bit_errors = temp_byte;

        temp_byte = readb(card->reg_remap + EV_FPGA_MBE_COUNT);
        request_ptr->num_ddr_multi_bit_errors = temp_byte;

        temp_dword = readl(card->reg_remap + EV_FPGA_ECC_ERROR_ADDR);
        temp_dword *= 8; 

        // The low 2 bits are always 0 and this provides a range of 32 addresses where the 
        // ECC error would have been.
        request_ptr->last_ddr_error_range_start = temp_dword;
        temp_dword += 32;    
        request_ptr->last_ddr_error_range_end = temp_dword;
    }
    else
    {
        EV_DEBUG_ALWAYS("ERROR: ECC is not enabled");
        ret_val = -EFAULT;
    }

    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}

static int ev_get_set_ecc_sbe_gen(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;
    get_set_val_t *request_ptr;
    uint8_t temp_byte;
    uint8_t temp_byte2;

    temp_arg = (ioctl_arg_t *)arg;
    request_ptr = &temp_arg->ioctl_union.get_set_val;

    if (card->ecc_is_enabled)
    {
        // Make sure it is within range.
        temp_byte = readb(card->reg_remap + EV_FPGA_ECC_CTRL);

        if (request_ptr->is_read)
        {
            request_ptr->value = ((temp_byte & GEN_SBE)==GEN_SBE);
        }
        else
        {
            if ((request_ptr->value>=0) && (request_ptr->value<=1))
            {
                if (request_ptr->value==0) 
                {
                    temp_byte &= ~GEN_SBE;
                }
                else
                {
                    temp_byte |= GEN_SBE;
                }

                // Write the current passcode to enable ECC error generation
                //writel(card->passcode, card->reg_remap + EV_FPGA_ID_PASSCODE);    
                writeb(temp_byte, card->reg_remap + EV_FPGA_ECC_CTRL);

                // If the bit did not get set properly, send error related to no passcode.
                temp_byte2 = readb(card->reg_remap + EV_FPGA_ECC_CTRL);
                if ((temp_byte & GEN_SBE) != (temp_byte2 & GEN_SBE))
                {
                    EV_DEBUG_ALWAYS("ERROR: ECC error generation is not enabled - please enter proper passcode");
                    ret_val = -EFAULT;
                }
            }
            else
            {
                EV_DEBUG3("Invalid value");
                ret_val = -EFAULT;
            }
        }
    }
    else
    {
        EV_DEBUG_ALWAYS("ERROR: ECC is not enabled");
        ret_val = -EFAULT;
    }

    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}

static int ev_get_set_ecc_dbe_gen(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;
    get_set_val_t *request_ptr;
    uint8_t temp_byte;
    uint8_t temp_byte2;

    temp_arg = (ioctl_arg_t *)arg;
    request_ptr = &temp_arg->ioctl_union.get_set_val;

    if (card->ecc_is_enabled)
    {
        // Make sure it is within range.
        temp_byte = readb(card->reg_remap + EV_FPGA_ECC_CTRL);

        if (request_ptr->is_read)
        {
            request_ptr->value = ((temp_byte & GEN_MBE)==GEN_MBE);
        }
        else
        {
            if ((request_ptr->value>=0) && (request_ptr->value<=1))
            {
                if (request_ptr->value==0) 
                {
                    temp_byte &= ~GEN_MBE;
                }
                else
                {
                    temp_byte |= GEN_MBE;
                }

                // Write the current passcode to enable ECC error generation
                //writel(card->passcode, card->reg_remap + EV_FPGA_ID_PASSCODE);    
                writeb(temp_byte, card->reg_remap + EV_FPGA_ECC_CTRL);

                // If the bit did not get set properly, send error related to no passcode.
                temp_byte2 = readb(card->reg_remap + EV_FPGA_ECC_CTRL);
                if ((temp_byte & GEN_MBE) != (temp_byte2 & GEN_MBE))
                {
                    EV_DEBUG_ALWAYS("ERROR: ECC error generation is not enabled - please enter proper passcode");
                    ret_val = -EFAULT;
                }
            }
            else
            {
                EV_DEBUG3("Invalid value");
                ret_val = -EFAULT;
            }
        }
    }
    else
    {
        EV_DEBUG_ALWAYS("ERROR: ECC is not enabled");
        ret_val = -EFAULT;
    }

    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}

static int ev_get_set_write_access(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;
    get_set_val_t *request_ptr;

    temp_arg = (ioctl_arg_t *)arg;
    request_ptr = &temp_arg->ioctl_union.get_set_val;

    EV_DEBUG_ALWAYS("write accees");

    if (request_ptr->is_read)
    {
        request_ptr->value = card->write_access;
    }
    else
    {
        if ((request_ptr->value>=0) && (request_ptr->value<=1))
        {
            card->write_access = (int)request_ptr->value;
        }
        else
        {
            EV_DEBUG3("Invalid value");
            ret_val = -EFAULT;
        }
    }

    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}


static int ev_get_data_log(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;

    temp_arg = (ioctl_arg_t *)arg;
    temp_arg->ioctl_union.data_logger_stats = card->data_logger;
    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}

static int ev_get_skip_sectors(struct cardinfo *card, unsigned long arg)
{
    int ret_val = 0;
    ioctl_arg_t *temp_arg;
    get_set_val_t *request_ptr;

    temp_arg = (ioctl_arg_t *)arg;
    request_ptr = &temp_arg->ioctl_union.get_set_val;

    request_ptr->value = card->skip_sectors;
    temp_arg->errnum = (ret_val == 0)?IOCTL_ERR_SUCCESS:IOCTL_ERR_FAIL;
    return ret_val;
}

/* WIP: For 32-bit support we will need something like this. */
#if defined(__x86_64__)
#ifndef HAVE_COMPAT_IOCTL
static int
netlist_ioctl32(unsigned int fd, unsigned int iocmd, unsigned long ioarg, struct file * filp)
{
   int ret;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 4, 26) || \
   (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 3))
   lock_kernel();
#endif
   ret = -ENOTTY;
   if (filp && filp->f_op && filp->f_op->ioctl == netlist_ioctl) {
      ret = netlist_ioctl(filp->f_dentry->d_inode, filp, iocmd, ioarg);
   }
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 4, 26) || \
   (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 3))
   unlock_kernel();
#endif
   return ret;
}
#endif /* !HAVE_COMPAT_IOCTL */
#endif /* __x86_64__ */

#ifdef USING_SGIO
#if defined(HAVE_UNLOCKED_IOCTL) || defined(HAVE_COMPAT_IOCTL)
static long    netlist_unlocked_ioctl(struct file *filp, u_int iocmd,unsigned long arg)
{
    long err;
    struct inode *inode;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 4, 26) || \
   (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 3))
       lock_kernel();
#endif


#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)
    inode = filp->f_dentry->d_inode;
#else
    inode = filp->f_path.dentry->d_inode;
#endif


    err = netlist_ioctl(inode, filp, iocmd, arg);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 4, 26) || \
   (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 3))
       unlock_kernel();
#endif
       return err;
}
#endif
#endif

/**********************************************************************************
*
* netlist_ioctl - hardware control through the application.
*
* Arguments: inode: A pointer to the struct inode
*             filp: A pointer to the struct file
*             cmd: Argument is passed from the user unchanged
*             arg: Optional argument is passed in the form of an unsigned long
*
* Return: Zero if Success - Meaning that the ioctl was able to execute and the value of 
*                            tmp_arg->errnum is a valid number.
*          Non Zero value in case of error.
*
**********************************************************************************/
static int netlist_ioctl(struct inode *i, struct file *f, unsigned int cmd, unsigned long arg)
{
    int err = 0; // This is the return value for this function
    int card_number;
    unsigned int minor;
    struct cardinfo *card;
    ioctl_arg_t *local_arg = NULL; // Pointer to kernel space copy of the IOCTL structure.
    int is_ev_ioc = FALSE;
    int num_user_bytes = 0; // Number of user space bytes to copy

#if defined(USING_BIO)
    struct hd_geometry geo;
#endif

//#if !defined(HAVE_UNLOCKED_IOCTL) && !defined(HAVE_COMPAT_IOCTL)
    if (!i || !i->i_rdev)
    {
        EV_DEBUG3("Invalid IOCTL");
        return -EINVAL;
    }
    minor = iminor(i);
//#else
//    minor = 0; /* Workaround for the later kernels - number needs to calculated differently. */
//#endif

    card_number = (minor >> EV_SHIFT);
    card        = &cards[card_number];

    EV_DEBUG("card %d, ioctl cmd = %x, arg = %lx\n", card->card_number, cmd, arg);
    
#if defined(DEBUG_STATS)
    //dbg_capture(card, 0x31 | (cmd<<8) | (card->card_is_accessible<<16)); 
#endif

    // If it is one of our IOCTLs, get the data
    if ((cmd >= EV_IOC_FIRST) && (cmd <= EV_IOC_LAST))
    {
        is_ev_ioc = TRUE;
        local_arg = (ioctl_arg_t *)kmalloc(sizeof(ioctl_arg_t), GFP_KERNEL);

        // Start with the errnum field
        // Due to field alignment there could be more space between errnum and the union structure than just the size of the errnum field. 
        num_user_bytes = offsetof(ioctl_arg_t,ioctl_union);

        // Determine how much to transfer.
        switch (cmd)
        {
            case EV_IOC_GET_MODEL:
                num_user_bytes += sizeof(dev_info_t);
                break;
            case EV_IOC_GET_MSZKB:
            case EV_IOC_GET_SET_BEACON:
            case EV_IOC_GET_SET_PASSCODE:
            case EV_IOC_GET_SET_MAX_DMAS:
            case EV_IOC_GET_SET_MEMORY_WINDOW:
            case EV_IOC_GET_SET_ECC_SINGLE_BIT_ERROR:
            case EV_IOC_GET_SET_ECC_MULTI_BIT_ERROR:
            case EV_IOC_GET_SKIP_SECTORS:
            case EV_IOC_GET_SET_CAPTURE_STATS:
            case EV_IOC_GET_SET_MAX_DESCRIPTORS:
            case EV_IOC_CARD_READY:
            case EV_IOC_GET_SET_WRITE_ACCESS:
                num_user_bytes += sizeof(get_set_val_t);
                break;
            case EV_IOC_WRITE:
            case EV_IOC_READ:
                num_user_bytes += sizeof(ev_buf_t);
                break;
            case EV_IOC_GET_IOEVENTS:
                num_user_bytes += sizeof(ev_io_event_ioc_t);
                break;
            case EV_IOC_GET_VERSION:
                num_user_bytes += sizeof(ver_info_t);
                break;
            case EV_IOC_HW_ACCESS:
                num_user_bytes += sizeof(mem_addr_size_t);
                break;
            case EV_IOC_GET_PERF_STATS:
                num_user_bytes += sizeof(performance_stats_t);
                break;
            case EV_IOC_ECC_STATUS:
                num_user_bytes += sizeof(ecc_status_t);
                break;
            case EV_IOC_GET_DATA_LOGGER_HISTORY:
                num_user_bytes += sizeof(data_logger_stats_t);
                break;
            case EV_IOC_FORCE_SAVE:
            case EV_IOC_FORCE_RESTORE:
            case EV_IOC_CHIP_RESET:
            case EV_IOC_DBG_LOG_STATE:
            case EV_IOC_DBG_RESET_STATS:
            case EV_IOC_ECC_RESET:
            case EV_IOC_ARM_NV:
            case EV_IOC_DISARM_NV:
            default:
                // No added transfer - only errnum - we could use a scalar transfer using get_user/put_user but these are not time sensitive.
                break;
        }

        // Get the IOCTL structure
        if (copy_from_user(local_arg, (void *) arg, num_user_bytes))
        {
            EV_DEBUG_ALWAYS("copy_from_user FAILED, cmd=%d", cmd);
            kfree(local_arg);
            return -EFAULT;
        }
    }

    switch(cmd) {
    case HDIO_GETGEO:
        EV_DEBUG_ALWAYS("HDIO_GETGEO called");
#if defined(USING_BIO)
        netlist_blk_get_geo(i->i_bdev, &geo);
        if (copy_to_user((void __user *) arg, &geo, sizeof(geo)))
        {
            EV_DEBUG("copy_to_user FAILED");
            return -EFAULT;
        }
        return 0;
#else
        return -EFAULT;
#endif
    case EV_IOC_GET_VERSION:
    {
        ver_info_t *version_info;
        unsigned long val = 0x00;

        version_info = (ver_info_t *)&local_arg->ioctl_union.ver_info;

        version_info->fpga_rev = card->fpgarev;
        version_info->fpga_ver = card->fpgaver;
        version_info->fpga_configuration = card->fpga_configuration;
        version_info->fpga_board_code = card->fpga_board_code;
        version_info->fpga_build = card->fpga_build;
        version_info->driver_rev = DRIVER_REV;
        version_info->driver_ver = DRIVER_VER;

        val = readl(card->reg_remap + CVER_REG);
        version_info->rtl_version = (int)((val >> 24) & 0xff);
        version_info->rtl_sub_version = (int)((val >> 16) & 0xff);
        version_info->rtl_sub_sub_version = (int)((val >> 8) & 0xff);

        val = readl(card->reg_remap + FW_VER_REG);
        version_info->fw_version = (int)((val >> 24) & 0xff);
        version_info->fw_sub_version = (int)((val >> 16) & 0xff);
        version_info->fw_sub_sub_version = (int)((val >> 8) & 0xff);

        // Find out which FPGA image is being used
        val = readl(card->reg_remap + IFP_STATUS);
        if ((val & IFP_FPGA_IMAGE_MASK) ==  IFP_FPGA_IMAGE_APPLICATION)
        {
            version_info->current_fpga_image = (int)(IOCTL_FPGA_IMAGE_APPLICATION & 0xff);
        }
        else
        {
            version_info->current_fpga_image = (int)(IOCTL_FPGA_IMAGE_FACTORY & 0xff);
        }

        // Include the NULL terminator
        strncpy (version_info->extra_info, DRIVER_DATE, strlen(DRIVER_DATE)+1);
        local_arg->errnum = IOCTL_ERR_SUCCESS;
    }
        break;
    case EV_IOC_GET_MODEL:
    {
        dev_info_t *device_info;

        device_info = (dev_info_t *)&local_arg->ioctl_union.dev_info;
        device_info->dev_id = card->dev->device;
        device_info->ven_id = card->dev->vendor;
        device_info->mfg_info = MFG_NETLIST;
        device_info->total_cards_detected = num_cards;

        local_arg->errnum = IOCTL_ERR_SUCCESS;
    }
        break;
    case EV_IOC_GET_MSZKB:
    {
        int ioctl_val;

        ioctl_val = (int) (card->size_in_sectors / 2);
        local_arg->ioctl_union.get_set_val.is_read = TRUE;     // Force to a read value
        local_arg->ioctl_union.get_set_val.value = ioctl_val;
        local_arg->errnum = IOCTL_ERR_SUCCESS;
    }
        break;
    case EV_IOC_FORCE_SAVE:
    {
        unsigned long val = 0x00;

        local_arg->errnum = IOCTL_ERR_COMMAND_NOT_SUPPORTED; // Assume error until completed

        // TBD - make #defines for the NV states
        val = readl(card->reg_remap + STATE_REG);
        if (val == 0xA) // State
        {
            //val = readl(card->reg_remap + CONTROL_REG);
            writel(0x2, card->reg_remap + CONTROL_REG);

            EV_DEBUG_ALWAYS("EV_IOC_FORCE_SAVE called: 0x%x written to CONTROL_REG\n", (unsigned  char)(0x2));
            local_arg->errnum = IOCTL_ERR_SUCCESS;
        }
        else
        {
            local_arg->errnum = IOCTL_ERR_COMMAND_CANNOT_EXECUTE; // We are not in a state to do this.
        }
    }
        break;
    case EV_IOC_FORCE_RESTORE:
    {
        unsigned long val = 0x00;

        local_arg->errnum = IOCTL_ERR_COMMAND_NOT_SUPPORTED; // Assume error until completed

        // TBD - make #defines for the NV states
        val = readl(card->reg_remap + STATE_REG);
        if (val == 0x0) // State
        {
            val = readl(card->reg_remap + NV_FLAG_STATUS_REG);
            if (val == 0x3) // Flags
            {
                //val = readl(card->reg_remap + CONTROL_REG);
                writel(0x1, card->reg_remap + CONTROL_REG);
            }

            EV_DEBUG_ALWAYS("EV_IOC_FORCE_RESTORE called: 0x%x written to CONTROL_REG\n", (unsigned  char)(0x1));
            local_arg->errnum = IOCTL_ERR_SUCCESS;
        }
        else
        {
            local_arg->errnum = IOCTL_ERR_COMMAND_CANNOT_EXECUTE; // We are not in a state to do this.
        }
    }
        break;
    case EV_IOC_ARM_NV:
    {
        unsigned long temp_long;
        unsigned long val = 0x00;

        local_arg->errnum = IOCTL_ERR_COMMAND_NOT_SUPPORTED; // Assume error until completed

        val = readl(card->reg_remap + STATE_REG);
        if (val == NV_STATE_DISARMED) // State
        {
            temp_long = PG | BUE;
            writel(temp_long, card->reg_remap + CONTROL_REG);

            EV_DEBUG_ALWAYS("EV_IOC_ARM_NV called: 0x%lx written to CONTROL_REG\n", temp_long);
            local_arg->errnum = IOCTL_ERR_SUCCESS;
        }
        else
        {
            local_arg->errnum = IOCTL_ERR_COMMAND_CANNOT_EXECUTE; // We are not in a state to do this.
        }
    }
        break;
    case EV_IOC_DISARM_NV:
    {
        unsigned long temp_long;
        unsigned long val = 0x00;

        local_arg->errnum = IOCTL_ERR_COMMAND_NOT_SUPPORTED; // Assume error until completed

        val = readl(card->reg_remap + STATE_REG);
        if (val == NV_STATE_ARMED) // State
        {
            temp_long = PG;
            writel(temp_long, card->reg_remap + CONTROL_REG);

            EV_DEBUG_ALWAYS("EV_IOC_DISARM_NV called: 0x%lx written to CONTROL_REG\n", temp_long);
            local_arg->errnum = IOCTL_ERR_SUCCESS;
        }
        else
        {
            local_arg->errnum = IOCTL_ERR_COMMAND_CANNOT_EXECUTE; // We are not in a state to do this.
        }
    }
        break;
    case EV_IOC_WRITE:
    {
        ev_buf_t *ev_buffer;
#ifdef USING_SGIO
        struct EvIoReq req;
#endif
        // Get the pointer to the buffer containing the data
        // BIO / PIO - this buffer will contain the data to transfer
        // SGIO - pointer to the structure EvIOReq 
        ev_buffer = (ev_buf_t *)&local_arg->ioctl_union.ev_buf;
#ifdef USING_SGIO
        req = (struct EvIoReq)(*(struct EvIoReq *)ev_buffer->buf);
#endif
        
        switch(ev_buffer->type) 
        {
            /* If BIO or SGIO was specified - use the driver's native method regardless. */
            case BIO:
                // Not supported yet regardless of compile options
                local_arg->errnum = IOCTL_ERR_COMMAND_NOT_SUPPORTED;
                break;
            case SGIO:
#ifdef USING_SGIO
                err = ev_do_sgio_write(card, &req, ev_buffer->sync);
#else
                EV_DEBUG("Buffer access type is undefined");
                local_arg->errnum = IOCTL_ERR_WRITE_FAIL;
                err = -EFAULT;
#endif
                break;
            case PIO:
                err = pio_read_write(card, 1, (ev_buf_t *)ev_buffer);
                break;
        }

        local_arg->errnum = IOCTL_ERR_SUCCESS; // Mark success since we got the ioctl and are processing it.
    }
        break;
    case EV_IOC_READ:
    {
        ev_buf_t *ev_buffer;
#ifdef USING_SGIO
        struct EvIoReq req;
#endif

        // Get the pointer to the buffer the data to be copied
        // BIO / PIO - this buffer will contain the buffer to be transferred
        // SGIO - pointer to the structure EvIOReq
        ev_buffer = (ev_buf_t *)&local_arg->ioctl_union.ev_buf;
#ifdef USING_SGIO
        req = (struct EvIoReq)(*(struct EvIoReq *)ev_buffer->buf);
#endif

        switch(ev_buffer->type) 
        {
            case BIO:
                // Not supported yet regardless of compile options
                local_arg->errnum = IOCTL_ERR_COMMAND_NOT_SUPPORTED;
                break;
             case SGIO:
#ifdef USING_SGIO
                err = ev_do_sgio_read(card, &req, ev_buffer->sync);
#else
                EV_DEBUG("SGIO is not Supported");
                local_arg->errnum = IOCTL_ERR_READ_FAIL;
                err = -EFAULT;
#endif
                break;
            case PIO:
                err = pio_read_write(card, 0, (ev_buf_t *)ev_buffer);
                break;
        }

        local_arg->errnum = IOCTL_ERR_SUCCESS; /* Mark success since we got the ioctl and are processing it. */ 
    }
        break;
#ifdef USING_SGIO
    case EV_IOC_GET_IOEVENTS:
        err = ev_get_io_events(card, (unsigned long)local_arg);
        break;
#endif
    case EV_IOC_CHIP_RESET:
        EV_DEBUG_ALWAYS("EV: Soft Reset issued by application\n");
        card->stop_io = TRUE; // TBD - consider moving this to inside chip_reset
        netlist_abort_with_error(card);
        chip_reset(card);

        // Zero out the INT STATUS queue and index while we're in reset
        {
            int j = 0;

            card->current_address = 0;
            for (j=0;j<EV_NUM_INT_STATUS_ENTRIES;j++)    
            {
                writeq(0, card->int_status_address + j);
            }
        }

        card->stop_io = FALSE; // TBD - consider moving this to inside chip_reset
        break;
    case EV_IOC_GET_SET_BEACON:
        err = ev_set_beacon(card, (unsigned long)local_arg);
        break;
    case EV_IOC_GET_SET_PASSCODE:
        err = ev_set_passcode(card, (unsigned long)local_arg);
        break;
    case EV_IOC_HW_ACCESS:
        err = ev_hw_access(card, (unsigned long)local_arg);
        break;
    case EV_IOC_DBG_LOG_STATE:
        show_card_state(card);
        local_arg->errnum = IOCTL_ERR_SUCCESS;
        break;
    case EV_IOC_DBG_RESET_STATS:
#ifndef DISABLE_DESCRIPTOR_DEBUG
        largest_pcnt=0;
        largest_xfer_bytes=0LL;
        smallest_xfer_bytes=0xffffffffLL;
#endif
        reset_debug_stats(card);
        local_arg->errnum = IOCTL_ERR_SUCCESS;
        break;
    case EV_IOC_GET_PERF_STATS: // Not implemented yet, fall through
        err = ev_get_perf_stats(card, (unsigned long)local_arg);
        break;
    case EV_IOC_GET_SET_MAX_DMAS:
        err = ev_set_max_outstanding_dmas(card, (unsigned long)local_arg);
        break;
    case EV_IOC_GET_SET_MEMORY_WINDOW:
        if (card->num_windows > 1)
        {
            err = ev_set_memory_window(card, (unsigned long)local_arg);
        }
        else
        {
            EV_DEBUG("MEMORY_WINDOW is supported only for window based configurations (config #2)");

            local_arg->errnum = IOCTL_ERR_COMMAND_NOT_SUPPORTED;
            err = -EFAULT;
        }
        break;
    case EV_IOC_ECC_RESET:
        err = ev_ecc_reset(card, (unsigned long)local_arg);
        break;
    case EV_IOC_ECC_STATUS:
        err = ev_ecc_status(card, (unsigned long)local_arg);
        break;
    case EV_IOC_GET_SET_ECC_SINGLE_BIT_ERROR:
        err = ev_get_set_ecc_sbe_gen(card, (unsigned long)local_arg);
        break;
    case EV_IOC_GET_SET_ECC_MULTI_BIT_ERROR:
        err = ev_get_set_ecc_dbe_gen(card, (unsigned long)local_arg);
        break;
    case EV_IOC_GET_SKIP_SECTORS:
        err = ev_get_skip_sectors(card, (unsigned long)local_arg);
        break;
    case EV_IOC_GET_SET_CAPTURE_STATS:
        err = ev_get_set_capture_stats(card, (unsigned long)local_arg);
        break;
    case EV_IOC_GET_SET_MAX_DESCRIPTORS:
        err = ev_set_max_outstanding_descriptors(card, (unsigned long)local_arg);
        break;
    case EV_IOC_CARD_READY:
    {
        unsigned long val = 0x00;

        val = readl(card->reg_remap + STATE_REG);
        val &= 0x000000ff;

        local_arg->ioctl_union.get_set_val.value = (val == NV_STATE_ARMED);
        local_arg->errnum = IOCTL_ERR_SUCCESS;
    }
        break;
    case EV_IOC_GET_SET_WRITE_ACCESS:
        err = ev_get_set_write_access(card, (unsigned long)local_arg);
        break;
    case EV_IOC_GET_DATA_LOGGER_HISTORY:
        err = ev_get_data_log(card, (unsigned long)local_arg);
        break;
    default:
        EV_DEBUG("Unknown IOCTL = %d", cmd);
        err = -EINVAL;    /* unknown command */
        return err;
    }

    if (is_ev_ioc)
    {
        if (copy_to_user((void __user *) arg, local_arg, num_user_bytes))
        {
            EV_DEBUG_ALWAYS("copy_to_user FAILED");
            err = -EFAULT;
        }

        kfree(local_arg);
    }

    return err;
}



/**********************************************************************************
*
* pio_read_write - read and write the data sent through the IOCTL using PIO
*
* Arguments: card: pointer to the struct cardinfo.
*             read_write : read/write information
*             ev_buf : buffer containing the data
*
* Returns: None
*
**********************************************************************************/
static int pio_read_write(struct cardinfo *card, int read_write, ev_buf_t* ev_buf)
{
    unsigned short len;
    unsigned int i = 0;
    unsigned long dst_addr;
    mem_addr_size_t *addr_size;    
    
    EV_DEBUG("pio_read_write  read_write=%d\n", read_write);
    
    addr_size = (mem_addr_size_t *)ev_buf->buf;
    dst_addr = addr_size->addr;
    len = addr_size->size;
    
    //EV_DEBUG("addr : %x, len : %x\n", addr_size->addr, addr_size->size);

    i = 0;

    // TBD - why is a lock needed ? I don't think it is 
    while(len)
    {
        if (read_write == 1)
        {
            spin_lock_bh(&card->lock);
            writel(addr_size->val, card->mem_remap + dst_addr + i);
            spin_unlock_bh(&card->lock);
        }
        else
        {
            spin_lock_bh(&card->lock);
            *(unsigned int *)(addr_size->buf + i) = readl(card->mem_remap + dst_addr + i);
            spin_unlock_bh(&card->lock);
        }
        i += 4;
        len -= 4;        
    }    

    return 0;
}

#ifdef USING_SGIO
static inline int ev_do_sgio_write(struct cardinfo *card, struct EvIoReq *req, int sync)
{
    return netlist_ioc_process_io_req(card, req, MM_SGIO_WRITE, sync);
}

static int ev_do_sgio_read(struct cardinfo *card, struct EvIoReq *req, int sync)
{
    return netlist_ioc_process_io_req(card, req, MM_SGIO_READ, sync);
}

// call ev_activate until no longer successful or limit has been exceeded 
static int ev_activate_loop(struct cardinfo *card) 
{
    int rc;
    int i;

    for (i = 0; i < EV_ACTIVATE_LIMIT; i++) 
    {
        rc = ev_activate(card);
        if (rc != 0)
            return rc;
    }

    return 0;
}

/**********************************************************************************
*
* netlist_ioc_process_io_req - This function will be called from the netlist_ioctl to 
*                        process the SGIO 
*
* Arguments: card: pointer to the structure cardinfo
*             req: pointer to the EvIOReq (pointer to buffer in 
*                  kernel space copied from user space)
*             op: Read /Write Operation
*
* Return: Zero if Success
*          Non Zero value in case of error.
*
**********************************************************************************/
static int netlist_ioc_process_io_req(struct cardinfo *card, struct EvIoReq *req, mm_sgio_op op, int sync)
{
    int err = 0;
    int nio = 0;
    struct SgVec stack_vec[NUM_STACK_VECS];
    struct SgVec *user_vec = req->vec;
    struct sgio *io = NULL;
    uint32_t nvec = req->nvec;
    LIST_HEAD(new_sg);                // Declares and initializes the list
    struct mm_kiocb *iocb;


    if (unlikely(req->nvec == 0)) 
    {
        EV_DEBUG_ALWAYS("process_io_req: no vec\n");
        return -EINVAL;
    }

    spin_lock_bh(&card->lock);
#if defined(DEBUG_STATS)
    {
        card->stats_dbg.ios_rcvd++; // Stats
        if ((card->stats_dbg.ios_rcvd-card->stats_dbg.ios_completed) > card->stats_dbg.ios_max_outstanding)
        {
            card->stats_dbg.ios_max_outstanding = (card->stats_dbg.ios_rcvd-card->stats_dbg.ios_completed); // Stats 
        }
        dbg_capture(card, 0x21); // Number of IOCTL IO requests made by an application
    }
#endif
    if (card->sgio_cnt > max_queued_sgio) 
    {
        spin_unlock_bh(&card->lock);
        EV_DEBUG("return EAGAIN\n");
        return -EAGAIN;
    }
    spin_unlock_bh(&card->lock);

    iocb = kmem_cache_alloc(card->mm_kiocb_cachep, GFP_KERNEL);
    ev_chr_init_kiocb(iocb, req->nvec, req->cookie);

    /* We copy a bunch of SgVec from user at a time */
    while (nvec > 0) 
    {
        uint32_t n;
        int ii;
        struct SgVec *vec;

        n = min(nvec, (uint32_t)NUM_STACK_VECS);
        if (copy_from_user(stack_vec, user_vec, sizeof(struct SgVec)*n)) 
        {
            err = -EFAULT;
            EV_DEBUG("copy_from_user FAILED");
            goto err_out;
        }

        for (ii = 0, vec = stack_vec; ii < n; ii++, vec++) 
        {
            io = kmem_cache_alloc(card->mm_sgio_cachep, GFP_KERNEL);
            ev_chr_init_sgio(io, op, iocb, vec, 0, sync);

            if (mm_do_zero_copy(vec->length)) 
            {
                err = mm_map_user_buf(io, (uint64_t)vec->ram_base, vec->length, op == MM_SGIO_READ);
                if (err) 
                {
                    goto err_out;
                }
            } 
            else 
            {
                io->kbuf_bytes = vec->length;
                if (op == MM_SGIO_WRITE) 
                {
                    if (copy_from_user(io->kbuf, vec->ram_base, vec->length)) 
                    {
                        err = -EFAULT;
                        EV_DEBUG("copy_from_user FAILED");
                        goto err_out;
                    }
                }
            }

            list_add_tail(&io->list, &new_sg);
            nio++;
            /* Set to NULL so we don't double free it on error */
            io = NULL;
        }
        nvec -= n;
        user_vec += n;
    }

    spin_lock_bh(&card->lock);
    list_add_tail(&iocb->list, &card->pending_iocb);
    card->sgio_cnt += nio;
    list_splice_tail(&new_sg, &card->sgio);

    ev_activate_loop(card);

    spin_unlock_bh(&card->lock);
    return 0;

err_out:
    if (io)
        kmem_cache_free(card->mm_sgio_cachep, io);
    if (iocb)
        kmem_cache_free(card->mm_kiocb_cachep, iocb);

    while (!list_empty(&new_sg)) 
    {
        io = list_first_entry(&new_sg, struct sgio, list);
        list_del(&io->list);
        mm_unmap_user_buf(io);
        EV_DEBUG("err free %p", iocb);
        kmem_cache_free(card->mm_sgio_cachep, io);
    }
    return err;
}

static int mm_iocb_completion(struct mm_kiocb *iocb, struct cardinfo *card)
{
    int err = 0;
    struct sgio *io;

    while (!list_empty(&iocb->compld)) {
        io = list_first_entry(&iocb->compld, struct sgio, list);
        list_del(&io->list);
        /*
         * If err is encountered during copyout, to avoid of creating
         * inconsistent state in driver, we just free all the sgio
         * without copying (application will ignore the data that we
         * already copied)
         */
        if (err == 0) {
            if (mm_do_zero_copy(mm_sgio_len(io))) {
                mm_unmap_user_buf(io);
            } else if (copy_to_user(mm_sgio_base(io), io->kbuf, mm_sgio_len(io))) {
                err = -EFAULT;
            }
        }
        kmem_cache_free(card->mm_sgio_cachep, io);
    }
    return err;
}

static int ev_get_io_events(struct cardinfo *card, unsigned long arg)
{
    LIST_HEAD(compl_list);
    wait_queue_t wait;
    struct ev_io_event_ioc_s ioc; // Temporary local copy
    int err = 0;
    int ii;
    ioctl_arg_t *temp_arg;

    temp_arg = (ioctl_arg_t *)arg;
    ioc = temp_arg->ioctl_union.ev_io_event_ioc;

    spin_lock_bh(&card->lock);

    if (list_empty(&card->compld_iocb)) 
    {
        init_waitqueue_entry(&wait, current);
        add_wait_queue_exclusive(&card->event_waiters, &wait);
        for (;;) 
        {
            set_current_state(TASK_INTERRUPTIBLE);
            if (signal_pending(current)) 
            {
                err = -EINTR;
                break;
            }
            if (!list_empty(&card->compld_iocb))
                break;
            spin_unlock_bh(&card->lock);
            schedule_timeout(MAX_SCHEDULE_TIMEOUT);
            spin_lock_bh(&card->lock);
        }
        remove_wait_queue(&card->event_waiters, &wait);
        set_current_state(TASK_RUNNING);
    }

    if (err) 
    {
        spin_unlock_bh(&card->lock);
        EV_DEBUG_ALWAYS("ev_get_io_events: ERROR: err return %d", err);
        return err;
    }

    list_splice_init(&card->compld_iocb, &compl_list);
    spin_unlock_bh(&card->lock);

    ii = 0;
    while (!list_empty(&compl_list) && (ii < ioc.count)) 
    {
        struct ev_io_event_s e;
        struct mm_kiocb *iocb;
        iocb = list_first_entry(&compl_list, struct mm_kiocb, list);
        list_del(&iocb->list);
        e.cookie = iocb->uptr;
        e.status = mm_iocb_completion(iocb, card);
        ioc.events[ii] = e;
        ii++;
        kmem_cache_free(card->mm_kiocb_cachep, iocb);
    }

    //
    // check whether we have consumed all the events. If not,
    // put back on the head of completion list.
    //

    if (!list_empty(&compl_list)) 
    {
        EV_DEBUG_ALWAYS("put back event");
        spin_lock_bh(&card->lock);
        list_splice(&compl_list, &card->compld_iocb);
        spin_unlock_bh(&card->lock);
    }

    if (err == 0) 
    {
        ioc.count = ii;
        temp_arg->ioctl_union.ev_io_event_ioc = ioc;
    }

    return 0;
}

static inline int mm_do_zero_copy(uint64_t len)
{
    return len > MM_ZERO_COPY_THRESHOLD;
}

static int mm_map_user_buf(struct sgio *io, uint64_t uaddr, unsigned long len,    int write_to_vm)
{
    int ii;
    struct page **pages = io->pages;
    int nr_pages;
    int ret, offset;
    uint64_t dev_addr = io->vec.dev_addr;
    unsigned long end = (uaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
    unsigned long start = uaddr >> PAGE_SHIFT;

    nr_pages = end - start;
    if (uaddr & 7) 
    {
        EV_DEBUG_ALWAYS("address is not dma-aligned: uaddr=%p len=%lu\n",(char *)uaddr, len);
        return -EINVAL;
    }

    if (nr_pages == 0) 
    {
        EV_DEBUG_ALWAYS("No page len %lu\n", len);
        return -EINVAL;
    } 
    else
    { 
        if (nr_pages > MM_MAX_SGVEC_PAGES) 
        {
            EV_DEBUG_ALWAYS("Too many pages: requested %d maximum %d len %lu\n",
                            nr_pages, MM_MAX_SGVEC_PAGES, len);
            return -EOVERFLOW;
        }
    }

    ret = get_user_pages_fast(uaddr, nr_pages, write_to_vm, pages);
    if (ret < nr_pages) 
    {
        for (ii = 0; ii < ret; ii++) 
        {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
            page_cache_release(pages[ii]);
#else
            put_page(pages[ii]);
#endif

        }

        EV_DEBUG_ALWAYS("get_user_pages_fast FAILED");
        return -EFAULT;
    }

    offset = uaddr & ~PAGE_MASK;
    for (ii = 0; ii < nr_pages; ii++) 
    {
        struct sgio_page_vec *pv;
        unsigned int bytes = PAGE_SIZE - offset;

        if (len <= 0)
            break;

        if (bytes > len)
            bytes = len;

        pv = &io->page_vec[io->pvcnt];
        pv->pv_page = pages[ii];
        pv->pv_len = bytes;
        pv->pv_offset = offset;
        pv->pv_devaddr = dev_addr;

        dev_addr += bytes;
        io->pvcnt++;

        len -= bytes;
        offset = 0;
    }
    return 0;
}

static int mm_unmap_user_buf(struct sgio *io)
{
    int ii;
    struct sgio_page_vec *pv = &io->page_vec[0];

    for (ii = 0; ii < io->pvcnt; ii++, pv++) {
        if (mm_sgio_rw(io) == MM_SGIO_READ)
            set_page_dirty_lock(pv->pv_page);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
        page_cache_release(pv->pv_page);
#else
        put_page(pv->pv_page);
#endif
    }
    return 0;
}

static inline int ev_chr_init_kiocb(struct mm_kiocb *iocb, uint32_t nvec, void *cookie)
{
    iocb->nvec = nvec;
    iocb->uptr = cookie;
    iocb->nadded = 0;
    iocb->ncompld = 0;
    INIT_LIST_HEAD(&iocb->compld);
    return 0;
}

static inline int ev_chr_init_sgio(struct sgio *io, mm_sgio_op op, struct mm_kiocb *iocb, struct SgVec *vec, int kernel_page, int sync)
{
    io->kiocb = iocb;
    io->op = op;
    io->vec = *vec;
    io->pvcnt = 0;
    io->compl_pvidx = 0;
    io->add_pvidx = 0;
    io->add_compl = 0;
    io->kernel_page = kernel_page;
    io->kbuf_bytes = 0;
    io->deleted = 0;
    io->sync = sync;
    return 0;
}

static int sgl_map_user_pages(const unsigned int max_pages, unsigned long uaddr, size_t count, int rw, struct page ***mapped_pages, int *page_offset)
{
    unsigned long end = (uaddr + count + PAGE_SIZE - 1) >> PAGE_SHIFT;
    unsigned long start = uaddr >> PAGE_SHIFT;
    const int nr_pages = end - start;
    int res;
    int j;
    struct page **pages;

    /* User attempted Overflow! */
    if ((uaddr + count) < uaddr)
        return -EINVAL;

    /* Too big */
    if (nr_pages > max_pages)
        return -ENOMEM;

    /* Hmm? */
    if (count == 0)
        return 0;

    if ((pages = kmalloc(max_pages * sizeof(*pages), GFP_KERNEL)) == NULL)
        return -ENOMEM;

    /* Try to fault in all of the necessary pages */
    down_read(&current->mm->mmap_sem);
    /* rw==READ means read from drive, write into memory area */

    // Pin those user space pages down.
    res = get_user_pages_fast(uaddr, nr_pages, rw == READ, pages);

    up_read(&current->mm->mmap_sem);

    /* Errors and no page mapped should return here */
    if (res < nr_pages)
        goto out_unmap;

    *mapped_pages = pages;
    *page_offset = (uaddr & ~PAGE_MASK);

    EV_DEBUG2("get_user_pages=%d nr_pages=%d uaddr=%lx page_offset=%x", res, nr_pages, uaddr, *page_offset);

    return nr_pages;

 out_unmap:
    if (res > 0) 
    {
        for (j=0; j < res; j++)
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
            page_cache_release(pages[j]);
#else
            put_page(pages[j]);
#endif

        res = 0;
    }

    kfree(pages);
    return res;
}

static void sgl_unmap_user_pages(const unsigned int num_pages, struct page ***mapped_pages)
{
    int j;
    struct page **pages = *mapped_pages;


    for (j=0; j < num_pages; j++)
    {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
        page_cache_release(pages[j]);
#else
        put_page(pages[j]);
#endif
    }

    kfree(pages);
}
#endif

static int chip_reset(struct cardinfo *card)
{
    int retVal = 0;
    unsigned int control_value = 0;
    int i;

    EV_DEBUG_ALWAYS("%s%d: chip_reset: CONTROL_REG Register set to 0x%08x\n", PCI_DRIVER_NAME, card->card_number, 0); 
    control_value = readl(card->reg_remap + EV_FPGA_CONTROL_REG);
    EV_DEBUG_ALWAYS("READ CONTROL REG = 0x%08X\n", control_value); 
    writel(control_value | EV_BL_RST, card->reg_remap + EV_FPGA_CONTROL_REG);
    EV_DEBUG_ALWAYS("WRITE CONTROL REG = 0x%08X - plus delay 100mS\n", control_value | EV_BL_RST); 
    mdelay(100);
    writel(control_value, card->reg_remap + EV_FPGA_CONTROL_REG);
    EV_DEBUG_ALWAYS("WRITE CONTROL REG = 0x%08X - plus delay 100mS\n", control_value); 
    mdelay(100);

    // A delay may be needed here to ensure that outstanding operations will not generate an interrupt. 

    // We are not able to read registers (they return FFs) at this point due to the PCIe core transmit logic. The PCIe core
    // receive logic is active and we can write to registers. The only registers that are affected by the soft reset are
    // REQ_ADDRL (0x000C), REQ_LEN (0x0010) and REQ_ADDRH (0x0024)
    writel((unsigned int)card->phys_int_status_address, card->reg_remap + EV_FPGA_INT_STAT_REG_L);
    writel((unsigned int)(card->phys_int_status_address >> 32), card->reg_remap + EV_FPGA_INT_STAT_REG_H);

    // Zero out the INT STATUS queue and index while we're in reset
    card->current_address = 0;
    for (i=0;i<EV_NUM_INT_STATUS_ENTRIES;i++)    
    {
        writeq(0, card->int_status_address + i);
    }

    return(retVal);
}

static int initialize_ev_request(ev_request_t *ev_req)
{
    ev_req->cnt = 0;
    ev_req->headcnt = 0;
    ev_req->io_type = IO_NONE;
    ev_req->io_context = NULL;
    ev_req->io_status = IO_STATUS_GOOD;
    ev_req->ready = 1;
#if defined(USING_BIO)
    ev_req->bio = NULL;
    ev_req->biotail = &ev_req->bio;
#endif

#if defined(USING_SGIO)
    INIT_LIST_HEAD(&ev_req->sgio);
    ev_req->current_sgio = NULL;
    ev_req->mapped_pages = NULL;  // Be careful to free these first.
#endif
    ev_req->sync = TRUE; 
    ev_req->done_buffer = 0;
    ev_req->check = 0;
    ev_req->nRequestID = 0;

    return 0;
}
static int initialize_ev_request_pool(struct cardinfo* card)
{
    int i;

    card->nEnqueued = 0;
    card->nIssued = 0;
    card->nProcessed = 0;
    card->nCompleted = 0;

    for (i = 0; i < MAX_EV_REQUESTS; i++)
    {
        card->ev_request_pool[i].desc_unaligned = pci_alloc_consistent(card->dev, 
                    (MAX_DESC_PER_DMA * sizeof(ev_dma_desc_t))+DESCRIPTOR_LIST_ALIGNMENT, 
                        (dma_addr_t *)&card->ev_request_pool[i].page_dma);

        card->ev_request_pool[i].desc = (struct ev_dma_desc*)((unsigned long long)card->ev_request_pool[i].desc_unaligned & ~(DESCRIPTOR_LIST_ALIGNMENT-1));

        if (card->ev_request_pool[i].desc == NULL)
        {
            EV_DEBUG_ALWAYS("%s%d: alloc failed\n", PCI_DRIVER_NAME, card->card_number);
        }
       
        initialize_ev_request(&card->ev_request_pool[i]);
    }

    return 0;
}

/**********************************************************************************
*
* netlist_pci_probe - This function is called by the PCI core of Linux kernel.
*
* Arguments: dev: A pointer to the struct pci_dev
*             id: A pointer to the struct pci_device_id
*
* Returns: Zero if Success
*           Non Zero value in case of error.
*
**********************************************************************************/

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
static int __devinit netlist_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
#else
static int netlist_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
#endif
{
    int ret=0;
    int iVal;
    struct cardinfo *card = &cards[num_cards];
    unsigned int mem_bar;
    unsigned int reg_bar;
    irqreturn_t irqstat;
    int i;
    unsigned int l;
    unsigned short reg16 = 0;
    unsigned short vendor_id = 0;
    unsigned short device_id = 0;
    unsigned int temp_dword = 0;
    uint32_t data_reg;
    int pos;

    // Make sure that the card is set as a bus master. A "pci_disable_device" from a previous unload will
    // clear this bit (PCI SPACE CONTROL, bit 2).
    pci_read_config_dword(dev, PCI_COMMAND, &l);
    l |= PCI_COMMAND_MASTER;
    pci_write_config_dword(dev, PCI_COMMAND, l);

    card->card_number = num_cards; // Set the card_number early.
    num_cards++; // Increment the global and do not use it again in this function.
    EV_DEBUG_ALWAYS("netlist_pci_probe, card number %d", card->card_number);

    i = pci_find_capability(dev, PCI_CAP_ID_EXP);
    pci_read_config_word(dev, i + PCI_EXP_LNKSTA, &reg16);

    card->num_negotiated_lanes = ((reg16 & LINK_STATUS_NEGOTIATED_WIDTH_MASK) >> LINK_STATUS_NEGOTIATED_WIDTH_SHIFT);
    card->link_speed = (reg16 & LINK_STATUS_LINK_SPEED_MASK);

    card->num_expected_lanes = lanes_expected;

    // We are making the assumption that all cards are in slots of equal number of lanes which may not be the case.
    // Otherwise we would need to specify per card individually.
    // If less we will consider triggering a link reset mechanism, but we will need to save the BAR registers and 
    // restore them afterwards.
    if (card->num_negotiated_lanes < card->num_expected_lanes)
    {
        EV_DEBUG_ALWAYS("WARNING: Negotiated %d PCIe lanes, expected %d PCIe lanes", card->num_negotiated_lanes, lanes_expected);
    }

    switch(card->link_speed)
    {
        case 1:
            EV_DEBUG_ALWAYS("Negotiated Link Speed is 2.5 GT/S, Negotiated %d PCIe lanes", card->num_negotiated_lanes); 
            break;
        case 2:
            EV_DEBUG_ALWAYS("Negotiated Link Speed is 5.0 GT/S, Negotiated %d PCIe lanes", card->num_negotiated_lanes); 
            break;
        case 3:
            EV_DEBUG_ALWAYS("Negotiated Link Speed is 8.0 GT/S, Negotiated %d PCIe lanes", card->num_negotiated_lanes); 
            break;
        default:
            EV_DEBUG_ALWAYS("WARNING: Unknown Link Speed, value is 0x%x, Negotiated %d PCIe lanes", card->link_speed, card->num_negotiated_lanes); 
            break;
    }

    card->capture_stats = capture_stats;
    
    // Parameters to set capture history for customer API
    card->data_logger_elapsed_ns = 0;
    card->data_logger.head = 0;
    card->data_logger.tail = 0;
    card->data_logger.sample_time = data_logger_sample_time_secs;
    card->data_logger.sample_count = data_logger_sample_length;
    card->data_logger.wraparound_index = 3 * card->data_logger.sample_count; // We are taking 3 measurements each time.
    if (card->data_logger.wraparound_index >= DATA_LOGGER_BUFFER_SIZE)
    {
        card->data_logger.wraparound_index = DATA_LOGGER_BUFFER_SIZE;
    }

    for (i=0;i<DATA_LOGGER_BUFFER_SIZE;i++)
    {
        card->data_logger.data_log_buffer[i] = 0;
    }
    
#if defined(USING_SGIO)
    sprintf(card->str1,"mm_sgio_cache%d",card->card_number);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
    card->mm_sgio_cachep = kmem_cache_create(card->str1, sizeof(struct sgio), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);
#else
    card->mm_sgio_cachep = kmem_cache_create(card->str1, sizeof(struct sgio), 0, SLAB_HWCACHE_ALIGN, NULL);
#endif
    if (card->mm_sgio_cachep == NULL) 
    {
        EV_DEBUG_ALWAYS("Failed to create sgio cache\n");
        goto out;
    }

    sprintf(card->str2,"mm_kiocb_cache%d",card->card_number);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
    card->mm_kiocb_cachep = kmem_cache_create(card->str2,
                                               sizeof(struct mm_kiocb),
                                               0, SLAB_HWCACHE_ALIGN,
                                               NULL, NULL);
#else
    card->mm_kiocb_cachep = kmem_cache_create(card->str2,
                                               sizeof(struct mm_kiocb),
                                               0, SLAB_HWCACHE_ALIGN,
                                               NULL);
#endif
    if (card->mm_kiocb_cachep == NULL) 
    {
        EV_DEBUG_ALWAYS("Failed to create kiocb cache\n");
        goto out;
    }

    // Default to DMA mode, allow users to change to PIO using IOCTL
    card->mmap_mode = MMAP_MODE_DMA; // TBD - Set this using load time parameter
    card->mapped_pages = NULL;
    card->num_mapped_pages = 0;
    card->offset = 0;
    card->rw = READ;
    card->page_offset = 0;
#endif

    EV_DEBUG("pci_enable_device");
    if (pci_enable_device(dev) < 0)
    {
        EV_DEBUG("pci_enable_device FAILED");
        return -ENODEV;
    }
    
    pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0xF8);
    
    card->dev = dev;
    
    reg_bar = 0; // BAR0 - Register map
    mem_bar = 1; // BAR1 - PIO memory map (default for now, may be adjusted below)

    // Get PCI Resource for Register Access
    card->reg_base = pci_resource_start(dev, reg_bar);
    card->reg_len  = pci_resource_len(dev, reg_bar);

    if (pci_set_dma_mask(dev, DMA_BIT_MASK(64)) &&
        pci_set_dma_mask(dev, DMA_BIT_MASK(32)) )
    {
        EV_DEBUG_ALWAYS("%s%d: No Suitable DMA found\n", PCI_DRIVER_NAME, card->card_number);    
    }

    if (!request_mem_region(card->reg_base, card->reg_len, PCI_DRIVER_NAME)) 
    {
        EV_DEBUG_ALWAYS("%s%d: Unable to request BAR0 region\n", PCI_DRIVER_NAME, card->card_number);
        ret = -ENOMEM;

        card->reg_len = 0;
        goto out;
    }

    card->reg_remap = ioremap_nocache(card->reg_base, card->reg_len);
    if (!card->reg_remap) 
    {
        EV_DEBUG_ALWAYS("%s%d: Unable to remap BAR0 region\n", PCI_DRIVER_NAME, card->card_number);
        ret = -ENOMEM;

        goto out;
    }

    EV_DEBUG_ALWAYS("%s%d: Registers 0x%08lx -> 0x%p (0x%lx)\n", PCI_DRIVER_NAME, card->card_number,
                    card->reg_base, card->reg_remap, card->reg_len);

    EV_DEBUG("Allocate Memory for SGLs");

    
    /* Allocate Memory for SGLs */
    card->max_sgl = max_sgl;
    for (iVal = 0; iVal < card->max_sgl; iVal++)
    {
        card->ev_sgls[iVal].desc_unaligned = pci_alloc_consistent(card->dev, (MAX_DESC_PER_DMA*sizeof(ev_dma_desc_t))+DESCRIPTOR_LIST_ALIGNMENT,
                                                                    (dma_addr_t *)&card->ev_sgls[iVal].page_dma);

        card->ev_sgls[iVal].desc = (struct ev_dma_desc  *) ((unsigned long long)card->ev_sgls[iVal].desc_unaligned & ~(DESCRIPTOR_LIST_ALIGNMENT-1));

        if (card->ev_sgls[iVal].desc == NULL)
        {
            EV_DEBUG_ALWAYS("%s%d: alloc failed\n", PCI_DRIVER_NAME, card->card_number);
            ret = -ENOMEM;
            goto out;
        }
        reset_sgl(&card->ev_sgls[iVal]);
    }
    
    initialize_ev_request_pool(card);

    // Set up the wait queues to be used for IOCTL threads
    init_waitqueue_head(&card->wq_forced_save_restore_op);
    init_waitqueue_head(&card->wq_flash_erase_op);

    // These are set by the ISR during a change of status event.
    card->restore_in_progress = FALSE;
    card->save_in_progress = FALSE;
    card->erase_in_progress = FALSE;

    // Treat as a link down situation. The card has taken control of the memory and external accesses are
    // not available when this is FALSE.
    card->card_is_accessible = FALSE;  
    card->flash_data_is_valid = FALSE; // We do not know yet, assume it is not valid. Indicates the status of previous restore.
    card->restored_data_status = 0; // We do not know yet, assume it is not corrupted.
    card->ecc_is_enabled = FALSE; // This will get set at load-time and depends on FPGA version
    card->write_access = FALSE; // DEBUG ONLY: Allow write access even if DISARMED.

    // Meant for internal control only
    card->inaccessible_status_change_detected = FALSE;
    card->idle_state_has_been_detected = FALSE;
    
    // This is a scratch buffer, currently used to handle block IOs of size 0 that are sometimes received when
    // using certain kernel versions. We DMA dummy data into this buffer and then complete the IO as normal.
    // This is always compiled in so we can use it for any scratch needs.
    card->scratch_dma_buffer = (struct ev_dma_buffer *)pci_alloc_consistent(card->dev, sizeof(ev_dma_buffer), 
                                                      (dma_addr_t *)&card->scratch_dma_bus_addr);
    if (card->scratch_dma_bus_addr == (phys_addr_t)NULL)
    {
        EV_DEBUG_ALWAYS("%s%d: alloc failed\n", PCI_DRIVER_NAME, card->card_number);
        ret = -ENOMEM;
        goto out;
    }

    card->stop_io = FALSE;      // Allow IO's through
    card->empty_sgl = 0;
    card->processed_sgl = -1;  /* We have not processed any yet - the first increment will set this to 0 */
    card->active_sgl = -1;     /* We have not activated any yet */
    card->next_active_sgl = 0; // Tracks SGL index after the currently active index, active_sgl+1 mod buffer size
    card->process_req_sgl = 0;
    card->ooo_count = 0;        // Track out-of-order counts
    card->msi_interrupt_count = 0;
    card->ev_interrupt_count = 0;
    card->current_address = 0;
    card->max_dmas_to_hw = max_dmas;    // Maximum outstanding commands to the DMA engine 
    card->max_descriptors_to_hw = max_descriptors;    // Maximum outstanding descriptors to the DMA engine 
    card->max_sgl = max_sgl;            // Maximum outstanding commands in the SGL array 
    card->num_make_request = 0;
    card->num_start_io = 0;
    card->num_sgl_processed = 0;

    card->last_accessed_system_address = 0;
    card->fpga_program_complete = 0;
    card->timeout_completion = 0;

    card->auto_save = auto_save;
    card->auto_restore = auto_restore;
    card->auto_erase = auto_erase;
    card->passcode = 0;

    card->beacon = 0;
    card->beacon_state = 0;

#ifdef USING_SGIO
    INIT_LIST_HEAD(&card->sgio);
    INIT_LIST_HEAD(&card->pending_iocb);
    INIT_LIST_HEAD(&card->compld_iocb);
    init_waitqueue_head(&card->event_waiters);
#endif
    // OS Interface related
    init_MUTEX(&card->sem);

    // Initialize the tasklet function which will be scheduled in Interrupt function

    for (i=0;i<MAX_TASKLETS;i++)
    {
        card->tasklet_context[i].card = card;
        card->tasklet_context[i].tasklet_id = i;
    
#ifdef USE_WORK_QUEUES
        INIT_WORK(&card->tasklet_context[i].ws, workq_func);
#else
        tasklet_init(&card->tasklet[i], netlist_process_completions, (unsigned long)&(card->tasklet_context[i]));
#endif    
    }

#ifdef USE_WORK_QUEUES

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
    card->wq = create_workqueue("ev3_wq");     
#else
    card->wq = alloc_workqueue("ev3_wq", WQ_UNBOUND , 32);     // Use up to 32 CPUS
#endif

#endif


    spin_lock_init(&card->lock);
    spin_lock_init(&card->CompleteLock);
    spin_lock_init(&card->EnqueueLock);
    spin_lock_init(&card->IssueLock);
    spin_lock_init(&card->PostprocessingLock);

    card->producerStatus = 0;
    card->consumerStatus = 0;
    card->scheduledTurn = 0;

    card->nDebugLoop = 0;
    card->current_finalizer = 0;

    for (i = 0; i < NUM_EV_FINALIZERS; i++)
    {

        EV_DEBUG_ALWAYS("Finalizer %d is intialized\n", i);
        
        card->finalizer_data[i].card = card;
        init_waitqueue_head(&card->finalizer_data[i].ev_finalizer_waitQ);
        card->finalizer_data[i].id = i;
        card->finalizer_data[i].status = 0;
        card->finalizer_data[i].finalizer_handler =
            kthread_create_on_node(finalizer_fn, &card->finalizer_data[i], 0, "n_finalizer");
        if (card->finalizer_data[i].finalizer_handler)
        {
            EV_DEBUG_ALWAYS("Finalizer %d is created\n", i);
            wake_up_process(card->finalizer_data[i].finalizer_handler);
        }
    }
    // JK
    netlist_req_consumer = kthread_create_on_node(consumer_fn, card, 0, "n_consumer");
//    netlist_req_consumer = kthread_create(consumer_fn, card, "n_consumer");
//    kthread_bind(netlist_req_consumer, 20);
    if (netlist_req_consumer)
    {
        printk("EV3 req consumer created successfully\n");
        wake_up_process(netlist_req_consumer);
    }
    else {
        printk(KERN_INFO "Thread creation failed\n");
    } //----end
    
    netlist_req_producer = kthread_create_on_node(producer_fn, card, 0, "n_producer");
//    netlist_req_producer = kthread_create(producer_fn, card, "n_producer");
//    kthread_bind(netlist_req_producer, 22);
    if (netlist_req_producer)
    {
        printk("EV3 req producer  created successfully\n");
        wake_up_process(netlist_req_producer);
    }
    else {
        printk(KERN_INFO "Thread creation failed\n");
    } //----end
    
    


    // We need to enable access to the registers first to see of this is a driver reload or a warm boot. If this is
    // a case of warm boot, we need to issue an I2C command to reset the NVvault state machine to enable multiple
    // consecutive restores.

    // Enable Message Signaled Interrupt
    pci_enable_msi(card->dev);
    irqstat = request_irq(dev->irq, (typeof (no_action) *)ev_interrupt,    0, PCI_DRIVER_NAME, card);
    if (irqstat) 
    {
        EV_DEBUG_ALWAYS("%s%d: Unable to allocate IRQ\n", PCI_DRIVER_NAME, card->card_number);
        ret = -ENODEV;

        goto out;
    }

    card->irq = dev->irq;
    EV_DEBUG_ALWAYS("%s%d: IRQ %d\n", PCI_DRIVER_NAME, card->card_number, card->irq);

    pci_set_drvdata(dev, card);

    if (!polling_mode)
    {
        pci_read_config_dword(dev, 0x54, &card->msi_address_reg_lsb);
        pci_read_config_dword(dev, 0x58, &card->msi_address_reg_msb);
        pci_read_config_word(dev, 0x5c, &card->msi_data_reg);    

        EV_DEBUG_ALWAYS("MSI_ADDR_REG - %x : %x\n", card->msi_address_reg_msb, card->msi_address_reg_lsb);
        EV_DEBUG_ALWAYS("MSI_DATA_REG - %x\n", card->msi_data_reg);
    }

    card->dma_int_factor = dma_int_factor; // Use default, could be changed dynamically on a per card basis. 
    EV_DEBUG_ALWAYS("DMA INTERRUPT FACTOR - %d\n", card->dma_int_factor);
    card->last_num_start_io = 0; // Used in coalescing
    card->polling_timer_is_active = FALSE;

    // NOTE: We need to add one entry extra to this buffer because the card will write an AA55AA55 to the end location
    //         plus one after an interrupt. We need to have a place to put this value but otherwise this location is
    //         unused.
    card->int_status_address = pci_alloc_consistent(card->dev, 
                                                    (EV_NUM_INT_STATUS_ENTRIES+1)*sizeof(unsigned long long), 
                                                    &card->phys_int_status_address);
    EV_DEBUG_ALWAYS("inst_status_address=%p phys_int_status_address=%llx size=%lx\n", 
                    card->int_status_address,
                    card->phys_int_status_address, (EV_NUM_INT_STATUS_ENTRIES+1)*sizeof(unsigned long long));

    // This is for the older 3.23 compatibility
    // We cannot reset the chip in the versions without I2C access. The restore will happen and flash will get erased
    // during the chip_reset.
    // However there is a problem in that on a cold boot, the SRST bit is not set, therefore we cannot read the 
    // EV registers. It is a hack that should work as long as we make the assumption that older cards only will have 
    // the vendor Id of 1172

    pci_read_config_word(dev, 0x00, &vendor_id);
    pci_read_config_word(dev, 0x02, &device_id);
    EV_DEBUG_ALWAYS("Vendor Id = 0x%x Device Id = 0x%x\n", vendor_id, device_id);

    chip_reset(card);

    // Enable the polling timer only after the DMA STATUS BUFFER has been properly initialized
    // Use polling timer if enabled, only card 0 is used because hires timers do not carry
    // a user context. So card 0 will poll all the cards in the system.

    init_polling_timer(card, TRUE); //JK

#if 0 
    if ((polling_mode) && (card->card_number==0))
    {
        EV_DEBUG2("Enabling polling mode");
        init_polling_timer(card, TRUE);
    }
    else
    {
        if (card->dma_int_factor > 1)
        {
            EV_DEBUG_ALWAYS("DMA interrupt coalescing is enabled, value is %d\n", card->dma_int_factor);
            init_polling_timer(card, FALSE); // Slow timer
        }
    }
#endif


#ifdef TBD
    // If we have a cold boot situation, set restored_data_status to non-zero use bit 0
    // We are now able to access the card's registers.
    // Read the cookie in the SCRATCH register to see if this is a cold or warm boot case.
    scratch_value = readl(card->reg_remap + EV_TEST_REG);
    EV_DEBUG_ALWAYS("TEST_REG=%x\n", scratch_value);
    if (scratch_value != DDR_ACCESS_MAGIC_NUMBER)
    {
        // A valid restore will set this back to FALSE
        // Bits 5 and 7 are used for other restore
        card->restored_data_status = 1;  // Indicate that the DDR memory is not initialized
    }
#endif

    // Get FPGA Revision and Version Numbers - we will get the other information later, we need to be able to decide
    // whether or not to try to recover the 416-bytes of DDR training area based on FPGA version and NVDIMM version.

    temp_dword = readl(card->reg_remap + EV_FPGA_REV_NUM); // This contains all version info.
    temp_dword &= 0xffff; 
    card->fpgaver = (temp_dword>>8) & 0xff; // Major
    card->fpgarev = (temp_dword & 0xff); // Minor

    // TBD Remove later when max_descriptors workaround is not an issue and no throttle is needed. 
    card->enable_descriptor_throttle = TRUE;
    if (temp_dword >= 0x000A) 
    {
        card->enable_descriptor_throttle = FALSE;
    }
    EV_DEBUG_ALWAYS("%s%d: Descriptor based throttle is %s EV_FPGA_REV_NUM=0x%X\n", PCI_DRIVER_NAME, card->card_number, card->enable_descriptor_throttle?"ON":"OFF", temp_dword); 


    // Now the NVDIMM is in a known state, do the reset with the proper parameters.
    // NOTE: Registers will not be accessible while the CONTROL SRST bit is not set.
    EV_DEBUG_ALWAYS("%s%d: CONTROL_REG Register set to 0x%08x\n", PCI_DRIVER_NAME, card->card_number, 0); 
    writel(0, card->reg_remap + EV_FPGA_CONTROL_REG);
    // Zero out the INT STATUS queue and index while we're in reset
    card->current_address = 0;
    for (i=0;i<EV_NUM_INT_STATUS_ENTRIES;i++)    
    {
        writeq(0, card->int_status_address + i);
    }

    // Initialize the done buffer - this is the buffer that passes the IO context from ISR to tasklet in the order of coompletion
    card->done_head = 0;  // ISR adds completion context to the head and increments
    card->done_tail = 0;  // Tasklet processes completion context from the tail and increments

    for (i=0;i<DEFAULT_MAX_OUTSTANDING_DMAS;i++)    
    {
        card->done_buffer[i] = 0;
    }

#ifdef TBD
    control_value = EV_SOFT_RESET;

    // Here we decide whether or not to save the contents to the flash on a power fail.
    if (!card->auto_save)
    {
        control_value |= EV_AUTO_SAVE_DISABLE;
    }

    // Here we decide whether or not to restore the contents of the flash (if the contents are valid that is).
    if (!card->auto_restore)
    {
        control_value |= EV_AUTO_RESTORE_DISABLE;
    }

    card->skip_sectors = EV_DEFAULT_SKIP_SECTORS; 

    EV_DEBUG_ALWAYS("%s%d: CONTROL_REG Register set to 0x%08x\n", PCI_DRIVER_NAME, card->card_number, control_value); 
    writel(control_value, card->reg_remap + EV_FPGA_CONTROL_REG);
#endif

    // If the user has decided to force the value of skip_sectors to 0 or 1, then use this value regardless
    // of what the driver has previously determined.
    // The rule is, if the driver load-time parameter has not been forced set to a valid value of 0 or 1,
    // then the FPGA version information will take over.
    // Here we want to use skip_sectors the global load-time variable.

    if ((skip_sectors == 0) || (skip_sectors == 1))
    {
        card->skip_sectors = skip_sectors; // Get the driver load-time setting.
    }

    // Get the DDR Memory size
    switch(device_id)
    {
        case EV_DEVICE_ID_BAR32_WINDOW_32M_4GB:
            card->size_in_sectors = (EV_SIZE_4GB/ EV_HARDSECT) - card->skip_sectors;
            break;
        case EV_DEVICE_ID_BAR32_WINDOW_32M_16GB:
            card->size_in_sectors = (EV_SIZE_16GB/ EV_HARDSECT) - card->skip_sectors;
            break;
        case EV_DEVICE_ID_BAR32_WINDOW_32M_8GB: // Fall through default to 8GB if not found
        default:
            card->size_in_sectors = (EV_SIZE_8GB/ EV_HARDSECT) - card->skip_sectors;
            break;
    }

    EV_DEBUG("Size in sectors : 0x%lX", card->size_in_sectors);

    // At this point, but before we set the mem_bar, we need to determine if this FPGA is version 4.29 or later. If 4.29
    // or later, multiple types of FPGA configurations are supported. One of those types, configuration 3, 
    // which supports 64-bit BAR using a flat memory model moved BAR1 to BAR2/BAR3 in order to be able to support
    // 64-bit operation.

    // Set default values - override as needed
    card->fpga_board_code = EV3_BOARD_REV_A;
    card->fpga_configuration = EV1_FPGA_CONFIG_32BIT_BAR_WINDOWED;   // All current configurations are windowed 32-bit
    card->fpga_build = 0;

    // Default the window related parameters
    card->window_size = WINDOW_SIZE_CONFIG2;
    card->num_windows = EV_SIZE_16GB/WINDOW_SIZE_CONFIG2; // WINDOWED - TBD - set per device id use size_in_sectors*SECTOR_SIZE
    card->cur_window = 0;  // Default - we will adjust if this is a windowed configuration.

    // Check to see if this version is FPGA capable and ECC reporting via AER
    // This global is set at load-time and depends on FPGA version
    // BL = 0.16 was first to implement AER support
    if ((card->fpgaver > 0) || ((card->fpgaver == 0) && (card->fpgarev >= 0x16)))
    {
        card->ecc_is_enabled = enable_ecc; 
    }
    else
    {
        EV_DEBUG_ALWAYS("AER is not enabled FPGA version/revision = %x.%x \n", card->fpgaver, card->fpgarev);
    }

    if (card->ecc_is_enabled)
    {
        nl_ecc_reset(card);

        // This has nothing to do with disabling single bit error correction.
        // That is only bit and auto correction feature is not about correcting single bit
        // error but scheduling another RMW command to the corrupted address to re-write
        // the location with the corrected data.
        // All read data from memory will still go through ECC decoder and any single bit
        // error will be flipped. There is no point in providing customer with corrupted data, 
        // if we can correct it through ECC.
        // By default we enable auto correction.
        temp_dword = readl(card->reg_remap + EV_FPGA_ECC_CTRL);
        temp_dword |= ENABLE_AUTO_CORR;
        writel(temp_dword, card->reg_remap + EV_FPGA_ECC_CTRL);

        EV_DEBUG_ALWAYS("AER capabilities are enabled\n");
        // Enable error reporting in device control register (AER)
        pci_enable_pcie_error_reporting(dev);
        // PCIe extended capability registers
        pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
        pci_read_config_dword(dev,pos+PCI_ERR_CAP , &data_reg);
        data_reg |= (PCI_ERR_CAP_ECRC_GENE | PCI_ERR_CAP_ECRC_CHKE);
        pci_write_config_dword(dev,pos+PCI_ERR_CAP , data_reg);
        // Read the Advanced Error Reporting capabilities and control register
        pci_read_config_dword(dev,pos+PCI_ERR_CAP , &data_reg);
        EV_DEBUG("AER Capabilities and Control Register: Read data 0x%x from PCI REG offset 0x%x\n", data_reg, (pos + PCI_ERR_CAP));
        // Read UNCORR SEVERITY AND MASK registers
        pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_SEVER, &data_reg);
        EV_DEBUG("AER Capabilities: Uncorrectable severity register: Read data 0x%x from PCI REG offset 0x%x\n", data_reg, (pos + PCI_ERR_UNCOR_SEVER));
        pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_MASK , &data_reg);
        EV_DEBUG("AER Capabilities: Uncorrectable mask register: Read data 0x%x from PCI REG offset 0x%x\n", data_reg, (pos + PCI_ERR_UNCOR_MASK));
        // Read CORR MASK register
        pci_read_config_dword(dev, pos + PCI_ERR_COR_MASK , &data_reg);
        EV_DEBUG("AER Capabilities: Correctable mask register: Read data 0x%x from PCI REG offset 0x%x\n", data_reg, (pos + PCI_ERR_COR_MASK));
    }

    EV_DEBUG("card->fpgarev : %d , card->fpgaver : %d\n", card->fpgarev, card->fpgaver);
    EV_DEBUG_ALWAYS("EXPRESSvault%d: Device Id = %02X, Vendor Id = %02X, FPGA Version = %d.%d, Size %ld sectors\n",
                    card->card_number,
                    card->dev->device,
                    card->dev->vendor,
                    card->fpgaver,
                    card->fpgarev,
                    card->size_in_sectors);


    // Get PCI Resource for PIO Memory Access
    // TBD - Adjust for the SKIP area that is unusable. I need to determine if this needs to synchronize with the
    // block driver offset.

    card->mem_len  = pci_resource_len(dev, mem_bar);
    card->mem_base = pci_resource_start(dev, mem_bar);

    EV_DEBUG_ALWAYS("%s%d: mem_base=0x%lx mem_len=0x%lx\n", PCI_DRIVER_NAME, card->card_number, card->mem_base, card->mem_len);

    if (!request_mem_region(card->mem_base, card->mem_len, PCI_DRIVER_NAME)) 
    {
        EV_DEBUG_ALWAYS("%s%d: Unable to request memory region\n", PCI_DRIVER_NAME, card->card_number);
        ret = -ENOMEM;

        card->mem_len = 0;
        goto out;
    }

    card->mem_remap = ioremap_nocache(card->mem_base, card->mem_len);
    if (!card->mem_remap) 
    {
        EV_DEBUG_ALWAYS("%s%d: Unable to remap BAR1 region\n", PCI_DRIVER_NAME,    card->card_number);
        ret = -ENOMEM;

        goto out;
    }

    EV_DEBUG_ALWAYS("%s%d: Memory Window 0x%08lx -> 0x%p (0x%lx)\n", PCI_DRIVER_NAME, card->card_number,
                    card->mem_base, card->mem_remap, card->mem_len);

#if defined(USING_BIO)
    add_block_disk(card);
#endif

    // This 100mS delay is just to allow the OS to set up the IOCTL path 
    // so that scripts using evutil can access it immediately after
    // the driver is loaded.
    mdelay(100); 
    EV_DEBUG("Exiting netlist_pci_probe");
    return 0;

out:
    netlist_pci_remove_body(card);

    return ret;
}


static void netlist_abort_with_error(struct cardinfo *card)
{
    int aborted_io_count = 0;

    // First let's remove any active IO's that have not completed. 
    // They will be terminated with an error status. 

    EV_DEBUG_ALWAYS("On exit - Aborted IO count = %d", aborted_io_count);
    
    while ((card->num_sgl_processed < card->process_req_sgl) &&
           (aborted_io_count < DEFAULT_MAX_OUTSTANDING_DMAS))
    {
        netlist_abort_io(card, ABORT_CODE); 
        aborted_io_count++;
    }

}


/**********************************************************************************
*
* netlist_pci_remove - PCI core calls when the struct pci_dev is being removed from 
*                    the system, or when the PCI driver is being unloaded from the kernel.
*
* Arguments: dev: A pointer to the struct pci_dev
*
* Returns: None
*
**********************************************************************************/
static void netlist_pci_remove(struct pci_dev *dev)
{
    struct cardinfo *card = pci_get_drvdata(dev);

    EV_DEBUG_ALWAYS("netlist_pci_remove\n");
    netlist_pci_remove_body(card);
}

static int netlist_pci_remove_body(struct cardinfo *card)
{
    int iVal;
#ifndef USE_WORK_QUEUES
    int i;
#endif

    // The driver will not unload until all IOs have completed or timed out.
    // Attempts to unload the driver while IOs are outstanding will show 
    // a message saying "ERROR: module <name< is in use".

#if defined(USING_BIO)
    if (card->queue)
        blk_cleanup_queue(card->queue);

    if (card->disk) 
    {
        EV_DEBUG_ALWAYS("disk has deleted");
        del_gendisk(card->disk);
        put_disk(card->disk);
    }
#endif

    if (card->beacon)
    {
        // Turning the beacon off
        EV_DEBUG_ALWAYS("BEACON OFF");
        (void) del_timer_sync(&card->beacon_timer);
    }

    if (card->irq)
        free_irq(card->irq, card);

    pci_disable_msi(card->dev);

    pci_free_consistent(card->dev, (EV_NUM_INT_STATUS_ENTRIES+1)*sizeof(unsigned long long), card->int_status_address, card->phys_int_status_address);
    pci_free_consistent(card->dev, sizeof(ev_dma_buffer), card->scratch_dma_buffer, card->scratch_dma_bus_addr);

#ifdef USE_WORK_QUEUES
     destroy_workqueue(card->wq); 
#else
    for (i=0;i<MAX_TASKLETS;i++)
    {
        if (card->tasklet[i].func)
            tasklet_kill(&card->tasklet[i]);
    }
#endif



    for (iVal = 0; iVal < card->max_sgl; iVal++)
//    for (iVal = 0; iVal < 1; iVal++)
    {
        if (card->ev_sgls[iVal].desc)
            pci_free_consistent(card->dev, MAX_DESC_PER_DMA*sizeof(ev_dma_desc_t),
                    card->ev_sgls[iVal].desc_unaligned,
                    card->ev_sgls[iVal].page_dma);
    }
    
    for (iVal = 0; iVal < MAX_EV_REQUESTS; iVal++)
    {
        if (card->ev_request_pool[iVal].desc)
            pci_free_consistent(card->dev, MAX_DESC_PER_DMA*sizeof(ev_dma_desc_t),
                    card->ev_request_pool[iVal].desc_unaligned,
                    card->ev_request_pool[iVal].page_dma);
    }

    if (card->mem_remap)
    {
        EV_DEBUG("netlist_pci_remove - iounmap card=%p mem_remap=%p\n",card, card->mem_remap);
        iounmap((void *) card->mem_remap);
    }
    else
    {
        EV_DEBUG_ALWAYS("ERROR: netlist_pci_remove - iounmap card=%p mem_remap=%p\n",card, card->mem_remap);
    }

    if (card->mem_len)
    {
        EV_DEBUG("netlist_pci_remove - releasing card=%p mem_base=%lx mem_len=%lx\n",card, card->mem_base,card->mem_len);
        release_mem_region(card->mem_base, card->mem_len);
    }
    else
    {
        EV_DEBUG_ALWAYS("ERROR: netlist_pci_remove - releasing card=%p mem_base=%lx mem_len=%lx\n",card, card->mem_base,card->mem_len);
    }

    if (card->reg_remap)
    {
        EV_DEBUG("netlist_pci_remove - iounmap card=%p reg_remap=%p\n",card, card->reg_remap);
        iounmap((void *) card->reg_remap);
    }
    else
    {
        EV_DEBUG_ALWAYS("ERROR: netlist_pci_remove - iounmap card=%p reg_remap=%p\n",card, card->reg_remap);
    }

    if (card->reg_len)
    {
        EV_DEBUG("netlist_pci_remove - releasing card=%p mem_base=%lx reg_len=%lx\n",card, card->mem_base,card->reg_len);
        release_mem_region(card->reg_base, card->reg_len);
    }
    else
    {
        EV_DEBUG_ALWAYS("ERROR: netlist_pci_remove - releasing card=%p mem_base=%lx reg_len=%lx\n",card, card->mem_base,card->reg_len);
    }

    pci_disable_device(card->dev);
    EV_DEBUG_ALWAYS("netlist_pci_remove is complete\n");
    
    return 0;
}


/**********************************************************************************
*
* netlist_init - Performs all the initialization required for the device to function.
*
* Arguments: None
*
* Returns:  Zero if Success
*            Non Zero value in case of error.
*
**********************************************************************************/
static int __init netlist_init(void)
{
    int err=0;
    int i;

    EV_DEBUG_ALWAYS("\n" DRIVER_DESC " " DRIVER_VERSION " " DRIVER_DATE "\n");

    // Show which compile options were used.
#if defined(USING_BIO)
    EV_DEBUG_ALWAYS("Netlist Block device driver is enabled (bio=1)");
#endif

#if defined(USING_SGIO)
    EV_DEBUG_ALWAYS("Netlist Character device driver is enabled (sgio=1)");
#endif

#if defined(NO_STATS_DEBUG)
    EV_DEBUG_ALWAYS("Netlist device driver has debug capture disabled");
#else
    EV_DEBUG_ALWAYS("Netlist device driver has debug capture enabled");
#endif

#if defined(USE_WORK_QUEUES)
    if ((num_workqueues<1) || (num_workqueues>MAX_WORKQUEUES))
    {
        // Use recommended default if user picks a number which is out of range
        num_workqueues = DEFAULT_NUM_WORKQUEUES;
        EV_DEBUG_ALWAYS("WARNING: num_workqueues parameter is out of range, using default value, s/b 1..%d", MAX_WORKQUEUES);
    }

    EV_DEBUG_ALWAYS("Number of work queues = %d  max loop counter = %d", num_workqueues, max_loop_counter);
#else
    EV_DEBUG_ALWAYS("Using tasklet for completions");
#endif

    /*
     * Allocate the device array and initialize.
     */
    cards = kmalloc(max_devices*sizeof (struct cardinfo), GFP_KERNEL);
    if (cards == NULL) 
    {
        EV_DEBUG_ALWAYS("netlist_init: ERROR: Unable to allocate device structures[max_devices]\n");
        goto out;
    }

    for (i=0;i<max_devices;i++)
    {
        memset(&cards[i], 0, sizeof(struct cardinfo));
    }

#ifdef USING_SGIO
    err = c_major_nr = register_chrdev(0, CHARACTER_DRIVER_NAME, &netlist_chr_ops);
    if (c_major_nr < 0) 
    {
        EV_DEBUG_ALWAYS("netlist_init: Could not register char device\n");
        goto out;
    }
#endif
#ifdef USING_BIO
    // This should be done after probe from netlist_pci_driver executes since
    // as soon as 'add_disk' is called, the OS can start querying the 'disk'.
    EV_DEBUG2("ev_reg_blkdev:\n");
    err = b_major_nr = register_blkdev(0, BLOCK_DRIVER_NAME);
    if (b_major_nr < 0) 
    {
        EV_DEBUG_ALWAYS("netlist_init: Could not register block device\n");
        goto out;
    }
#endif

    EV_DEBUG2("after ev_reg_blkdev: block major=%d\n", b_major_nr);

    err = pci_register_driver(&netlist_pci_driver);
    if (err < 0)
    {
        EV_DEBUG_ALWAYS("netlist_init: PCI REG FAIL\n");
        goto out;
    }


    EV_DEBUG_ALWAYS("netlist_init: PCI REG PASS %d\n", err);
    EV_DEBUG_ALWAYS("netlist_init: desc_per_page = %d\n", MAX_DESC_PER_DMA);
    EV_DEBUG_ALWAYS("netlist_init: %s block major number = %d\n", BLOCK_DRIVER_NAME, b_major_nr);
    EV_DEBUG_ALWAYS("netlist_init: %s char major number = %d\n", CHARACTER_DRIVER_NAME, c_major_nr);

    return 0;

out:
    netlist_cleanup_body();
    return err;
}
/**********************************************************************************
*
* netlist_cleanup - Driver exit entry point. Unregisters the pci, block and character devices.
*
* Arguments: None
*
* Returns: None
*
**********************************************************************************/
static void __exit netlist_cleanup(void)
{
    EV_DEBUG_ALWAYS("netlist_cleanup\n");

    netlist_cleanup_body();
}

static int netlist_cleanup_body(void)
{
    int ret;
    int i;

    // Cancel polling timer if active.
    // Card 0 polls all other cards that are present.
    i=0;
   
    printk(KERN_INFO "try to cleanup producer/consumer\n"); 
    if (netlist_req_producer)
    {
        kthread_stop(netlist_req_producer);
        printk(KERN_INFO "after kthread_stop\n"); 
        cards[i].producerStatus = 1;
        wake_up_interruptible(&WaitQueue_Producer);
        printk("EV3 Producer Cleaning up\n");
    } // --- end
    
    if (netlist_req_consumer)
    {
        kthread_stop(netlist_req_consumer);
        cards[i].consumerStatus = 1;
        wake_up_interruptible(&WaitQueue_Consumer);
        printk("EV3 Consumer Cleaning up\n");
    } // --- end
   
    printk(KERN_INFO "completed cleanup producer/consumer\n"); 

    for(i = 0; i < NUM_EV_FINALIZERS; i++)
    {
        if (cards[0].finalizer_data[i].finalizer_handler)
        {
            kthread_stop(cards[0].finalizer_data[i].finalizer_handler);
            wake_up_interruptible(&cards[0].finalizer_data[i].ev_finalizer_waitQ);
            EV_DEBUG_ALWAYS("Finalizer %d cleaning up\n", i);
        }
    }
    
    if (cards[i].polling_timer_is_active)
    {
        ret = hrtimer_cancel( &(cards[i].polling_timer) );
        if (ret) 
        {
            EV_DEBUG_ALWAYS("The timer was still in use...\n");
        }
        else
        {
            cards[i].polling_timer_is_active = FALSE;
        }
    }
    

#ifdef USING_SGIO
    {
        int ii;
        struct mm_kiocb *iocb, *iocb_tmp;
        struct sgio *io, *io_tmp;

        for (ii = 0; ii < num_cards; ii++) 
        {
            list_for_each_entry_safe(iocb, iocb_tmp, &cards[ii].compld_iocb, list) 
            {
                list_for_each_entry_safe(io, io_tmp, &iocb->compld, list) 
                {
                    kmem_cache_free(cards[ii].mm_sgio_cachep, io);
                }
                kmem_cache_free(cards[ii].mm_kiocb_cachep, iocb);
            }

            if (cards[ii].mm_kiocb_cachep)
                kmem_cache_destroy(cards[ii].mm_kiocb_cachep);
            if (cards[ii].mm_sgio_cachep)
                kmem_cache_destroy(cards[ii].mm_sgio_cachep);
        }
    }
#endif

    if (b_major_nr > 0)
        unregister_blkdev(b_major_nr, BLOCK_DRIVER_NAME);
    if (c_major_nr > 0)
        unregister_chrdev(c_major_nr, CHARACTER_DRIVER_NAME);

    pci_unregister_driver(&netlist_pci_driver);
    EV_DEBUG_ALWAYS("netlist_cleanup freeing cards\n");
    kfree(cards);
    cards = NULL;
    EV_DEBUG_ALWAYS("netlist_cleanup is complete\n");

    return 0;
}


static void show_card_state(struct cardinfo *card)
{
    int i;
    unsigned long long idx=0;
    int print_count;

#if defined(USING_BIO)
    EV_PRINTK("Showing device state start\n");
    EV_PRINTK("current_bio=%p bio=%p &card->bio=%p biotail=%p\n", 
              card->currentbio, card->bio, &card->bio, card->biotail);
    EV_PRINTK("SGL info\n");
#endif

    for (i=0;i<card->max_sgl;i++)
    {
        show_sgl(card, i);
    }

#if defined(DEBUG_STATS_CORE)
    EV_PRINTK("Showing debug buffer stats_dbg_head=0x%llx stats_dbg_tail=0x%llx stats_dbg_valid_entries=%lld stats_dbg_cycles=%lld\n", 
              card->stats_dbg.dbg_head, card->stats_dbg.dbg_tail, card->stats_dbg.dbg_valid_entries, card->stats_dbg.dbg_cycles);

    idx = card->stats_dbg.dbg_tail;                              
    idx &= 0xfffffffffffffffc; /* Boundary of 4 */

    if (idx >= (DBG_BUF_SIZE-4))
    {
        // Back off on wraparound
        idx = (DBG_BUF_SIZE-4); 
    }

    // We will print on boundaries of 4 so we may go back up to 3 entries before the actual start.

    print_count = 0; // Limit amount of printing to messages file

    while ((print_count < (card->stats_dbg.dbg_valid_entries+4)) &&
           (print_count < DBG_MAX_PRINT_LINE))
    {
        EV_PRINTK(KERN_NOTICE "%016llX: [%016llX] [%016llX] [%016llX] [%016llX]\n", 
                idx,
                card->stats_dbg.dbg_buffer[idx],
                card->stats_dbg.dbg_buffer[idx+1],
                card->stats_dbg.dbg_buffer[idx+2],
                card->stats_dbg.dbg_buffer[idx+3]);

        idx+=4; // Entries printed per line
        print_count+=4; // Entries printed per line

        if (idx>=(DBG_BUF_SIZE-4))
        {
            idx=0;
        }
    }

    EV_PRINTK(KERN_NOTICE "EV: Event counts\n");
    for (idx=0; idx<DBG_MAX_EVENTS; idx++)
    {
        if (card->stats_dbg.dbg_histogram[idx]>0)
        {
            EV_PRINTK(KERN_NOTICE "Event %llX: Count=%lld\n", idx, card->stats_dbg.dbg_histogram[idx]);
        }
    }

    // No control functions
    EV_PRINTK(KERN_NOTICE "EV: Resettable statistics variables with no control functionality\n");
    EV_PRINTK(KERN_NOTICE "EV: ios_rcvd                 =%lld\n", card->stats_dbg.ios_rcvd);
    EV_PRINTK(KERN_NOTICE "EV: num_unplug_fn_called     =%lld\n", card->stats_dbg.num_unplug_fn_called);
    EV_PRINTK(KERN_NOTICE "EV: dmas_queued              =%lld\n", card->stats_dbg.dmas_queued);
    EV_PRINTK(KERN_NOTICE "EV: dmas_started             =%lld\n", card->stats_dbg.dmas_started);
    EV_PRINTK(KERN_NOTICE "EV: dmas_completed           =%lld\n", card->stats_dbg.dmas_completed);
    EV_PRINTK(KERN_NOTICE "EV: ios_completed            =%lld\n", card->stats_dbg.ios_completed);
    EV_PRINTK(KERN_NOTICE "EV: dmas_timed_out           =%lld\n", card->stats_dbg.dmas_timed_out);
    EV_PRINTK(KERN_NOTICE "EV: dmas_errored             =%lld\n", card->stats_dbg.dmas_errored);
    EV_PRINTK(KERN_NOTICE "EV: dma_completion_ints      =%lld\n", card->stats_dbg.dma_completion_ints);
    EV_PRINTK(KERN_NOTICE "EV: dmas_max_outstanding     =%lld\n", card->stats_dbg.dmas_max_outstanding);
    EV_PRINTK(KERN_NOTICE "EV: dmas_num_outstanding     =%lld\n", card->stats_dbg.dmas_num_outstanding);
    EV_PRINTK(KERN_NOTICE "EV: descriptors_max_outstanding      =%lld\n", card->stats_dbg.descriptors_max_outstanding);
    EV_PRINTK(KERN_NOTICE "EV: ios_max_outstanding      =%lld\n", card->stats_dbg.ios_max_outstanding);
    EV_PRINTK(KERN_NOTICE "EV: num_read_bios      =%lld\n", card->stats_dbg.num_read_bios);
    EV_PRINTK(KERN_NOTICE "EV: num_write_bios     =%lld\n", card->stats_dbg.num_write_bios);
    EV_PRINTK(KERN_NOTICE "EV: num_dbg_1          =%lld\n", card->stats_dbg.num_dbg_1);
    EV_PRINTK(KERN_NOTICE "EV: num_dbg_2          =%lld\n", card->stats_dbg.num_dbg_2);
    EV_PRINTK(KERN_NOTICE "EV: num_dbg_3          =%lld\n", card->stats_dbg.num_dbg_3);
    EV_PRINTK(KERN_NOTICE "EV: num_dbg_4          =%lld\n", card->stats_dbg.num_dbg_4);
    EV_PRINTK(KERN_NOTICE "EV: num_dbg_5          =%lld\n", card->stats_dbg.num_dbg_5);
    EV_PRINTK(KERN_NOTICE "EV: num_dbg_6          =%lld\n", card->stats_dbg.num_dbg_6);
    EV_PRINTK(KERN_NOTICE "EV: num_dbg_7          =%lld\n", card->stats_dbg.num_dbg_7);
#endif
    // These variable have control functions so they are not considered stats
    EV_PRINTK(KERN_NOTICE "EV: Control variables (non-resettable)\n");
    EV_PRINTK(KERN_NOTICE "EV: skip_sectors            =%d\n", card->skip_sectors);
    EV_PRINTK(KERN_NOTICE "EV: msi_interrupt_count     =%lld\n", card->msi_interrupt_count);
    EV_PRINTK(KERN_NOTICE "EV: ev_interrupt_count      =%lld\n", card->ev_interrupt_count);
    EV_PRINTK(KERN_NOTICE "EV: max_dmas_to_hw          =%d\n", card->max_dmas_to_hw);
    EV_PRINTK(KERN_NOTICE "EV: max_descriptors_to_hw   =%d\n", card->max_descriptors_to_hw);
    EV_PRINTK(KERN_NOTICE "EV: max_sgl                 =%d\n", card->max_sgl);
    EV_PRINTK(KERN_NOTICE "EV: ecc_is_enabled          =0x%x\n", card->ecc_is_enabled);
    EV_PRINTK(KERN_NOTICE "EV: passcode                =0x%x\n", card->passcode);
    EV_PRINTK(KERN_NOTICE "EV: auto_save               =%d\n", card->auto_save);
    EV_PRINTK(KERN_NOTICE "EV: auto_restore            =%d\n", card->auto_restore);
    EV_PRINTK(KERN_NOTICE "EV: auto_erase              =%d\n", card->auto_erase);
    EV_PRINTK(KERN_NOTICE "EV: save_in_progress        =%d\n", card->save_in_progress);
    EV_PRINTK(KERN_NOTICE "EV: restore_in_progress     =%d\n", card->restore_in_progress);
    EV_PRINTK(KERN_NOTICE "EV: erase_in_progress       =%d\n", card->erase_in_progress);
    EV_PRINTK(KERN_NOTICE "EV: card_is_accessible      =%d\n", card->card_is_accessible);
    EV_PRINTK(KERN_NOTICE "EV: flash_data_is_valid     =%d\n", card->flash_data_is_valid);
    EV_PRINTK(KERN_NOTICE "EV: restored_data_status    =%d\n", card->restored_data_status);
    EV_PRINTK(KERN_NOTICE "EV: inaccessible_status_change_detected    =%d\n", card->inaccessible_status_change_detected);
    EV_PRINTK(KERN_NOTICE "EV: idle_state_has_been_detected           =%d\n", card->idle_state_has_been_detected);
    EV_PRINTK(KERN_NOTICE "EV: num_make_request        =%lld\n", card->num_make_request);
    EV_PRINTK(KERN_NOTICE "EV: num_start_io            =%lld\n", card->num_start_io);
    EV_PRINTK(KERN_NOTICE "EV: last_num_start_io       =%lld\n", card->last_num_start_io); 
    EV_PRINTK(KERN_NOTICE "EV: process_req_sgl         =%lld\n", card->process_req_sgl);
    EV_PRINTK(KERN_NOTICE "EV: num_sgl_processed       =%lld\n", card->num_sgl_processed);
    EV_PRINTK(KERN_NOTICE "EV: active_sgl              =0x%x\n", card->active_sgl);
    EV_PRINTK(KERN_NOTICE "EV: next_active_sgl         =0x%x\n", card->next_active_sgl);
    EV_PRINTK(KERN_NOTICE "EV: processed_sgl           =0x%x\n", card->processed_sgl);
    EV_PRINTK(KERN_NOTICE "EV: empty_sgl               =0x%x\n", card->empty_sgl);
    EV_PRINTK(KERN_NOTICE "EV: active_descriptors      =0x%x\n", card->active_descriptors);

    EV_PRINTK(KERN_NOTICE "EV: interrupt status buffer (current index) current_address=%x\n", card->current_address);
    for (i=0;i<EV_NUM_INT_STATUS_ENTRIES;i++)
    {
#if defined(__powerpc__)
        // TBD uncomment this out when tested for PPC
        EV_PRINTK(KERN_NOTICE "EV: int_status_address[%x]=%llx\n", i, readq(card->int_status_address + i));
#else
        EV_PRINTK(KERN_NOTICE "EV: int_status_address[%x]=%lx\n", i, readq(card->int_status_address + i));
#endif
    }

#if defined(USING_BIO)
    EV_PRINTK(KERN_NOTICE "EV: users                   =%d\n", card->users);
#endif
    
    // Show the performance variables that are always compiled in.
    EV_PRINTK(KERN_NOTICE "EV: Performance Information\n");
    EV_PRINTK(KERN_NOTICE "EV: ios_completed           =%lld\n", card->stats_perf.ios_completed);
    EV_PRINTK(KERN_NOTICE "EV: start_time              =%lld\n", card->stats_perf.start_time);
    EV_PRINTK(KERN_NOTICE "EV: end_time                =%lld\n", card->stats_perf.end_time);
    EV_PRINTK(KERN_NOTICE "EV: bytes_transferred       =%lld\n", card->stats_perf.bytes_transferred);
    EV_PRINTK(KERN_NOTICE "EV: total_interrupts        =%lld\n", card->stats_perf.total_interrupts);
    EV_PRINTK(KERN_NOTICE "EV: completion_interrupts   =%lld\n", card->stats_perf.completion_interrupts);
    EV_PRINTK(KERN_NOTICE "EV: Showing device state end\n");

#ifndef DISABLE_DESCRIPTOR_DEBUG
    EV_PRINTK(KERN_NOTICE "EV: max descriptor index used is   =%d, smallest_xfer per desc=%lld largest xfer per desc=%lld\n", largest_pcnt, smallest_xfer_bytes,largest_xfer_bytes);
#endif


}

#if defined(DEBUG_STATS) && defined(USING_BIO)
// This routine is not used but can be called as needed for debugging purposes as needed.
// inline is used here only to avoid the compiler warning "Function is defined but not used".
static inline void show_bio(struct bio *bio, struct pci_dev *dev)
{
    int counter=0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
    int segno;
    struct bio_vec *bvec;
#else
    struct bvec_iter segno;
    struct bio_vec bvec;
#endif

    int len;
    int rw;
    dma_addr_t dma_handle;

    len = ev_bio_len(bio);
    rw = bio_data_dir(bio);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
    EV_PRINTK(KERN_NOTICE "EV: bio: %p cmd=%x len=%x bi_next=%p bi_flags=%lx bi_vcnt=%x bi_idx=%x bi_phys_segments=%x bi_size=%x\n", 
              bio, rw, len, bio->bi_next, bio->bi_flags, bio->bi_vcnt, bio->bi_idx, bio->bi_phys_segments, bio->bi_size); 
    EV_PRINTK(KERN_NOTICE "EV: bio: bi_sector=%ld num_sectors=%d\n", bio->bi_sector, bio_sectors(bio)); 
#else
    EV_PRINTK(KERN_NOTICE "EV: bio: %p cmd=%x len=%x bi_next=%p bi_flags=%x bi_vcnt=%x bi_idx=%x bi_phys_segments=%x bi_size=%x\n", 
              bio, rw, len, bio->bi_next, bio->bi_flags, bio->bi_vcnt, bio->bi_iter.bi_idx, bio->bi_phys_segments, bio->bi_iter.bi_size); 
    EV_PRINTK(KERN_NOTICE "EV: bio: bi_sector=%ld num_sectors=%d\n", bio->bi_iter.bi_sector, bio_sectors(bio)); 
#endif

    bio_for_each_segment(bvec, bio, segno) 
    {
        /* Do something with this segment */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0) // ARbitrary switch-over to dma_map 
        bvec = bio_iter_iovec(bio, segno);
        dma_handle = dma_map_page(&(dev->dev), bvec.bv_page, bvec.bv_offset, len, (rw != WRITE) ? DMA_FROM_DEVICE : DMA_TO_DEVICE);			  

        EV_PRINTK(KERN_NOTICE "EV: vector: segno=%x bv_page=%p bv_len=%x bv_offset=%x dma_handle=%llx\n", 
                  segno.bi_idx, bvec.bv_page, bvec.bv_len, bvec.bv_offset, dma_handle); 
        if (dma_mapping_error(&(dev->dev), dma_handle))
        {
            EV_DEBUG_ALWAYS("ERROR: dma mapping");
        }
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
        bvec = bio_iovec_idx(bio, segno);
        dma_handle = pci_map_page(dev, bvec->bv_page, bvec->bv_offset, len, (rw != WRITE) ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);

        EV_PRINTK(KERN_NOTICE "EV: vector: segno=%x bv_page=%p bv_len=%x bv_offset=%x dma_handle=%llx\n", 
                  segno, bvec->bv_page, bvec->bv_len, bvec->bv_offset, dma_handle); 
#else
	    bvec = bio_iter_iovec(bio, segno);
	    dma_handle = pci_map_page(dev, bvec.bv_page, bvec.bv_offset, len, (rw != WRITE) ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);			  

        EV_PRINTK(KERN_NOTICE "EV: vector: segno=%x bv_page=%p bv_len=%x bv_offset=%x dma_handle=%llx\n", 
                  segno.bi_idx, bvec.bv_page, bvec.bv_len, bvec.bv_offset, dma_handle); 
#endif
#endif

        counter++;
    }
}
#endif

static void show_descriptor(int index, struct ev_dma_desc *desc)
{
    unsigned long long ddr_mem_addr; 

    ddr_mem_addr = ((unsigned long long)desc->ddr_mem_addr_hi << 32) | (desc->ddr_mem_addr_lo); 

    if ((desc->data_dma_handle != 0) || (desc->transfer_size !=0) || (ddr_mem_addr != 0) )
    {
        EV_PRINTK(KERN_NOTICE "EV: Descriptor: index=0x%x data_dma_handle=%llx ddr_mem_addr=%llx transfer_size=0x%x\n", 
                  index, desc->data_dma_handle, ddr_mem_addr, desc->transfer_size);
    }
}
#if 1
static void show_sgl(struct cardinfo *card, int index)
{
    int j;

    EV_PRINTK(KERN_NOTICE "EV: SGL Index=0x%x ready=%d cnt=%d headcnt=%d io_type=%d io_status=%d sync=%d\n", 
              index, card->ev_sgls[index].ready, card->ev_sgls[index].cnt, card->ev_sgls[index].headcnt, 
              card->ev_sgls[index].io_type, card->ev_sgls[index].io_status, card->ev_sgls[index].sync);
    for (j=0;j<card->ev_sgls[index].cnt;j++)
    {
        show_descriptor(j, &card->ev_sgls[index].desc[j]);  
    }
}
#endif
// IOSTAT support

void __ev_iostat_start(struct bio *bio, unsigned long *start)
{
    struct gendisk *disk = bio->bi_bdev->bd_disk;
    const int rw = bio_data_dir(bio);
    int cpu = part_stat_lock();

    *start = jiffies;
    part_round_stats(cpu, &disk->part0);
    part_stat_inc(cpu, &disk->part0, ios[rw]);
    part_stat_add(cpu, &disk->part0, sectors[rw], bio_sectors(bio));
    part_inc_in_flight(&disk->part0, rw);
    part_stat_unlock();
}

//EXPORT_SYMBOL(__ev_iostat_start);
void ev_iostat_end(struct bio *bio, unsigned long start)
{
    struct gendisk *disk = bio->bi_bdev->bd_disk;
    unsigned long duration = jiffies - start;
    const int rw = bio_data_dir(bio);
    int cpu = part_stat_lock();

    part_stat_add(cpu, &disk->part0, ticks[rw], duration);
    part_round_stats(cpu, &disk->part0);
    part_dec_in_flight(&disk->part0, rw);
    part_stat_unlock();
}
//EXPORT_SYMBOL(ev_iostat_end);

module_init(netlist_init);
module_exit(netlist_cleanup);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("Dual BSD/GPL");
