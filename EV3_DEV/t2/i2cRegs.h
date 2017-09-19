/***************************************************************************************
****************************************************************************************
* FILE		: i2c.h
* Description	: i2c head file 
*			  
* NETLIST CORPORATION
* 51 Discovery
* Irvine, CA 92618
* 949-435-0025
* 
*  History:
*  Version		Name       	Date			       Description
*  FW0C         Sunny       2015/09/08             update ifp register
*  EV3 support  Daniel Roig  2015/07/15 
*  FW301002		Sunny	      2014/11/25	    
****************************************************************************************
****************************************************************************************/
#ifndef I2CREGS_H
#define I2CREGS_H

#ifndef DRIVERS_EV_H
// include files
#include "altera_avalon_pio_regs.h"
#include "system.h"
#define NV_BASE (0x0000)
#define SIZE_FACTOR (4)
#else
#define NV_BASE (0x0200)
#define SIZE_FACTOR (1)
#endif

// Register Definitions

// all i2c register accesses are 32 bit register accesses with byte addressing
//      NOTE - all IORD and IOWR register offsets are multiplied by 4 and added to the base in the macro
#define COMMAND_REG                     (NV_BASE + (0x000/SIZE_FACTOR))
#define CONTROL_REG                     (NV_BASE + (0x004/SIZE_FACTOR))
#define STATUS_REG                      (NV_BASE + (0x008/SIZE_FACTOR))
#define NV_FLAG_STATUS_REG              (NV_BASE + (0x00C/SIZE_FACTOR))
#define STATE_REG                       (NV_BASE + (0x010/SIZE_FACTOR))
#define INTERNAL_INTERRUPT_STATUS_REG   (NV_BASE + (0x014/SIZE_FACTOR))
#define HOST_INTERRUPT_STATUS_REG       (NV_BASE + (0x018/SIZE_FACTOR))
#define HOST_INTERRUPT_MASK_REG         (NV_BASE + (0x01C/SIZE_FACTOR))
#define ERROR_REG                       (NV_BASE + (0x020/SIZE_FACTOR))
#define ERROR_CODE_REG                  (NV_BASE + (0x024/SIZE_FACTOR))
#define WARNING_REG                     (NV_BASE + (0x028/SIZE_FACTOR))
#define IRAA_REG                        (NV_BASE + (0x030/SIZE_FACTOR))    // Internal RAM Access Address Register L and H - 16 bits
#define IRAA_DAT                        (NV_BASE + (0x034/SIZE_FACTOR))    // Internal RAM Access Address Register L and H - 16 bits
#define CVER_REG                        (NV_BASE + (0x038/SIZE_FACTOR))    // Contains the FPGA RTL Code Version and sub version
#define FW_VER_REG                      (NV_BASE + (0x03C/SIZE_FACTOR))
#define PCIE_STATUS                     (NV_BASE + (0x040/SIZE_FACTOR))
#define BACK_SIZE                       (NV_BASE + (0x044/SIZE_FACTOR))
#define NCTROL                          (NV_BASE + (0x048/SIZE_FACTOR))
#define MAR_AM                          (NV_BASE + (0x050/SIZE_FACTOR))
#define CUST_CONFIG_REG                 (NV_BASE + (0x060/SIZE_FACTOR))    // Contains the FPGA RTL Code sub sub version
#define BLNVL_INTERFACE_REG             (NV_BASE + (0x06c/SIZE_FACTOR))    // Contains the FPGA RTL Code sub sub version
#define PMU_SPD_ADDRESS                 (NV_BASE + (0x078/SIZE_FACTOR))
#define PMU_SPD_DATA                    (NV_BASE + (0x07C/SIZE_FACTOR))


#define LED_CONTROL                     (NV_BASE + (0x090/SIZE_FACTOR))
#define BL_CONTROL                      (NV_BASE + (0x0AC/SIZE_FACTOR))
#define BL_ADDRESS                      (NV_BASE + (0x0B0/SIZE_FACTOR))
#define BL_LENGTH                       (NV_BASE + (0x0BC/SIZE_FACTOR))
#define BL_PATTERN                      (NV_BASE + (0x0C0/SIZE_FACTOR))


#define EV3_SERIAL_NUM0                 (NV_BASE + (0x100/SIZE_FACTOR))
#define EV3_SERIAL_NUM1                 (NV_BASE + (0x104/SIZE_FACTOR))
#define EV3_PART_NUM0                   (NV_BASE + (0x108/SIZE_FACTOR))
#define EV3_PART_NUM1                   (NV_BASE + (0x10C/SIZE_FACTOR))
#define EV3_PART_NUM2                   (NV_BASE + (0x110/SIZE_FACTOR))
#define EV3_PART_NUM3                   (NV_BASE + (0x114/SIZE_FACTOR))
#define EV3_PART_NUM4                   (NV_BASE + (0x118/SIZE_FACTOR))
#define EV3_PART_NUM5                   (NV_BASE + (0x11C/SIZE_FACTOR))
#define EV3_REVISION_NUM                (NV_BASE + (0x120/SIZE_FACTOR))
#define PMU_SERIAL_NUM0                 (NV_BASE + (0x130/SIZE_FACTOR))
#define PMU_SERIAL_NUM1                 (NV_BASE + (0x134/SIZE_FACTOR))
#define PMU_PART_NUM0                   (NV_BASE + (0x138/SIZE_FACTOR))
#define PMU_PART_NUM1                   (NV_BASE + (0x13C/SIZE_FACTOR))
#define PMU_PART_NUM2                   (NV_BASE + (0x140/SIZE_FACTOR))
#define PMU_PART_NUM3                   (NV_BASE + (0x144/SIZE_FACTOR))
#define PMU_PART_NUM4                   (NV_BASE + (0x148/SIZE_FACTOR))
#define PMU_PART_NUM5                   (NV_BASE + (0x14C/SIZE_FACTOR))
#define PMU_REVISION_NUM                (NV_BASE + (0x150/SIZE_FACTOR))
#define PMU_NOMINAL_CAPACITANCE         (NV_BASE + (0x15C/SIZE_FACTOR))
#define PMU_MAX_OPERATING_VOLTAGE       (NV_BASE + (0x160/SIZE_FACTOR))
#define PMU_MAX_OPERATING_TEMP          (NV_BASE + (0x164/SIZE_FACTOR))
#define FIRST_CAPACITANCE               (NV_BASE + (0x168/SIZE_FACTOR))   // (RO to system)


#define CUST_NV_REG0                    (NV_BASE + (0x180/SIZE_FACTOR))
#define CUST_NV_REG1                    (NV_BASE + (0x184/SIZE_FACTOR))
#define CUST_NV_REG2                    (NV_BASE + (0x188/SIZE_FACTOR))
#define CUST_NV_REG3                    (NV_BASE + (0x18C/SIZE_FACTOR))
#define CUST_NV_REG4                    (NV_BASE + (0x190/SIZE_FACTOR))
#define CUST_NV_REG5                    (NV_BASE + (0x194/SIZE_FACTOR))
#define CUST_NV_REG6                    (NV_BASE + (0x198/SIZE_FACTOR))
#define CUST_NV_REG7                    (NV_BASE + (0x19C/SIZE_FACTOR))  //PAGE 3


#define UNCOR_BACK_ECC_ERR_CNT          (NV_BASE + (0x200/SIZE_FACTOR))   // (RO to system)
#define UNCOR_RSTR_ECC_ERR_CNT          (NV_BASE + (0x204/SIZE_FACTOR))   // (RO to system)
#define CUM_BACK_UNCOR_ECC_ERR_CNT      (NV_BASE + (0x208/SIZE_FACTOR))   // (RO to system)
#define CUM_RSTR_UNCOR_ECC_ERR_CNT      (NV_BASE + (0x20C/SIZE_FACTOR))   // (RO to system)
#define RESERVE_BLOCK_LEVEL             (NV_BASE + (0x240/SIZE_FACTOR))   // eMMC ECSD[267]
#define RESERVE_BLOCK_ERROR_THRESH      (NV_BASE + (0x244/SIZE_FACTOR))
#define RESERVE_BLOCK_WARN_THRESH       (NV_BASE + (0x248/SIZE_FACTOR))
#define LIFE_ESTIMATE                   (NV_BASE + (0x24C/SIZE_FACTOR))   // DEVICE_LIFE_TIME_EST_TYPE_B from eMMC ECSD[269]
#define LIFE_ESTIMATE_ERR_THRESH        (NV_BASE + (0x250/SIZE_FACTOR))
#define LIFE_ESTIMATE_WARN_THRESH       (NV_BASE + (0x254/SIZE_FACTOR))
#define BACKUP_START_CT                 (NV_BASE + (0x258/SIZE_FACTOR))
#define BACKUP_COUNTET                  (NV_BASE + (0x25C/SIZE_FACTOR))
#define BACKUP_COMPLETE_ERROR_CT        (NV_BASE + (0x260/SIZE_FACTOR))
#define BACKUP_COMPLETE_WARN_CT         (NV_BASE + (0x264/SIZE_FACTOR)) //page 4




#define LAST_BACKUP_TIME                (NV_BASE + (0x280/SIZE_FACTOR))   // (RO to system)
#define MAX_THRSH_BACKUP_TIME           (NV_BASE + (0x284/SIZE_FACTOR))   // (RW to system)
#define MAX_WARN_BACKUP_TIME            (NV_BASE + (0x288/SIZE_FACTOR))   // (RW to system)
#define LONGEST_BACKUP                  (NV_BASE + (0x28c/SIZE_FACTOR))   // (RO to system)
#define LAST_RESTORE_TIME               (NV_BASE + (0x290/SIZE_FACTOR))   // (RO to system)
#define MAX_THRSH_RESTORE_TIME          (NV_BASE + (0x294/SIZE_FACTOR))   // (RW to system)
#define MAX_WARN_RESTORE_TIME           (NV_BASE + (0x298/SIZE_FACTOR))   // (RW to system)
#define LONGEST_RESTORE                 (NV_BASE + (0x29c/SIZE_FACTOR))   // page 5



#define  CAP_VOLTAGE                    (NV_BASE + (0x300/SIZE_FACTOR))   // page 6
#define  MAX_CHARGE_CAP_VOLTAGE         (NV_BASE + (0x304/SIZE_FACTOR))   //  ADD CODE
#define  PRG_MIN_CAP_VOLT_THR           (NV_BASE + (0x308/SIZE_FACTOR))   // (RW to system)
#define  LAST_CAP_VOLT_BEFORE_BACKUP    (NV_BASE + (0x30C/SIZE_FACTOR))   // (RO to system)
#define  LAST_CAP_VOLT_AFTER_BACKUP     (NV_BASE + (0x310/SIZE_FACTOR))   // (RO to system)
#define  PRG_ERR_CAP_VOLT_THR           (NV_BASE + (0x314/SIZE_FACTOR))   // (RW to system)
#define  PRG_WARN_CAP_VOLT_THR          (NV_BASE + (0x318/SIZE_FACTOR))   // (RW to system)
#define  LOWEST_CAP_VOLT_BACKUP         (NV_BASE + (0x31C/SIZE_FACTOR))   // (RO to system)
#define  LAST_CAP_VOLT_END_BACKUP       (NV_BASE + (0x320/SIZE_FACTOR))   // (RO to system)
#define  LAST_CHARGE_TIME               (NV_BASE + (0x324/SIZE_FACTOR))   // (RO to system)
#define  LONGEST_CHARGE                 (NV_BASE + (0x328/SIZE_FACTOR))   // (RO to system)
#define  LAST_CAPACITANCE               (NV_BASE + (0x340/SIZE_FACTOR))   // (RO to system)
#define  PRG_MIN_CAPACITANCE            (NV_BASE + (0x344/SIZE_FACTOR))   // (RW to system)
#define  PRG_WARN_CAPACITANCE           (NV_BASE + (0x348/SIZE_FACTOR))   // (RW to system)
#define  LOWEST_CAPACITANCE             (NV_BASE + (0x34C/SIZE_FACTOR))   // (RO to system)
#define  CAPACITANCE_MEAS_FREQ          (NV_BASE + (0x350/SIZE_FACTOR))   // Seconds
#define  CAPACITANCE_MIN_ENERGY         (NV_BASE + (0x354/SIZE_FACTOR))
#define  CAPACITANCE_ENERGY             (NV_BASE + (0x358/SIZE_FACTOR))   // LAST_CAPACITANCE * V^2 / 2
#define  CAPACITANCE_LAST_BACKUP_ENERGY (NV_BASE + (0x35C/SIZE_FACTOR))   // LAST_CAPACITANCE * (LAST_CAP_VOLT_BEFORE_BACKUP^2 - LAST_CAP_VOLT_AFTER_BACKUP^2)/2
#define  CAPACITANCE_ENERGY_GUARD_BAND  (NV_BASE + (0x360/SIZE_FACTOR))   // Percentage



#define CURRENT_PMU_TEMP                (NV_BASE + (0x380/SIZE_FACTOR))   // (RO to system)
#define PROG_MAX_PMU_TEMP               (NV_BASE + (0x384/SIZE_FACTOR))   // (RW to system)
#define PROG_MAX_WARN_PMU_TEMP          (NV_BASE + (0x388/SIZE_FACTOR))   // (RW to system)
#define HIGHEST_PMU_TEMP                (NV_BASE + (0x38C/SIZE_FACTOR))   // (RO to system)
#define NUM_OVER_TEMP_EVENTS            (NV_BASE + (0x390/SIZE_FACTOR))   // (RO to system)
#define NUM_OVER_TEMP_WARN_EVENTS       (NV_BASE + (0x394/SIZE_FACTOR))   // (RO to system)
#define PMU_AVERAGE_TEMP                (NV_BASE + (0x398/SIZE_FACTOR))
#define PMU_NUM_TEMP_SAMPLES            (NV_BASE + (0x39C/SIZE_FACTOR))   //PAGE 7
#define EV3_CARD_TEMP                   (NV_BASE + (0x3C0/SIZE_FACTOR))
#define EV3_FPGA_TEMP                   (NV_BASE + (0x3D0/SIZE_FACTOR)) 


#define ERROR_INJECTION_OPERATION       (NV_BASE + (0x400/SIZE_FACTOR))
#define ERROR_INJECTION_BACKUP_PWR      (NV_BASE + (0x404/SIZE_FACTOR))
#define ERROR_INJECTION_FLASH_LIFE      (NV_BASE + (0x408/SIZE_FACTOR))
#define ERROR_INJECTION_CONTROL         (NV_BASE + (0x410/SIZE_FACTOR))
#define ERROR_INJECTION_DATA_PATH       (NV_BASE + (0x414/SIZE_FACTOR))

#ifndef DRIVERS_EV_H
#define    	IFP_COMMAND                 (NV_BASE + (0x0/SIZE_FACTOR))    	
#define    	IFP_CONTROL                 (NV_BASE + (0x4/SIZE_FACTOR))    	
#define    	IFP_STATUS                  (NV_BASE + (0x8/SIZE_FACTOR))   	   
#define    	SIGNATURE_TMP0              (NV_BASE + (0xc/SIZE_FACTOR))   	
#define    	SIGNATURE_TMP1              (NV_BASE + (0x10/SIZE_FACTOR))     	
#define    	IFP_CHECKSUM                (NV_BASE + (0x14/SIZE_FACTOR))    	
#define    	IFP_AI_CHECKSUM             (NV_BASE + (0x18/SIZE_FACTOR))    	
#define    	IFP_DATA_WRITE_BUFFER       (NV_BASE + (0x1c/SIZE_FACTOR))       
#define    	IFP_DATA_READ_BUFFER        (NV_BASE + (0x20/SIZE_FACTOR))    	
#define     IFP_FIFO_DATA				(NV_BASE + (0x24/SIZE_FACTOR))		
#define    	IFP_ADDRESS                 (NV_BASE + (0x28/SIZE_FACTOR))    	
#define	    ERROR_INJECTION_IFP			(NV_BASE + (0x2c/SIZE_FACTOR))	    
#define		DIAG_FIFO_ACC				(NV_BASE + (0x30/SIZE_FACTOR))	    
#define		FIFO_CHECK_SUM				(NV_BASE + (0x34/SIZE_FACTOR))	    
#else
#define    	IFP_COMMAND                 (NV_BASE + (0x800/SIZE_FACTOR))    	
#define    	IFP_CONTROL                 (NV_BASE + (0x804/SIZE_FACTOR))    	
#define    	IFP_STATUS                  (NV_BASE + (0x808/SIZE_FACTOR))   	   
#define    	SIGNATURE_TMP0              (NV_BASE + (0x80c/SIZE_FACTOR))   	
#define    	SIGNATURE_TMP1              (NV_BASE + (0x810/SIZE_FACTOR))     	
#define    	IFP_CHECKSUM                (NV_BASE + (0x814/SIZE_FACTOR))    	
#define    	IFP_AI_CHECKSUM             (NV_BASE + (0x818/SIZE_FACTOR))    	
#define    	IFP_DATA_WRITE_BUFFER       (NV_BASE + (0x81c/SIZE_FACTOR))       
#define    	IFP_DATA_READ_BUFFER        (NV_BASE + (0x820/SIZE_FACTOR))    	
#define     IFP_FIFO_DATA				(NV_BASE + (0x824/SIZE_FACTOR))		
#define    	IFP_ADDRESS                 (NV_BASE + (0x828/SIZE_FACTOR))    	
#define	    ERROR_INJECTION_IFP			(NV_BASE + (0x82c/SIZE_FACTOR))	    
#define		DIAG_FIFO_ACC				(NV_BASE + (0x830/SIZE_FACTOR))	    
#define		FIFO_CHECK_SUM				(NV_BASE + (0x834/SIZE_FACTOR))	 
#endif

// End Register Definitions


#define registerRead(addr)              IORD( NV_REG_0_BASE, addr )
#define registerWrite(addr,data)        IOWR( NV_REG_0_BASE, addr, data )
//

// Command Register Bit definitions
#define RESET                       0x01    // A 1 commands a reset of the asic
#define WRITECT                     0x02    // write table to flash to update ev3 sn & pn
#define SPD_RW_EN                   0x04    // Write spd command
#define RAM_RD_EN                   0x08    // commands to access ram data
#define FLASH_SANITIZE              0x10    // not implement in current fwe
#define RD_CAP                      0x20    // A 1 commands the firmware to computer the UltraCapacitor capacitance
#define LFD                         0x80    // to load defaut factory data  and ev sn pn


// Control Register Bit definitions
#define PG                          0x01    // A 1 enables the module to prepare for a subsequent unscheduled power loos
#define BUE                         0x02    // A 1  Backup Enable - enables entrance into protected mode
#define FLSH                        0x04    // A 1 indicates that the host processor has completed the Flush of DRAM
#define DCM                         0x10    // Disable Capacitor Monitor
#define EACM                        0x0100  //enable cap auto test
#define SPD_WRITE                   0x0200  //spd operation direction 1: writer 0: read
#define PCIE_RESET_CONTROL			0x0400	//Control PCIe reset
// Status Register Bit definitions
#define BUIP                        0x01    // Backup In Progress
#define RIP                         0x02    // Restore In Progress
#define SCHANGE                     0x04    // this bit meas the state changes
#define BPSP                        0x08    // this bit is defined by the hardware 
#define BPSR                        0x10    // Capacitor Array Charged
#define CA                          0x40    // Configuration Allowed
#define OE                          0x80    // HARDWARE implemented-Operation Error - logic OR of all Err. Reg. (0x20) bits
// Status bits 8 - 15 - wasStatus Signal State Register bit definitions
#define CACHE_DIRTY                 0x0100  // Cache Dirty in DRAM
#define DRAM_AVAIL                  0x0200  // DRAM avaialable for system access
#define NVDIMM_RDY                  0x0400  // NVDIMM ready - NVDIMM can support a backup operation if set
#define NV_VALID                    0x0800  // Non-Volatile registers are Valid
#define BKUP_DONE                   0x1000  // backup done, data valid data is in the flash
#define RSTR_DONE                   0x2000  // Restore is done, ready to Erase
#define CAP_NOT                     0x8000  // Capacitor Not Present (read only - set/cleared from hardware only)
#define PFD                         0x10000 // 1:power fail     0 : no power fail

// 0x0c Non-Volatile Flag Status Register
#define FLASH_OK                    0x01    // there is no data in the flash
#define FLASH_DIRTY                 0x02    // there is some data in the flash 
#define FLASH_STATE_M               (FLASH_OK | FLASH_DIRTY)   // there is valid data in the flash
#define FLASH_INIT                  0x04

// 0x1c  host Interrupt Status Register 

#define NVRI                       0x01    // nvdimm rdy interrupt
#define NVNRI                      0x02    // nvdimm from rdy to non rdy interrupt
#define SCI                        0x04    // nvdimm state change interrupt
#define OEI                        0x20    // nvdimm error occur interrupt
#define OWI                        0x40    // nvdimm warn occur interrupt

#define INTER_STATUS_ALL            (NVRI|NVNRI|SCI|OEI|OWI)

// Error Register bit definitions
// Warning error bits are the same as the Error Register bits
#define BKUF                        0x01    // Backup Failure - all cache data lost
#define UECC                        0x02    // un correctable ecc error
#define RESF                        0x04    // Restore Failure - unrecoverable ecc errors found, data in cache is corrupted
#define EASE                        0x08    // erase and Sanitize and  failed - the sanitize operation failed
#define PMUAE                       0x10    // Spd access error  - there is no ack responce from the spd
#define CALC                        0x20    // Capacitor Array Lost Charge - the capacitor array voltage has dropped to less than threshold or it has been disconnected
#define CAUVAB                      0x40    // Capacitor Array Under Value after backup- the capacitor array was below minimum threshold at the end of a backup
#define FATAL                       0x80    // Fatal Error - a catistrophic error has happend and we cannot continue to operate in protected mode.

// Error Byte 1
#define FBEX                        0x0100  // Free Blocks Exhausted - There number of bad blocks are greater than the threshold, this
#define PMUCE                       0x0200  // SPD  checksum is not correct - fw read the spd information and  calculate the checksum, it is not equal the checksum store in the spd                                          
#define FDE                         0x0400  // read ext_csd error - error when read ext_csd from the emmc device FATAL
#define FCE                         0x0800  // Flash Config Error - error configuring Flash devices, FATAL_BIT
#define BKCTE                       0x1000  // backup counter exceed the max counter
#define FEE                         0x4000  // Flash Erase error - error erasing flash, FATAL
#define CTE                         0x8000  // Capacitance Test error ( cap dropped below the threshold value for backup )

// Error Byte 2
#define CBT                         0x00010000  // Capacitance below threshold detected in state 10.
#define INCPMU                      0x00020000  // PMU is not match the board
#define EMPSPD                      0x00040000  // the spd is empty
#define CFGTF                       0x00080000  // Configuration Table Failure, FATAL
#define IFPTO                       0x00200000  // IFP download error
#define ECOM                        0x00400000  // Erase count over Max - FATAL
#define LTEE                        0x00800000  //ltee   life  estimate 

// Error Byte 3
#define BCD                         0x01000000  // Bad Configuration Data error
#define PMUINV                      0x02000000  // Bd pmu 
#define PMU_TEMP_ALERT              0x04000000  // PMU is too hot 
#define CFGTR                       0x08000000  // Error reading configuration table, earlier version used, data may have been lost
#define FTTL                        0x20000000  // the function took too long
#define CAVE                        0x80000000  // CA Violation Error

//

#define INTERRUPT_BK_ENABLE         0x01    // interrupt enable function
#define FORCE_DISCHARGE_ENABLE      0x02    // interrupt enable function

//0x60
#define AUTO_ARMED                  0x01
#define AUTO_RESTORE                0x02


//0x6c
#define NL_H_DMA    			    0x00000001
#define NL_DMA_H    				0x00000002
#define NL_DMA_RST  				0x00000004
//0xAC
#define BL_ACCESS_ENABLE            0x01
#define BL_RW                       0x02     // 1: write 0:read
#define BL_PRINT                    0x04
#define BL_PATTERN_BIT0             0x08
#define BL_PATTERN_BIT1             0x10
#define BL_VERIFY                   0x20

#define PATTERN_WITH_0              0
#define PATTERN_WITH_1              BL_PATTERN_BIT0
#define PATTERN_WITH_REG            BL_PATTERN_BIT1
//error injection 620
#define IBE                         0x00000001  // 0 backup error
#define IRE                         0x00000002  // 1 restore error
#define IEE                         0x00000004  // 2 erase error
//error injection 624
#define ILVR_S                      0x00000001  // 0 
#define ILVR_P                      0x00000002  // 1
#define IMCW_S                      0x00000004  // 2
#define IMCW_P                      0x00000008  // 3
#define IMCE_S                      0x00000010  // 4
#define IMCE_P                      0x00000020  // 5
#define IMPTW_S                     0x00000040  // 6
#define IMPTW_P                     0x00000080  // 7
#define IMPTA_S                     0x00000100  // 8
#define IMPTA_P                     0x00000200  // 9
#define IABFP_P                     0x00000400  // 10
//error injection 628
#define IRBW_S                      0x00000001  // 0
#define IRBE_S                      0x00000002  // 1
#define ILTW_S                      0x00000004  // 2
#define ILTE_S                      0x00000008  // 3
//error injection 62C
#define IFWE                        0x00000001  // 0
//0x800 command register
#define IFP_DOWNLOAD_ENABLE         0x01  // (1 << 0)
#define IFP_APPLICA_ENABLE          0x02  // (1 << 1)
#define IFP_FIFO_RST                0x04  // (1 << 2)

//IFP Status Register Bits
#define IFP_CRC_ERR 				    0x00000001// (1 << 0)
#define IFP_TIMEOUT 				    0x00000002//(1 << 1)
#define IFP_F_ERR 					    0x00000004//(1 << 2)
#define IFP_F_OFL 					    0x00000008// (1 << 3)
#define IFP_ERR							0x00000010//(1 << 4)
#define IFP_AI_ACTV 			        0x00000020//(1 << 5)
#define IFP_VALIDE_SIGN 				0x00000040//(1 << 6)
#define IFP_APPLICATION_IMAGE_EXIST 	0x00000080//(1 << 7)
#define IFP_FIFO_FULL_BIT 				0x00000100//(1 << 8)
#define IFP_FIFO_WEMPTY_BIT				0x00000200//(1 << 9)
#define IFP_PRG_DONE					0x00000400//(1 << 10)
#define IFP_PRG_BUSY					0x00000800//(1 << 11)
#define IFP_FIFO_REMPTY_BIT				0x00001000//(1 << 12)
#define IFP_H_DC						0x00002000//(1 << 13)		//IFP Image Download Passed 
#define	IFP_H_DF						0x00004000//(1 << 14)		//IFP Image Download Failed
#define	IFP_RCFG_DONE					0x00008000//(1 << 15)		//FPGA has completed reconfiguration cycle without errors	
//IFP Control Register Bits
#define IFP_RECONFIG_BIT                0x00000001// (1 << 0) 		//ST_CFG
#define IFP_WR1WRD_CMD                  0x00000002// (1 << 1)		//W1W
#define IFP_ERASE_WR1WRD_CMD            0x00000004// (1 << 2)		//EW1W
#define IFP_READ_1WRD_CMD               0x00000008// (1 << 3)		//R1W
#define IFP_ERASE_32SECTOR_CMD          0x00000010// (1 << 4)		//E16S
#define IFP_BULK_ERASE_CMD              0x00000020// (1 << 5)		//RU_6
#define IFP_ENABLE_4BYTE                0x00000040// (1 << 6)		//
#define IFP_NIOS_AFN_CFG                0x00000080// (1 << 7)		//C_AF
#define IFP_FIFO_RE_DIAG                0x00000100// (1 << 8)		//DR_IF
#define IFP_FIFO_WR_DIAG                0x00000200// (1 << 9)		//DW_IF
#define IFP_PAGE_SEL                    0x00000400// (1 << 10)		//IFP_AS
#define IFP_BIT_SWAP                    0x00000800// (1 << 11)		//BSWP
#define IFP_RESET_CMD                   0x00008000// (1 << 15)		//IFP_RES

// Warning register bits that are different from error bits
#define MAR_VALUE                       0x4d41
#define MAR_MASK                        0xFFFF
// defines for ERROR_CODE_REG, contains info on what caused an error and
// where we were in code when it happened

#define DDR3_SDRAM_INIT_FAIL                    7
#define BLNVL_DMA_H_TO                          8
#define SLC_FLASH_CONFIG_DONE                   9
#define SELF_TEST_FAILURE                       11
#define WRITE_INVALID_CFG_ERROR                 12
#define CAP_CAP_FAIL_ERR                        13
#define UNDEFINED_STATE                         14
#define TEMP_EXCEED_MAXIMUM                     15
#define WRITE_SPD_ERROR1                        16
#define WRITE_SPD_ERROR2                        17
#define WRITE_SPD_ERROR3                        18
#define BKUP_VOLTAGE_FAIL_ERR                   19

#define B_ERR_WAIT_FIFO_EMPTY_TIMEOUT           21
#define B_ERR_WAIT_DMA_RDY_TIMEOUT              22
#define B_ERR_WAIT_FLASH_TX_FIFO_EMPTY_TIMEOUT  23
#define B_ERR_BAD_FLASH_CMD                     24
#define B_ERR_BAD_FLASH_CMD_CRC                 25
#define B_ERR_FLASH_ERROR                       26
#define B_ERR_FLASH_RESPONSE_TIMEOUT            27
#define B_ERR_BAD_FLASH_RESPONSE_CRC            28		           
#define B_ERR_FLASH_CMD_FAILED                  29
#define B_ERR_WAIT_FLASH_BUSY                   31
#define B_ERR_WAIT_FLASH_CTLR_BUSY              32
#define B_ERR_DEFAULT                           33

#define R_ERR_ECC_FAILED                        41
#define R_ERR_WAIT_DMA_RDY_TIMEOUT              42
#define R_ERR_WAIT_FLASH_TX_FIFO_EMPTY_TIMEOUT  43
#define R_ERR_BAD_FLASH_CMD                     44
#define R_ERR_BAD_FLASH_CMD_CRC                 45
#define R_ERR_FLASH_ERROR                       46
#define R_ERR_FLASH_RESPONSE_TIMEOUT            47
#define R_ERR_BAD_FLASH_RESPONSE_CRC            48
#define R_ERR_FLASH_CMD_FAILED                  49
#define R_ERR_WAIT_FLASH_BUSY                   51
#define R_ERR_WAIT_FLASH_CTLR_BUSY              52
#define R_ERR_DEFAULT                           53
//FATAL
#define READ_CONFIG_FATAL_ER1                   61
#define ERASE_CONFIG_FATAL_ERR1                 62
#define WRITE_CONFIG_FATAL_ER1                  63
#define WRITE_CONFIG_FATAL_ER2                  64
#define WRITE_CONFIG_FATAL_ER3                  65
#define WRITE_CONFIG_FATAL_ER4                  66
#define WRITE_CONFIG_FATAL_ER5                  67
#define FLASH_BLOCKS_FALTAL_EXHAUSTED           68
#define LIFE_TIME_ESTIMATE_FALTAL_ERROR         69
#define FLASH_CONFIG_FATAL_ERR1                 71
#define FLASH_READ_CSD_FALTAL_ERROR             72
#define ERASE_COUNT_THRESH_FATAL_ERR            73
#define FLASH_ERASE_FATAL_FAILURE               74
#define BACKUP_EXCEED_MAX_CT                    75
#define WRITE_CONFIG_FATAL_ER6                  76
#define SANITIZE_FATAL_ERROR                    77

#define UNDEFINED_FAILURE                       99



#endif  // #ifndef I2CREGS_H


