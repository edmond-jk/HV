#ifndef _DRIVERS_EV_H // Include once
#define _DRIVERS_EV_H

#include <linux/version.h>

#if !defined(TRUE)
#define TRUE (1==1)
#define FALSE (!TRUE)
#endif

// Compile options - some of these are not set by the makefile command-line
#define DEBUG_STATS_CORE  // Defining this enables the core code that supports debugging - but does not use it.
#define DDR_ACCESS_MAGIC_NUMBER (0x12345678) // This value means that DDR memory was initialized.

// By default DEBUG_STATS is OFF, developer may compile out by "make DBG=1"
#if !defined(NO_STATS_DEBUG)
#define DEBUG_STATS  // Enable this when debugging non-performance related issues.  
#endif

#define EV_VENDOR_ID 0x1C1B    // Netlist
#define EV_DEVICE_ID_BAR32_WINDOW_32M_4GB 0x0004 // EXPRESSvault NVDIMM/DDR3 PCIe GEN3 x8 lane - For debug use only
#define EV_DEVICE_ID_BAR32_WINDOW_32M_8GB 0x0005 // EXPRESSvault NVDIMM/DDR3 PCIe GEN3 x8 lane - For debug use only
#define EV_DEVICE_ID_BAR32_WINDOW_32M_16GB 0x0006 // EXPRESSvault NVDIMM/DDR3 PCIe GEN3 x8 lane - For debug use only
#define EV_MINORS DISK_MAX_PARTS // Maximum allowed by the Linux kernel
//#define EV_MINORS 129 // Number of partitions allowed - 129 disk nodes with up to 128 partitions by default, can be change to up to 65 at driver load time. 

#define EV_MAXCARDS 16
#define EV_SHIFT 7  // max 2 to the power of 7 partitions on each card. This is 128 partitions. 

#define DEFAULT_ENABLE_POLLING (FALSE)   // TBD - Set to FALSE when interrupts are enabled. 
#define DEFAULT_POLLS_DELAY_NS (10000)   // Delay in nanoseconds between polls when in polled mode
#define DEFAULT_DMA_INTERRUPT_FACTOR (1) // Interrupt on every DMA by completion by default, allows coalescing
#define DEFAULT_DATA_LOGGER_SAMPLE_TIME_SECONDS (3600) // Capture data log once an hour by default
#define DEFAULT_DATA_LOGGER_SAMPLES (24) // Number of samples to capture 
#define DEFAULT_IO_STAT_SUPPORT (0) // Disabled by default
#ifdef USE_WORK_QUEUES
#define DEFAULT_LOOP_COUNTER 256  // For work queues do 32 completions maximum
#define DEFAULT_NUM_WORKQUEUES 2 // Number of work queues 32 will be maximum
#else
#define DEFAULT_LOOP_COUNTER 256 // For tasklets do 256 completions maximum
#endif

#define EV_NUM_INT_STATUS_ENTRIES 32

#define DEFAULT_MAX_OUTSTANDING_DMAS 64 // This can be changed at run-time - how many may be built ahead of delivery.
#define DEFAULT_MAX_DMAS_QUEUED_TO_HW MAX_DMAS_QUEUED_TO_HW // This can be changed at run-time - at higher values there are timeouts.
#define DEFAULT_MAX_DESCRIPTORS_QUEUED_TO_HW 800 // Used as as throttle for descriptors

#define DEFAULT_AUTO_SAVE           TRUE   // Default value of auto save if no load time parameter is passed
#define DEFAULT_AUTO_RESTORE        TRUE   // Default value of auto restore if no load time parameter is passed
#define DEFAULT_AUTO_ERASE          FALSE  // Default value of auto erase if no load time parameter is passed
#define DEFAULT_ENABLE_ECC          TRUE   // Default value of enable ecc if no load time parameter is passed
#define DEFAULT_NUM_EXPECTED_LANES  8      // Number of lanes that the driver expects to see on entry.
#define DEFAULT_CAPTURE_STATS       FALSE  // Disabled by default - do not capture statistics during runtime.


#define IRQ_TIMEOUT (1 * HZ)

// Device driver and hardware names
#define BLOCK_DRIVER_NAME "ev3mem"
#define CHARACTER_DRIVER_NAME "ev3map"
#define PCI_DRIVER_NAME "ev3pci"
#define CARD_NAME "EXPRESSvault"

// DMA engine restrictions
// Starting addresses on both host and card memory must start on a 64 byte boundary.
// The transfer size must be a multiple of 64 bytes.
// The Scatter-Gather List must not cross a 4K boundary.
// The maximum SGL size is 256 (each entry is 16 bytes * 256 = 4K maximum. 

#define MAX_EV_REQUESTS        256 
#define NUM_EV_FINALIZERS        2

// Internal IO status values
#define IO_STATUS_GOOD                  0
#define IO_STATUS_ECC_MULTIBIT_ERROR    1
#define IO_STATUS_TIMEOUT               2    

// Card alignment for DMA - This is subject to change 
#define EV_DMA_DESCRIPTOR_ALIGNMENT 16
#define EV_DMA_DATA_ALIGNMENT 64

#define ERASE_COMPL_TMOUT_VAL           15       // seconds
#define RESTORE_COMPL_TMOUT_VAL         60       // seconds
#define BACKUP_COMPL_TMOUT_VAL          60       // seconds
#define DMA_TIMEOUT_VAL                  3       // seconds - Needs to be smaller than the OS timeout. For Linux it is 30 seconds typical. Note PIO tends to slow down the DMA.
#define I2C_TIMEOUT_VAL                  1       // seconds TBD Remove

#define DESCRIPTOR_LIST_ALIGNMENT (4096) // All descriptors must fall with a 4K page. 
#define MAX_DESC_PER_DMA (256) //((PAGE_SIZE)/sizeof(struct ev_dma_desc))
#define MAX_TRANSFER_BYTES_PER_DESCRIPTOR (0xFFFF) // We have 16 bits - as defined by the DMA engine.
#define MAX_SECTORS_PER_IO (8*MAX_DESC_PER_DMA) // 8 sectors fit in a Linux page of 4K, times 256 descriptors supported by the hardware.

// Definitions for compatibility with different kernels

#ifndef module_param
// We only need to support "int" in the module_param
#define module_param(name, type, perm)    MODULE_PARM(name, "i")
#endif
#ifndef __user
#define __user
#endif

// Do not support WORK QUEUES for older kernels - the reason is that these are slower than using tasklet
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
#ifdef USE_WORK_QUEUES
#undef USE_WORK_QUEUES
#endif
#endif


#define MAX_TASKLETS    EV_NUM_INT_STATUS_ENTRIES 
#define MAX_WORKQUEUES  MAX_TASKLETS  // Must be less than or equal to MAX_TASKLETS

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
#define ev_bio_len(bio) (bio_iovec(bio)->bv_len) // This is the length of current vector only. TBD - check for NULL pointer
#else
#define ev_bio_len(bio) (bio_iovec(bio).bv_len) // This is the length of current vector only. TBD - check for NULL pointer
#endif

#define ev_bio_next(bio) ((bio)->bi_next)
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
#define ev_bio_uptodate(bio,f) ((f) ? 0 : clear_bit(BIO_UPTODATE, &(bio)->bi_flags))
#else
#define ev_bio_uptodate(bio,f) ( { if (f == 0) { bio->bi_error = -EIO; } })
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
#define ev_bio_endio(bio, status, size) bio_endio((bio), (size), (status))
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
#define ev_bio_endio(bio, status, size) bio_endio((bio), (status))
#else
#define ev_bio_endio(bio, status, size) bio_endio((bio))
#endif
#endif

typedef struct bio ev_bio;
// End of definitions for compatibility with different kernels

#ifdef USING_SGIO
// read/write operation type
typedef enum 
{
    MM_SGIO_READ,       /* read from ev */
    MM_SGIO_WRITE,      /* write to ev */
} mm_sgio_op;

#define mm_sgio_base(sg) (sg)->vec.ram_base
#define mm_sgio_offset(sg) (sg)->vec.dev_addr
#define mm_sgio_len(sg) (sg)->vec.length
#define mm_sgio_rw(sg) (sg)->op

/* maximum allowed pages in a iovec buffer */
#define MM_MAX_SGVEC_PAGES      256
#define MM_MAX_SGVEC_BUF_LEN    ((MM_MAX_SGVEC_PAGES-1) * PAGE_SIZE)
#define MM_ZERO_COPY_THRESHOLD  256

//JK

struct mm_kiocb 
{
    struct list_head list;
    void *uptr;       // saved user pointer
    uint32_t nvec;    // number of SgVec in IoReq
    uint32_t ncompld; // number of completed sgio in this kiocb
    uint32_t nadded;  // number of sgio added to DMA descriptor table
    struct list_head compld; // completed sgio requests
    unsigned long long start_time;
    unsigned long long end_time;
};

struct sgio_page_vec 
{
    struct page  *pv_page;
    unsigned int pv_len;
    unsigned int pv_offset;
    uint64_t pv_devaddr;
};

struct sgio 
{
    struct list_head list;
    
    struct list_head page_list[DEFAULT_MAX_OUTSTANDING_DMAS]; // list_head for adding to mm_pages
    struct mm_kiocb *kiocb; // owner kiocb
    mm_sgio_op op;
    struct SgVec vec;
    struct sgio_page_vec page_vec[MM_MAX_SGVEC_PAGES];
    unsigned int pvcnt;     // elements in above array are used */
    unsigned int add_pvidx; // added pages so far
    unsigned int compl_pvidx; // completed pages so far
    unsigned int add_compl;   // flag to indicate adding to DMA descriptor has completed */
    struct page  *pages[MM_MAX_SGVEC_PAGES];
    char kbuf[MM_ZERO_COPY_THRESHOLD];
    unsigned int kbuf_bytes;
    unsigned int kernel_page; // whether the buffer in pages are kernel memory
    unsigned int deleted;     // TBD: remove it. This is for debugging only
    int sync; // TRUE means SYNCHRONOUS, FALSE means ASYNCHRONOUS
};
#endif

typedef enum 
{
    IO_NONE,    // This is the initial value
    IO_BIO,     // The IO was added and is managed by "add_bio"
    IO_SGIO,    // The IO was added and is managed by "add_sgio"
    IO_MAPPED   // The IO was added and is managed by "add_mapped_io"
} io_type_t;

typedef struct ev_tasklet_data
{
#ifdef USE_WORK_QUEUES
    struct work_struct ws; // My work structure
#endif
    struct cardinfo *card;
    int num_dmas_to_complete;
    int tasklet_id;
} ev_tasklet_data_t;

typedef struct ev_request 
{
    int                 ready;
    dma_addr_t          page_dma;           // Bus address of descriptor array
    void                *desc_unaligned;    // Used to align SGLs on 4K boundaries
    struct ev_dma_desc  *desc;              // CPU address of the DMA descriptor array
    int                 cnt;                // descriptor transfer counts
    int                 headcnt;            // descriptor transfer counts
    io_type_t           io_type;            // How this IO was generated and is managed
    void                *io_context;        // Context of IO that set up this SGL
    volatile int        io_status;          // Status of the completed IO.    
    ev_bio              *bio;               // Used for BIO only:
    ev_bio              **biotail;          // Used for BIO only:
    int                 idx;                // Used for BIO only:
    struct list_head    sgio;               // Used for SGIO only: sgio queue
    struct sgio         *current_sgio;      // Used for SGIO only: current sgio
    struct page         **mapped_pages;     // Used for SGIO only: Free after completion
    int                 sync;               // TRUE means SYNCHRONOUS, FALSE means ASYNCHRONOUS
    unsigned long long done_buffer;
    int                 check;
  
    uint64_t            nRequestID;
} ev_request_t;

typedef struct ev_finalizer_data
{
    struct cardinfo* card;
    wait_queue_head_t   ev_finalizer_waitQ;
    struct task_struct* finalizer_handler;
    int id;
    int status;
} ev_finalizer_data_t;

struct cardinfo 
{
    int card_number;
    struct pci_dev *dev;
    struct timer_list dma_timer[DEFAULT_MAX_OUTSTANDING_DMAS];
    ktime_t polling_ktime; // This is a high resolution polling timer - counts in nanoseconds.
    struct hrtimer polling_timer; // This is a high resolution polling timer - counts in nanoseconds.
    int polling_timer_is_active;  // TRUE if polling timer is running in either fast or slow mode
    uint64_t last_num_start_io;   // Used in coalescing, snapshot of last timer's num_start_io value

    struct timer_list compl_timer;
    int dma_int_factor; // Used in interrupt coalescing
    unsigned char fpgarev;
    unsigned char fpgaver;
    unsigned char fpga_configuration;
    unsigned char fpga_board_code;
    unsigned char fpga_build;
    int irq;

    unsigned long mem_base; // Base of memory-mapped DDR area
    unsigned long mem_len;  // Length in bytes of the DDR memory-mapped area.
    unsigned char __iomem *mem_remap;

    unsigned long reg_base;
    unsigned long reg_len;
    unsigned char __iomem *reg_remap;
    unsigned long size_in_sectors;

    unsigned long long total_memory; // Total memory accessible on card
    unsigned int window_size;   // Size of memory-mapped window in bytes
    unsigned int num_windows;   // Total number of windows or banks
    unsigned int cur_window;    // Set to the current window

    unsigned int skip_sectors;  // FPGA versions prior to 4.31 had to reserve 512 bytes for DDR training.

#if defined(USING_BIO)
    int users; 
    ev_bio *bio;
    ev_bio *currentbio;
    ev_bio **biotail;
    int current_idx;
    sector_t current_sector; // Holds the sector from the OS plus any offsets due to HW restrictions
    struct request_queue *queue;
    struct gendisk    *disk;
#endif

#ifdef USING_SGIO
    struct list_head pending_iocb;      /* inprogress kiocb */
    struct list_head compld_iocb;       /* completed kiocb */
    struct list_head sgio;              /* sgio queue */
    unsigned int sgio_cnt;              /* number of items on sgio queue */
    wait_queue_head_t event_waiters;    /* wait queue of events */
    struct sgio *current_sgio;          /* sgio which is being added to DMA descriptor list */

    // Cache names
    char str1[20];  // 20 is the maximum size for the name including the trailing terminator.
    char str2[20];  // 20 is the maximum size for the name including the trailing terminator.
    struct kmem_cache *mm_sgio_cachep;
    struct kmem_cache *mm_kiocb_cachep;

    // Set by netlist_chr_aux/sgl_map_user_pages
    enum mmap_mode_type mmap_mode;    // Use DMA or PIO mode or (future - maybe we will do a dynamic mode)
    struct page **mapped_pages;
    int num_mapped_pages;
    loff_t offset; // Offset into the file is the same as offset into the card, (except for skip area - TBD).
    size_t count;
    int rw;
    int page_offset; // Offset of data into a system page
#endif

    // Common for all builds
    struct semaphore sem; // Related to OS interfacing routines only - mutual exclusion semaphore

#if 1
    // This is the array of scatter/gather lists that will be queued to the hardware.
    struct ev_sgl 
    {
        int                 ready;            
        dma_addr_t          page_dma;           // Bus address of descriptor array
        void                *desc_unaligned;    // Used to align SGLs on 4K boundaries
        struct ev_dma_desc  *desc;              // CPU address of the DMA descriptor array
        int                 cnt;                // descriptor transfer counts
        int                 headcnt;            // descriptor transfer counts
        io_type_t           io_type;            // How this IO was generated and is managed
        void                *io_context;        // Context of IO that set up this SGL
        volatile int        io_status;          // Status of the completed IO.    
        ev_bio              *bio;               // Used for BIO only:
        ev_bio              **biotail;          // Used for BIO only:
        int                 idx;                // Used for BIO only:
        struct list_head    sgio;               // Used for SGIO only: sgio queue
        struct sgio         *current_sgio;      // Used for SGIO only: current sgio
        struct page         **mapped_pages;     // Used for SGIO only: Free after completion
        int                 sync;               // TRUE means SYNCHRONOUS, FALSE means ASYNCHRONOUS
#ifdef COMPILE_IOSTAT
        unsigned long       start;              // Used by IOSTAT to ytack IO start if enabled
#endif
//   } ev_sgls[DEFAULT_MAX_OUTSTANDING_DMAS];
   } ev_sgls[1];
#endif
    ev_request_t ev_request_pool[MAX_EV_REQUESTS];
    uint16_t    nEnqueued;
    uint16_t    nIssued;
    uint16_t    nProcessed;
    uint16_t    nCompleted;
    
    spinlock_t  CompleteLock;
    spinlock_t  EnqueueLock;
    spinlock_t  IssueLock;
    spinlock_t  PostprocessingLock;

    ev_finalizer_data_t finalizer_data[NUM_EV_FINALIZERS];
    int current_finalizer;

    int    nInterrupts;

    uint16_t    nDebugLoop;

 //   uint64_t         nIssuedRequests;
 //   uint64_t         nProcessedRequests;
 //   uint64_t         nEnqueuedRequests;


    int done_head;  // ISR adds completion context to the head and increments
    int done_tail;  // Tasklet processes completion context from the tail and increments
    unsigned long long done_buffer[DEFAULT_MAX_OUTSTANDING_DMAS];
  
    int stop_io;                    // Used during error handling to stop incoming IO's. 
    int empty_sgl;
    int active_sgl;
    int next_active_sgl;            // Tracks active_sgl+1 mod buffer size
    int processed_sgl;
    unsigned long long ooo_count;

    // Obsolescent
    int enable_descriptor_throttle; // If FALSE, do not count descriptors or throttle using descriptor counts. 
    int active_descriptors;         // Tracks number of descriptors currently active at the hardware.    
    int max_descriptors_to_hw;      // Maximum number of descriptors to the hardware

    int max_dmas_to_hw;             // Maximum number of DMA requests to the hardware
    int max_sgl;                    // Maximum number of SGL entries

    // Wait queues used for IOCTL threads - these probably could be a single wait queue since these cannot happen 
    // at the same time.
    wait_queue_head_t wq_forced_save_restore_op;
    wait_queue_head_t wq_flash_erase_op;

#ifdef USE_WORK_QUEUES
    struct workqueue_struct *wq;
#endif    

    // These are status variables. 
    volatile int restore_in_progress;       // A restore was started
    volatile int save_in_progress;          // A save was started
    volatile int erase_in_progress;         // A flash erase was started
    volatile int card_is_accessible;        // False when DMA and DDR PIO operations are not available.
    volatile int flash_data_is_valid;       // This is valid if a restore is done. If TRUE, then the restore had valid data.
    volatile int restored_data_status;      // If this is set, then the DDR memory has some form of ts reflect STATUS_REG or bit 0 set if not inited.    
    // Internal use only
    volatile int inaccessible_status_change_detected;   // Internal: True means that the requested save/restore/erase has started.
    volatile int idle_state_has_been_detected;          // Internal: True means that the SM_IDLE has been generated.
    // I2C access related (of NVvault DIMM module registers).
    int ecc_is_enabled;                     // TRUE means that the card is ECC capable and AER has been enabled. Set at load time. 
    int auto_save;                  // Auto save on power fail
    int auto_restore;               // Auto restore on driver initialization
    int auto_erase;                 // Auto erase flash on after a successful restore 
    int beacon;                     // TRUE to blink all LEDs at a given rate
    struct timer_list beacon_timer;
    int beacon_state;               // State of the LED ON or OFF
    uint32_t passcode;              // Saved passcode - pass from application.
    int write_access;               // DEBUG ONLY: TRUE means allow write access to RAM even if in DISARMED state
    int dma_access;                 // If TRUE, driver will accept DMA, otherwise requests are terminated with some busy or error status. 

    uint32_t temp_status;
    uint32_t temp_prev_status;

    // TBD - This will not work in a wraparound situation - fix is needed. Fortunately it is a very (1M+ years) large number.
    uint64_t num_make_request;      // Counts successful make_request calls
    uint64_t num_start_io;          // Counts IOs started at the DMA engine
    uint64_t process_req_sgl;       // Used to schedule bottom-half tasklet
    uint64_t num_sgl_processed;     // Counts the number of IOs processed by the bottom-half tasklet
    uint64_t ev_interrupt_count;    // Number of interrupts received total
    uint64_t msi_interrupt_count;   // Number of MSI interrupts received total (non-polled real ints)
    uint32_t msi_address_reg_lsb;
    uint32_t msi_address_reg_msb;    
    uint16_t msi_data_reg;
    uint16_t current_address;

    struct tasklet_struct tasklet[MAX_TASKLETS];

    spinlock_t  lock;

    // Producer/Consumer Thread Status
    uint32_t producerStatus;
    uint32_t consumerStatus; 
    uint32_t scheduledTurn;

    // Debug
 //   uint32_t totalPollingCounts;
 //   uint32_t producerWakeupCounts;
 //   uint32_t consumerWakeupCounts;

    int         flags;
    uint64_t    dma_desc_addr;
    int         completion_time;
    uint64_t    *int_status_address;
    phys_addr_t phys_int_status_address;
    u64 last_accessed_system_address;
    unsigned char fpga_program_complete;
    int timeout_completion; // If 1, then an IOCTL has timed out.

    // Low level PCIe status information
    int num_expected_lanes;
    int num_negotiated_lanes;
    int link_speed;  // 1 = 2.5 Ghz

    // Stats section - These are not control variables, just informational, statistical and
    // debug data. The performance fields could be compiled out, however for the sake of 
    // performance measurements we will leave these always in place.
    int capture_stats; // If TRUE read runtime stats. 
    performance_stats_t stats_perf;

#if defined(DEBUG_STATS_CORE)
    debug_stats_t stats_dbg;
#endif
    // Scratch buffer for when the OS sends an IO of size zero.
    struct ev_dma_buffer *scratch_dma_buffer;
    phys_addr_t scratch_dma_bus_addr;

    // Capture log for customer API - load time parameters
    uint64_t data_logger_elapsed_ns; // Elapsed time
        
    data_logger_stats_t data_logger; // Captured data


    struct ev_tasklet_data tasklet_context[MAX_TASKLETS];  // This is being used for either TASKLETS or WORK QUEUES - for work queues we limit the number used to MAX_WORK_QUEUES

};

typedef struct ev_dma_desc 
{
#ifndef TEMP_TBD
    uint16_t transfer_size;
    uint8_t control_bits;
    uint8_t ddr_mem_addr_hi;      // DDR memory start addr
    uint32_t ddr_mem_addr_lo;     // DDR memory start addr
    uint64_t data_dma_handle;     // system memory start addr
#else
	// want (or something like this)
    uint64_t data_dma_handle;     // system memory start addr
    uint32_t ddr_mem_addr_lo;     // DDR memory start addr
    uint8_t ddr_mem_addr_hi;      // DDR memory start addr
    uint8_t control_bits;
    uint16_t transfer_size;
#endif
} ev_dma_desc_t __attribute__((aligned(EV_DMA_DESCRIPTOR_ALIGNMENT)));

typedef struct ev_dma_buffer 
{
    uint8_t data[EV_HARDSECT+EV_DMA_DESCRIPTOR_ALIGNMENT];  // A local DMA buffer of one sector in size
} ev_dma_buffer;

/* For Debug Prints uncomment the second one */
#define EV_DEBUG(...) 
//#define EV_DEBUG(fmt, ...) printk("%s: " fmt "\n", __FUNCTION__, ## __VA_ARGS__)

// TBD - take out again
#define EV_DEBUG2(...) 
//#define EV_DEBUG2(fmt, ...) printk("EV: %s: " fmt "\n", __FUNCTION__, ## __VA_ARGS__)

#if defined(DEBUG_STATS)
#define EV_DEBUG3(fmt, ...) printk("EV: %s: " fmt "\n", __FUNCTION__, ## __VA_ARGS__)
#else
#define EV_DEBUG3(...) 
#endif

//#define DEBUG_FAST_PATH
#if defined(DEBUG_FAST_PATH)
#define EV_DEBUG_FASTPATH(fmt, ...) printk("EV: %s: " fmt "\n", __FUNCTION__, ## __VA_ARGS__)
#else
#define EV_DEBUG_FASTPATH(...) 
#endif

// Never turn this one off - it is intended to be used for portability. It may be used for error cases and
// for any time an output to the log must be guaranteed.
#define EV_DEBUG_ALWAYS(fmt, ...) printk("EV: %s: " fmt "\n", __FUNCTION__, ## __VA_ARGS__)

// printk equivalent used for portability
#define EV_PRINTK(fmt, ...)  printk( fmt , ## __VA_ARGS__)


void __ev_iostat_start(struct bio *bio, unsigned long *start);
static inline bool ev_iostat_start(struct bio *bio, unsigned long *start)
{
    struct gendisk *disk = bio->bi_bdev->bd_disk;

    if (!blk_queue_io_stat(disk->queue))
            return false;

    __ev_iostat_start(bio, start);
    return true;
}
void ev_iostat_end(struct bio *bio, unsigned long start);

#endif // _DRIVERS_EV_H 
