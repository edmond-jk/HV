#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>

#include <linux/hdreg.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/errno.h>

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/hrtimer.h>

#include <linux/seq_file.h>
#include <linux/time.h>
#include <uapi/linux/stat.h>
#include <linux/delay.h>

#include "../drivers/hv_cmd.h"


#define REQ_SIZE                    8           // 4KB

#define HV_OFFSET                   0x80000000  // 1GB offset in # of sectors

#define TOTAL_TIME                  120          // seconds
#define REQ_OFFSET                  256			// number of blocks, where block size is 512 Byte 
#define REAL_REQ_SIZE               512 * REQ_OFFSET
#define WRITE_RATIO                 67 


#define HV_MMIO_SIZE                0x100000    // 1MB in Bytes
#define HV_MMLS_DRAM_SIZE           0x6000000   // 96MB
#define HV_BUFFER_SIZE              0x40000000  // 1GB
#define IO_BUFFER_SIZE              0x20000000  // 512MB

#define LOG_SIZE                    4096
#define TIME_DIFF                   7*60*60     // To synchronize the time difference between user & kernel
/*
 * 26GB = 16GB LRDIMM + 8GB HVDIMM + 2GB (reserved memory by the system)
 */
#if 0
#define LRDIMM_SIZE                 0x400000000 // 16GB
#define SYS_RESERVED_MEM_SIZE       0x80000000  // 2GB
#define HV_DRAM_SIZE                0x200000000 // 8GB
#define TOP_OF_SYS_MEM              LRDIMM_SIZE + SYS_RESERVED_MEM_SIZE + HV_DRAM_SIZE
#define FAKE_BUFF_SYS_PADDR         TOP_OF_SYS_MEM - HV_MMIO_SIZE - HV_MMLS_DRAM_SIZE - HV_BUFFER_SIZE  
#else
#define FAKE_BUFF_SYS_PADDR         0x200000000 // 8GB ....0x5C0000000 // 23GB
#endif

DECLARE_WAIT_QUEUE_HEAD(WQ_worker);
DECLARE_WAIT_QUEUE_HEAD(WQ_checker);

int data;

static struct task_struct *checker;
static struct task_struct *worker;
int worker_cond, checker_cond;

extern int bsm_write_command_emmc(unsigned int tag, unsigned int sector, unsigned int lba,
                unsigned char *buf, unsigned char *vbuf, unsigned char async, void *callback_func);

extern int bsm_read_command_emmc(unsigned int tag, unsigned int sector, unsigned int lba,
                unsigned char *buf, unsigned char *vbuf, unsigned char async, void *callback_func);

// Debug FS
static struct dentry *log_file;
static struct dentry *control_file;
static u32    control_key = 0;

char *log_buf[LOG_SIZE][100];
struct timeval  current_time;
struct tm   broken;
unsigned long   local_time;

/* Reserved memory to be used for HYBRIDIMM IO 
 */
void*   fake_read_buff_pa;
void*   fake_read_buff_va;
void*   fake_write_buff_pa;
void*   fake_write_buff_va; 

void*   tmp_FRB_PA;
void*   tmp_FRB_VA;

/*
 * Performance Measurement 
 */
unsigned int total_count, write_count;


static int show (struct seq_file *m, void *v)
{
    int i;
    
    for (i = 0; i < LOG_SIZE; i++)
    {
        seq_printf(m, log_buf[i]);
    }

    return 0;
}

static int open (struct inode *inode, struct file *file)
{
    return single_open(file, show, NULL);
}

static const struct file_operations fops = {
    .llseek = seq_lseek,
    .open = open,
    .owner = THIS_MODULE,
    .read = seq_read,
    .release = single_release,
};

static int worker_fn(void *data)
{ 
    phys_addr_t pmem_off =  HV_OFFSET;
    int result, write_enable;
    ktime_t startTime;
    s64 timeTaken_us;

    allow_signal(SIGKILL|SIGSTOP);

    
    while (1)
    {
        if (control_key != 0)
        {
            printk("HV_PERF module starts to generate I/O requests onto HVDIMM directly!!\n");

            break;
        }
        else
        {
            msleep(1000); //sleep for 1 second.
        }
    }

    write_enable = 0;
    startTime = ktime_get();  
    while (!kthread_should_stop()) // 1 sec
    { 
        total_count++;
       
        if (write_enable != 0) { 
            result = bsm_write_command_emmc(0, REQ_SIZE, (unsigned int)(pmem_off), tmp_FRB_PA, 
                    tmp_FRB_VA, 0, NULL); 
            
            write_count++; 
            
            tmp_FRB_PA += REAL_REQ_SIZE; 
            tmp_FRB_VA += REAL_REQ_SIZE; 
        
            if (tmp_FRB_PA >= fake_read_buff_pa + IO_BUFFER_SIZE)
            {
                tmp_FRB_PA = fake_read_buff_pa;
            } 
            
            if (tmp_FRB_VA >= fake_read_buff_va + IO_BUFFER_SIZE)
            {
                tmp_FRB_VA = fake_read_buff_va;
            }
        }
        else 
        { 
            result = bsm_read_command_emmc(0, REQ_SIZE, (unsigned int)(pmem_off), fake_write_buff_pa, 
                    fake_write_buff_va, 0, NULL);
        } 
        
        pmem_off += REQ_OFFSET;

        if (write_count * 100 > total_count * WRITE_RATIO)
        {
            write_enable = 0;
        }
        else
        {
            write_enable = 1;
        }

        if (result < 0) 
        { 
            printk("bsm_write_command_emmc error\n");
        } 
        
        timeTaken_us = ktime_us_delta(ktime_get(), startTime);

        if (timeTaken_us >= 1000000)
        {
            worker_cond = 0;
            checker_cond = 1;

            wake_up_interruptible(&WQ_checker);
            wait_event_interruptible(WQ_worker, worker_cond == 1);
           
            startTime = ktime_get();  
        
            if(signal_pending(current))
                break;
        }
    } 

    do_exit(0);

    return 0;
}


static int checker_fn(void *data)
{ 
    int log_index = 0;
    int runtime = 0;

    fake_read_buff_pa = (unsigned char*) FAKE_BUFF_SYS_PADDR;
    fake_read_buff_va = (unsigned char*) ioremap_cache(FAKE_BUFF_SYS_PADDR, IO_BUFFER_SIZE);
    fake_write_buff_pa = (unsigned char*) FAKE_BUFF_SYS_PADDR + IO_BUFFER_SIZE;
    fake_write_buff_va = (unsigned char*) ioremap_wc(FAKE_BUFF_SYS_PADDR + IO_BUFFER_SIZE, IO_BUFFER_SIZE);
    
    tmp_FRB_PA = fake_read_buff_pa;
    tmp_FRB_VA = fake_read_buff_va;

    allow_signal(SIGKILL|SIGSTOP);

    checker_cond = 0;
    wait_event_interruptible(WQ_checker, checker_cond == 1);
   
    write_count = 0; 
    total_count = 0;


    while (!kthread_should_stop())
    {
       // To make outputs 
        do_gettimeofday(&current_time);

       // local_time = (u32)(current_time.tv_sec - (sys_tz.tz_minuteswest * 60));
        local_time = (u32)(current_time.tv_sec - TIME_DIFF);
        time_to_tm(local_time, 0, &broken);
          
        sprintf(log_buf[log_index], "%d, %u\n", (3600*broken.tm_hour + 60*broken.tm_min + broken.tm_sec),  
                total_count >> 3);

//        printk("Hours:%u, Min:%u, Seconds:%u >> total count:%u\n", 
//               broken.tm_hour, broken.tm_min, broken.tm_sec, total_count);
        log_index++;
            

        total_count = 0;
        write_count = 0;

        runtime ++;

        if (runtime >= TOTAL_TIME)
        {
            worker_cond = 1;
            wake_up_interruptible(&WQ_worker);
            if (worker)
            {
                kthread_stop(worker);
                printk(KERN_INFO "worker kthread stopped\n");
            }

            break;
        } 
        else
        {
            worker_cond = 1;
            checker_cond = 0;

            wake_up_interruptible(&WQ_worker);
            wait_event_interruptible(WQ_checker, checker_cond == 1);
           
            if(signal_pending(current))
                break;
        }
    } 
    
    do_gettimeofday(&current_time);

    local_time = (u32)(current_time.tv_sec - TIME_DIFF);
    time_to_tm(local_time, 0, &broken); 
    
    sprintf(log_buf[log_index], "%d, %u\n", (3600*broken.tm_hour + 60*broken.tm_min + broken.tm_sec),  
                total_count >> 3);

    iounmap(fake_read_buff_va);
    iounmap(fake_write_buff_va);

    printk(KERN_INFO "Thread Stopping\n");
    do_exit(0);

    return 0;
}

int hvPerf_init(void)
{ 
    data = 1;

    printk (KERN_INFO "Creating thread\n");

    log_file = debugfs_create_file( "hvperf_log", S_IRUSR, NULL, NULL, &fops);
    control_file = debugfs_create_u32 ("start_HV_IO", 0666, NULL, &control_key);
    
    
    worker = kthread_create_on_node(&worker_fn, (void *) data, 0, "worker_thread");
    if (worker) {
        wake_up_process(worker);
    } 
    
    checker = kthread_create_on_node(&checker_fn, (void *) data, 0, "checker_thread");
    if (checker)
    {
        wake_up_process(checker);
    }


  
    return 0;
}

int hvPerf_exit(void)
{ 
    printk(KERN_INFO "Cleaning up\n");

    debugfs_remove(log_file);
    debugfs_remove(control_file);
/*
    checker_cond = 1;
    wake_up_interruptible(&WQ_checker);
    if(checker)
    {
        kthread_stop(checker);
        printk(KERN_INFO "checker kthread stopped\n");
    }
*/
    printk("hvPerf module exit\n");

    return 0;
}

module_init(hvPerf_init);
module_exit(hvPerf_exit);

MODULE_LICENSE("Dual BSD/GPL");

