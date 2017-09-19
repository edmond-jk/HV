/* Netlist Copyright 2011, All Rights Reserved */
#ifndef _EV_IOCTL_H
#define _EV_IOCTL_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define DRIVERS_EV_H // Parameter needed for the header file. 
#include "i2cRegs.h" // Get the common used for the NV3 Registers

#define EV_DEFAULT_SKIP_SECTORS 0 // Default value to original
#define EV_HARDSECT 512 // 512-byte hardware sectors
#define EV_IOC_MAGIC 0xB5
#define MAX_DMAS_QUEUED_TO_HW 31 // This is the supported maximum. 

#define EV_SIZE_4GB (((uint64_t)4*1024*1024*1024)) 
#define EV_SIZE_8GB (((uint64_t)8*1024*1024*1024)) 
#define EV_SIZE_16GB (((uint64_t)16*1024*1024*1024)) 

#define EV_IOC_GET_MODEL	                _IOWR(EV_IOC_MAGIC, 1, ioctl_arg_t)
#define EV_IOC_GET_MSZKB	                _IOWR(EV_IOC_MAGIC, 2, ioctl_arg_t)
#define EV_IOC_FORCE_SAVE   		        _IOWR(EV_IOC_MAGIC, 11, ioctl_arg_t)
#define EV_IOC_FORCE_RESTORE   		        _IOWR(EV_IOC_MAGIC, 13, ioctl_arg_t)
#define EV_IOC_WRITE                        _IOWR(EV_IOC_MAGIC, 17, ioctl_arg_t)
#define EV_IOC_READ                         _IOWR(EV_IOC_MAGIC, 18, ioctl_arg_t)
#define EV_IOC_GET_IOEVENTS                 _IOWR(EV_IOC_MAGIC, 21, ioctl_arg_t)
#define EV_IOC_CHIP_RESET	                _IOWR(EV_IOC_MAGIC, 22, ioctl_arg_t)
#define EV_IOC_GET_SET_BEACON	            _IOWR(EV_IOC_MAGIC, 23, ioctl_arg_t)
#define EV_IOC_GET_SET_PASSCODE		        _IOWR(EV_IOC_MAGIC, 24, ioctl_arg_t)
#define EV_IOC_GET_VERSION	                _IOWR(EV_IOC_MAGIC, 25, ioctl_arg_t)
#define EV_IOC_HW_ACCESS                    _IOWR(EV_IOC_MAGIC, 28, ioctl_arg_t)
#define EV_IOC_DBG_LOG_STATE                _IOWR(EV_IOC_MAGIC, 29, ioctl_arg_t)
#define EV_IOC_GET_PERF_STATS               _IOWR(EV_IOC_MAGIC, 30, ioctl_arg_t)
#define EV_IOC_DBG_RESET_STATS              _IOWR(EV_IOC_MAGIC, 31, ioctl_arg_t)
#define EV_IOC_GET_SET_MAX_DMAS             _IOWR(EV_IOC_MAGIC, 33, ioctl_arg_t)
#define EV_IOC_GET_SET_MEMORY_WINDOW        _IOWR(EV_IOC_MAGIC, 36, ioctl_arg_t)
#define EV_IOC_ECC_RESET                    _IOWR(EV_IOC_MAGIC, 37, ioctl_arg_t)
#define EV_IOC_ECC_STATUS                   _IOWR(EV_IOC_MAGIC, 38, ioctl_arg_t)
#define EV_IOC_GET_SET_ECC_SINGLE_BIT_ERROR _IOWR(EV_IOC_MAGIC, 39, ioctl_arg_t)
#define EV_IOC_GET_SET_ECC_MULTI_BIT_ERROR  _IOWR(EV_IOC_MAGIC, 40, ioctl_arg_t)
#define EV_IOC_GET_SKIP_SECTORS             _IOWR(EV_IOC_MAGIC, 41, ioctl_arg_t)
#define EV_IOC_GET_SET_CAPTURE_STATS        _IOWR(EV_IOC_MAGIC, 48, ioctl_arg_t)
#define EV_IOC_ARM_NV                       _IOWR(EV_IOC_MAGIC, 49, ioctl_arg_t)
#define EV_IOC_GET_SET_MAX_DESCRIPTORS      _IOWR(EV_IOC_MAGIC, 50, ioctl_arg_t)
#define EV_IOC_CARD_READY                   _IOWR(EV_IOC_MAGIC, 51, ioctl_arg_t)
#define EV_IOC_DISARM_NV                    _IOWR(EV_IOC_MAGIC, 52, ioctl_arg_t)
#define EV_IOC_GET_SET_WRITE_ACCESS         _IOWR(EV_IOC_MAGIC, 53, ioctl_arg_t)
#define EV_IOC_GET_DATA_LOGGER_HISTORY      _IOWR(EV_IOC_MAGIC, 54, ioctl_arg_t)

// Define the range of IOCTLS
#define EV_IOC_FIRST EV_IOC_GET_MODEL
#define EV_IOC_LAST EV_IOC_GET_DATA_LOGGER_HISTORY

enum error_list 
{
	IOCTL_ERR_SUCCESS = 0x00,
	IOCTL_ERR_GET_MODEL,
	IOCTL_ERR_GET_MSZKB,
	IOCTL_ERR_GET_FLASH_DATA_STATUS,
	IOCTL_ERR_GET_BACKUP_STATUS,
	IOCTL_ERR_GET_RESTORE_STATUS,
	IOCTL_ERR_GET_FLASH_ERASE_STATUS,
	IOCTL_ERR_GET_CAP_CHARGE_STATUS,
	IOCTL_ERR_SET_ERASE_FLASH_BIT,
	IOCTL_ERR_SET_ENABLE_AUTO_SAVE_BIT,
	IOCTL_ERR_SET_FORCE_RESTORE_BIT,
	IOCTL_ERR_GET_FPGA_STATUS,
	IOCTL_ERR_WRITE_FAIL,
	IOCTL_ERR_READ_FAIL,
	IOCTL_ERR_RESTORE_CORRUPTED,		// Data corrupted bit was set during restore.
	IOCTL_ERR_TIMEOUT,					// Generic timeout
	IOCTL_ERR_COMMAND_CANNOT_EXECUTE,	// State of card prohibits this command's use.
	IOCTL_ERR_COMMAND_NOT_SUPPORTED,
	IOCTL_ERR_FAIL,
    IOCTL_ERR_ERRNUM_UNINITIALIZED
};

enum mfg_list 
{
	MFG_NETLIST=2
};

typedef struct dev_info_s 
{
    __u16 ven_id;
    __u16 dev_id;
	__u8  mfg_info;
	__u8  total_cards_detected;
}dev_info_t;

#define MAX_EXTRA_INFO 256

// Field - current_fpga_image
enum fpga_image_types 
{
	IOCTL_FPGA_IMAGE_UNKNOWN = 0x00,
	IOCTL_FPGA_IMAGE_FACTORY,
	IOCTL_FPGA_IMAGE_APPLICATION,
};

typedef struct ver_info_s 
{
    __u8 driver_rev;
    __u8 driver_ver;
	__u8 fpga_rev;
	__u8 fpga_ver;
	__u8 fpga_configuration;			// Which configuration of FPGA is being used. 																		
	__u8 fpga_board_code;				// Board layout compatibility code - changes if FPGA pinout changes.
	__u8 fpga_build;  					// The intent is to use 00 for released versions, non-00 for experimental releases.
	__u8 rtl_version;				    // NV RTL version
	__u8 rtl_sub_version;			    // NV RTL sub version
	__u8 rtl_sub_sub_version;			// NV RTL sub sub version
	__u8 fw_version;				    // NV FW version
	__u8 fw_sub_version;			    // NV FW sub version
	__u8 fw_sub_sub_version;			// NV FW sub sub version
	__u8 current_fpga_image;			// 1 means factory image, 2 means application image, otherwise unknown.
	char extra_info[MAX_EXTRA_INFO];
}ver_info_t;

typedef struct ev_buf_s
{
    unsigned int len;           // NUmber of bytes to transfer
    unsigned short offset;      // Start location in "buf" 
    unsigned long dest_addr;    // Address on card DDR memory 
    char type;                  // Type of transfer SGIO=DMA via IOCTL or PIO for use with non-DMA requests
    char *buf;                  // Data buffer to transfer 
    int sync;                   // TRUE means synchronous, FALSE means asynchronous
} ev_buf_t;

enum transfer_type 
{
    PIO = 0x0,
    BIO,
    SGIO
};

typedef struct get_set_val_s
{
	__u8 is_read;		/* TRUE means get the value, FALSE means set the value */
	__u64 value;
} get_set_val_t;

enum space_type 
{
    SPACE_REG = 0x0,
	SPACE_PIO,
    SPACE_PCI,
    SPACE_I2C
};

enum mmap_mode_type
{
    MMAP_MODE_DMA = 0x0,
	MMAP_MODE_PIO,
	MMAP_MODE_INVALID	// Make this the last value always.
};

typedef struct mem_addr_size_s
{
	__u64 addr;			// Address or register offset
	__u64 size;			// This is the length
	__u64 val;			// Value for writes
	__u8 access_size;	// Size of each atomic access (1=readb/writeb, 2=readw/writew, 4=readl/writel)
	__u8 is_read;		// Non-zero means READ, otherwise is WRITE
	__u8 space;			// SPACE_REG, SPACE_PIO, SPACE_PCI
	char *buf;
} mem_addr_size_t;

typedef struct fpga_prog_s 
{
	__u32 len;
	__u8 cntrl_byte;
	char *buf;
} fpga_prog_t;

// Scatter-gather vector, analogous to iovec.
struct SgVec
{
    void        *ram_base;      // Memory address       
    uint64_t    dev_addr;       // Address in a device  
    uint64_t    length;         // Length               
};

// IO request structure, used with UMEM_SGIO_READ and UMEM_SGIO_WRITE 
struct EvIoReq
{
    void         *cookie;   // user context pointer         
    struct SgVec *vec;      // array pointer                
    uint32_t     nvec;      // array size                   
    int          status;    // request completion status    
};

// IO completion event
struct ev_io_event_s
{
    void *cookie;         // user IO context, it is the same "cookie" pointer in the corresponding IO request
    int status;           // Completion status
} ev_io_event_t;

#define MAX_IO_EVENTS 4   // We really only use one event currently, but in case needed for future needs. 

// structure for passing argument for GET_IOEVENTS ioctl command
typedef struct ev_io_event_ioc_s
{
    struct ev_io_event_s events[MAX_IO_EVENTS]; // event array
    uint32_t count; // number of events: IN:  size of input events array OUT: number of events returned
} ev_io_event_ioc_t;

typedef struct ecc_status_s
{
	__u8 is_sbe; 						// TRUE if single bit error was detected.
	__u8 is_mbe;						// TRUE if multi bit error was detected.
	__u8 is_auto_corr_enabled;			// TRUE if auto correction is enabled
	__u64 num_ddr_single_bit_errors;
	__u64 num_ddr_multi_bit_errors;
	__u64 last_ddr_error_range_start;
	__u64 last_ddr_error_range_end;
} ecc_status_t;

typedef struct performance_stats_s
{
	__u64 ios_completed; // DMAs completed in reality.
	__u64 start_time;
	__u64 end_time;
	__u64 bytes_transferred;
	__u64 total_interrupts; 		
	__u64 completion_interrupts;
    __u8 is_enabled;	
} performance_stats_t;

#define DBG_BUF_SIZE (1024) // Debug related only - capture buffer size  (4K max is what I have tested with and works)
//#define DBG_BUF_SIZE (4*1024) // Debug related only - capture buffer size
#define DBG_MAX_EVENTS (256)
#define DBG_MAX_PRINT_LINE (DBG_BUF_SIZE)

// All of these stats are cleared with a "reset_stats" command.
typedef struct debug_stats_s
{
	__u64 ios_rcvd;
	__u64 num_unplug_fn_called;							  
	__u64 dmas_queued; // queued and ready for the DMA engine.
	__u64 dmas_started;
	__u64 dmas_completed;
	__u64 ios_completed;
	__u64 dmas_timed_out;
	__u64 dmas_errored;
	__u64 dma_completion_ints;
	__u64 dmas_max_outstanding; // Largest of value of DMA starts queued to the hardware
	__u64 dmas_num_outstanding; // The accumulated sum of all outstanding DMAs when start_io was invoked.
	__u64 descriptors_max_outstanding; // Largest of value of DMA starts queued to the hardware
	__u64 ios_max_outstanding; // Largest of value of (ios_rcvd-ios_completed) queued by OS
    __u64 num_read_bios;  // Counts the number of READ Block IOs queued
    __u64 num_write_bios; // Counts the number of WRITE Block IOs queued

	__u64 num_dbg_1; // Generic counter for random use
	__u64 num_dbg_2; // Generic counter for random use
	__u64 num_dbg_3; // Generic counter for random use
	__u64 num_dbg_4; // Generic counter for random use
	__u64 num_dbg_5; // Generic counter for random use
	__u64 num_dbg_6; // Generic counter for random use
	__u64 num_dbg_7; // Generic counter for random use

	__u64 dbg_head;
	__u64 dbg_tail;
	__u64 dbg_cycles;
	__u64 dbg_valid_entries;
	__u64 dbg_buffer[DBG_BUF_SIZE];
	__u64 dbg_histogram[DBG_MAX_EVENTS]; 
}debug_stats_t;

#define DATA_LOGGER_BUFFER_SIZE (256)
#define DATA_LOGGER_DATA_MASK   (0x0000FFFF) // lower 32 bits are valid, upper are reserved for type info
#define DATA_LOGGER_TYPE_MASK   (0xFFFF0000) // lower 32 bits are valid, upper are reserved for type info

#define DATA_LOGGER_CARD_TEMPERATURE   (0x00010000) // The value is a reading of card temperature
#define DATA_LOGGER_FPGA_TEMPERATURE   (0x00020000) // The value is a reading of fpga temperature
#define DATA_LOGGER_PMU_TEMPERATURE    (0x00030000) // The value is a reading of PMU temperature
#define DATA_LOGGER_CAPACITANCE        (0x00040000) // The value is a reading of PMU capacitance


typedef struct data_logger_stats_s
{
    int sample_time;
    int sample_count; // Number of samples in a complete cycle
    int wraparound_index; // Sample count * number of measurements made per each measurement time.
	int head; // Add latest sample to head index, then increment, head location is empty. head==tail means empty.
	int tail; // Oldest sample is at tail index
	__u64 data_log_buffer[DATA_LOGGER_BUFFER_SIZE]; // Upper 32-bits are reserved for encoding data type information.
} data_logger_stats_t;

typedef union ioctl_union
{
    dev_info_t dev_info;
    ver_info_t ver_info;
    ev_buf_t ev_buf;
    get_set_val_t get_set_val;
    mem_addr_size_t mem_addr_size;
    ecc_status_t ecc_status;
    ev_io_event_ioc_t ev_io_event_ioc;
    performance_stats_t performance_stats;
    data_logger_stats_t data_logger_stats;
} ioctl_union_t;

typedef struct ioctl_arg_s
{
	int errnum;
    ioctl_union_t ioctl_union;
} ioctl_arg_t;


// PCI Configuration Space
// Read the bytes in configuration space at offset 0x92 (LSB) and 0x93 (MSB). 
// This is the PCIe Capability Structure "Link Status".
// Bits 9:4 are defined as the negotiated lane width.
// The possible values are:
// 000001b = x1
// 000010b = x2
// 000100b = x4
// 001000b = x8
// 010000b = x16
// 100000b = x32
// Bits 3:0 are defined as link speed
#define LINK_STATUS_NEGOTIATED_WIDTH_MASK 	0x03f0
#define LINK_STATUS_NEGOTIATED_WIDTH_SHIFT 	4
#define LINK_STATUS_LINK_SPEED_MASK 		0x000f

// Register and field values are defined next
// FPGA Register definitions
#define EV_FPGA_REV_NUM			0x00
#define EV_FPGA_VER_NUM			0x01
#define EV_FPGA_BUILD_NUM		0x02
#define EV_FPGA_CONFIG_NUM		0x03
#define EV_FPGA_CONTROL_REG		0x04
#define EV_FPGA_STATUS_REG		0x08
#define EV_FPGA_REQ_ADDR_REG_L 	0x0C
#define EV_FPGA_REQ_ADDR_REG_H 	0x10
#define EV_FPGA_REQ_LEN_REG 	0x14
#define EV_FPGA_INT_STAT_REG_L 	0x18
#define EV_FPGA_INT_STAT_REG_H 	0x1C
#define EV_TEST_REG				0x20
#define EV_FPGA_WINDOW_SELECT	0x24
#define EV_FPGA_ECC_CTRL		0x28
#define EV_FPGA_ECC_STATUS		0x2C // Byte access
#define EV_FPGA_SBE_COUNT		0x2D // Byte access
#define EV_FPGA_MBE_COUNT		0x2E // Byte access
#define EV_FPGA_ECC_ERROR_ADDR	0x30 // Address of the most recent ECC error

// DEBUG ???
#define EV_TX_DATA_FIFO_RD_CNT  0x40
#define EV_SGL_CNT              0x44
#define EV_SGL_FIFO_CNT         0x48
#define EV_STS_CNT              0x4C

// STATUS BUFFER 
// Bits 0..5 in STATUS BUFFER
// These bits are mutually exclusive. 

#define EV_INT_DMA_COMPLETION    (0x01)  // DMA Completion - Bits 6 .. 63 is the DMA context
#define EV_INT_NVL_STATUS_CHANGE (0x02)  // NL has generated a status
#define EV_INT_DMA_ERROR         (0x04)  // TLD Completion with data error, lower 3 bits has CPLD TLP status field

// Interrupt Enable bit for last Descriptor of SGL
#define DMASGL_DESCRIPTOR_CHAIN_END         0x02
#define DMASGL_CHAIN_END_INTERRUPT_ENABLE   0x04

// EV STATUS REGISTER
// I have seen a ststus change with no bits set. I believe that this is when the ultracapacitor is no longer
// fully charged. I need to verify this.
#define EV_STATUS_BACKUP_IN_PROGRESS 		0x01
#define EV_STATUS_RESTORE_IN_PROGRESS		0x02
#define EV_STATUS_VALID_FLASH_DATA 			0x04
#define EV_STATUS_READY_FOR_DMA				0x08
#define EV_STATUS_CAPACITOR_CHARGED			0x10
#define EV_STATUS_FLASH_CORRUPTED	 		0x20
#define EV_STATUS_BACKUP_FAILED				0x40
#define EV_STATUS_RESTORE_FAILED			0x80
#define EV_STATUS_I2C_DONE					0x10000
#define EV_STATUS_I2C_ERR					0x20000
#define EV_STATUS_I2C_INVALID				0x40000
#define EV_STATUS_FLASH_ERASE_STARTED		0x80000	 
#define EV_STATUS_SM_IDLE					0x100000 // 1 = PCIe state machine is IDLE and ready for I2C ops

// EV CONTROL REGISTER - TBD - go through document and set bits properly. 
#define EV_SRST			0x000001 // Must be set and kept set for normal operation.
#define EV_LOAD_RX_SIZE 0x200000 // Self-clearing field used to load RX_BUFFER values.
#define EV_SOFT_RESET	0x280001 //0x01
#define EV_NORMAL_STATE	0x400001 // Keep 0x01 set - EVUTIL used this value after an FPGA_PROGRAM

#define EV_BL_RST       0x000002 // Set to reset the bridge logic - this is the main soft reset.

#define EV_FLASH_ERASE_ENABLE	(0x20000000)
#define EV_ERASE_FLASH	(0x02 | EV_SRST | EV_FLASH_ERASE_ENABLE)
#define EV_FORCE_SAVE	(0x08)
#define EV_FORCE_RESTORE	(0x10)

#define EV_CONFIGURATION_START 	(0x00000020 | EV_SRST)
#define EV_CONFIGURATION_DONE 	(0x00000040 | EV_SRST)
#define EV_NO_SNOOP 			(0x00000080 | EV_SRST)
#define EV_AUTO_SAVE_DISABLE	(0x01000000) // OR in EV_SRST for all
#define EV_AUTO_RESTORE_DISABLE	(0x02000000)  
#define EV_DUMP_ENABLE			(0x04000000)
#define EV_LED_MODE				(0x08000000)
#define EV_SET_PCIE_PARAM		(0x10000000)
#define EV_DISABLE_416_BYTE_RECOVERY	(0x40000000) // Disable the use of the 416-byte DDR training area recovery.

// DATA R/W
#define DMA_READ_FROM_HOST 0
#define DMA_WRITE_TO_HOST 1

// FPGA_LED_DATA
#define EV_LED_DATA_PCIE_LINK		0x01
#define EV_LED_DATA_DDR_ACTIVITY	0x02
#define EV_LED_DATA_SAVE			0x04
#define EV_LED_DATA_RESTORE			0x08
#define EV_LED_DATA_CAP_CHARGED		0x10

// FPGA_WINDOW_SELECT
#define EV_WIN_SEL_CHANGE_START		(0x80000000)
#define EV_WIN_SEL_CUR_WIN_MASK		(0x0000001F)	// Low 5 bits are valid only

// FPGA_ECC_CTRL
#define ENABLE_ECC					0x01
#define ENABLE_AUTO_CORR			0x02
#define GEN_SBE						0x04
#define GEN_MBE						0x08
#define ENABLE_INTR					0x10 // TBD - or is this a disable intr when 1?
#define ENA_SBE_INTR				0x20 // TBD - These are reserved and not related to the actual INT
#define ENA_MBE_INTR				0x40 // TBD - These are reserved and not related to the actual INT
#define CLEAR_ECC_ERROR				0x80 // Clears the ECC interrupt, error status and error address

// FPGA_ECC_STATUS
#define SBE							0x01
#define MBE							0x02

// FPGA_ECC_ERROR_ADDR

// These values are used for convenience when requesting a hardware access via the IOCTL.
#define I2C_SPD_ADDR 0x0	// I2C_SLAVE_SEL = 1 
#define I2C_SPD_RANGE 0x100	// Number of bytes max

// Product specific parameters

// Board revision info
#define EV3_BOARD_REV_A	(0)

// Card personality info (configuration)
#define EV1_FPGA_CONFIG_32BIT_BAR_FLAT (1)
#define EV1_FPGA_CONFIG_32BIT_BAR_WINDOWED (2)
#define EV1_FPGA_CONFIG_64BIT_BAR_FLAT (3)

// Window parameters
#define WINDOW_SIZE_CONFIG2 (32*1024*1024)  // 32M


// Next are defines used by the NV logic registers.

// Reference EV3 Product Specification, the NV Register Set section

// NV_STATE_REG

#define NV_STATE_INIT 0x0           // A final state
#define NV_STATE_DISARMED 0x4       // A final state
#define NV_STATE_RESTORE 0x6
#define NV_STATE_ARMED 0xa          // A final state
#define NV_STATE_HALT_DMA 0xb
#define NV_STATE_BACKUP 0xc
#define NV_STATE_FATAL 0xd
#define NV_STATE_DONE 0xf           
#define NV_STATE_UNDEFINED 0x10


// FPGA programming related
#define FPGA_PROG_BUFFER_SIZE (8192)
#define FPGA_PROG_NUM_BUFS (2) // Multiple buffer system

// cntrl_byte values
#define FPGA_CTRL_BYTE_ERASE 0x01
#define FPGA_CTRL_BYTE_PROGRAM 0x02

// IFP STATUS
#define IFP_FPGA_IMAGE_MASK 0x20  // Bit 5
#define IFP_FPGA_IMAGE_APPLICATION 0x20  // Application Image, otherwise factory image

// LED_CONTROL
#define LED_LINK_CONTROL    0x0001  // D15
#define LED_LINK_STATE      0x0002
#define LED_ARMED_CONTROL   0x0004  // D16
#define LED_ARMED_STATE     0x0008
#define LED_BACKUP_CONTROL  0x0010  // D9
#define LED_BACKUP_STATE    0x0020
#define LED_RESTORE_CONTROL 0x0040  // D14
#define LED_RESTORE_STATE   0x0080
#define LED_PMU_CONTROL     0x0100  // D13
#define LED_PMU_STATE       0x0200
#define LED_ERROR_CONTROL   0x0400  // D8
#define LED_ERROR_STATE     0x0800
#define LED_DCAL_CONTROL    0x1000  // D10
#define LED_DCAL_STATE      0x2000

#endif
