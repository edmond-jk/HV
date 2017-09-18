/*
 * HVDIMM DDR4 Slave IO training
 *
 * _1007	SJ		-T test. Retry 3 times for LENOVO
 * 					#define LENOVO
 *					#define FAKE_RD_DATA_TEST
 */

/*
./devmem2h 0x47ff10000 w 64 1 0 0 0	=>> ECC 0x55447789

 256(0x100): 01 00 00 00 89
 272(0x110): 00 00 00 00 77
 288(0x120): 00 00 00 00 44
 304(0x130): 00 00 00 00 55
 320(0x140): 01 00 00 00 89
 336(0x150): 00 00 00 00 77
 352(0x160): 00 00 00 00 44
 368(0x170): 00 00 00 00 55

 256(0x100): 11 11 11 11 66		=>> 0x00000066
 272(0x110): 22 22 22 22 00
 288(0x120): 33 33 33 33 00
 304(0x130): 44 44 44 44 00
 320(0x140): 55 55 55 55 44		=>> 0x88225544
 336(0x150): 66 66 66 66 55
 352(0x160): 77 77 77 77 22
 368(0x170): 88 88 88 88 88

*/
#define VERSION "-10.31.16"
//
// -T 0		- New option. Read both memory and i2c register data.
// -T		- Modifed read 64B from memory then compare data.
// -FW		- New option for FakeWR testing
// -CMD		- Modified option for BSM/MMLS testing
// BugFix   - FakeRd Counter: Reg 0x7E & 7F from 0x70 & 71
// 
char version[] = VERSION;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>	
#include <sys/time.h>
#include <sys/io.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include "hv_training_1014.h"
#include "bdw-cb-256.h"				// 9.13.2016

#define LOAD_IO_DELAY		1		// 9.07.2016
#define CHECK_ECC_MODE		0		// 9.07.2016
#define CHECK_DATA_SCRAMBLE	1		// 9.07.2016
#define CLFLUSH_CACHE_RANGE 0

#define PER_BIT_TX_DQDQS2	0
#define FAKE_RD_DATA_TEST	0		// 10.14.2016
#define SHOW_RESULT_FE_RE	1		// 10.14.2016
#define LENOVO				0		// 10.7.2016

#define PLOT_SYMBOL_ZERO 	'0'
#define PLOT_SYMBOL_ONE 	' '

#define BITS_PER_LONG_LONG 64
#define GENMASK_ULL(h, l) \
	(((~0ULL) << (l)) & (~0ULL >> (BITS_PER_LONG_LONG - 1 - (h))))
#define GET_BITFIELD(v, lo, hi)	\
	(((v) & GENMASK_ULL(hi, lo)) >> (lo))

#define FATAL do { fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", \
  __LINE__, __FILE__, errno, strerror(errno)); exit(1); } while(0)

#define PLATFORM_INFO_ADDR          (0xCE)

#if 0
#define HV_TRAINING_OFFSET			0x40000000	// tohm - 1GB
#else
#define HV_TRAINING_OFFSET			0x200000	// tohm - 2MB
#endif

#define MMIO_WINDOW_SIZE			0x100000	// tohm - 1MB
#define FAKE_RD_OFFSET				0x00000		// tohm - 2MB 
#define CMD_OFFSET					0x110000	// tohm - 1MB + 0x10000
#define BCOM_SW_OFFSET				0x118000	// tohm - 1MB + 0x18000
#define RD_BUFF_OFFSET				0x10000		// tohm - 2MB + 0x10000
#define GEN_WR_OFFSET				0x100000	// tohm - 2MB + 0x10000
#define QUERY_STATUS_OFFSET			0x108000	// tohm - 2MB + 0x10000 + 0x8000

#define MAP_SIZE2 1024*1024*2UL					// 2MB
#define MAP_MASK2 (MAP_SIZE2 - 1)
#define TX_DQDQS1_DATA_SIZE			64
#define RX_DQDQS1_DATA_SIZE			64
#define TX_DQDQS2_DATA_SIZE			64

#include "spd-3-16-2016.c"

#define CMD_CK				0x01	// CMD-CK: Post RCD Signal Setup/Hold Time for FPGA
#define RD_CMD_CK			0x02	//
#define TX_DQ_DQS_0			0x0400	// Tx DQ-DQS(1) - Both TxDQS1 and TxDQDQS1 
#define TX_DQ_DQS_1			0x04	// TxDQS1 ==> TxDQDQS(1) - DB-to-FPGA Write Timing
#define TX_DQS_1			0x05	// 
#define RX_DQ_DQS_0			0x0800	// Rx DQ-DQS(1) - Both RxDQS1 and RxDQDQS1 
#define RX_DQ_DQS_1			0x08	// RxDQS1 ==> RxDQDQS(1) - DDR4-to-FPGA Fake Read Timing
#define RX_DQS_1			0x09	// 
#define TX_DQ_DQS_2_0		0x1000	// Tx DQ-DQS(2) - FPGA-to-DDR4 Fake Write Timing
#define TX_DQ_DQS_2			0x10	// Tx DQ-DQS(2) - FPGA-to-DDR4 Fake Write Timing
#define TX_VREF    			0x20	// Tx VREF      - 8/26/2016

#define RD_CMD_CK_MASK  	0xc0

#define RD					0x00
#define WR					0x10
#define WRECC				0x11
#define WR_ALT				0x12

#define SPD					0xA
#define TSOD				0x3
#define FPGA1				0x5
#define FPGA2				0x7

#define FPGA_TIMEOUT		100		// 100 = 100MS


int lenovo;
int failstop=0;
#if 0//LENOVO	// LENOVO				
#define FPGA_WRITE_DLY		1000	// 1000 = 1ms 
#define FPGA_READ_DLY		1000	// 1000 = 1ms 
#define FPGA_CMD_WR_DLY		10000	// 10000 = 10ms 
#define FPGA_SETPAGE_DLY	10000	// 10000 = 10ms 
//#else
#define FPGA_WRITE_DLY		1
#define FPGA_READ_DLY		1
#define FPGA_CMD_WR_DLY		10		// 
#define FPGA_SETPAGE_DLY	10
#endif
#if 1
#define FPGA_WRITE_DLY		100		// X10DRC	8/26/2016
#define FPGA_READ_DLY		1		// 9/7/2016
#define FPGA_CMD_WR_DLY		100		// 
#define FPGA_SETPAGE_DLY	1000	// 
#endif
#define FPGA_LOAD_DELAY		1000	// 10/24/2016
#define SPD_WRITE_DLY		10000	// 10/24/2016

int rDQS[256][18], reDQS[18], feDQS[18], cpDQS[18], lwDQS[18];
int tDQS[256][18], REDQS[18], FEDQS[18], CPDQS[18];
int rDQ[256][9], RE[72], FE[72], CP[72], PW[72], VR[256];
int IoCycDly[18];
int CPmin, CPmax, FEmin, FEmax, REmin, REmax, PWmin, PWmax;
int VRmin, VRmax;
int fpga1_status, fpga2_status, HWrev[2];

char logfile[128], system_info[80], hostname[80];

struct DIMM {
	char sSN[12];
	char sPN[18];
	int Temp;
} dimm[24];

int dti=0, N, C, D, Status, Training=0, update_hw=0, eccMode=0, dataScramble=0, SetMemDelay=0;
int fd, fd1, logging=0, listing=0, i2cRdWr=0, showsummary=0, MemDelay=1, ShowCounter=0;
int loadfromfile=0, savetofile=0, viewspd=0, debug=0, sendCmdtest=0, fakeWrtest=0, SingleDimm=0;
int clearspd=0, loadfromspd=0, savetospd=0, SetIoCycDly=0, SetOffset=0, CycDly=0;
int write_fpga_data=0, bcomsw=0, wrCmdtest=0, cmdMuxCyc=0, initEcc=0, PLLReset=0;
int colorlimit=20, TestTime=0, SetPattern=0, slaveON=0, ModeReset=0, SetVref=0, SetClkDelay=0;
int HA=0, mcscramble_seed_sel;
int start_lp=0, end_lp=256, step=1;
int start_lp1=10, end_lp1=60, step1=1;
int result1, result2, resultecc;
#if 0
// -K 0
int start_lp4=32, end_lp4=164, step4=1;
int start_lp8=64, end_lp8=190, step8=1;
#else
// -K 30, -f 0x32, M= 0x44 FPGA - *V44 9/12/2016
// -K 30, -f 0x32, M= 0x33 FPGA - *V46 9/20/2016
// V44
int start_lp4=96, end_lp4=232, step4=1;
int start_lp8=64, end_lp8=196, step8=1;
int start_lp400=40, end_lp400=196, step400=2;
int start_lp800=0, end_lp800=148, step800=2;
// -K 30, -f 0x42, M= 0x22 FPGA - *V44-TOP 9/16/2016
// V44-TOP
//int start_lp4=32, end_lp4=32+128, step4=1;
//int start_lp8=128, end_lp8=128+128, step8=1;
#endif
int start_lp16=0, end_lp16=128, step16=2;
int start_lp32=0, end_lp32=72, step32=1;
int start_lp1600=0, end_lp1600=128, step1600=2;

#if 0
unsigned long data1 = 0x1a371a3791a391a3;
unsigned long data2 = 0xd844d8448d848d84;
#else
unsigned long data1 = 0xFFFFFFFFFFFFFFFF;
unsigned long data2 = 0x0000000000000000;
#endif
unsigned long Data, pattern1, pattern2;
unsigned long elapsedtime, elapsedtime1, elapsedtime2;

//++ _1031 10/31/2016
//
#define MMLS_READ				0x10
#define MMLS_WRITE				0x20
#define TBM_WRITE				0x80
#define TBM_READ 				0x90
#define QUERY     				0x70
#define	LRDIMM_SIZE				0x400000000		// 16GB
#define	SYS_RESERVED_MEM_SIZE	0x80000000		// 2GB
struct hv_rw_cmd_field {
	unsigned char cmd;
	unsigned char tag;
	unsigned char reserve1[2];
};

union hv_rw_cmd_field_union {
	struct hv_rw_cmd_field cmd_field;
	unsigned int cmd_field_32b;
};

struct hv_rw_cmd {
	union hv_rw_cmd_field_union command[2];
	unsigned int sector[2];
	unsigned int lba[2];
	unsigned int dram_addr[2];
	unsigned int checksum[2];
	unsigned int reserve1[6];
};

struct hv_query_cmd_field {
	unsigned char cmd;
	unsigned char query_tag;
	unsigned char cmd_tag;
	unsigned char reserve1;
};

union hv_query_cmd_field_union {
	struct hv_query_cmd_field cmd_field;
	unsigned int cmd_field_32b;
};

struct hv_query_cmd {
	union hv_query_cmd_field_union command[2];
	unsigned int reserve1[6];
	unsigned int checksum[2];
	unsigned int reserve2[6];
};

//-- _1031 10/31/2016

struct timeval start, end, start1, end1;
struct timespec start2, end2;

void *map_base, *virt_addr, *mmio_addr, *buff_addr, *map_page, *virt_page;
off_t target, targetPage=0;
//MK-begin
char *debug_str=NULL;
const char *fpga1_data1_str=NULL, *fpga1_data2_str=NULL;
const char *fpga2_data1_str=NULL, *fpga2_data2_str=NULL;
unsigned int debug_flag=1;
unsigned int fpga1_data1=0, fpga1_data2=0;
unsigned int fpga2_data1=0, fpga2_data2=0;
//MK-end

FILE *fp;

#define _mm_clflush(addr)\
asm volatile("clflush %0" : "+m" (*(volatile char *)addr));

#define _mm_clwb(addr)\
asm volatile("xsaveopt %0" : "+m" (*(volatile char *)addr));

typedef unsigned long uintptr_t;
#define FLUSH_ALIGN	64

//MK-begin
void writeInitialPatternToFPGA(int write_fpga_data, unsigned long data1, unsigned long data2);
//MK-end
void PrintTestTime()
{
	gettimeofday(&end, NULL);
	
	elapsedtime = ((end.tv_sec * 1000000 + end.tv_usec)
			     - (start.tv_sec * 1000000 + start.tv_usec));
	elapsedtime1 = ((end1.tv_sec * 1000000 + end1.tv_usec)
			     - (start1.tv_sec * 1000000 + start1.tv_usec));
	elapsedtime2 = ((end2.tv_sec * 1000000000 + end2.tv_nsec)
			     - (start2.tv_sec * 1000000000 + start2.tv_nsec));
	
	printf(" ... (%ldnS, %lduS) %ldmS", 
				elapsedtime2, elapsedtime1, elapsedtime/1000); 

}

void SetTrainingMode( int mode )
{
	WriteSMBus(FPGA1, D, TRN_MODE_ADDR, mode);
	WriteSMBus(FPGA2, D, TRN_MODE_ADDR, mode);
	usleep(FPGA_CMD_WR_DLY);

}		

void memcpy_64B_movnti(void *dst, void *src, unsigned int len)
{
	register unsigned long t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"                                     \n"
		"mov     0*8(%[src]),    %[t1]        \n"
		"mov     1*8(%[src]),    %[t2]        \n"
		"mov     2*8(%[src]),    %[t3]        \n"
		"mov     3*8(%[src]),    %[t4]        \n"
		"mov     4*8(%[src]),    %[t5]        \n"
		"mov     5*8(%[src]),    %[t6]        \n"
		"mov     6*8(%[src]),    %[t7]        \n"
		"mov     7*8(%[src]),    %[t8]        \n"
		"                                     \n"
		"1:                                   \n"
		"movnti  %[t1],         0*8(%[dst])   \n"
		"movnti  %[t2],         1*8(%[dst])   \n"
		"movnti  %[t3],         2*8(%[dst])   \n"
		"movnti  %[t4],         3*8(%[dst])   \n"
		"movnti  %[t5],         4*8(%[dst])   \n"
		"movnti  %[t6],         5*8(%[dst])   \n"
		"movnti  %[t7],         6*8(%[dst])   \n"
		"movnti  %[t8],         7*8(%[dst])   \n"
		"                                     \n"
		"addq    $64,           %[dst]        \n"
		"subl    $64,           %[len]        \n"
		"jnz     1b                           \n"
		"                                     \n"
		: [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3), [t4]"=&r"(t4),
		  [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7), [t8]"=&r"(t8),
		  [src]"+S"(src), [dst]"+D"(dst), [len]"+c"(len)
		: 
		: "cc"
	);

}

void memcpy_64B_movnti_2(void *dst, void *src, unsigned int len)
{
	register unsigned long t1, t2, t3, t4, t5, t6, t7, t8;

	len = (len + 63) & ~63;

	__asm__ __volatile__ (
		"2:                                   \n"
		"mov     0*8(%[src]),    %[t1]        \n"
		"mov     1*8(%[src]),    %[t2]        \n"
		"mov     2*8(%[src]),    %[t3]        \n"
		"mov     3*8(%[src]),    %[t4]        \n"
		"mov     4*8(%[src]),    %[t5]        \n"
		"mov     5*8(%[src]),    %[t6]        \n"
		"mov     6*8(%[src]),    %[t7]        \n"
		"mov     7*8(%[src]),    %[t8]        \n"
		"                                     \n"
		"1:                                   \n"
		"movnti  %[t1],         0*8(%[dst])   \n"
		"movnti  %[t2],         1*8(%[dst])   \n"
		"movnti  %[t3],         2*8(%[dst])   \n"
		"movnti  %[t4],         3*8(%[dst])   \n"
		"movnti  %[t5],         4*8(%[dst])   \n"
		"movnti  %[t6],         5*8(%[dst])   \n"
		"movnti  %[t7],         6*8(%[dst])   \n"
		"movnti  %[t8],         7*8(%[dst])   \n"
		"                                     \n"
		"addq    $64,           %[src]        \n"
		"addq    $64,           %[dst]        \n"
		"subl    $64,           %[len]        \n"
		"jnz     2b                           \n"
		"                                     \n"
		: [t1]"=&r"(t1), [t2]"=&r"(t2), [t3]"=&r"(t3), [t4]"=&r"(t4),
		  [t5]"=&r"(t5), [t6]"=&r"(t6), [t7]"=&r"(t7), [t8]"=&r"(t8),
		  [src]"+S"(src), [dst]"+D"(dst), [len]"+c"(len)
		: 
		: "cc"
	);

}

void clflush_cache_range(void *addr, long size)
{
	uintptr_t uptr;
	/*
	* Loop through cache-line-size (typically 64B) aligned chunks
	* covering the given range.
	*/
	for (uptr = (uintptr_t)addr & ~(FLUSH_ALIGN - 1);
			uptr < (uintptr_t)addr + size; uptr += FLUSH_ALIGN)
		_mm_clflush((char *)uptr);
}

void Set_i2c_Page( int page )
{
	WriteSMBus(FPGA1, D, 0xA0, page);	// Set Page 
	WriteSMBus(FPGA2, D, 0xA0, page);	// Set Page 				
	usleep(FPGA_SETPAGE_DLY);
	
}

void ReadMmioData(int RdWr, unsigned long mmio[8], unsigned long* ecc)
{
	int i, bl;

	if(RdWr==WR) Set_i2c_Page( 1 );
	if(RdWr==RD) Set_i2c_Page( 2 );
	
	for(*ecc=0,bl=0;bl<8;bl++) {
		mmio[bl] = 0;
		for(i=0;i<4;i++) { 
			mmio[bl] |= ((unsigned long)ReadSMBus(FPGA1, D, bl*16+i) << (i*8));
		//	usleep(FPGA_READ_DLY);
		}
	
		if(eccMode)		
			*ecc |= ((unsigned long)ReadSMBus(FPGA1, D, bl*16+4) << (bl*8));

		if(lenovo) usleep(FPGA_SETPAGE_DLY);

		for(i=0;i<4;i++) {
			mmio[bl] |= ((unsigned long)ReadSMBus(FPGA2, D, bl*16+i) << (i*8+32));
		//	usleep(FPGA_READ_DLY);
		}
	
		if(dataScramble) 
			mmio[bl] ^= DS_MMIOCmdWindow[bl];		
	}

	if((dataScramble)&&(eccMode))
		*ecc ^= DS_MMIOCmdWindow_ECC;

	Set_i2c_Page( 0 );
}

//MK-begin
void enable_bcom_switch(int mode)
{
	unsigned long mmioData[8];
	unsigned int i;

	memset(mmioData,0x1,sizeof(mmioData));

	if(mode==2) {
		printf("\n Set bit7 of Byte14 to '1'");
		WriteSMBus(FPGA1, D, 14, ReadSMBus(FPGA1,D,14) | 0x80);
		WriteSMBus(FPGA2, D, 14, ReadSMBus(FPGA2,D,14) | 0x80);
		usleep(FPGA_SETPAGE_DLY);	
	}

	printf("\n Write 64B data for BCOM_MUX_SW @ %p ...", target + BCOM_SW_OFFSET);

	mmio_addr = virt_addr + BCOM_SW_OFFSET;
#if 0
	for (i=0; i < 8; i++)
	{
		*((unsigned long *) (mmio_addr + i*8)) = 0x123456789ABCDEF0;
	}
#else
	memcpy_64B_movnti( mmio_addr, (void *)mmioData, 64 );
#endif
	
#if CLFLUSH_CACHE_RANGE
	clflush_cache_range(mmio_addr, 64);
#endif
	usleep(100);

	printf(" Done\n\n");

	exit(1);

}
//MK-end
void fpga_pll_i_o_dly_load(void)
{
	unsigned char Byte1, Byte2;

	Byte1 = ReadSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR);
	Byte2 = ReadSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR);
		
	// set PLL reset High
	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR,(Byte1 | 0x61));
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR,(Byte2 | 0x61));
	usleep(10000);
	// set PLL reset low
	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR,(Byte1 & 0x9E));
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR,(Byte2 & 0x9E));
	usleep(10000);		
}

void fpga_pll_reset_high_low(void)
{
	unsigned char Byte1, Byte2;

	Byte1 = ReadSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR);
	Byte2 = ReadSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR);
		
	// set PLL reset High
	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR,(Byte1 | 0x01));
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR,(Byte2 | 0x01));
	usleep(FPGA_LOAD_DELAY);
	// set PLL reset low
	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR,(Byte1 & 0xFE));
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR,(Byte2 & 0xFE));
	usleep(FPGA_LOAD_DELAY);
	
}		
void fpga_i_Dly_load_signal(void)
{
	unsigned char Byte1, Byte2;

	Byte1 = ReadSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR);
	Byte2 = ReadSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR);

//	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR, (Byte1 & 0xDF));	// reset bit 5
//	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR, (Byte2 & 0xDF));
//	usleep(FPGA_LOAD_DELAY);									

	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR, (Byte1 | 0x20));	// Set Bit5 '1'
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR, (Byte2 | 0x20));
	usleep(FPGA_LOAD_DELAY);	
	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR, (Byte1 & 0xDF));
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR, (Byte2 & 0xDF));
	usleep(FPGA_LOAD_DELAY);
		
}

void fpga_o_Dly_load_signal(void)	// Fake-WR output delay
{
	unsigned char Byte1, Byte2;

	Byte1 = ReadSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR);
	Byte2 = ReadSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR);

//	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR, (Byte1 & 0xBF));	// reset bit 6
//	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR, (Byte2 & 0xBF));
//	usleep(FPGA_LOAD_DELAY);									

	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR, (Byte1 | 0x40));	// Set Bit6 '1'
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR, (Byte2 | 0x40));
	usleep(FPGA_LOAD_DELAY);	
	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR, (Byte1 & 0xBF));
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR, (Byte2 & 0xBF));
	usleep(FPGA_LOAD_DELAY);
		
}

void ResetCounter()
{
	WriteSMBus(FPGA1, D, 0x0F, ReadSMBus(FPGA1, D, 0x0F) | 0x54);	// Reg0xF, bit[6,4,2] to '1'
	WriteSMBus(FPGA2, D, 0x0F, ReadSMBus(FPGA2, D, 0x0F) | 0x54);	// Reg0xF, bit[6,4,2] to '1'
	usleep(FPGA_WRITE_DLY);
	WriteSMBus(FPGA1, D, 0x0F, ReadSMBus(FPGA1, D, 0x0F) & 0xAB);	// Reg0xF, bit[6,4,2] to '0'
	WriteSMBus(FPGA2, D, 0x0F, ReadSMBus(FPGA2, D, 0x0F) & 0xAB);	// Reg0xF, bit[6,4,2] to '0'
	usleep(FPGA_WRITE_DLY);
}

void PrintCounter()
{
	fprintf(stderr,"WrCmd#: %d,%d FakeRd#: %d,%d WR#: %d,%d RD#: %d,%d",
			(ReadSMBus(FPGA1, D, 0x7B)<<8)+ReadSMBus(FPGA1, D, 0x7C), 
			(ReadSMBus(FPGA2, D, 0x7B)<<8)+ReadSMBus(FPGA2, D, 0x7C),
			(ReadSMBus(FPGA1, D, 0x7E)<<8)+ReadSMBus(FPGA1, D, 0x7F), 
			(ReadSMBus(FPGA2, D, 0x7E)<<8)+ReadSMBus(FPGA2, D, 0x7F),
			(ReadSMBus(FPGA1, D, 0x60)<<8)+ReadSMBus(FPGA1, D, 0x61), 
			(ReadSMBus(FPGA2, D, 0x60)<<8)+ReadSMBus(FPGA2, D, 0x61), 
			(ReadSMBus(FPGA1, D, 0x62)<<8)+ReadSMBus(FPGA1, D, 0x63), 
			(ReadSMBus(FPGA2, D, 0x62)<<8)+ReadSMBus(FPGA2, D, 0x63)); 
}

void MemCpy(int RdWr, unsigned long data1, unsigned long data2, unsigned int byteCnt)
{
	unsigned long pattern[8], rd[4096];
	int i;

	gettimeofday(&start1, NULL);	
	clock_gettime(CLOCK_MONOTONIC, &start2);

	memset(rd, -1, sizeof(rd));

	if(RdWr == WR) {
		memset(pattern, data1, sizeof(pattern));
		pattern[0] = data2;			// FPGA F/W 02-07.09.16, RxDQDQS1

		if(SingleDimm) {
			pattern[0] = pattern[2] = pattern[4] = pattern[6] = data2;
			pattern[1] = pattern[3] = pattern[5] = pattern[7] = data1;
		}
		if(HWrev[0]==0x00010728) 	// aurora_top_V40D ... Demo Version
		{
			pattern[3] = data2;		// FPGA F/W 01-07.28.16, RxDQDQS1
			pattern[5] = data2;		// FPGA F/W 01-07.28.16, TxDQDQS1
		}		
	}
	if(RdWr == WR_ALT) {
		pattern[0] = pattern[2] = pattern[4] = pattern[6] = data1;
		pattern[1] = pattern[3] = pattern[5] = pattern[7] = data2;
	}
	if(RdWr == WRECC) {
		memset(pattern, 0, sizeof(pattern));
		pattern[3] = 0x200;
	}
	if(dataScramble) {
		if(RdWr == WRECC) 
			pattern[3] = 0x40000;
		for (i=0; i < 8; i++) {
			pattern[i] ^= DS_TrainingWindow[i];
		}
	}	

	if(RdWr==RD)
		memcpy_64B_movnti( (void *)rd, virt_addr,  byteCnt );
	//	FIXME
	//	memcpy( (void *)rd, virt_addr,  byteCnt );
	else
		memcpy_64B_movnti( virt_addr, (void *)pattern, byteCnt );

#if CLFLUSH_CACHE_RANGE
	clflush_cache_range(virt_addr, byteCnt);
#endif	

	gettimeofday(&end1, NULL);
	clock_gettime(CLOCK_MONOTONIC, &end2);

}

/*
 * per JS, find the falling edge first and then rising 
 */
void FindCenterPoint(int test, int perBit, int start_lp, int end_lp, int step, int DQSoffset)
{
	int i, j, strobe, foundzero, DQ, DQmax, BITMASK;
	int longestzero=0, tmpfeDQS;
	int Cp1, Cp2, Cp3, i1=0, i2=0, i3=0;

	if(logging) {
		if(test==TX_DQ_DQS_1) fprintf(fp, "TxDQDQS1:");
		if(test==RX_DQ_DQS_1) fprintf(fp, "RxDQDQS1:");
		if(test==TX_DQ_DQS_2) fprintf(fp, "TxDQDQS2:");
	}

	if (perBit) DQmax = 4;
	else		DQmax = 1;

	CPmax = REmax = FEmax = PWmax = 0;
	CPmin = REmin = FEmin = PWmin = 256;
	
	for(DQ=0;DQ<DQmax;DQ++) 
	{
		BITMASK = 1 << DQ;

		for(strobe=0;strobe<16+eccMode*2;strobe++) {
			RE[strobe*4 + DQ] = 0;
			longestzero = 0;
			foundzero = 0;
			/* find the start of longest continuous zero as falling point */
			for(i=start_lp;i<end_lp;i=i+step) {
			//	printf("\ni=%3d rDQS=%x", i, rDQS[i][strobe]&0x1);
			//	if(rDQS[i][strobe]<0) continue;
				if((rDQS[i][strobe] & BITMASK)==0) {
					if (!foundzero)
						tmpfeDQS = i;
					foundzero++;
				} else {
					if (foundzero > longestzero) {
						FE[strobe*4 + DQ] = tmpfeDQS;
						longestzero = foundzero;	
					}
					foundzero = 0;
				}
			}

	//MK At this point, for the current strobe (column), we have found the start
	//MK index of the array (rDQS[]) of the longest consecutive zeros and saved it
	//MK in feDQS[strobe]. Also, saved the number of zeros in that group in
	//MK longestzero.

			/* find consecutive three '1' as rising point */
			for(i=FE[strobe*4 + DQ];i<end_lp;i=i+step) {
			//	printf("\n bitmask=%x i=%3d feDQS=%x", BITMASK, i, rDQS[i][strobe]);	
			//	if(rDQS[i][strobe]<0) continue;
				if((rDQS[i][strobe] & BITMASK) && 
			   	   (rDQS[i+step][strobe] & BITMASK) //&& 
		       	 //  (rDQS[i+2*step][strobe] & BITMASK) 
				  ) {
					RE[strobe*4 + DQ] = i - 0;
					break;
				}
			}

			/* if rising point is not found, set it to 255 */
			if (RE[strobe*4 + DQ] < FE[strobe*4 + DQ])
				RE[strobe*4 + DQ] = end_lp;

		//	printf("\n RE=%d FE=%d", RE[strobe*4 + DQ], FE[strobe*4 + DQ]);

			PW[strobe*4 + DQ] = RE[strobe*4 + DQ] - FE[strobe*4 + DQ];
			CP[strobe*4 + DQ] = (FE[strobe*4 + DQ] + RE[strobe*4 + DQ])/2;

			if (DQSoffset) {
				CP[strobe*4 + DQ] = CP[strobe*4 + DQ] + FEDQS[strobe] - DQSoffset;
			}
#if 1	
			if(FE[strobe*4 + DQ] < FEmin ) FEmin = FE[strobe*4 + DQ];
			if(FE[strobe*4 + DQ] > FEmax ) FEmax = FE[strobe*4 + DQ];
			if(RE[strobe*4 + DQ] < REmin ) REmin = RE[strobe*4 + DQ];
			if(RE[strobe*4 + DQ] > REmax ) REmax = RE[strobe*4 + DQ];
			if(CP[strobe*4 + DQ] < CPmin ) CPmin = CP[strobe*4 + DQ];
			if(CP[strobe*4 + DQ] > CPmax ) CPmax = CP[strobe*4 + DQ];
			if(PW[strobe*4 + DQ] < PWmin ) PWmin = PW[strobe*4 + DQ];
			if(PW[strobe*4 + DQ] > PWmax ) PWmax = PW[strobe*4 + DQ];
#endif
	//		FE[strobe*4 + DQ] = feDQS[strobe];
	//		RE[strobe*4 + DQ] = reDQS[strobe];
	//		CP[strobe*4 + DQ] = cpDQS[strobe];
	//		PW[strobe*4 + DQ] = lwDQS[strobe];
		}		

		if(DQ==0) {
			printf("\n\n\033[1;38m  DQ:"); 
			for (j=0; j < 16+eccMode*2; j++) printf("  %2d-", j*4+DQ);
		 	printf("\033[0m");
		} else {
		 	printf("\n");
		}
#if SHOW_RESULT_FE_RE
	if(debug) {
		printf("\n  FE:"); for (j=0; j < 16+eccMode*2; j++) printf("%5d", FE[j*4+DQ]);
		printf("\n  RE:"); for (j=0; j < 16+eccMode*2; j++) printf("%5d", RE[j*4+DQ]);
	}
#endif
		printf("\n  PW:");
		for (j=0; j < 16+eccMode*2; j++)
			printf(" \033[%d;%dm%4d\033[0m",
							PW[j*4+DQ]<=colorlimit?1:0,	// 1: Bold, 5: Blink, 7: reverse
							PW[j*4+DQ]<=colorlimit?31:38,PW[j*4+DQ]);
	//	printf("  [%3d - %3d]", PWmax, PWmin);

		printf("\n  CP:");
		
		for (j=0; j < 16 + eccMode*2; j++) {
			printf("%5d", CP[j*4 + DQ]);	
			if(logging)
				fprintf(fp, "%5d", CP[j*4 + DQ]);
		}
	}

	int cpavg[18];
	if((test==TX_DQ_DQS_1)||(test==RX_DQ_DQS_1)) { 
		printf("\n\n  cp:");
		for (i=0; i < 16 + eccMode*2; i++) {
			cpavg[i] = (int)((CP[i*4] + CP[i*4+1] + CP[i*4+2]+ CP[i*4+3])/4);
			printf("%5d",cpavg[i]);
		}
		for (j=0; j < 4; j++) {
			printf("\n  %d :",j);
			for (i=0; i < 16 + eccMode*2; i++) {
				printf("%5d",CP[i*4+j] - cpavg[i]);
			}
		}
	}

	Cp1 = Cp2 = Cp3 = result1 = result2 = resultecc = 0;

	if(test==TX_DQ_DQS_1) { 
		for (i=0; i < 32; i++)  {Cp1 += CP[i]; if(CP[i]) i1++;}
		for (i=32; i < 64; i++) {Cp2 += CP[i]; if(CP[i]) i2++;} 
		for (i=64; i < 72; i++) {Cp3 += CP[i]; if(CP[i]) i3++;} 

		if(i1) Cp1 = (int)(Cp1/i1);
		if(i2) Cp2 = (int)(Cp2/i2);
		if(i3) Cp3 = (int)(Cp3/i3);

		printf("\n\n CenterPoint: %d, %d   Max/Min: %d, %d ", Cp1, Cp2, CPmax, CPmin);


		for (j=0; j < 32; j++) {
			if(PW[j] <= colorlimit || CP[j] < (Cp1-50) || CP[j] > (Cp1+50)) result1 |= 1<<j; 
		}		
		for (j=32; j < 64; j++) {
			if(PW[j] <= colorlimit || CP[j] < (Cp2-50) || CP[j] > (Cp2+50)) result2 |= 1<<j; 
		}
		for (j=64; j < 72; j++) {
			if((eccMode)&&(PW[j] <= colorlimit || CP[j] < (Cp3-50) || CP[j] > (Cp3+50))) resultecc |= 1<<j; 
		}	

		if(result1||result2||resultecc) {
			printf("\n Failed DQs:");
			for (j=0; j < 32; j++) {
				if(PW[j] <= colorlimit || CP[j] < (Cp1-50) || CP[j] > (Cp1+50)) printf(" %2d",j); 
			}		
			for (j=32; j < 64; j++) {
				if(PW[j] <= colorlimit || CP[j] < (Cp2-50) || CP[j] > (Cp2+50)) printf(" %2d",j); 
			}
			for (j=64; j < 72; j++) {
				if((eccMode)&&(PW[j] <= colorlimit || CP[j] < (Cp3-50) || CP[j] > (Cp3+50))) printf(" %2d",j); 
			}	
		}
	}
//	printf(" - 0x%08X 0x%08X",result1,result2);
//	if(eccMode) printf("ECC: %02X",resultecc);


	if (update_hw) { 
		for (j=0; j < 64 + eccMode*8; j++) {
			if(test==TX_DQ_DQS_1) {
				WriteSMBus( (i2cTXDQS1p[j]>>12), D, i2cTXDQS1p[j]&0xFF, CP[j]);
			//	printf("  (%d) %d", j, CP[j]);
			}
			if(test==RX_DQ_DQS_1) WriteSMBus( (i2cRXDQS1p[j]>>12), D, i2cRXDQS1p[j]&0xFF, CP[j]);
			usleep(FPGA_WRITE_DLY);
		//	 printf("\n dq=%d dti= %d, addr= %d data= %d", j, (i2cTXDQS1p[j]>>12), i2cTXDQS1p[j]&0xFF, CP[j]);
		}
#if LOAD_IO_DELAY
		fpga_i_Dly_load_signal();	
#endif
	}

	Set_i2c_Page( 0 );

	if(showsummary) {	
		printf("\n");
		if(!DQSoffset) {	
			int k, cp, BITMASK;
			BITMASK = ~(step - 1);
	//		printf("\n %x:",BITMASK);	
			for(i=-48;i<48;i=i+step) {
				printf("\n %3d:",i);	
				for (j=0; j < 8 + eccMode; j++) {
					for(k=0;k<8;k++) {
						if(k%4==0) printf(" ");
						cp = CP[j*8+k] & BITMASK;
						if((cp+i)<0) printf(" ");
						else		 printf("\033[0;%dm%c\033[0m",
										(i==0)?31:38,((rDQ[cp+i][j]>>k)&0x1)?' ':'0');
					}
				}	
			}
	  	}

		printf("\n\n  DQ:"); for (j=0; j < 64; j++) printf("%5d", j);
#if SHOW_RESULT_FE_RE
	if(debug) {
		printf("\n  FE:");	for (j=0; j < 64; j++)	printf("%5d", FE[j]);
		printf("\n  RE:");	for (j=0; j < 64; j++)	printf("%5d", RE[j]);
	}
#endif
		printf("\n  PW:");	for (j=0; j < 64; j++)	printf("%5d", PW[j]);
		printf("\n  CP:");	for (j=0; j < 64; j++)	printf("%5d", CP[j]);
	}	

}

void FindCenterPoint2(int test, int perBit, int start_lp, int end_lp, int step)
{
	int i, j, strobe, zeroWidth, zeroStart, DQ, DQmax, BITMASK;
	int longestzero=0, tmpfeDQS=0;

	if(logging) {
		if(test==TX_DQ_DQS_2) fprintf(fp, "TxDQDQS2:");
	}

	DQmax = 1;

	CPmax = REmax = FEmax = PWmax = 0;
	CPmin = REmin = FEmin = PWmin = 256;

	DQ=0;

//	for(DQ=0;DQ<DQmax;DQ++) 
	{
		BITMASK = 0xF;

		for(strobe=0;strobe<16+eccMode*2;strobe++) {
	//	for(strobe=10;strobe<11;strobe++) {
			RE[strobe] = longestzero =  zeroWidth = zeroStart = 0;
			/* find the start of longest continuous zero as falling point */
			for(i=start_lp;i<end_lp;i=i+step) {
				if(rDQS[i][strobe]==0) {
					if (!zeroWidth)
						zeroStart = i;
					zeroWidth++;
				} else {
					if (zeroWidth > longestzero) {
						FE[strobe] = zeroStart;
						longestzero = zeroWidth;	
					}
					zeroWidth = 0;
				}
		//		printf("\n DQS%d %3d: [%X]  zeroStart=%2d zeroWidth=%2d longestzero=%2d", 
		//					strobe, i, rDQS[i][strobe], zeroStart, zeroWidth, longestzero);
			}

			if((longestzero==0)||(zeroWidth > longestzero)) FE[strobe] = zeroStart;

			/* find consecutive three '1' as rising point */
			for(i=FE[strobe];i<end_lp;i=i+step) {
				if(rDQS[i][strobe]) {
					RE[strobe] = i - step;
					break;
				}
			}

			if (RE[strobe] <= FE[strobe]) RE[strobe] = end_lp;

			PW[strobe] = RE[strobe] - FE[strobe];
			CP[strobe] = (FE[strobe] + RE[strobe])/2;
		}		

		printf("\n\n  DQ:"); for (j=0; j < 16+eccMode*2; j++) printf("%5d", j*4);

		printf("\n  FE:"); for (j=0; j < 16+eccMode*2; j++) printf("%5d", FE[j]);
		printf("\n  RE:"); for (j=0; j < 16+eccMode*2; j++) printf("%5d", RE[j]);

		printf("\n  PW:");
		for (j=0; j < 16+eccMode*2; j++)
			printf(" \033[%d;%dm%4d\033[0m",
							PW[j]<=colorlimit?1:0,	// 1: Bold, 5: Blink, 7: reverse
							PW[j]<=colorlimit?31:38,PW[j]);
	//	printf("  [%3d - %3d]", PWmax, PWmin);

		printf("\n  CP:");
		Set_i2c_Page( 1 );
		for (j=0; j < 16 + eccMode*2; j++) {
			printf("%5d", CP[j]);	
			if(logging)
				fprintf(fp, "%5d", CP[j]);
			if (update_hw) { 
				WriteSMBus( (i2cTXDQS2[j]>>12), D, i2cTXDQS2[j]&0xFF, CP[j]);
				usleep(FPGA_WRITE_DLY);
			}
		}
		Set_i2c_Page( 0 );

	}

	Set_i2c_Page( 0 );

}

void Find_1st_DQS_RE(int test, int start_lp, int end_lp, int step)
{
	int i, j, grp, foundzero, DQ, BITMASK;
	int longestzero=0, tmpfeDQS;

	for(grp=0;grp<2+eccMode;grp++) 
	{
		for(DQ=0;DQ<8;DQ++) { 
			BITMASK = 1 << DQ;
			REDQS[grp*8 + DQ] = 0;
			longestzero = 0;
			foundzero = 0;
			/* find the start of longest continuous zero as falling point */
			for(i=start_lp;i<end_lp;i=i+step) {
				if((tDQS[i][grp] & BITMASK)==0) {
					if (!foundzero)
						tmpfeDQS = i;
					foundzero++;
				} else {
					if (foundzero > longestzero) {
						FEDQS[grp*8 + DQ] = tmpfeDQS;
						longestzero = foundzero;	
					}
					foundzero = 0;
				}
			}

			/* find consecutive three '1' as rising point */
			for(i=FEDQS[grp*8 + DQ];i<end_lp;i=i+step) {
				if((tDQS[i][grp] & BITMASK) && 
			   	(tDQS[i+step][grp] & BITMASK) && 
		       	(tDQS[i+2*step][grp] & BITMASK) ) {
					REDQS[grp*8 + DQ] = i - step;
					break;
				}
			}

			/* if rising point is not found, set it to 255 */
			if (REDQS[grp*8 + DQ] < FEDQS[grp*8 + DQ])
				REDQS[grp*8 + DQ] = end_lp;

			CPDQS[grp*8 + DQ] = (FEDQS[grp*8 + DQ] + REDQS[grp*8 + DQ])/2;
		//	FEDQS[grp*8 + DQ] = feDQS[grp];
		//	REDQS[grp*8 + DQ] = reDQS[grp];
		}		

	}

	printf("\n\n DQS:"); for (j=0; j < 16+eccMode*2; j++) printf("%5d", j);
#if SHOW_RESULT_FE_RE
	if(debug) {
	printf("\n *FE:"); for (j=0; j < 16+eccMode*2; j++) printf("%5d", FEDQS[j]);
	printf("\n  RE:"); for (j=0; j < 16+eccMode*2; j++) printf("%5d", REDQS[j]);
	}
#endif
	printf("\n  CP:"); for (j=0; j < 16+eccMode*2; j++) printf("%5d", CPDQS[j]);	

}

/*
 * per JS, find the falling edge first and then rising 
 */
void FindCMDCenterPoint(int *cs0, int *reCS0, int *feCS0, int *cpCS0)
{
	int i, found, pfeCS0, longestzero, tmpfeCS0;

	*reCS0 = *feCS0 = found = longestzero = 0;

	/* find the start of longest continuous zero as falling point */
	for(i=start_lp; i<end_lp; i++) {
		if(cs0[i] == 0) {
			if (!found)
				tmpfeCS0 = i;
			found++;
		}
		else {
			if (found > longestzero) {
				*feCS0 = tmpfeCS0;
				longestzero = found;	
			}
			found = 0;
		}
	}

	/* find consecutive three '1' as rising point */
	for(i=*feCS0;i<256;i=i+1) {
		if(cs0[i] == 1 && cs0[i+1] == 1 && cs0[i+2] == 1 ) {
			*reCS0 = i;
			break;
		}
	}
	
	/* if rising point is not found, set it to 255 */
	if (*feCS0 > *reCS0)
		*reCS0 = 255;	

	*cpCS0 = (*feCS0 + *reCS0)/2;

}

void FindCMDCenterPoint2(int *cs0a, int *cs0b, int start_lp, int end_lp, int step)
{
	int i, founda, foundb;
	int REa, REb, FEa, FEb, CPa, CPb;

	REa = REb = founda = foundb = 0;

	for(i=start_lp; i<end_lp; i=i+step) {
		if((founda == 0)&&(cs0a[i] == 0)) {
			FEa = i;
			founda++;
		}
		if((foundb == 0)&&(cs0b[i] == 0)) {
			FEb = i;
			foundb++;
		}		
	}
	
	CPa = (FEa + REa)/2;
	CPb = (FEb + REb)/2;

	printf("\n\n  FE: %4d, %4d",FEa, FEb);
	printf("\n  CP: %4d, %4d", CPa, CPb);

	if (update_hw) {
		WriteSMBus(FPGA1,D,CMD_CK_ADDR,CPa);
#if BOTH_FPGA
		WriteSMBus(FPGA2,D,CMD_CK_ADDR,CPb);
#endif		
		usleep(FPGA_WRITE_DLY);
		fpga_pll_reset_high_low();
	}
		
}

void PrintBinary(unsigned char D)
{
	int i=8;
	printf(" ");	
	while(i) {
		if(D&0x80)  printf(" 1");
		else		printf(" 0");
		D <<=1;
		i--;
	}
}

void ReadDQSResultFromFPGA(int training, int i)
{
	unsigned char dqByte;
	int j;

	for (j=0; j < 8 + eccMode; j++) {
		dqByte = ReadSMBus((i2cDQRESULT[j]>>12), D, i2cDQRESULT[j]&0xFF);
		usleep(FPGA_READ_DLY);
	}		
	
	for (j=0; j < 2 + eccMode; j++) {
		tDQS[i][j] = ReadSMBus((i2cDQSRESULT[j]>>12), D, i2cDQSRESULT[j]&0xFF);
		usleep(FPGA_READ_DLY);

		if(j==2)
			printf(" %c%c", 
					((tDQS[i][j]>>0)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
					((tDQS[i][j]>>1)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO);
		else
			printf(" %c%c%c%c%c%c%c%c", 
					((tDQS[i][j]>>0)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
					((tDQS[i][j]>>1)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
					((tDQS[i][j]>>2)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
					((tDQS[i][j]>>3)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
					((tDQS[i][j]>>4)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
					((tDQS[i][j]>>5)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
					((tDQS[i][j]>>6)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
					((tDQS[i][j]>>7)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO);
	}		
		 
	if(!eccMode) printf("%2s"," ");		
	
}

void ReadDQResultFromFPGA(int training, int i, int perBit, int BitMask, int Vref)
{
	unsigned char dqByte;
	int j, bit, result, sum=0;

	for (j=0; j < 8; j++) {
		dqByte = ReadSMBus((i2cDQRESULT[j]>>12), D, i2cDQRESULT[j]&0xFF);
		rDQS[i][2*j] = dqByte & BitMask;
		rDQS[i][2*j+1] = (dqByte >> 4 ) & BitMask;
		rDQ[i][j] = dqByte;
		usleep(FPGA_READ_DLY);
	}		
#if 1
	if((training==TX_DQ_DQS_1) && (SingleDimm)) {
		Set_i2c_Page( 1 );
		for (j=0; j < 8; j++) {
			dqByte = ReadSMBus((i2cMMioData[j]>>12), D, i2cMMioData[j]&0xFF);
			rDQS[i][2*j] = dqByte & BitMask;
			rDQS[i][2*j+1] = (dqByte >> 4 ) & BitMask;
			rDQ[i][j] = dqByte;
			usleep(FPGA_READ_DLY);
		}
		Set_i2c_Page( 0 );
	}
#endif
	if((training==RX_DQ_DQS_1) && (SingleDimm)) {
		Set_i2c_Page( 2 );
		for (j=0; j < 8; j++) {
			dqByte = ReadSMBus((i2cMMioData[j]>>12), D, (i2cMMioData[j]&0xFF)+0);
			rDQS[i][2*j] = dqByte & BitMask;
			rDQS[i][2*j+1] = (dqByte >> 4 ) & BitMask;
			rDQ[i][j] = dqByte;
			usleep(FPGA_READ_DLY);
		//	printf("\n %d %d %d %2x",(i2cMMioData[j]>>12), D, (i2cMMioData[j]&0xFF)+0,dqByte);
		}
		Set_i2c_Page( 0 );
	}

	VR[i] = 0;

	if(perBit) {
		for (j=0; j < 8; j++) {
			for(bit=0; bit<8; bit++) {
				if(!(Vref) && (bit%4==0)) printf(" ");
				result = (rDQ[i][j]>>bit)&0x1;
				VR[i] += result;
				if(!Vref) printf("%c",result?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO);
			}
		}		
	} else {		
		for (j=0; j < 8; j++) {
			if(!Vref) printf(" %c    %c   ", 
							((rDQ[i][j]>>0)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
							((rDQ[i][j]>>4)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO);
		}
	}
	
//	if((Vref)&&(eccMode)) fprintf(stdout," %3d", VR[i]);
	if(Vref) {
		if((VR[i]==0)||(VR[i]>31)) printf("   %c", VR[i]?' ':'P');
		else		 	  		   printf(" %3d", VR[i]);
	}
}

void ReadECCResultFromFPGA(int training, int i, int perBit, int BitMask, int Vref)
{
	unsigned char dqByte;
	int j, bit, result, sum=0;

	
	dqByte = ReadSMBus((i2cDQRESULT[8]>>12), D, i2cDQRESULT[8]&0xFF);
	rDQS[i][16] = dqByte & BitMask;
	rDQS[i][17] = (dqByte >> 4 ) & BitMask;

	dqByte = ( dqByte & 0xFB ) | ((dqByte << 1) & 0x4 ); //set CB2 data to CB1;
	rDQ[i][8] = dqByte;
			
	if(perBit) {
		for(bit=0; bit<8; bit++) {
			if(!(Vref) && (bit%4==0)) printf(" ");
			result = (rDQ[i][8]>>bit)&0x1;
			VR[i] += result;
			if(!Vref) printf("%c",result?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO);
		}
	} else {		
		if(!Vref) printf(" %c    %c   ", 
						((rDQ[i][8]>>0)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
						((rDQ[i][8]>>4)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO);
	}
	
	if(Vref) fprintf(stdout," %3d", VR[i]);
}

void Set_fpga_delay(int training, int delay, int toffset, int from)
{
	int i, value;

	if((training == TX_DQ_DQS_1)||(training == RX_DQ_DQS_1)) {
		for (i=0; i < 64 + 8*eccMode; i++) {
			if(toffset) {
				if(from==1) value = FEDQS[(int)(i/4)] - toffset + delay;
				if(from==2) value = CP[i] - toffset + delay;
			}
			else        value = delay;
			if(value<0) value = 0;
			if(training == TX_DQ_DQS_1)
				WriteSMBus( (i2cTXDQS1p[i]>>12), D, i2cTXDQS1p[i]&0xFF, value);
			if(training == RX_DQ_DQS_1)
				WriteSMBus( (i2cRXDQS1p[i]>>12), D, i2cRXDQS1p[i]&0xFF, value);
			usleep(FPGA_WRITE_DLY);
		}
#if LOAD_IO_DELAY
		fpga_i_Dly_load_signal();
#endif
	}		

	if((training == TX_DQS_1)||(training == RX_DQS_1)) {
		Set_i2c_Page( 1 );
		for (i=0; i < 16 + 2*eccMode; i++) {
			if(training == TX_DQS_1)	
				WriteSMBus( (i2cWrDQS[i]>>12), D, i2cWrDQS[i]&0xFF, delay);
			if(training == RX_DQS_1)	
				WriteSMBus( (i2cRdDQS[i]>>12), D, i2cRdDQS[i]&0xFF, delay);
			usleep(FPGA_WRITE_DLY);
		}
		Set_i2c_Page( 0 );			
#if LOAD_IO_DELAY
		fpga_i_Dly_load_signal();
#endif
	}

	if(training == TX_DQ_DQS_2) {
		Set_i2c_Page( 1 );
#if PER_BIT_TX_DQDQS2
		for (i=0; i < 64 + 8*eccMode; i++) {
			WriteSMBus( (i2cTXDQS2p[i]>>12), D, i2cTXDQS2p[i]&0xFF, delay);
			usleep(FPGA_WRITE_DLY);
		}
#else				
		for (i=0; i < 16 + 2*eccMode; i++) {
			WriteSMBus( (i2cTXDQS2[i]>>12), D, i2cTXDQS2[i]&0xFF, delay);
			usleep(FPGA_WRITE_DLY);
		} 
#endif
		Set_i2c_Page( 0 );			
#if LOAD_IO_DELAY
		fpga_o_Dly_load_signal();
#endif
	}	
}		

//MK-end
int Check_fpga_status(int dti, int bitmask)
{
	uint64_t start;
	unsigned char Data;

	start = getTickCount();

	do {
		usleep(10);
		Data = ReadSMBus(dti, D, TRN_STATUS);
		if(Data & bitmask)
			break;
	} while(elapsedTime(start) < FPGA_TIMEOUT);

	if(dti==5) fpga1_status = Data;
	else	   fpga2_status = Data;	

	return Data;
}

void CMDCK(int cmd, int start_lp, int end_lp, int step)
{
	int i, cs0a[256], cs0b[256];		// for CMD-CK training
	unsigned char rddata;
//FIXME
//	if(cmd == RD) {
//		MemCpy(WR, data1, data2, 4096);	// first prefill the 4k data pattern
//	}		
	printf("\n\n %s: CMD_CK %s Training, start= %d, end= %d\n",
					  __func__ , cmd==RD?"RD":"WR", start_lp, end_lp);

	SetTrainingMode( CMD_CK_MODE );

	printf("\n Cnt:  7 6 5 4 3 2 1 0 dec -----  7 6 5 4 3 2 1 0 dec -----");
	for (i=start_lp; i<end_lp; i=i+step) {

		gettimeofday(&start, NULL);	
		printf("\n %3d:",i);	

		// Step 2. set clk delay time to 0
		if( (Check_fpga_status(FPGA1,TRN_MODE)==TRN_MODE)
#if BOTH_FPGA						
		 && (Check_fpga_status(FPGA2,TRN_MODE)==TRN_MODE)
#endif
		) {
			WriteSMBus(FPGA1,D,CMD_CK_ADDR,i);			// 88
			WriteSMBus(FPGA2,D,CMD_CK_ADDR,i);
			usleep(FPGA_CMD_WR_DLY);
		} else {
			printf("CMDCK:: test mode(A[103] bit0 is 0)! %#.8x, %#.8x\n", 
							fpga1_status, fpga2_status);
			exit (-1);
		}

		fpga_pll_reset_high_low();

		MemCpy(cmd, data1, data2, 4096);

		// Step 5. set FPGA Reset to Low
	//	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR, (Byte1 & 0xFE));	// 8/3/2016
	//	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR, (Byte2 & 0xFE));	// 8/3/2016
	//	usleep(30000);

		// Step 6. read cs0 status from FPGA-1
		if( (Check_fpga_status(FPGA1,CAPTURE_DN)==CAPTURE_DN)
#if BOTH_FPGA						
		 && (Check_fpga_status(FPGA2,CAPTURE_DN)==CAPTURE_DN)
#endif					
		) {
			rddata = ReadSMBus(FPGA1,D,TRN_RESULTS_CMD) & 0x0ff;
			PrintBinary(rddata);
			if((rddata>65)||(rddata<64))cs0a[i] = 0;
			else						cs0a[i] = 1;
			printf(" %3d [ %1d ]", rddata, cs0a[i]);

			rddata = ReadSMBus(FPGA2,D,TRN_RESULTS_CMD) & 0x0ff;
			PrintBinary(rddata);
			if((rddata>65)||(rddata<64))cs0b[i] = 0;
			else						cs0b[i] = 1;
			printf(" %3d [ %1d ]", rddata, cs0b[i]);

		} else {
			printf("CMDCK:: I2C error status = %#.8x, %#.8x\n", 
							fpga1_status, fpga2_status);
			exit (-1);
		}	

		if(TestTime) PrintTestTime(); 
		if(ShowCounter) PrintCounter(); 
	} // Step 7.

	// Step 8. find middle passing point
	FindCMDCenterPoint2(cs0a, cs0b, start_lp, end_lp, step);

	/* Set FPGA back to NORMAL mode per jsung's request */
	SetTrainingMode( NORMAL_MODE );

}

void CMDCKv0(int cmd, int start_lp, int end_lp, int step)
{
	int i, reCS0a, feCS0a, cpCS0a, reCS0b, feCS0b, cpCS0b;
	int cs0a[256], cs0b[256];		// for CMD-CK training

	if(cmd == RD) {
		MemCpy(WR, data1, data2, 4096);	// first prefill the 4k data pattern
	}		
	printf("\n %s: CMD_CK Training, CMD= %s, Data pattern= 0x%lx\n",
					  __func__ , cmd==RD?"RD":"WR",data1);
	printf("\nloop: FPGA_1 FPGA_2");
	// Ste 1. set training mode
	SetTrainingMode( CMD_CK_MODE );

	for (i=start_lp; i<end_lp; i=i+step) {

		gettimeofday(&start, NULL);	
		printf("\n %3d:",i);	

		// Step 2. set clk delay time to 0
		if( (Check_fpga_status(FPGA1,TRN_MODE)==TRN_MODE)
#if BOTH_FPGA						
		 && (Check_fpga_status(FPGA2,TRN_MODE)==TRN_MODE)
#endif
		) {
			WriteSMBus(FPGA1,D,CMD_CK_ADDR,i);			// 88
			WriteSMBus(FPGA2,D,CMD_CK_ADDR,i);
			usleep(FPGA_CMD_WR_DLY);
		} else {
			printf("CMDCK:: test mode(A[103] bit0 is 0)! %#.8x, %#.8x\n", 
							fpga1_status, fpga2_status);
			exit (-1);
		}

		fpga_pll_reset_high_low();

		MemCpy(cmd, data1, data2, 64*64);

		// Step 5. set FPGA Reset to Low
	//	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR, (Byte1 & 0xFE));	// 8/3/2016
	//	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR, (Byte2 & 0xFE));	// 8/3/2016
	//	usleep(30000);

		// Step 6. read cs0 status from FPGA-1
		if( (Check_fpga_status(FPGA1,CAPTURE_DN)==CAPTURE_DN)
#if BOTH_FPGA						
		 && (Check_fpga_status(FPGA2,CAPTURE_DN)==CAPTURE_DN)
#endif					
		) {
			cs0a[i] = ReadSMBus(FPGA1,D,TRN_RESULTS_CMD) & 0x0ff;

			PrintBinary((unsigned char)cs0a[i]);
			printf(" %6d", cs0a[i]);
			cs0a[i] &= RD_CMD_CK_MASK;
			printf("[%3d]", cs0a[i]);

			cs0b[i] = ReadSMBus(FPGA2,D,TRN_RESULTS_CMD) & 0x0ff;

			PrintBinary((unsigned char)cs0b[i]);
            printf(" %6d", cs0b[i]);
			cs0b[i] &= RD_CMD_CK_MASK;
            printf("[%3d]", cs0b[i]);

		} else {
			printf("CMDCK:: I2C error status = %#.8x, %#.8x\n", 
							fpga1_status, fpga2_status);
			exit (-1);
		}	

		if(TestTime) PrintTestTime(); 
		if(ShowCounter) PrintCounter(); 
	} // Step 7.

	// Step 8. find middle passing point
	FindCMDCenterPoint(cs0a, &reCS0a, &feCS0a, &cpCS0a);
#if BOTH_FPGA
	FindCMDCenterPoint(cs0b, &reCS0b, &feCS0b, &cpCS0b);
#endif
	printf("\n\n  FE: %4d",feCS0a);
	printf("\n  RE: %4d",reCS0a);
	printf("\n  CP: %4d",cpCS0a);
	if(logging) fprintf(fp, "CMDCKa:%5d",cpCS0a);
#if BOTH_FPGA
	printf("\n\n  FE: %4d",feCS0b);
	printf("\n  RE: %4d",reCS0b);
	printf("\n  CP: %4d",cpCS0b);
	if(logging) fprintf(fp, "\tCMDCKb:%5d",cpCS0b);
#endif
	// write cp to CMD_CK register
	if (update_hw) {
		WriteSMBus(FPGA1,D,CMD_CK_ADDR,cpCS0a);
#if BOTH_FPGA
		WriteSMBus(FPGA2,D,CMD_CK_ADDR,cpCS0b);
#endif		
		usleep(FPGA_WRITE_DLY);
		fpga_pll_reset_high_low();
	}

	SetTrainingMode( NORMAL_MODE );
}

void TxDQDQS1vREF(int start_lp, int end_lp, int step, int toffset)
{
	int i, j, perBit, data;

	SetTrainingMode( TX_DQDQS_1_MODE );

	for (i=start_lp; i < end_lp; i=i+step) 
	{
		gettimeofday(&start, NULL);	
	//	printf("\n %3d:",i - toffset);	

		if( (Check_fpga_status(FPGA1,TRN_MODE)==TRN_MODE)
#if BOTH_FPGA						
		 && (Check_fpga_status(FPGA2,TRN_MODE)==TRN_MODE)
#endif
		 ) {
			perBit = 1;

			Set_fpga_delay(TX_DQ_DQS_1, i, toffset, 2);

		} else {
			printf("\n\tTxVREF:: test mode (A[103] bit0 is 0)! %#.8x, %#.8x\n", 
								fpga1_status, fpga2_status);
			exit (-1);
		}

		MemCpy(WR, data1, data2, TX_DQDQS1_DATA_SIZE);
		usleep(MemDelay);

	//	printf("(%2d)",i);	
		ReadDQResultFromFPGA(TX_DQ_DQS_1, i, perBit, 0xF, 1);
	
		if(TestTime) PrintTestTime(); 
		if(ShowCounter) PrintCounter(); 
	}

//	FindCenterPoint(TX_DQ_DQS_1, perBit, start_lp, end_lp, step, toffset);
	
	SetTrainingMode( NORMAL_MODE );
}

void TxVREF(int start_lp, int end_lp, int step)
{
	int i, j, k, v[18], toffset;

	printf("\n\n %s: WR DB-to-FPGA Training, Data=%#.16lx & %#.16lx\n",
					__func__ , data1, data2);

	// Save TxDQDQS timing
	for (j=0; j < 64 + 8*eccMode; j++) {
		CP[j] = ReadSMBus( (i2cTXDQS1p[j]>>12), D, i2cTXDQS1p[j]&0xFF);
	//	printf(" %d", CP[j]);
		usleep(FPGA_READ_DLY);
	}
	// Save TxVref Voltage
	Set_i2c_Page( 1 );
	for(i=0;i<16+eccMode*2;i++) {
		v[i] = ReadSMBus( (i2cvRefDQS[i]>>12), D, i2cvRefDQS[i]&0xFF);
		usleep(FPGA_READ_DLY);				
	}
	Set_i2c_Page( 0 );

	toffset = 50;
	for (i=end_lp; i > start_lp; i=i-step) {
		printf("\n %3d(0x%02X): ",i, i);	

		Set_i2c_Page( 1 );
		for (j=0; j < 16 + 2*eccMode; j++) {
			WriteSMBus( (i2cvRefDQS[j]>>12), D, i2cvRefDQS[j]&0xFF, i);
			usleep(FPGA_WRITE_DLY);
		}
		Set_i2c_Page( 0 );

		TxDQDQS1vREF(0, 100, 4, toffset);

		for(k=0;k<100;k=k+4) {
			if(!VR[k]) {VRmin = k; break;}
		}
		for(k=VRmin;k<100;k=k+4) {
			if(VR[k]) {VRmax = k; break;}
		}

		printf(" : %2d(%2d/%2d)", VRmax - VRmin, VRmin, VRmax);

	} // vREF 

	printf("\n ---------+-----------------------------------------------------------------------------------------------------");
	printf("\n TxDQDQS1 : ");
	for (i=0; i < 100; i=i+4) printf(" %3d",i - toffset);

	// Restore TxDQDQS timing
	for (j=0; j < 64 + 8*eccMode; j++) {
		WriteSMBus( (i2cTXDQS1p[j]>>12), D, i2cTXDQS1p[j]&0xFF, CP[j]);
		usleep(FPGA_WRITE_DLY);
	}
	// Restore TxVref Voltage
	Set_i2c_Page( 1 );
	for(i=0;i<16+eccMode*2;i++) {
		WriteSMBus( (i2cvRefDQS[i]>>12), D, i2cvRefDQS[i]&0xFF, v[i]);
		usleep(FPGA_WRITE_DLY);				
	}
	Set_i2c_Page( 0 );	
}		

void TxDQS1(int start_lp, int end_lp, int step)
{
	int i, j, perBit;

	printf("\n\n %s: WR DQS Training, Data=%#.16lx & %#.16lx, start= %d, end= %d\n", 
					__func__ , data1, data2, start_lp, end_lp);

	SetTrainingMode( TX_DQDQS_1_MODE );

	printf("\n DQS: 0------7 8-----15 --");
	for (i=start_lp; i < end_lp; i=i+step) {

		gettimeofday(&start, NULL);	
		printf("\n %3d:",i);	

		if( (Check_fpga_status(FPGA1,TRN_MODE)==TRN_MODE)
#if BOTH_FPGA						
		 && (Check_fpga_status(FPGA2,TRN_MODE)==TRN_MODE)
#endif		
		) {
			Set_fpga_delay(TX_DQ_DQS_1, i, 0, 0);
			Set_fpga_delay(TX_DQS_1, i, 0, 0);
		} else {
			printf("\n\tTxDQS1:: test mode (A[103] bit0 is 0)! %#.8x, %#.8x\n", 
								fpga1_status, fpga2_status);
			exit (-1);
		}

		MemCpy(WR, data1, data2, TX_DQDQS1_DATA_SIZE);
		usleep(MemDelay);

		ReadDQSResultFromFPGA(TX_DQ_DQS_1, i);

		usleep(FPGA_CMD_WR_DLY);

		if(TestTime) PrintTestTime(); 
		if(ShowCounter) PrintCounter(); 
	}

	// Find middle passing point
	Find_1st_DQS_RE(TX_DQ_DQS_1, start_lp, end_lp, step);

	SetTrainingMode( NORMAL_MODE );
}

void TxDQDQS1(int start_lp, int end_lp, int step)
{
	int i, j, repeat, perBit, data, toffset;

	printf("\n\n %s: WR DB-to-FPGA, Data1/2=%#.16lx & %#.16lx, start= %d, end= %d\n", 
					__func__ , data1, data2, start_lp, end_lp);

	printf("\n  DQ: 0-------7 8------15 16-----23 24-----31 32-----39 40-----47 48-----55 56-----63 64-----71");

	for(repeat=1;repeat<2;repeat++) {
	
		SetTrainingMode( TX_DQDQS_1_MODE );

		if(repeat==1) {start_lp=0; end_lp=120; toffset=60;}
		if(repeat==2) {start_lp=0; end_lp=100; toffset=50;}
		if(repeat==2) printf("\n");

		for (i=start_lp; i < end_lp; i=i+step) 
		{
			gettimeofday(&start, NULL);	
			printf("\n %3d:",i - toffset);	

			if( (Check_fpga_status(FPGA1,TRN_MODE)==TRN_MODE)
#if BOTH_FPGA						
		 	&& (Check_fpga_status(FPGA2,TRN_MODE)==TRN_MODE)
#endif		
		 	) {
				perBit = 1;
				Set_fpga_delay(TX_DQ_DQS_1, i, toffset, repeat);

			} else {
				printf("\n\tTxDQDQS1:: test mode (A[103] bit0 is 0)! %#.8x, %#.8x\n", 
									fpga1_status, fpga2_status);
				exit (-1);
			}

			MemCpy(WR, data1, data2, TX_DQDQS1_DATA_SIZE);
			usleep(MemDelay);

			ReadDQResultFromFPGA(TX_DQ_DQS_1, i, perBit, 0xF, 0);
			
			if(eccMode) {
				MemCpy(WRECC, data1, data2, TX_DQDQS1_DATA_SIZE);
				usleep(MemDelay);

				ReadECCResultFromFPGA(TX_DQ_DQS_1, i, perBit, 0xF, 0);
			}
			if(TestTime) PrintTestTime(); 
			if(ShowCounter) PrintCounter(); 
		}

		FindCenterPoint(TX_DQ_DQS_1, perBit, start_lp, end_lp, step, toffset);
	
		SetTrainingMode( NORMAL_MODE );
	}	
}

void TxDQDQS1v0(int start_lp, int end_lp, int step)
{
	int i, j, repeat, perBit, data, toffset;

	printf("\n\n %s: WR DB-to-FPGA, Data1/2=%#.16lx & %#.16lx, start= %d, end= %d\n", 
					__func__ , data1, data2, start_lp, end_lp);

	printf("\n                                                                                                |<---    DQS   --->|");
	printf("\n  DQ: 0-------7 8------15 16-----23 24-----31 32-----39 40-----47 48-----55 56-----63 64-----71 0------7 8-----15 --");

	for(repeat=1;repeat<2;repeat++) {
		SetTrainingMode( TX_DQDQS_1_MODE );

	//	if(repeat==1) {start_lp=0; end_lp=120; toffset=60;}
	//	if(repeat==2) {start_lp=0; end_lp=100; toffset=50;}
	//	if(repeat==2) printf("\n");

		toffset=0;

		for (i=start_lp; i < end_lp; i=i+step) 
		{
			gettimeofday(&start, NULL);	
			printf("\n %3d:",i - toffset);	

			if( (Check_fpga_status(FPGA1,TRN_MODE)==TRN_MODE)
#if BOTH_FPGA						
		 	&& (Check_fpga_status(FPGA2,TRN_MODE)==TRN_MODE)
#endif		
		 	) {
				perBit = 1;

				Set_fpga_delay(TX_DQ_DQS_1, i, toffset, repeat);
				Set_fpga_delay(TX_DQS_1, i, 0, 0);

			} else {
				printf("\n\tTxDQDQS1:: test mode (A[103] bit0 is 0)! %#.8x, %#.8x\n", 
									fpga1_status, fpga2_status);
				exit (-1);
			}

			MemCpy(WR, data1, data2, TX_DQDQS1_DATA_SIZE);
			usleep(MemDelay);

			ReadDQResultFromFPGA(TX_DQ_DQS_1, i, perBit, 0xF, 0);
			printf("          ");
			ReadDQSResultFromFPGA(TX_DQ_DQS_1, i);

		//	MemCpy(WRECC, data1, data2, TX_DQDQS1_DATA_SIZE);
		//	usleep(MemDelay);

		//	ReadECCResultFromFPGA(TX_DQ_DQS_1, i, perBit, 0xF, 0);
			
			if(TestTime) PrintTestTime(); 
			if(ShowCounter) PrintCounter(); 
		}

		if(repeat==1) 
			FindCenterPoint(TX_DQ_DQS_1, perBit, start_lp, end_lp, step, toffset);
		Find_1st_DQS_RE(TX_DQ_DQS_1, start_lp, end_lp, step);
	
		SetTrainingMode( NORMAL_MODE );
	}	
}

void RxDQS1(int start_lp, int end_lp, int step)
{
	int i, j;

	printf("\n\n %s: Fake-RD DQS Training, Data=%#.16lx & %#.16lx, start= %d, end= %d\n", 
					__func__ , data1, data2, start_lp, end_lp);

	// Prefill the 4KB data pattern
	MemCpy(WR, data1, data2, RX_DQDQS1_DATA_SIZE);

	SetTrainingMode( RX_DQDQS_1_MODE );

	printf("\n DQS: 0------7 8-----15 --");
	for(i=start_lp; i<end_lp; i=i+step) {

		gettimeofday(&start, NULL);	
		printf("\n %3d:",i);	

		if( (Check_fpga_status(FPGA1,TRN_MODE)==TRN_MODE)
#if BOTH_FPGA						
		 && (Check_fpga_status(FPGA2,TRN_MODE)==TRN_MODE)
#endif
		) {
				
			Set_fpga_delay(RX_DQ_DQS_1, i, 0, 0);
			Set_fpga_delay(RX_DQS_1, i, 0, 0);

		} else {
			printf("RxDQS1:: test mode (A[103] bit0 is 0)! %#.8x, %#.8x\n", 
							fpga1_status, fpga2_status);
			exit (-1);
		}

		MemCpy(RD, data1, data2, RX_DQDQS1_DATA_SIZE);
		usleep(MemDelay);

		ReadDQSResultFromFPGA(RX_DQ_DQS_1, i);

		if(TestTime) PrintTestTime(); 
		if(ShowCounter) PrintCounter(); 
	}

	// Find middle passing point
	Find_1st_DQS_RE(RX_DQ_DQS_1, start_lp, end_lp, step);

	SetTrainingMode( NORMAL_MODE );
}

void RxDQDQS1(int start_lp, int end_lp, int step)
{
	int i, j, repeat, perBit, data, toffset;

	printf("\n\n %s: Fake-RD DDR4-to-FPGA Training, Data=%#.16lx & %#.16lx, start= %d, end= %d\n", 
					__func__ , data1, data2, start_lp, end_lp);

	printf("\n  DQ: 0-------7 8------15 16-----23 24-----31 32-----39 40-----47 48-----55 56-----63 64-----71");

	for(repeat=1;repeat<2;repeat++) {
		SetTrainingMode( RX_DQDQS_1_MODE );

		if(repeat==1) {start_lp=0; end_lp=120; toffset=90;}
		if(repeat==2) {start_lp=0; end_lp=100; toffset=50;}
		if(repeat==2) printf("\n");

		for(i=start_lp; i<end_lp; i=i+step) 
		{
			gettimeofday(&start, NULL);	
		//	printf("\n %3d:",i - toffset);	
			printf("\n %3d:",i - 0);	

			if( (Check_fpga_status(FPGA1,TRN_MODE)==TRN_MODE)
#if BOTH_FPGA						
		 	&& (Check_fpga_status(FPGA2,TRN_MODE)==TRN_MODE)
#endif
			) {
				perBit = 1;
				Set_fpga_delay(RX_DQ_DQS_1, i, toffset, repeat);

			} else {
				printf("RxDQDQS1:: test mode (A[103] bit0 is 0)! %#.8x, %#.8x\n", 
									fpga1_status, fpga2_status);
				exit (-1);
			}

			MemCpy(WR, data1, data2, RX_DQDQS1_DATA_SIZE);
			usleep(MemDelay);
			MemCpy(RD, data1, data2, RX_DQDQS1_DATA_SIZE);
			usleep(MemDelay);

			ReadDQResultFromFPGA(RX_DQ_DQS_1, i, perBit, 0xF, 0);

			if(eccMode) {
				MemCpy(WRECC, data1, data2, RX_DQDQS1_DATA_SIZE);
				usleep(MemDelay);
				MemCpy(RD, data1, data2, RX_DQDQS1_DATA_SIZE);
				usleep(MemDelay);

				ReadECCResultFromFPGA(RX_DQ_DQS_1, i, perBit, 0xF, 0);
			}

			if(TestTime) PrintTestTime(); 
			if(ShowCounter) PrintCounter(); 
		}

		if(repeat==1) FindCenterPoint(RX_DQ_DQS_1, perBit, start_lp, end_lp, step, toffset);
	
		SetTrainingMode( NORMAL_MODE );
	}
}

void RxDQDQS1v0(int start_lp, int end_lp, int step)
{
	int i, j, repeat, perBit, data, toffset;

	printf("\n\n %s: Fake-RD DDR4-to-FPGA Training, Data=%#.16lx & %#.16lx, start= %d, end= %d\n", 
					__func__ , data1, data2, start_lp, end_lp);

	printf("\n                                                                                                |<---    DQS   --->|");
	printf("\n  DQ: 0-------7 8------15 16-----23 24-----31 32-----39 40-----47 48-----55 56-----63 64-----71 0------7 8-----15 --");

	for(repeat=1;repeat<2;repeat++) {

		SetTrainingMode( RX_DQDQS_1_MODE );

	//	if(repeat==1) {start_lp=0; end_lp=120; toffset=90;}
	//	if(repeat==2) {start_lp=0; end_lp=100; toffset=50;}
	//	if(repeat==2) printf("\n");

		toffset=0;

		for(i=start_lp; i<end_lp; i=i+step) 
		{
			gettimeofday(&start, NULL);	
			printf("\n %3d:",i - toffset);	

			if( (Check_fpga_status(FPGA1,TRN_MODE)==TRN_MODE)
#if BOTH_FPGA						
		 	&& (Check_fpga_status(FPGA2,TRN_MODE)==TRN_MODE)
#endif
			) {
//#if PER_BIT_RX_DQDQS1
				perBit = 1;
//#else
//				perBit = 0;
//#endif			
				Set_fpga_delay(RX_DQ_DQS_1, i, toffset, repeat);
				Set_fpga_delay(RX_DQS_1, i, 0, 0);

			} else {
				printf("RxDQDQS1:: test mode (A[103] bit0 is 0)! %#.8x, %#.8x\n", 
									fpga1_status, fpga2_status);
				exit (-1);
			}

			MemCpy(WR, data1, data2, RX_DQDQS1_DATA_SIZE);
			usleep(MemDelay);
			MemCpy(RD, data1, data2, RX_DQDQS1_DATA_SIZE);
			usleep(MemDelay);

			ReadDQResultFromFPGA(RX_DQ_DQS_1, i, perBit, 0xF, 0);
			printf("          ");
			ReadDQSResultFromFPGA(RX_DQ_DQS_1, i);

		//	MemCpy(WRECC, data1, data2, RX_DQDQS1_DATA_SIZE);
		//	usleep(MemDelay);
		//	MemCpy(RD, data1, data2, RX_DQDQS1_DATA_SIZE);
		//	usleep(MemDelay);

		//	ReadECCResultFromFPGA(RX_DQ_DQS_1, i, perBit, 0xF, 0);
			
			if(TestTime) PrintTestTime(); 
			if(ShowCounter) PrintCounter(); 
		}

		if(repeat==1) 
			FindCenterPoint(RX_DQ_DQS_1, perBit, start_lp, end_lp, step, toffset);
	
		SetTrainingMode( NORMAL_MODE );
	}
}

/**
 * TxDQDQS2 - FPGA-to-DDR4 Write Training for fake-write operation
 *
 **/
void TxDQDQS2(int start_lp, int end_lp, int step)
{
	unsigned long qRd[8], exp1, exp2, qw1, qw2, qwresult0, qwresult;
	unsigned int i, j, bl;
	void *v_addr;

	//
	// Byte14.bit7 - '1' Enable FakeWR operation
	//
	if(!(ReadSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR)&0x80)) {		
		printf("\n FPGA1 Set Byte14 bit 7 to '1' ... ");
		WriteSMBus(FPGA1, D, FPGA_PLL_RESET_ADDR, ReadSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR) | 0x80);
		printf("Done");
	}	
#if BOTH_FPGA
	if(!(ReadSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR)&0x80)) {
		printf("\n FPGA1 Set Byte14 bit 7 to '1' ... ");
		WriteSMBus(FPGA2, D, FPGA_PLL_RESET_ADDR, ReadSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR) | 0x80);
		printf("Done");
	}
#endif

	if((ReadSMBus(FPGA1,D,6))) {		
		printf("\n\n\033[1;31m ERROR!! BCOM_MUX is 'High', run -E option before FakeWR training !!\033[0m\n\n");
		exit(1);
	}

	printf("\n\n %s: Fake-WR FPGA-to-DDR4 Training, Data1,2= %#.8x & %#.8x, repeat with complement data\n", 
					__func__ , data1, data2 );

	if ( debug_flag & 0x2 ) {
		printf("\n  DQ: 0:63       [BL0] 0:63       [BL1] 0:63       [BL2] 0:63       [BL3]"
			   " 0:63       [BL4] 0:63       [BL5] 0:63       [BL6] 0:63       [BL7]   0:63   '0': Pass");
	} else {
		printf("\n  DQ: 0-------7 8------15 16-----23 24-----31 32-----39 40-----47 48-----55 56-----63 64-----71");
		printf("  0123456789ABCDEF");
	}		
	/* Get the base addr of training_data window */
	v_addr = map_base + (target & MAP_MASK2);

	for (i=start_lp; i < end_lp; i=i+step)
	{
		gettimeofday(&start, NULL);

		/* 2. Write a pattern to training_data window ... Background */
		MemCpy(WR_ALT, 0x5555555555555555, 0x5555555555555555, TX_DQDQS2_DATA_SIZE);

		/* 3. Set training mode to 3 for Tx DQ-DQS(2) */
		SetTrainingMode( TX_DQDQS_2_MODE );

		Set_fpga_delay(TX_DQ_DQS_2, i, 0, 0);

		printf("\n %3d:",i);

		// Master - Inverted DQS 10/27/2016
		exp1 = (data1 << 32) | data1;
		exp2 = (data2 << 32) | data2;

		writeInitialPatternToFPGA(3, data1, data2);

		/* 5. Fake WR operation */
		MemCpy(WR_ALT, 0x6666666666666666, 0x9999999999999999, TX_DQDQS2_DATA_SIZE);
		usleep(MemDelay);

		/* Read 8 x 64-bit data from DRAM */
		memcpy_64B_movnti((void *)qRd, v_addr, 64);
		memcpy_64B_movnti((void *)qRd, v_addr, 64);

		qw1 = qRd[0] | qRd[2] | qRd[4] | qRd[6];	// data1-data1
		qw2 = qRd[1] | qRd[3] | qRd[5] | qRd[7];	// data2-data2
										
		qwresult0 = (qw1 ^ exp1) | (qw2 ^ exp2);
#if 1
	  if(debug||SetPattern) {
		if ( debug_flag & 0x1 ) {
			for(j=0;j<16;j++) {
				rDQS[i][j] = (qwresult0>>j*4)&0xF;
				printf(" %4X",rDQS[i][j]);
			}
			printf("            ");
			for(j=0;j<16;j++) {
				printf("\033[0;%dm%1X\033[0m", (qwresult0>>j*4)&0xF?31:34,(qwresult0>>j*4)&0xF);
			}
		}				
		if ( debug_flag & 0x2 ) {
		//	printf(" ");
			for(bl=0;bl<8;bl++) {
				printf(" ");
				for(j=0;j<16;j++) printf("%01x",(qRd[bl]>>j*4)&0xF);			
			}
			printf("   ");
			for(j=0;j<16;j++) {
				rDQS[i][j] = (qwresult0>>j*4)&0xF;
				printf("\033[0;%dm%1X\033[0m", (qwresult0>>j*4)&0xF?31:34, (qwresult0>>j*4)&0xF);
			}
		}
		if(!SetPattern) printf("\n    :");
	  }
#endif
#if 1
	if(!SetPattern) {
		exp1 = ~exp1;
		exp2 = ~exp1;

		writeInitialPatternToFPGA(3, ~data1, ~data2);

		/* 5. Fake WR operation */
		MemCpy(WR_ALT, 0x6666666666666666, 0x9999999999999999, TX_DQDQS2_DATA_SIZE);
		usleep(MemDelay);

		/* Read 8 x 64-bit data from DRAM */
		memcpy_64B_movnti((void *)qRd, v_addr, 64);
		memcpy_64B_movnti((void *)qRd, v_addr, 64);

		qw1 = qRd[0] | qRd[2] | qRd[4] | qRd[6];	// data1-data1
		qw2 = qRd[1] | qRd[3] | qRd[5] | qRd[7];	// data2-data2
										
		qwresult = qwresult0 | (qw1 ^ exp1) | (qw2 ^ exp2);

		if ( debug_flag & 0x1 ) {
			for(j=0;j<16;j++) {
				rDQS[i][j] = (qwresult>>j*4)&0xF;
			//	printf(" %4X",rDQS[i][j]);
				printf(" %c%c%c%c",
								(rDQS[i][j]>>0)&0x1?' ':'*',
								(rDQS[i][j]>>1)&0x1?' ':'*',
								(rDQS[i][j]>>2)&0x1?' ':'*',
								(rDQS[i][j]>>3)&0x1?' ':'*');
			}
			printf("         ");
		//	for(j=0;j<16;j++) {
		//		printf("\033[0;%dm%1X\033[0m", (qwresult>>j*4)&0xF?31:34, (qwresult>>j*4)&0xF);
		//	}
		}				
		if ( debug_flag & 0x2 ) {
			for(bl=0;bl<8;bl++) {
				printf(" ");
				for(j=0;j<16;j++) printf("%01x",(qRd[bl]>>j*4)&0xF);			
			}
		}
		printf("   ");
		for(j=0;j<16;j++) {
			rDQS[i][j] = (qwresult>>j*4)&0xF;
			printf("\033[0;%dm%1X\033[0m", (qwresult>>j*4)&0xF?31:34, (qwresult>>j*4)&0xF);
		}
	  }
#endif
		/* 6. Set training mode to NORMAL mode */
		SetTrainingMode( NORMAL_MODE );
	
		if(TestTime) PrintTestTime(); 
		if(ShowCounter) PrintCounter(); 

	} // end of for

	/* Find middle passing point */
	FindCenterPoint2(TX_DQ_DQS_2, 0, start_lp, end_lp, step);

	SetTrainingMode( NORMAL_MODE );
}

void TxDQDQS2v0(int start_lp, int end_lp, int step)
{
	unsigned int i, j, bl, dwordData1, perBit;
	unsigned long qw1, qw2, qw1xor, qw2xor, qwresult;
	unsigned long qRd[8], exp1, exp2;
	void *v_addr;
	unsigned long data1_2, data2_2;

	//
	// Byte14.bit7 - '1' Enable FakeWR operation
	//
	if(!(ReadSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR)&0x80)) {		
		printf("\n FPGA1 Set Byte14 bit 7 to '1' ... ");
		WriteSMBus(FPGA1, D, FPGA_PLL_RESET_ADDR, ReadSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR) | 0x80);
		printf("Done");
	}	
#if BOTH_FPGA
	if(!(ReadSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR)&0x80)) {
		printf("\n FPGA1 Set Byte14 bit 7 to '1' ... ");
		WriteSMBus(FPGA2, D, FPGA_PLL_RESET_ADDR, ReadSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR) | 0x80);
		printf("Done");
	}
#endif

	if((ReadSMBus(FPGA1,D,6))) {		
		printf("\n\n\033[1;31m ERROR!! BCOM_MUX is 'High', run -E option before FakeWR training !!\033[0m\n\n");
		exit(1);
	}

	/*
	 * 1. Read I2C A[118:115] for Data1 (32-bit) and A[122:119] for Data2 (32-bit)
	 * This step is no longer needed in this routine since we generate
	 * the table with the result from FPGA.
	 */
	
	if(SetPattern) {
		printf("\n data1=%x data2=%x data1_2=%x data2_2=%x", data1, data2, data1_2, data2_2);
		writeInitialPatternToFPGA(2, data1, data2);
		data1 = data1 & 0xffffffff;
		data2 = data2 & 0xffffffff;
		data1_2 = data1;
		data2_2 = data2;
	} else {		
		data1 = data2 = data1_2 = data2_2 = 0;
		for (i=0; i < 4; i++) {
			dwordData1 = (unsigned int)ReadSMBus(FPGA1, D, TX_DQDQS2_FPGA_DATA1+i);
			data1 = data1 | (dwordData1 << (i*8));
			dwordData1 = (unsigned int)ReadSMBus(FPGA1, D, TX_DQDQS2_FPGA_DATA2+i);
			data2 = data2 | (dwordData1 << (i*8));
#if BOTH_FPGA
			dwordData1 = (unsigned int)ReadSMBus(FPGA2, D, TX_DQDQS2_FPGA_DATA1+i);
			data1_2 = data1_2 | (dwordData1 << (i*8));
			dwordData1 = (unsigned int)ReadSMBus(FPGA2, D, TX_DQDQS2_FPGA_DATA2+i);
			data2_2 = data2_2 | (dwordData1 << (i*8));
#endif
	}
		printf("\n data1=%x data2=%x data1_2=%x data2_2=%x", data1, data2, data1_2, data2_2);
	}

#if BOTH_FPGA
	printf("\n\n %s: Fake-WR FPGA-to-DDR4 Training, Data1,2= %#.8x & %#.8x / %#.8x & %#.8x\n", 
					__func__ , data1, data2, data1_2, data2_2);
#else
	printf("\n\ %s: Fake-WR FPGA-to-DDR4 Training, Data1,2= %#.8x & %#.8x\n",  
					__func__ , data1, data2);
#endif

	if ( debug_flag & 0x2  ) {
		printf("\n  DQ:  0:63       [BL0] 0:63       [BL1] 0:63       [BL2] 0:63       [BL3]"
			   " 0:63       [BL4] 0:63       [BL5] 0:63       [BL6] 0:63       [BL7]     0:63   '0': Pass");
	} else {
		printf("\n  DQ: 0-------7 8------15 16-----23 24-----31 32-----39 40-----47 48-----55 56-----63 64-----71");
		printf("  0123456789ABCDEF");
	}		

	for (i=start_lp; i < end_lp; i=i+step)
	{
		/* Get the base addr of training_data window */
		v_addr = map_base + (target & MAP_MASK2);

		gettimeofday(&start, NULL);
		printf("\n %3d:",i);

		/* 2. Write a pattern to training_data window ... Background */
		MemCpy(WR_ALT, 0x5555555555555555, 0x5555555555555555, TX_DQDQS2_DATA_SIZE);
	//	MemCpy(WR_ALT, data1, data2, TX_DQDQS2_DATA_SIZE);

		/* 3. Set training mode to 3 for Tx DQ-DQS(2) */
		SetTrainingMode( TX_DQDQS_2_MODE );

		if( (Check_fpga_status(FPGA1,TRN_MODE)==TRN_MODE)
#if BOTH_FPGA						
		 && (Check_fpga_status(FPGA2,TRN_MODE)==TRN_MODE)
#endif
		) {

			perBit = 0;			// 9.8.2016
			Set_fpga_delay(TX_DQ_DQS_2, i, 0, 0);

		} else {
			printf("TxDQDQS2:: test mode (A[103] bit0 is 0)! %#.8x, %#.8x\n", 
								fpga1_status, fpga2_status);
			exit (-1);
		}

		/* 5. Write a pattern to training_data window for Fake WR operation */
		MemCpy(WR_ALT, 0x6666666666666666, 0x9999999999999999, TX_DQDQS2_DATA_SIZE);
	//	MemCpy(WR_ALT, 0xffffffffffffffff, 0xffffffffffffffff, TX_DQDQS2_DATA_SIZE);
		usleep(MemDelay);

		/* 6. Set training mode to NORMAL mode */
		SetTrainingMode( NORMAL_MODE );

		/* Draw the graph based on the result from FPGA */
		if ( debug_flag == 0 ) 
			ReadDQResultFromFPGA(TX_DQ_DQS_2, i, perBit, 0xF, 0);

		/* Read 8 x 64-bit data from DRAM */
		for(j=0;j<8;j++) qRd[j] = *((unsigned long *) v_addr+j);

		qw1 = qRd[0] | qRd[2] | qRd[4] | qRd[6];	// data1-data1
		qw2 = qRd[1] | qRd[3] | qRd[5] | qRd[7];	// data2-data2
										
		//
		// data2 => data1 order, HW 01.07.28.16  V7.31.16, SJ
		//
#if 1
		// Master - Inverted DQS 10/27/2016
		exp1 = data1_2 << 32 | data1;
		exp2 = data2_2 << 32 | data2;
#else
		// Master & Slave - Inverted DQS 10/27/2016
		exp1 = data1_2 << 32 | data1;
		exp2 = data2_2 << 32 | data2;
#endif
		qw1xor = qw1 ^ exp1;		// RD data1 ^ expected Data1
		qw2xor = qw2 ^ exp2;		// RD data2 ^ expected Data2
		// 
		// final result should be ORed not XORed.    V7.31.16, SJ
		//
		qwresult = qw1xor | qw2xor;

		if ( debug_flag & 0x1 ) {
			/* Display memory contents for this iteration */
			for(j=0;j<16;j++) {
				rDQS[i][j] = (qwresult>>j*4)&0xF;
				printf(" %4X",rDQS[i][j]);
			}
			printf("            ");
			for(j=0;j<16;j++) {
				printf("\033[0;%dm%1X\033[0m", 
						(qwresult>>j*4)&0xF?31:34,(qwresult>>j*4)&0xF);
			}
		}				
		if ( debug_flag & 0x2 ) {
			//	printf("  %016lx %016lx %016lx %016lx %016lx %016lx %016lx %016lx XOR:",
			//				qRd[0], qRd[1], qRd[2], qRd[3], qRd[4], qRd[5], qRd[6], qRd[7]);
				
			printf(" ");
			for(bl=0;bl<8;bl++) {
				printf(" ");
				for(j=0;j<16;j++) printf("%01x",(qRd[bl]>>j*4)&0xF);
				
			}
			printf(" XOR:");
			for(j=0;j<16;j++) {
				rDQS[i][j] = (qwresult>>j*4)&0xF;
				printf("\033[0;%dm%1X\033[0m", (qwresult>>j*4)&0xF?31:34, (qwresult>>j*4)&0xF);
			}
		}

		if(TestTime) PrintTestTime(); 
		if(ShowCounter) PrintCounter(); 

	} // end of for

	/* Find middle passing point */
	FindCenterPoint2(TX_DQ_DQS_2, 0, start_lp, end_lp, step);

	SetTrainingMode( NORMAL_MODE );
}

void writeInitialPatternToFPGA(int write_fpga_data, unsigned long data1, unsigned long data2)
{
	unsigned int i;
	char *ptr;

	if(write_fpga_data==1) {
		/*
	 	* Get intial values from the user and write them to SPD
	 	*/
		if ( (fpga1_data1_str = getenv("HVT_FPGA1_DATA1")) != NULL )
			fpga1_data1 = (unsigned int) strtoul(fpga1_data1_str, &ptr, 16);
		if ( (fpga1_data2_str = getenv("HVT_FPGA1_DATA2")) != NULL )
			fpga1_data2 = (unsigned int) strtoul(fpga1_data2_str, &ptr, 16);
		if ( (fpga2_data1_str = getenv("HVT_FPGA2_DATA1")) != NULL )
			fpga2_data1 = (unsigned int) strtoul(fpga2_data1_str, &ptr, 16);
		if ( (fpga2_data2_str = getenv("HVT_FPGA2_DATA2")) != NULL )
			fpga2_data2 = (unsigned int) strtoul(fpga2_data2_str, &ptr, 16);
	
		printf("\n Write FakeWR training Data1/2 0x%x, 0x%x, 0x%x, 0x%x ... ", 
						fpga1_data1, fpga1_data2, fpga2_data1, fpga2_data2);
		for (i=0; i < TX_DQDQS2_FPGA_DATA_SZ; i++)
		{
			WriteSMBus(FPGA1, D, TX_DQDQS2_FPGA_DATA1+i, (unsigned char)(fpga1_data1 >> (i*8)));
			usleep(FPGA_WRITE_DLY);
			WriteSMBus(FPGA1, D, TX_DQDQS2_FPGA_DATA2+i, (unsigned char)(fpga1_data2 >> (i*8)));
			usleep(FPGA_WRITE_DLY);
			WriteSMBus(FPGA2, D, TX_DQDQS2_FPGA_DATA1+i, (unsigned char)(fpga2_data1 >> (i*8)));
			usleep(FPGA_WRITE_DLY);
			WriteSMBus(FPGA2, D, TX_DQDQS2_FPGA_DATA2+i, (unsigned char)(fpga2_data2 >> (i*8)));
			usleep(FPGA_WRITE_DLY);
		}
		printf("Done\n\n");
	} if(write_fpga_data==2) {
		printf("\n Write FakeWR training Data1/2 0x%x, 0x%x ... ", data1, data2);
		for (i=0; i < TX_DQDQS2_FPGA_DATA_SZ; i++)
		{
			WriteSMBus(FPGA1, D, TX_DQDQS2_FPGA_DATA1+i, (unsigned char)(data1 >> (i*8)));
			usleep(FPGA_WRITE_DLY);
			WriteSMBus(FPGA1, D, TX_DQDQS2_FPGA_DATA2+i, (unsigned char)(data2 >> (i*8)));
			usleep(FPGA_WRITE_DLY);
			WriteSMBus(FPGA2, D, TX_DQDQS2_FPGA_DATA1+i, (unsigned char)(data1 >> (i*8)));
			usleep(FPGA_WRITE_DLY);
			WriteSMBus(FPGA2, D, TX_DQDQS2_FPGA_DATA2+i, (unsigned char)(data2 >> (i*8)));
			usleep(FPGA_WRITE_DLY);
		}
		printf("Done\n\n");			
	} else if(write_fpga_data==3) {
	//	printf("\n Write FakeWR training Data1/2 0x%x, 0x%x ... ", data1, data2);
		for (i=0; i < TX_DQDQS2_FPGA_DATA_SZ; i++)
		{
			WriteSMBus(FPGA1, D, TX_DQDQS2_FPGA_DATA1+i, (unsigned char)(data1 >> (i*8)));
			usleep(FPGA_WRITE_DLY);
			WriteSMBus(FPGA1, D, TX_DQDQS2_FPGA_DATA2+i, (unsigned char)(data2 >> (i*8)));
			usleep(FPGA_WRITE_DLY);
			WriteSMBus(FPGA2, D, TX_DQDQS2_FPGA_DATA1+i, (unsigned char)(data1 >> (i*8)));
			usleep(FPGA_WRITE_DLY);
			WriteSMBus(FPGA2, D, TX_DQDQS2_FPGA_DATA2+i, (unsigned char)(data2 >> (i*8)));
			usleep(FPGA_WRITE_DLY);
		}
	//	printf("Done\n\n");	
		usleep(10000);		
	}		
}

//MK-end

void list(int D)
{
	int i, j, bl, page, maxPage=2;
	unsigned char data;

	printf("\n DIMM[%d] N%d.C%d.D%d : %-18s S/N - %s %6.1fC",
					D, N, C, (int)(D%3), dimm[D].sPN, dimm[D].sSN, dimm[D].Temp/16.);

	for(i=0;i<4;i++) {
			HWrev[0] |= ReadSMBus(FPGA1,D,1+i)<<(24-i*8);
			HWrev[1] |= ReadSMBus(FPGA2,D,1+i)<<(24-i*8);
	}
	printf("  FPGA H/W - %02X-%02X.%02X.%02X, %02X-%02X.%02X.%02X",
						(HWrev[0]>>24)&0xFF, (HWrev[0]>>16)&0xFF, (HWrev[0]>>8)&0xFF, (HWrev[0])&0xFF,
						(HWrev[1]>>24)&0xFF, (HWrev[1]>>16)&0xFF, (HWrev[1]>>8)&0xFF, (HWrev[1])&0xFF);

	printf("\n\n");
	printf("           | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\t");
	printf(" 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
	printf(" ----------+------------------------------------------------\t");
	printf("------------------------------------------------\n");

#if FAKE_RD_DATA_TEST
	maxPage=3;
#endif

	for(page=0;page<maxPage;page++) {
		Set_i2c_Page( page );
		if(page==1) printf(" >>> mmioCmd Data @0x1FFF10000 = B:3-3, X:FFFC, Y:xxx <<<\n");
		if(page==2) printf(" >>> FakeRd Data @0x1FFe00000 = B:2-3, X:FFF0, Y:xxx (BG0:x) <<<\n");
		for(i=0;i<8;i++) {
			printf(" %03d(0x%03X):",i*16+256*page,i*16+256*page);
			for(j=0;j<16;j++) {
				data = ReadSMBus(FPGA1, D, i*16+j);
				printf("\033[0;%dm %02X\033[0m", (data)?34:38, data);
			}
			printf("\t");
			for(j=0;j<16;j++) {
				data = ReadSMBus(FPGA2, D, i*16+j);
				printf("\033[0;%dm %02X\033[0m", (data)?34:38, data);
			}		
			printf("\n");
		}
		printf("\n");
	}
	Set_i2c_Page( 0 );
//	printf("\n BCOM_MUX Control[31]: %3d", ReadSMBus(FPGA1, D, 0x1F));

	printf("\n Fake_WR_Data1/2[115]: Data1/2= 0x%02X%02X%02X%02X, 0x%02X%02X%02X%02X", 
					ReadSMBus(FPGA1, D, 118),ReadSMBus(FPGA1, D, 117),
					ReadSMBus(FPGA1, D, 116),ReadSMBus(FPGA1, D, 115),
					ReadSMBus(FPGA1, D, 122),ReadSMBus(FPGA1, D, 121),
					ReadSMBus(FPGA1, D, 120),ReadSMBus(FPGA1, D, 119));
	printf("     \t");
	printf(" Data1/2= 0x%02X%02X%02X%02X, 0x%02X%02X%02X%02X", 
					ReadSMBus(FPGA2, D, 118),ReadSMBus(FPGA2, D, 117),
					ReadSMBus(FPGA2, D, 116),ReadSMBus(FPGA2, D, 115),
					ReadSMBus(FPGA2, D, 122),ReadSMBus(FPGA2, D, 121),
					ReadSMBus(FPGA2, D, 120),ReadSMBus(FPGA2, D, 119));

	printf("\n\n Bcom Control: %s", ReadSMBus(FPGA1, D, 6)&0x1?"RCD(High)":"FPGA(Low)");
	Set_i2c_Page( 1 );
	printf("\n Tx-Vref(DQ0): %3d(0x%X),%3d(0x%X) Ticks @%3d(0x%X)",
					ReadSMBus(i2cvRefDQS[0]>>12, D, i2cvRefDQS[0]&0xFF), 
					ReadSMBus(i2cvRefDQS[0]>>12, D, i2cvRefDQS[0]&0xFF), 
					ReadSMBus(i2cvRefDQS[8]>>12, D, i2cvRefDQS[8]&0xFF), 
					ReadSMBus(i2cvRefDQS[8]>>12, D, i2cvRefDQS[8]&0xFF), 
					256+(i2cvRefDQS[0]&0xFF), 256+(i2cvRefDQS[0]&0xFF));
	Set_i2c_Page( 0 );
	printf("\n CMD-CK Delay: %3d(0x%02X),%3d(0x%02X) Ticks @%3d(0x%X)",
					ReadSMBus(FPGA1, D, CMD_CK_ADDR), ReadSMBus(FPGA1, D, CMD_CK_ADDR), 
					ReadSMBus(FPGA2, D, CMD_CK_ADDR), ReadSMBus(FPGA2, D, CMD_CK_ADDR), 
					CMD_CK_ADDR, CMD_CK_ADDR);

	printf("\n i2c Reg Addr: TxDQDQS1(WR) %d(0x%X),", i2cTXDQS1p[0]&0xFFF, i2cTXDQS1p[0]&0xFFF);
	printf(" RxDQDQS1(FakeRD) %d(0x%X),", i2cRXDQS1p[0]&0xFFF, i2cRXDQS1p[0]&0xFFF);
#if PER_BIT_TX_DQDQS2
	printf(" TxDQDQS2(FakeWR) %d(0x%X)", i2cTXDQS2p[0]&0xFFF, i2cTXDQS2p[0]&0xFFF);
#else
	printf(" TxDQDQS2(FakeWR) %d(0x%X)", i2cTXDQS2[0]&0xFFF, i2cTXDQS2[0]&0xFFF);
#endif

	printf("\n\n");
	fprintf(stderr," Counter: "); PrintCounter();
	printf("\n\n");

	exit(1);
}

void getSN(int D)
{
	int i, ww, wrem, N, C;
	char SN[6];

	SetPageAddress(D, 1);
	usleep(FPGA_SETPAGE_DLY);

	for(i=0;i<18;i++) {
	//	sPN[D][i]=ReadSMBus(SPD,D,(329-256+i));
		dimm[D].sPN[i]=ReadSMBus(SPD,D,(329-256+i));
	}

	for(i=0;i<6;i++) {
		SN[i] = ReadSMBus(SPD,D,(323-256+i));
	}
	SetPageAddress(D, 0);
	usleep(FPGA_SETPAGE_DLY);

	SN[0] = SN[0] - 8;
	ww = 10*(SN[1]>>4) + SN[1]&0xF -1;
	wrem = SN[2]>>4;
	sprintf(dimm[D].sSN,"%X%03d%X%02X%02X%02X",
					SN[0]&0xF,ww*7+wrem,SN[2]&0xF, SN[3]&0xFF, SN[4]&0xFF, SN[5]&0xFF);

	dimm[D].Temp = ReadSMBus(TSOD, D, 5) & 0xFFF;

	for(i=0;i<4;i++) {
		HWrev[0] |= ReadSMBus(FPGA1,D,1+i)<<(24-i*8);
		HWrev[1] |= ReadSMBus(FPGA2,D,1+i)<<(24-i*8);
	}
	
#if CHECK_DATA_SCRAMBLE	
	N =  D<12?0:1;
	C = (int)(D)<12?(int)(D/3):(int)(D/3)-4;
	dataScramble = MMioReadDword(Bus+N*(Bus+1),20+(C>>1)*3*HA,2+(C&1),0x1E0) & 0x7;
	mcscramble_seed_sel = MMioReadDword(Bus+N*(Bus+1),20+(C>>1)*3*HA,2+(C&1),0x1E4);
#endif	
}

void dumpspd(int D)
{
	int i, j, spd;

	printf(" DIMM[%d] spd data\n\n", D);
#if 1	
	SetPageAddress(D, 0);

	for(i=0;i<16;i++) {
		printf(" %03X :",i*16);
		for(j=0;j<16;j++) printf(" %02X", ReadSMBus(SPD, D, i*16+j));
		printf("  ");
		for(j=0;j<16;j++) {
			spd = ReadSMBus(SPD, D, i*16+j);
			printf("%c", (spd<127)&&(spd>31)?spd:'.');
		}
		printf("\n");
	}
	printf("\n");	
#endif
	SetPageAddress(D, 1);

	for(i=0;i<16;i++) {
		if(i==8) printf("\n");
		printf(" %03X :",i*16+256);
		for(j=0;j<16;j++) printf(" %02X",ReadSMBus(SPD, D, i*16+j));
		printf("  ");
		for(j=0;j<16;j++) {
			spd = ReadSMBus(SPD, D, i*16+j);
			printf("%c", (spd<127)&&(spd>31)?spd:'.');
		}		
		printf("\n");
	}

	SetPageAddress(D, 0);	

	printf("\n");

	exit(1);
}	

void LoadfromSpd(int D)
{
	int i, j, cp[18], val[2], spd;

	printf("\n Load training data from SPD\n");

	SetPageAddress(D, 1);

	printf("\n H/W Revision: ");
	for(i=0;i<4;i++) {
		printf("%02X",ReadSMBus(SPD, D, spdHWRev[i]&0xFF));
	}	

	printf("\n CMD-CLK Dly :");
	for(i=0;i<2;i++) {
		spd = ReadSMBus(SPD, D, spdCMDtoCK[i]&0xFF);
		printf(" 0x%02X(%2d)",spd, spd);
		WriteSMBus( (i2cCMDtoCK[i]>>12), D, i2cCMDtoCK[i]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
	}

	printf("\n RdWr-Latency:");
	for(i=0;i<2;i++) {
		spd = ReadSMBus(SPD, D, spdRdWrLat[i]&0xFF);
		printf(" 0x%02X(%2d)", spd, spd);
		WriteSMBus(i2cRdWrLat[i]>>12, D, i2cRdWrLat[i]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
	}

	printf("\n Sio-OE-Dly  :");
	for(i=0;i<2;i++) {
		spd = ReadSMBus(SPD, D, spdOEDelay[i]&0xFF);
		printf(" 0x%02X(%2d)", spd, spd);
		WriteSMBus(i2cOEDelay[i]>>12, D, i2cOEDelay[i]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
	}

	printf("\n Vref DQ     :");
	Set_i2c_Page( 1 );
	for(i=0;i<16 + eccMode*2;i++) {
		if( i<8)       		 spd = ReadSMBus(SPD, D, spdvRefDQS[0]&0xFF);
		else if(i>7 && i<16) spd = ReadSMBus(SPD, D, spdvRefDQS[1]&0xFF);
		else          		 spd = ReadSMBus(SPD, D, spdvRefDQS[0]&0xFF);
		printf(" 0x%02X", spd);
		WriteSMBus(i2cvRefDQS[i]>>12, D, i2cvRefDQS[i]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
	}
	Set_i2c_Page( 0 );

	printf("\n Cmd-DQ-Mux  :");
	for(i=0;i<8 + eccMode;i++) {
		spd = ReadSMBus(SPD, D, spdCmdDQMux[i]&0xFF);
		printf(" 0x%02X",spd);
		WriteSMBus((i2cMuxSet[i]>>12), D, i2cMuxSet[i]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
	}

	printf("\n Sio-cyc-Dly :\n     ");	
	Set_i2c_Page( 1 );
	for (i=0; i < 16 + eccMode*2; i++) {
		spd = ReadSMBus(SPD, D, spdIoCycDly[i]&0xFF);
		printf(" 0x%02X", spd);
		WriteSMBus(i2cIoCycDly[i]>>12, D, i2cIoCycDly[i]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
	}
	Set_i2c_Page( 0 );

	printf("\n\n TxDQDQS1(mmioWR):\n  cp:");	
	for (i=0; i < 16 + eccMode*2; i++) {
		spd = ReadSMBus(SPD, D, spdTXDQS1[i]&0xFF);
		printf("%5d", spd);
		WriteSMBus(i2cTXDQS1p[i*4+0]>>12, D, i2cTXDQS1p[i*4+0]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
		WriteSMBus(i2cTXDQS1p[i*4+1]>>12, D, i2cTXDQS1p[i*4+1]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
		WriteSMBus(i2cTXDQS1p[i*4+2]>>12, D, i2cTXDQS1p[i*4+2]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
		WriteSMBus(i2cTXDQS1p[i*4+3]>>12, D, i2cTXDQS1p[i*4+3]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
	}

	printf("\n\n RxDQDQS1(FakeRD):\n  cp:");	
	for (i=0; i < 16 + eccMode*2; i++) {
		spd = ReadSMBus(SPD, D, spdRXDQS1[i]&0xFF);
		printf("%5d", spd);
		WriteSMBus(i2cRXDQS1p[i*4+0]>>12, D, i2cRXDQS1p[i*4+0]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
		WriteSMBus(i2cRXDQS1p[i*4+1]>>12, D, i2cRXDQS1p[i*4+1]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
		WriteSMBus(i2cRXDQS1p[i*4+2]>>12, D, i2cRXDQS1p[i*4+2]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
		WriteSMBus(i2cRXDQS1p[i*4+3]>>12, D, i2cRXDQS1p[i*4+3]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
	}	

	printf("\n\n TxDQDQS2(FakeWR):\n  cp:");	
	Set_i2c_Page( 1 );
	for (i=0; i < 16 + eccMode*2; i++) {
		spd = ReadSMBus(SPD, D, spdTXDQS2[i]&0xFF);
		printf("%5d", spd);
		WriteSMBus(i2cTXDQS2[i]>>12, D, i2cTXDQS2[i]&0xFF, spd);
		usleep(FPGA_WRITE_DLY);
	}
	Set_i2c_Page( 0 );

	SetPageAddress(D, 0);

	printf("\n\n DIMM[%d] PLL Reset & load i/o delay ... ", D);
	fpga_pll_reset_high_low();
#if LOAD_IO_DELAY
	fpga_i_Dly_load_signal();		// 9.28.2016
	fpga_o_Dly_load_signal();		// 9.28.2016
#endif
	printf("Done\n\n");
	
	exit(1);
}		

void WritetoSpd(int D)
{
	int i, j, cp[18], reg;

	printf("\n Save training data to SPD H/W Rev: %08X\n", HWrev[0]);

	SetPageAddress(D, 1);

	for(i=0;i<4;i++) {
		WriteSMBus(SPD, D, spdHWRev[i]&0xFF, (HWrev[0]>>(24-i*8))&0xFF);
		usleep(SPD_WRITE_DLY);
	}

	printf("\n CMD-CLK Dly :");
	for(i=0;i<2;i++) {
		reg = ReadSMBus(i2cCMDtoCK[i]>>12, D, i2cCMDtoCK[i]&0xFF);
		printf(" 0x%02X(%2d)", reg, reg);
		WriteSMBus(SPD, D, spdCMDtoCK[i]&0xFF, reg);
		usleep(SPD_WRITE_DLY);
	}

	printf("\n RdWr-Latency:");
	for(i=0;i<2;i++) {
		reg = ReadSMBus(i2cRdWrLat[i]>>12, D, i2cRdWrLat[i]&0xFF);
		printf(" 0x%02X(%2d)", reg, reg);
		WriteSMBus(SPD, D, spdRdWrLat[i]&0xFF, reg);
		usleep(SPD_WRITE_DLY);
	}

	printf("\n Sio-OE-Dly  :");
	for(i=0;i<2;i++) {
		reg = ReadSMBus(i2cOEDelay[i]>>12, D, i2cOEDelay[i]&0xFF);
		printf(" 0x%02X(%2d)", reg, reg);
		WriteSMBus(SPD, D, spdOEDelay[i]&0xFF, reg);
		usleep(SPD_WRITE_DLY);
	}

	printf("\n Vref DQ     :");
	Set_i2c_Page( 1 );
	for(i=0;i<2;i++) {
		reg = ReadSMBus(i2cvRefDQS[i*8]>>12, D, i2cvRefDQS[i*8]&0xFF);
		printf(" 0x%02X(%2d)", reg, reg);
		WriteSMBus(SPD, D, spdvRefDQS[i]&0xFF, reg);
		usleep(SPD_WRITE_DLY);
	}
	Set_i2c_Page( 0 );

	printf("\n Cmd-DQ-Mux  :");
	for(i=0;i<8 + eccMode;i++) {
		reg = ReadSMBus( (i2cMuxSet[i]>>12), D, i2cMuxSet[i]&0xFF);
		printf(" 0x%02X",reg);
		WriteSMBus(SPD, D, spdCmdDQMux[i]&0xFF, reg);
		usleep(SPD_WRITE_DLY);
	}

	printf("\n Sio-cyc-Dly :\n     ");	
	Set_i2c_Page( 1 );
	for (i=0; i < 16 + eccMode*2; i++) {
		reg = ReadSMBus(i2cIoCycDly[i]>>12, D, i2cIoCycDly[i]&0xFF);
		printf(" 0x%02X", reg);
		WriteSMBus(SPD, D, spdIoCycDly[i]&0xFF, reg);
		usleep(SPD_WRITE_DLY);
	}
	Set_i2c_Page( 0 );

	printf("\n\n TxDQDQS1(mmioWR):\n  cp:");	
	for(i=0;i<64+eccMode*8;i++) {
		CP[i] = ReadSMBus(i2cTXDQS1p[i]>>12, D, i2cTXDQS1p[i]&0xFF);
		usleep(FPGA_READ_DLY);
	}
	for (i=0; i < 16 + eccMode*2; i++) {
		cp[i] = (int)((CP[i*4] + CP[i*4+1] + CP[i*4+2]+ CP[i*4+3])/4);
		printf("%5d",cp[i]);
		WriteSMBus(SPD, D, spdTXDQS1[i]&0xFF, cp[i]);
		usleep(SPD_WRITE_DLY);
	}
	for (j=0; j < 4; j++) {
		printf("\n  %d :",j);
		for (i=0; i < 16 + eccMode*2; i++) {
			printf("%5d",CP[i*4+j] - cp[i]);
		}
	}

	printf("\n\n RxDQDQS1(FakeRD):\n  cp:");	
	for(i=0;i<64+eccMode*8;i++) {
		CP[i] = ReadSMBus(i2cRXDQS1p[i]>>12, D, i2cRXDQS1p[i]&0xFF);
		usleep(FPGA_READ_DLY);
	}
	for (i=0; i < 16 + eccMode*2; i++) {
		cp[i] = (int)((CP[i*4] + CP[i*4+1] + CP[i*4+2]+ CP[i*4+3])/4);
		printf("%5d",cp[i]);
		WriteSMBus(SPD, D, spdRXDQS1[i]&0xFF, cp[i]);
		usleep(SPD_WRITE_DLY);
	}
	for (j=0; j < 4; j++) {
		printf("\n  %d :",j);
		for (i=0; i < 16 + eccMode*2; i++) {
			printf("%5d",CP[i*4+j] - cp[i]);
		}
	}
	
	printf("\n\n TxDQDQS2(FakeWR):\n  cp:");	
	Set_i2c_Page( 1 );
	for (i=0; i < 16 + eccMode*2; i++) {
		reg = ReadSMBus(i2cTXDQS2[i]>>12, D, i2cTXDQS2[i]&0xFF);
		printf("%5d", reg);
		WriteSMBus(SPD, D, spdTXDQS2[i]&0xFF, reg);
		usleep(SPD_WRITE_DLY);
	}
	Set_i2c_Page( 0 );

	SetPageAddress(D, 0);

	printf("\n\n");
	exit(1);
}		

void ClearSpd(int D)
{
	int i, j;

	printf("\n Clear training data in SPD ...");

	SetPageAddress(D, 1);

	for(i=0x80;i<0xba;i++) {
		WriteSMBus(SPD, D, i, (int)0);
		usleep(SPD_WRITE_DLY);
	}

	for(i=0xd0;i<0x100;i++) {
		WriteSMBus(SPD, D, i, (int)0);
		usleep(SPD_WRITE_DLY);
	}

	SetPageAddress(D, 0);

	printf(" Done\n\n");
	exit(1);
}		

void LoadfromFile(int D)
{
	int i, j, RegValue;
	char *ptr, sline[512], tname[20];
	FILE *fp;
	
	if ((fp=fopen(logfile,"r"))==NULL) {
		fprintf(stdout,"\n\n Cannot open file %s !! \n\n",logfile); exit(1); 
	}

	fprintf(stdout,"\n Load training data from File - %s \n", logfile);

	fprintf(stdout,"\n     DQ :");
	for (j=0; j < 72; j++) fprintf(stdout,"%4d",j);
	
	while (fgets(sline,512,fp)!=NULL) {
		if ( strstr(sline,"TxDQDQS1:") != NULL) {
			printf("\nTxDQDQS1:");
			ptr = strstr(sline,":");	
			ptr++;

			for(i=0;i<72;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &RegValue);
				printf("%4d", RegValue);
				WriteSMBus( (i2cTXDQS1p[i]>>12), D, i2cTXDQS1p[i]&0xFF, RegValue);
				usleep(FPGA_WRITE_DLY);
			}
		}	
		if ( strstr(sline,"RxDQDQS1:") != NULL) {
			printf("\nRxDQDQS1:");
			ptr = strstr(sline,":");	
			ptr++;

			for(i=0;i<72;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &RegValue);
				printf("%4d", RegValue);
				WriteSMBus( (i2cRXDQS1p[i]>>12), D, i2cRXDQS1p[i]&0xFF, RegValue);
				usleep(FPGA_WRITE_DLY);
			}
		}	

		if ( strstr(sline,"TxDQDQS2:") != NULL) {
			printf("\nTxDQDQS2:");
			ptr = strstr(sline,":");	
			ptr++;

			Set_i2c_Page( 1 );
			
#if PER_BIT_TX_DQDQS2
			for(i=0;i<72;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &RegValue);
				printf("%4d", RegValue);
				WriteSMBus( (i2cTXDQS2p[i]>>12), D, i2cTXDQS2p[i]&0xFF, RegValue);
				usleep(FPGA_WRITE_DLY);
			}
#else
			for(i=0;i<18;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &RegValue);
				printf("%4d", RegValue);
				WriteSMBus( (i2cTXDQS2[i]>>12), D, i2cTXDQS2[i]&0xFF, RegValue);
				usleep(FPGA_WRITE_DLY);
			}			
#endif
			Set_i2c_Page( 0 );
		}			

		if ( strstr(sline,"TxVREFDQ:") != NULL) {
			printf("\nTxVREFDQ:");
			ptr = strstr(sline,":");	
			ptr++;

			Set_i2c_Page( 1 );
			for(i=0;i<18;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &RegValue);
				printf("%4d", RegValue);
				WriteSMBus( (i2cvRefDQS[i]>>12), D, i2cvRefDQS[i]&0xFF, RegValue);
				usleep(FPGA_WRITE_DLY);
			}			
			Set_i2c_Page( 0 );
		}

		if ( strstr(sline,"SIO-CYC :") != NULL) {
			printf("\nSIO-CYC :");
			ptr = strstr(sline,":");	
			ptr++;

			Set_i2c_Page( 1 );
			for(i=0;i<18;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &RegValue);
				printf("%4d", RegValue);
				WriteSMBus( (i2cIoCycDly[i]>>12), D, i2cIoCycDly[i]&0xFF, RegValue);
				usleep(FPGA_WRITE_DLY);
			}			
			Set_i2c_Page( 0 );
		}

		if ( strstr(sline,"CMD-CLK :") != NULL) {
			printf("\nCMD-CLK :");
			ptr = strstr(sline,":");	
			ptr++;
		
			for(i=0;i<2;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &RegValue);
				printf("%4d", RegValue);
			//	if(i==0) WriteSMBus(FPGA1, D, CMD_CK_ADDR, RegValue);
			//	else	 WriteSMBus(FPGA2, D, CMD_CK_ADDR, RegValue);
				WriteSMBus( (i2cCMDtoCK[i]>>12), D, i2cCMDtoCK[i]&0xFF, RegValue);
				usleep(FPGA_WRITE_DLY);
			}			
		}
		
		if ( strstr(sline,"CMD-MUX :") != NULL) {
			printf("\nCMD-MUX :");
			ptr = strstr(sline,":");	
			ptr++;
		
			for(i=0;i<8 + eccMode;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &RegValue);
				printf("%4d", RegValue);
				WriteSMBus( (i2cMuxSet[i]>>12), D, i2cMuxSet[i]&0xFF, RegValue);
				usleep(FPGA_WRITE_DLY);
			}
		}

		if ( strstr(sline,"CLK-DLY :") != NULL) {
			printf("\nCLK-DLY :");
			ptr = strstr(sline,":");	
			ptr++;
		
			for(i=0;i<2;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &RegValue);
				printf("%4d", RegValue);
				if(i==0) WriteSMBus(FPGA1, D, 0x7, RegValue);
				else	 WriteSMBus(FPGA2, D, 0x7, RegValue);
				usleep(FPGA_WRITE_DLY);
			}			
		}
	}

	printf("\n DIMM[%d] PLL Reset & load i/o delay ... ", D);
	fpga_pll_reset_high_low();
#if LOAD_IO_DELAY
	fpga_i_Dly_load_signal();		// 8.2.2016
	fpga_o_Dly_load_signal();		// 8.2.2016
#endif

	printf("\nDone\n\n");

	fclose(fp);

	exit(1);
}

void WritetoFile(int D)
{
	int i;
	unsigned char RegValue;
	time_t  timer;
	struct tm *tim;
	FILE *fp;
	
	timer = time(0);
	tim = localtime(&timer);

	if(logging==0) {
		sprintf(logfile, "%s-%s-%02d%02d-%02d%02d%02d.log", 
					//	sSN[D], hostname, tim->tm_hour, tim->tm_min, 
						dimm[D].sSN, hostname, tim->tm_hour, tim->tm_min, 
						tim->tm_mon+1, tim->tm_mday,tim->tm_year+1900);
	}

	fp = fopen(logfile,"w");
	fprintf(stdout,"\n Save training data to File - %s ...", logfile);

	fprintf(fp,"H/W REV : %08X, %08X",HWrev[0], HWrev[1]);
	fprintf(fp,"\n     DQ :");
	for (i=0;i<72;i++) fprintf(fp,"%4d",i);
			
	fprintf(fp,"\nTxDQDQS1:");	
	for (i=0;i<72;i++) {
		fprintf(fp," %3d", ReadSMBus( (i2cTXDQS1p[i]>>12), D, i2cTXDQS1p[i]&0xFF));
		usleep(FPGA_READ_DLY);
	}

	fprintf(fp,"\nRxDQDQS1:");	
	for (i=0;i<72;i++) {
		fprintf(fp," %3d", ReadSMBus( (i2cRXDQS1p[i]>>12), D, i2cRXDQS1p[i]&0xFF));
		usleep(FPGA_READ_DLY);
	}

	fprintf(fp,"\nTxDQDQS2:");	
	Set_i2c_Page( 1 );
#if PER_BIT_TX_DQDQS2
	for (i=0;i<72;i++) {
		fprintf(fp," %3d", ReadSMBus( (i2cTXDQS2p[i]>>12), D, i2cTXDQS2p[i]&0xFF));
		usleep(FPGA_READ_DLY);
	}
#else				
	for (i=0;i<18;i++) {
		fprintf(fp," %3d", ReadSMBus( (i2cTXDQS2[i]>>12), D, i2cTXDQS2[i]&0xFF));
		usleep(FPGA_READ_DLY);
	}
#endif

	fprintf(fp,"\nTxVREFDQ:");	
	for (i=0;i<18;i++) {
		fprintf(fp," %3d", ReadSMBus( (i2cvRefDQS[i]>>12), D, i2cvRefDQS[i]&0xFF));
		usleep(FPGA_READ_DLY);
	}

	fprintf(fp,"\nSIO-CYC :");	
	for (i=0;i<18;i++) {
		fprintf(fp," %3d", ReadSMBus( (i2cIoCycDly[i]>>12), D, i2cIoCycDly[i]&0xFF));
		usleep(FPGA_READ_DLY);
	}

	Set_i2c_Page( 0 );
	
	fprintf(fp,"\nCMD-CLK : %3d %3d", 
					ReadSMBus(FPGA1, D, CMD_CK_ADDR), 
					ReadSMBus(FPGA2, D, CMD_CK_ADDR));
	fprintf(fp,"\nCMD-MUX :");	
	for (i=0;i<8 + eccMode;i++) {
		fprintf(fp,"%4d", ReadSMBus( (i2cMuxSet[i]>>12), D, i2cMuxSet[i]&0xFF));
		usleep(FPGA_READ_DLY);
	}
	fprintf(fp,"\nCLK-DLY : %3d %3d\n", ReadSMBus(FPGA1, D, 7), ReadSMBus(FPGA2, D, 7));

	printf("Done\n\n");

	fclose(fp);

	exit(1);
}

unsigned long MmioCmd64B(int RdWr, int loops, unsigned long data[8], 
						 int bytes, off_t offset, unsigned long *eccxor)
{
	int i, j, k, bl, cb, cb1, cb2, ecc1, ecc2, retry=3;
	unsigned long mmio[8], xored[8], ecc, xor;

	if(loops<2)
		fprintf(stderr,"\n (%04d): %016lx %016lx %016lx %016lx %016lx %016lx %016lx %016lx",
					loops+1,
					data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);

	if(dataScramble) {
		for(j=0;j<8;j++) {
			data[j] ^= DS_MMIOCmdWindow[j];
		}
	}	
	mmio_addr = map_base + (target & MAP_MASK2) + offset;
	buff_addr = mmio_addr + RD_BUFF_OFFSET;

#if 0
	memcpy(mmio_addr, (void *)data, bytes);
#else
	memcpy_64B_movnti(mmio_addr, (void *)data, bytes);
#endif
	usleep(MemDelay);

#if CLFLUSH_CACHE_RANGE
	clflush_cache_range(mmio_addr, bytes);
#endif

	// i2c Data is good. However, DDR4 data is bad!!! ... 10/31/2016
	memcpy((void *)data, mmio_addr, bytes);		

	if(RdWr==RD) 
#if 1
		memcpy(buff_addr, mmio_addr, bytes);
#else
		memcpy_64B_movnti_2(buff_addr, mmio_addr, bytes);
#endif

	do {

		xor = *eccxor = 0;
	
		ReadMmioData(RdWr, mmio, &ecc);

		for(bl=0;bl<8;bl++) {
			xored[bl] = mmio[bl] ^ data[bl];
			xor |= xored[bl];
		}

		if(eccMode) {
			ecc1 = ecc & 0xffffffff;	
			ecc2 = (ecc>>32) & 0xffffffff;
			if(loops<2) 
				fprintf(stderr," %08x-%08x",ecc1, ecc2);

			for(cb1=0, bl=0;bl<4;bl++) {
				for(j=0;j<64;j++) 
					if( ( mmio[bl]>>j ) & 0x1 ) cb1 ^=CB[j][bl];
			}

			for(cb2=0, bl=4;bl<8;bl++) {
				for(j=0;j<64;j++) 
					if( ( mmio[bl]>>j ) & 0x1 ) cb2 ^=CB[j][bl-4];
			}		

			*eccxor = ((unsigned long)(ecc2^cb2) <<32 ) | ecc1^cb1;
		}		

		if((xor)&&(RdWr==RD)) {
			fprintf(stderr,"\n       :");
			for(xor=0, bl=0;bl<8;bl++) {
				xored[bl] = mmio[(bl+4)%8] ^ data[bl];
				xor |= xored[bl];
				fprintf(stderr,"\033[0;%dm %016lX\033[0m",xored[bl%8]?31:35, mmio[(bl+4)%8]);
			}
		}

		if((xor)||(*eccxor)||(debug))
		{
		//	printf("\n  Read(%03d):", loops);
			if((loops>=2) || (*eccxor))
			{
				fprintf(stderr,"\n (%04d): %016lx %016lx %016lx %016lx %016lx %016lx %016lx %016lx",
						loops+1,
						data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);			
				if(eccMode) 
				fprintf(stderr," %08x-%08x",ecc1, ecc2);
			}
		//	fprintf(stderr,"\n       :");	// 10/31/2016
			fprintf(stderr,"\n  XOR  :");
			for(bl=0;bl<8;bl++)
			//	fprintf(stderr,"\033[0;%dm %016lX\033[0m",xored[bl]?31:38, mmio[bl]);	// 10/31/2016
				fprintf(stderr,"\033[0;%dm %016lX\033[0m",xored[bl]?31:38, xored[bl]);
			if((eccMode) && (*eccxor)) 
				fprintf(stderr,"\033[0;31m %08x-%08x\033[0m",cb1, cb2);
		} else break;
		usleep(FPGA_SETPAGE_DLY);
		retry--;
	} while(retry&&(xor||*eccxor));

	return xor ;
}

void initEcctest(int D)
{
	int i, j, k, bl, cb;
	unsigned long data[8], mmio[8], ecc;
	
	mmio_addr = map_base + (target & MAP_MASK2) + CMD_OFFSET;

	printf("\n Write 64B to HV_MMIO_CMD window - %p\n", target + CMD_OFFSET);

	for(k=0;k<4;k++) {	
		data[0] = data[2] = data[4] = data[6] = 0;
		data[1] = data[3] = data[5] = data[7] = 0;

		for(i=0;i<64;i++) {

			data[k] = data[k+4] = 1UL<<i;

			printf("\n (%03d): %016lx %016lx %016lx %016lx",
						i+1, data[0], data[1], data[2], data[3]);

			memcpy_64B_movnti( mmio_addr, (void *)data, 64 );

			usleep(MemDelay);
	
			ReadMmioData(WR, mmio, &ecc);

			printf(" :");
			
			for(cb=0, bl=0;bl<4;bl++) {
				printf("\033[0;34m %016lX\033[0m", 
							(dataScramble)?mmio[bl]^DS_MMIOCmdWindow[bl]:mmio[bl]);
				for(j=0;j<64;j++) {
					if( ( mmio[bl]>>j ) & 0x1 ) cb ^=CB[j][bl];
				}
			}
		
		//	printf(" %08X (%08X) %s", ecc, cb, (ecc^cb)?"":"ERROR");
			printf(" %08X (%08X) %8x", ecc, cb, (ecc^cb));
	
			usleep(1000);
		}
		printf("\n");
	}

	printf("\n\n");

	exit(1);
}

void WrCmdtest(int testmode)
{
	int i, j, failcount=0, loops=2, iteration;
	unsigned long result, eccresult, mmio[8], data[8], Pat[2][8], bytes=1, ecc;
	
	if((testmode == 2) && (pattern1>2)) loops = pattern1;
	if((testmode == 2) && (pattern1==0)) {
		
		memcpy((void *)data, virt_addr + CMD_OFFSET, 64);

		ReadMmioData(WR, mmio, &ecc);

		printf("\n Read 64B from mmioCmd & i2c registers\n");
		printf("\n memory:");
		for(i=0;i<8;i++) printf(" %016lX", data[i]);
		
		printf("\n i2cReg:");
		for(i=0;i<8;i++) printf(" %016lX", mmio[i]);

		printf("\n XORed :");
		for(i=0;i<8;i++) printf(" %16lX", data[i]^mmio[i]);

		printf("\n\n");
			
		exit(1);

	}	

	ResetCounter();

	printf("\n Write 64B to HV_MMIO_CMD window - %p , loop= %d\n",
					target + CMD_OFFSET, loops);
	
	if(testmode == 3) {
#if 1
		loops = pattern1;
		bytes = pattern2;

		for(i=0;i<bytes;i++) 
			memset(data,(int)i+1,sizeof(data));

		for(i=0;i<loops;i++) {
			ResetCounter();
			memcpy_64B_movnti_2(virt_addr + CMD_OFFSET, (void *)data, bytes*64);

			printf("\n #%04d: WrCmd#: %4d,%4d   FakeRd#: %4d,%4d   WR#: %6d,%6d   RD#: %6d,%6d",
							i,
							(ReadSMBus(FPGA1, D, 0x7B)<<8) + ReadSMBus(FPGA1, D, 0x7C), 
							(ReadSMBus(FPGA2, D, 0x7B)<<8) + ReadSMBus(FPGA2, D, 0x7C),
							(ReadSMBus(FPGA1, D, 0x7E)<<8) + ReadSMBus(FPGA1, D, 0x7F), 
							(ReadSMBus(FPGA2, D, 0x7E)<<8) + ReadSMBus(FPGA2, D, 0x7F),
							(ReadSMBus(FPGA1, D, 0x60)<<8) + ReadSMBus(FPGA1, D, 0x61), 
							(ReadSMBus(FPGA2, D, 0x60)<<8) + ReadSMBus(FPGA2, D, 0x61), 
							(ReadSMBus(FPGA1, D, 0x62)<<8) + ReadSMBus(FPGA1, D, 0x63), 
							(ReadSMBus(FPGA2, D, 0x62)<<8) + ReadSMBus(FPGA2, D, 0x63)); 			
		}
		printf("\n\n");			
#else

		result = MmioCmd64B(WR, loops, data, 64, CMD_OFFSET, &eccresult);	

		if(result) fprintf(stderr," #%03d: %016lX,", ++failcount, result); 
		else	   fprintf(stderr," pass");

#endif
		exit(1);
	}

	Pat[0][0] = 0x1111111111111111; Pat[0][1] = 0x2222222222222222;
	Pat[0][2] = 0x3333333333333333; Pat[0][3] = 0x4444444444444444;
	Pat[0][4] = 0x5555555555555555; Pat[0][5] = 0x6666666666666666;
	Pat[0][6] = 0x7777777777777777; Pat[0][7] = 0x8888888888888888;
	memset(Pat[1],0,sizeof(Pat[1]));
	
//	if(debug) { Pat[1][4]=Pat[1][0]=1; }

	for(i=0;i<loops/2;i++) {
		for(iteration=0;iteration<2;iteration++) {

			result = MmioCmd64B(WR, i*2+iteration, Pat[iteration], 64, CMD_OFFSET, &eccresult);	

			if(result||eccresult) {
		 		fprintf(stderr," #%03d: %016lX,", ++failcount, result); 
		 	//	if(eccresult) fprintf(stderr," %016lX,", eccresult); 
				PrintCounter();	
				if(failstop) exit(1);	// 10.7.2016
			} else {
				if(loops<2) printf(" pass");
			}
			if(loops==2) {
				if(!result) {
					fprintf(stderr,"\n\n Counter: ");
					PrintCounter();
				}
				printf("\n\n");
				exit(1);
			}
		}
	//	usleep(MEM_CPY_DELAY);
	}

	fprintf(stderr,"\n\n Counter: ");
	PrintCounter(); 
		
	if(failcount) fprintf(stderr,"\n\n Total Fail Count: %d\n\n", failcount);
	else		  fprintf(stderr,"\n\n PASS !\n\n");

	exit(1);
}

void FakeWrtest(int loopCounter)
{
	int i, j, failcount=0, loops=1, iteration;
	unsigned long result, eccresult, data[8];
	int RData[16];

	memset(data,0,sizeof(data));
	
	if(loopCounter) loops = loopCounter;
	
	ResetCounter();

	buff_addr = virt_addr + GEN_WR_OFFSET;
	printf("\n FakeWr test ... Write '0' to GEN_WR_OFFSET window - %p, loops= %d\n",
					target + GEN_WR_OFFSET, loops);
 
	fprintf(stderr,"\n loop DQ[63:32]-DQ[31:0], BL0 to BL7");

	for(i=0;i<loops;i++) {
		memcpy_64B_movnti(buff_addr, data, 64);
		clflush_cache_range(virt_addr + GEN_WR_OFFSET, 64);

#if 0
	//	fprintf(stderr,"\n (%04d): %016lx %016lx %016lx %016lx %016lx %016lx %016lx %016lx",
	//				i+1,
	//				data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
		memcpy(data, buff_addr, 64);

		fprintf(stderr,"\n %4d: %016lX %016lX %016lX %016lX %016lX %016lX %016lX %016lX",
					i+1,
					data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
#else
		memcpy_64B_movnti((void *)RData, buff_addr, 64);
		fprintf(stderr,"\n %4d",i+1);
		for(j=0;j<8;j++) 
			fprintf(stderr," %08X-%08X", RData[j*2+1], RData[j*2]);
		//	fprintf(stderr,"      %08X", RData[j*2]);
		
#endif
	}

//	fprintf(stderr,"\n\n Counter: ");
//	PrintCounter(); 
		
//	if(failcount) fprintf(stderr,"\n\n Total Fail Count: %d\n\n", failcount);
//	else		  fprintf(stderr,"\n\n PASS !\n\n");
	fprintf(stderr,"\n\n");

	exit(1);
}

void FakeRdtestv0(int testmode)
{
	int i, j, failcount=0, loops=2, iteration;
	unsigned long result, eccresult, data[8], Pat[2][8], bytes=1;

	buff_addr = virt_addr + RD_BUFF_OFFSET;
	
	if((testmode == 2) && (pattern1>2)) loops = pattern1;
	
	ResetCounter();

	printf("\n Write 64B to FAKE_RD_TEST window - %p , loop= %d\n", target, loops);

	if(testmode == 3) {
		loops = pattern1;
		bytes = pattern2;

		for(i=0;i<bytes;i++) {
			memset(data,(int)i+1,sizeof(data));
			memcpy_64B_movnti( virt_addr+64*i, (void *)data, 64 );
		}

		for(i=0;i<loops;i++) {
			ResetCounter();
		//	memcpy_64B_movnti_2(buff_addr, virt_addr, bytes*64);
			memcpy(buff_addr, virt_addr, bytes*64);

			printf("\n #%04d: WrCmd#: %4d,%4d   FakeRd#: %4d,%4d   WR#: %6d,%6d   RD#: %6d,%6d",
							i,
							(ReadSMBus(FPGA1, D, 0x7B)<<8)+ReadSMBus(FPGA1, D, 0x7C), 
							(ReadSMBus(FPGA2, D, 0x7B)<<8)+ReadSMBus(FPGA2, D, 0x7C),
							(ReadSMBus(FPGA1, D, 0x7E)<<8)+ReadSMBus(FPGA1, D, 0x7F), 
							(ReadSMBus(FPGA2, D, 0x7E)<<8)+ReadSMBus(FPGA2, D, 0x7F),
							(ReadSMBus(FPGA1, D, 0x60)<<8)+ReadSMBus(FPGA1, D, 0x61), 
							(ReadSMBus(FPGA2, D, 0x60)<<8)+ReadSMBus(FPGA2, D, 0x61), 
							(ReadSMBus(FPGA1, D, 0x62)<<8)+ReadSMBus(FPGA1, D, 0x63), 
							(ReadSMBus(FPGA2, D, 0x62)<<8)+ReadSMBus(FPGA2, D, 0x63)); 			
		}
		printf("\n\n");
		exit(1);
	}

	Pat[0][0] = 0x1111111111111111; Pat[0][1] = 0x2222222222222222;
	Pat[0][2] = 0x3333333333333333; Pat[0][3] = 0x4444444444444444;
	Pat[0][4] = 0x5555555555555555; Pat[0][5] = 0x6666666666666666;
	Pat[0][6] = 0x7777777777777777; Pat[0][7] = 0x8888888888888888;
	memset(Pat[1],0,sizeof(Pat[1]));

	for(i=0;i<loops/2;i++) {
		for(iteration=0;iteration<2;iteration++) {

			result = MmioCmd64B(RD, i*2+iteration, Pat[iteration], 64, FAKE_RD_OFFSET, &eccresult);	

			if(result||eccresult) {
		 		fprintf(stderr," #%03d: %016lX,", ++failcount, result); 
		 	//	if(eccresult) fprintf(stderr," %016lX,", eccresult); 
				PrintCounter();	
			} else {
				if(loops<2) printf(" pass");
			}
			if(loops==2) {
				if(!result) {
					fprintf(stderr,"\n\n Counter: ");
					PrintCounter();
				}					
				printf("\n\n");
				exit(1);
			}
		}
	}

	fprintf(stderr,"\n\n Counter: ");
	PrintCounter(); 
		
	if(failcount) fprintf(stderr,"\n\n Total Fail Count: %d\n\n", failcount);
	else		  fprintf(stderr,"\n\n PASS !\n\n");

	exit(1);
}

void SendCmdtest(unsigned char Command)
{
	int i, j, failcount=0, loops=2, iteration;
	unsigned long result, eccresult, data[8], Pat[2][8], status[8], page_addr;
	int RData[16];
	unsigned char tag=1, sCmd[80];
	unsigned int sector=8, lba=0;

	struct hv_rw_cmd *pBsm;
	struct hv_query_cmd *pQuery;

	pBsm = (struct hv_rw_cmd *)malloc(sizeof(struct hv_rw_cmd));
	pQuery = (struct hv_query_cmd *) malloc(sizeof(struct hv_rw_cmd));

	memset(pBsm, 0, 64);
	memset(pQuery, 0, 64);	
	memset(data,0,sizeof(data));

	mmio_addr = virt_addr + CMD_OFFSET;

	if(targetPage) {
		buff_addr = virt_page;
		page_addr = targetPage - LRDIMM_SIZE - SYS_RESERVED_MEM_SIZE;
	}else {
		buff_addr = virt_addr + RD_BUFF_OFFSET;
		page_addr = target + RD_BUFF_OFFSET - LRDIMM_SIZE - SYS_RESERVED_MEM_SIZE;
	}

	pBsm->command[0].cmd_field.cmd = Command;
	pBsm->command[0].cmd_field.tag = tag;
	pBsm->command[1].cmd_field_32b = pBsm->command[0].cmd_field_32b;
	pBsm->sector[0] = sector;
	pBsm->sector[1] = sector;
	pBsm->lba[0] = lba;
	pBsm->lba[1] = lba;
	pBsm->dram_addr[0] = (unsigned int)((unsigned long)(page_addr)>>3);	
	pBsm->dram_addr[1] = pBsm->dram_addr[0];
	pBsm->checksum[0] = pBsm->command[0].cmd_field_32b^pBsm->sector[0]^pBsm->lba[0]^pBsm->dram_addr[0];
	pBsm->checksum[1] = pBsm->command[1].cmd_field_32b^pBsm->sector[1]^pBsm->lba[1]^pBsm->dram_addr[1];	

	pQuery->command[0].cmd_field.cmd = QUERY;
	pQuery->command[0].cmd_field.query_tag = (unsigned char)0x80;
	pQuery->command[0].cmd_field.cmd_tag = tag;
	pQuery->command[1].cmd_field_32b = pQuery->command[0].cmd_field_32b;
	pQuery->checksum[0] = pQuery->command[0].cmd_field_32b;
	pQuery->checksum[1] = pQuery->command[1].cmd_field_32b;

	Pat[0][0] = 0x1111111111111111; Pat[0][1] = 0x2222222222222222;
	Pat[0][2] = 0x3333333333333333; Pat[0][3] = 0x4444444444444444;
	Pat[0][4] = 0x5555555555555555; Pat[0][5] = 0x6666666666666666;
	Pat[0][6] = 0x7777777777777777; Pat[0][7] = 0x8888888888888888;

	memset(Pat[1],0,sizeof(Pat[1]));

	if (Command == MMLS_READ) 		sprintf(sCmd,"MMLS_READ(eMMC-to-DDR4)");
	else if(Command == MMLS_WRITE)	sprintf(sCmd,"MMLS_WRITE(DDR4-to-eMMC)");
	else if(Command == TBM_READ) 	sprintf(sCmd,"TBM_READ(TBM-to-DDR4)");
	else if(Command == TBM_WRITE) 	sprintf(sCmd,"TBM_WRITE(DDR4-to-TBM)");
	else if(Command == QUERY) 		sprintf(sCmd,"QUERY");
	else { printf("\n\n Unknown Command !!\n\n"); exit(1); }

	printf("\n Write %s Cmd to %p, 4KB Data @%p, Rank Addr - %p (%p)\n",
					 sCmd, target + CMD_OFFSET, target + RD_BUFF_OFFSET,	
					 page_addr,	(page_addr)>>3);

	if(Command==MMLS_WRITE||Command==MMLS_READ||Command==TBM_WRITE||Command==TBM_READ) 
	{
		memcpy_64B_movnti( buff_addr, (void *)Pat[0], 4096 );	// Init. 4KB Data for FakeRD operation
	
		memcpy( mmio_addr, (void *)pBsm, 64 );					// Send mmioCmd
		memcpy((void *)RData, mmio_addr, 64);					// Check mmioCmd
	//	printf("\n Send Cmd:");
		for(j=0;j<8;j++) printf("\n  BL%d  Master: 0x%08X  Slave: 0x%08X", j, RData[j*2], RData[j*2+1]);
	} else if(Command==QUERY) {
		memcpy( mmio_addr, (void *)pQuery, 64 );				// Send mmioCmd
		memcpy((void *)RData, mmio_addr, 64);					// Check mmioCmd
		printf("\n Send Cmd:");
		for(j=0;j<8;j++) printf("\n  BL%d  Master: 0x%08X  Slave: 0x%08X", j, RData[j*2], RData[j*2+1]);

		buff_addr = virt_addr + QUERY_STATUS_OFFSET;
		printf("\n\n Check Query ... Write '0' to QUERY_STATUS_OFFSET window - %p\n",
					target + QUERY_STATUS_OFFSET);

		memcpy_64B_movnti(buff_addr, data, 64);					// Fake WR
		clflush_cache_range(virt_addr + QUERY_STATUS_OFFSET, 64);
		memcpy_64B_movnti((void *)RData, buff_addr, 64);		// Read Status
	//	printf("\n    :");
		for(j=0;j<8;j++) 
			printf("\n  BL%d  Master: 0x%08X  Slave: 0x%08X", j, RData[j*2], RData[j*2+1]);		
	}
	
	printf("\n\n");

	exit(1);
}

void cmdMuxCycTest(int D, int tgt_value, int expectedOffset, int page)
{
	int i, j, bl, cmdMuxOld[18], cmdMuxNew[18], rdata, cdata;

	printf("\n %s testing ... Find '%x' in each Nibble and expectedOffset(BL) is %d", 
					page==1?"CMD-Mux-Cycle":"FakeRD-Mux-Cycle",tgt_value, expectedOffset); 
	printf("\n");

	if(page==2) Set_i2c_Page( page );

	for(i=0; i < 8 + eccMode ; i++) {
		cdata = ReadSMBus( i2cMuxSet[i]>>12, D, (i2cMuxSet[i] & 0xFF));
		cmdMuxOld[i*2] = cdata & 0xF;
		cmdMuxOld[i*2+1] = (cdata>>4) & 0xF;
	//	printf(" %2X", cmdMuxOld[i]);
	}

	Set_i2c_Page( page );

	for(i=0; i < 16 + eccMode * 2 ; i++) {
		cmdMuxNew[i] = cmdMuxOld[i];
		j=(i>>1);
		
		for(bl=0;bl<8;bl++) {
			rdata = ReadSMBus( i2cMuxCmd[j]>>12, D, (i2cMuxCmd[j] & 0xFF) + bl*16 );
			if(i%2==0) rdata = rdata & 0xF;
			else       rdata = (rdata>>4) & 0xF;
			if (tgt_value == rdata) {
				cmdMuxNew[i] = cmdMuxOld[i] + (expectedOffset - bl);
				if(cmdMuxNew[i] < 0) {
					printf("\n\t Error!! Nibble %d, new target value is %d", cmdMuxNew[i]);
					cmdMuxNew[i] = cmdMuxOld[i];
				}		
				break;
			}
		}
		if(bl==8) printf("\n\033[1;31m ERROR!! Nibble %d has no data %d !!!\033[0m\n", i, tgt_value);

		if(i>15) cmdMuxNew[i] = cmdMuxNew[6];	// Force ECC value from DQS6	9/27/2016
			
	}

	Set_i2c_Page( 0 );

	printf("\n  FROM: ");
	for(i=0; i < 8 + eccMode ; i++) {
		printf("  0x%02x", (cmdMuxOld[i*2+1]<<4) + cmdMuxOld[i*2]);
	}
	printf("\n    TO: ");	
	for(i=0; i < 8 + eccMode ; i++) {
		printf("  0x%02x", (cmdMuxNew[i*2+1]<<4) + cmdMuxNew[i*2]);
		if(update_hw) {
			WriteSMBus(i2cMuxSet[i]>>12,D,(i2cMuxSet[i] & 0xFF),(cmdMuxNew[i*2+1]<<4)+cmdMuxNew[i*2]);
			usleep(FPGA_WRITE_DLY);
		}		
	}
	if(update_hw) 
		printf("\n\n Update Done");

	printf("\n\n");

	exit(1);	
}

void help(int argc, char **argv)
{
	int i;
	char *mesg =
		"\n\n Usage: %-20s           S/W Version: %s"
		"\n  -h --help"
		"\n  -i --i2c                              | dump FPGA i2c data"
		"\n  -d --dimm [0~23]                      | DIMM ID for training"
		"\n  -t --tid  [1|2|4|8|16] {<start> <end> {<step>}}"
		"\n              1= WR-CK"	// fpga-V47, 9/20/2016
//FIXME
//		"\n              2= RD-CMD-CK"
		"\n              4= Tx-DQDQS1(WR)"
		"\n              8= Rx-DQDQS1(FakeRD)"
		"\n           0x10= Tx-DQDQS2(FakeWR)"
		"\n           0x20= Tx-Vref(WR),  ex) 0x1F= all"
		"\n          0x400= Tx-DQDQS1v0(WR) Both DQS1 and TxDQDQS1"
		"\n          0x800= Rx-DQDQS1v0(FakeRD) Both RxDQS1 and RxDQDQS1"
		"\n         0x1000= Tx-DQDQS2v0(FakeRD)"
		"\n  -s --start [0~256]"
		"\n  -e --end   [0~256]"
		"\n  -z --step  [1~2^n]"
		"\n  -V --vref <value>                     | Set txVREF"
		"\n  -p --pattern <data1> <data2>          | Set Training data1, data2"
		"\n  -u --update                           | update_hw, default - no update"
		"\n  -U --Update                           | update_hw & show summary"
		"\n  -l --log                              | enable logging, default - no logging"
		"\n  -A --Address                          | Target Training Address"
		"\n  -Page --PageAddress                   | Target FakeWR Address"
		"\n  -P --Pattern {<data1> <data2>}        | Program Tx-DQDQS2(FakeWR) data pattern"
	    "\n                                        | <data1> <data2> or HVT_FPGA[1|2]_DATA[1|2]"
		"\n  -B --bcom_sw                          | {re}set BCOM[3:0] MUX to Low"
		"\n  -E --Enable                           | Enable Slave I/O i/f for FakeWR"
		"\n  -R --Reset                            | Reset training mode"
		"\n  -D --display [0|1|2]                  | TxDQDQS2 Debug(display) flag, default= 0"
		"\n  -M --cmd_dq_mux [0~0xF] <0~7>         | cmd-dq-mux delay setting, - <pattern>, <bl>"
		"\n  -M2 --fakeRd_dq_mux [0~0xF] <0~7>     | FakeRd-dq-mux delay setting, - <pattern>, <bl>"
		"\n  -T --test <iteration> <#64B>          | Write command test"
		"\n  -T --test <iteration>                 | Write command test"
		"\n  -CMD --Command <cmd>                  | BSM/MMLS/Query test"
		"\n                 0x90 FakeWR(TBM-to-DDR4)"
		"\n                 0x80 FakeRD(DDR4-to-TBM)"
		"\n                 0x70 Query Command Statis"
		"\n  -FW --FakeWr <iteration>              | Fake Wr test"
		"\n  -GWS --G.W.S <iteration>              | Read G.W.S"
		"\n  -QCS --Q.C.S <iteration>              | Read Q.C.S"
		"\n  -K --clock <delay>                    | Set internal CLK delay"
		"\n  -5 --fpga1 <addr> <data>              |"
		"\n  -7 --fpga2 <addr> <data>              |"
		"\n  -f --fpga <addr> <data>               | Write both fpga <addr> <data>"
		"\n  -off --offset <test> <offset>         | Set <test> offset +/- <offset>"
		"\n  -MS --msdelay <delay>                 | Set <delay> ms after memcpy()"
		"\n  -ecc --ECC                            | Check ECC Table"
		"\n  -pll --pll_reset                      | FPGA PLL RESET"
		"\n  -dbg --debug                          | Enable debugging flag"
		"\n  -dly --iodly <clock>                  | Set Slave IO Clock Delay <clk>, 0= reset"
		"\n"
		"\n  export HVT_DEBUG=[0|1|2]              | Tx-DQDQS2(FakeWR) - 0= FPGA, 1/2= DDR4 RD result"
		"\n  export HVT_FPGA[1|2]_DATA[1|2]=<data> | set Tx-DQDQS2(FakeWR) training data"
		"\n  printenv | grep HVT_                  | Check Environment Variable"
		"\n"
		"\n  -v --viewspd                          | view spd data" 
		"\n  -S --save {<filename>}                | [S]ave training data to file"
		"\n  -L --load {<filename>}                | Load training data from file"
		"\n  -S2 --savetospd                       | Save training data to SPD"
		"\n  -L2 --loadfromspd                     | Load training data from SPD"
		"\n  -C --clear                            | [C]lear training data in SPD"
		"\n  -ts --time                            | Show test time/loop"
		"\n  -cnt --counter                        | Show counter"
		"\n  -cl --colorlimit [0~255]              | Set PW color limit"
		"\n\n";

//	printf("\n Scan DIMM \n");
	for(D=0;D<24;D++) {
		N =  D<12?0:1;
		C = (int)(D)<12?(int)(D/3):(int)(D/3)-4;
			
		SetPageAddress(D, 0);	
		usleep(FPGA_SETPAGE_DLY);		// 8-2-2016, Lenovo
  		if(ReadSMBus(SPD,D,0) == 0x23) {
			printf("\n DIMM[%d] N%d.C%d.D%d : ", D, N, C, (int)(D%3));

			getSN(D);

			printf("%-18s S/N - %s %6.1fC", dimm[D].sPN, dimm[D].sSN, dimm[D].Temp/16.);

			if(ReadSMBus(FPGA1,D,2) != 0xFF) {
				printf("   FPGA H/W - %02X-%02X.%02X.%02X, %02X-%02X.%02X.%02X",
										ReadSMBus(FPGA1,D,1), ReadSMBus(FPGA1,D,2), 
										ReadSMBus(FPGA1,D,3), ReadSMBus(FPGA1,D,4),
										ReadSMBus(FPGA2,D,1), ReadSMBus(FPGA2,D,2), 
										ReadSMBus(FPGA2,D,3), ReadSMBus(FPGA2,D,4));
			}
		}
	}

	fprintf (stdout, mesg, argv[0], version);

	exit(1);
}

int main( int argc, char **argv ) {

	Word Wvalue;
	int i, j, addr, dti, data, mcmtr, tdata;
	struct tm *tim;
	unsigned long tohm, tohm0, tohm1;

	FILE *pipe_fp;
	int expectedOffset=0;

//MK-begin
	/* Get debug instruction from the user */
	if ( (debug_str = getenv("HVT_DEBUG")) != NULL )
		debug_flag = atoi(debug_str);
//MK-end

	nominal_frequency = ((rdmsr(0, PLATFORM_INFO_ADDR) >> 8) & 0xFF) * 100000000ULL;

	memset(rDQS, -1, sizeof(rDQS));
	memset(rDQ, -1, sizeof(rDQ));
	sprintf(logfile, "result.log");

	// exit with useful diagnostic if iopl() causes a segfault
	if ( iopl( 3 ) ) { perror( "iopl" ); return 1; }
	Wvalue = ioperm( 0xCF8, 8, 1);	// set port input/output permissions
	if(Wvalue) printf("ioperm return value is: 0x%x\n", Wvalue);

	pipe_fp = popen("dmidecode -t 1 | grep Product | awk '{print $0}'","r");
	fgets(system_info,80,pipe_fp);
	system_info[strlen(system_info)-1]='\0';
	pclose(pipe_fp);

	gethostname(hostname, sizeof hostname);

	Bus = (MMioReadDword(0,5,0,0x108)>>8) & 0xFF;
	mcmtr = MMioReadDword(Bus,19,0,0x7C);
	if(MMioReadDword(Bus,18,4,0)==0x2F608086) HA=1;		// HSW - HA0: 2FA0, HA1:2F60
	if(MMioReadDword(Bus,18,4,0)==0x6F608086) HA=1;		// BDW - HA0: 6FA0, HA1:6F60
	
#if CHECK_ECC_MODE
	if(mcmtr&0x4) {					// if MCMTR bit2 == 1, ECC mode is ON
		eccMode = 1;
//		MMioWriteDword(Bus,19,0,0x7c,mcmtr&0xfffffffB);	
//		printf("\n MCMTR : %08X ==> %08X \n\n", mcmtr,mcmtr&0xfffffffB);
	}
#endif	

	tohm0 = MMioReadDword(0,5,0,0xd4);
	tohm1 = MMioReadDword(0,5,0,0xd8);
	tohm0 = GET_BITFIELD(tohm0, 26, 31);

	tohm = ((tohm1 << 6) | tohm0) << 26;
	tohm = (tohm | 0x3FFFFFF) + 1;

	target = tohm - HV_TRAINING_OFFSET;	

	for (i = 1; i < argc; i++) {
    	char *arg = argv[i];
		if (strcmp (arg, "-h") == 0 || strcmp (arg, "--help") == 0) help(argc, argv);
		if (strcmp (arg, "-i") == 0 || strcmp (arg, "--i2c") == 0) listing=1;
		if (strcmp (arg, "-u") == 0 || strcmp (arg, "--update") == 0) update_hw=1;
		if (strcmp (arg, "-R") == 0 || strcmp (arg, "--Reset") == 0) ModeReset=1;
		if (strcmp (arg, "-dbg") == 0 || strcmp (arg, "--debug") == 0) debug=1;
		if (strcmp (arg, "-pll") == 0 || strcmp (arg, "--pll_reset") == 0) PLLReset=1;
		if (strcmp (arg, "-U") == 0 || strcmp (arg, "--Update") == 0) {update_hw=1; showsummary=1;}
		if (strcmp (arg, "-dly") == 0 || strcmp (arg, "--iodly") == 0) {
			if((i < (argc-1)) ) {
				SetIoCycDly = 1;
				CycDly = atoi(argv[i+1]);
				if(CycDly<0 || CycDly>2) exit(1);
			}						
		}
		if (strcmp (arg, "-l") == 0 || strcmp (arg, "--log") == 0) {
			logging=1;
			if(i <(argc-1)) {
				sprintf(logfile, argv[i+1]);
				if(strlen(logfile)==0) {printf("\n Error --logfile '%s'\n",argv[i+1]); exit(1);}
			}
		}
		if (strcmp (arg, "-ts") == 0 || strcmp (arg, "--time") == 0) TestTime=1;
		if (strcmp (arg, "-cnt") == 0 || strcmp (arg, "--counter") == 0) ShowCounter=1;
		if (strcmp (arg, "-cl") == 0 || strcmp (arg, "--colorlimit") == 0) colorlimit=atoi(argv[i+1]);
		if (strcmp (arg, "-t") == 0 || strcmp (arg, "--tid") == 0) {
			Training |= strtoul(argv[i+1],0,0);
			if((i < (argc-3)) && (isdigit((int)argv[i+2][0]) && isdigit((int)argv[i+3][0]))) {
				if(strtoul(argv[i+1],0,0) & CMD_CK) { start_lp1 = atoi(argv[i+2]); end_lp1 = atoi(argv[i+3]); }
				if(strtoul(argv[i+1],0,0) & TX_DQ_DQS_0) { start_lp400 = atoi(argv[i+2]); end_lp400 = atoi(argv[i+3]); }
				if(strtoul(argv[i+1],0,0) & TX_DQ_DQS_1) { start_lp4 = atoi(argv[i+2]); end_lp4 = atoi(argv[i+3]); }
				if(strtoul(argv[i+1],0,0) & RX_DQ_DQS_0) { start_lp800 = atoi(argv[i+2]); end_lp800 = atoi(argv[i+3]); }	
				if(strtoul(argv[i+1],0,0) & RX_DQ_DQS_1) { start_lp8 = atoi(argv[i+2]); end_lp8 = atoi(argv[i+3]); }	
				if(strtoul(argv[i+1],0,0) & TX_DQ_DQS_2_0) { start_lp1600 = atoi(argv[i+2]); end_lp1600 = atoi(argv[i+3]); }	
				if(strtoul(argv[i+1],0,0) & TX_DQ_DQS_2) { start_lp16 = atoi(argv[i+2]); end_lp16 = atoi(argv[i+3]); }	
				if(strtoul(argv[i+1],0,0) & TX_VREF) { start_lp32 = atoi(argv[i+2]); end_lp32 = atoi(argv[i+3]); }				
			}
			if((i < (argc-4)) && (isdigit((int)argv[i+2][0]) && isdigit((int)argv[i+3][0]) && isdigit((int)argv[i+4][0]))) {
				if(strtoul(argv[i+1],0,0) & CMD_CK) { start_lp1 = atoi(argv[i+2]); end_lp1 = atoi(argv[i+3]); step1 = atoi(argv[i+4]); }
				if(strtoul(argv[i+1],0,0) & TX_DQ_DQS_0) { start_lp400 = atoi(argv[i+2]); end_lp400 = atoi(argv[i+3]); step400 = atoi(argv[i+4]); }
				if(strtoul(argv[i+1],0,0) & TX_DQ_DQS_1) { start_lp4 = atoi(argv[i+2]); end_lp4 = atoi(argv[i+3]); step4 = atoi(argv[i+4]); }
				if(strtoul(argv[i+1],0,0) & RX_DQ_DQS_0) { start_lp800 = atoi(argv[i+2]); end_lp800 = atoi(argv[i+3]); step800 = atoi(argv[i+4]); }	
				if(strtoul(argv[i+1],0,0) & RX_DQ_DQS_1) { start_lp8 = atoi(argv[i+2]); end_lp8 = atoi(argv[i+3]); step8 = atoi(argv[i+4]); }	
				if(strtoul(argv[i+1],0,0) & TX_DQ_DQS_2_0) { start_lp1600 = atoi(argv[i+2]); end_lp1600 = atoi(argv[i+3]); step1600 = atoi(argv[i+4]); }	
				if(strtoul(argv[i+1],0,0) & TX_DQ_DQS_2) { start_lp16 = atoi(argv[i+2]); end_lp16 = atoi(argv[i+3]); step16 = atoi(argv[i+4]); }	
				if(strtoul(argv[i+1],0,0) & TX_VREF) { start_lp32 = atoi(argv[i+2]); end_lp32 = atoi(argv[i+3]); step32 = atoi(argv[i+4]); }				
			}		
		}
		if (strcmp (arg, "-s") == 0 || strcmp (arg, "--start") == 0) {
			start_lp = start_lp1 = start_lp4 = start_lp8 = start_lp16 = start_lp32 = start_lp400 = start_lp800 = start_lp1600 = atoi(argv[i+1]);
		}
		if (strcmp (arg, "-e") == 0 || strcmp (arg, "--end") == 0) {
			end_lp = end_lp1 = end_lp4 = end_lp8 = end_lp16 = end_lp32 = end_lp400 = end_lp800 = end_lp1600 = atoi(argv[i+1]);
		}
		if (strcmp (arg, "-z") == 0 || strcmp (arg, "--step") == 0) {
			step = step1 = step4 = step8 = step16 = step32 = step400 = step800 = step1600 = atoi(argv[i+1]);
		}
		if (strcmp (arg, "-v") == 0 || strcmp (arg, "--viewspd") == 0) viewspd=1;
		if (strcmp (arg, "-L") == 0 || strcmp (arg, "--load") == 0) {
			loadfromfile=1;
			if(i <(argc-1)) {
				sprintf(logfile, argv[i+1]);
				if(strlen(logfile)==0) {printf("\n Error <filename> '%s'\n",argv[i+1]); exit(1);}
			}
		}
		if (strcmp (arg, "-S") == 0 || strcmp (arg, "--save") == 0) {
			savetofile=1;
			if(i <(argc-1)) {
				sprintf(logfile, argv[i+1]);
				if(strlen(logfile)==0) {printf("\n Error <filename> '%s'\n",argv[i+1]); exit(1);}
				logging = 1;
			}
		}
		if (strcmp (arg, "-S2") == 0 || strcmp (arg, "--savetospd") == 0)  savetospd=1;
		if (strcmp (arg, "-L2") == 0 || strcmp (arg, "--loadfromspd") == 0) loadfromspd=1;
		if (strcmp (arg, "-C") == 0 || strcmp (arg, "--clear") == 0) clearspd=1;
		
		if (strcmp (arg, "-d") == 0 || strcmp (arg, "--dimm") == 0) {
			if(!isdigit((int)argv[i+1][0])) {printf("\n Error --dimm id '%s'\n",argv[i+1]); exit(1);}
			D = atoi(argv[i+1]);
			if(D<0 || D>23) {printf("\n Error --dimm id '%s'\n",argv[i+1]); exit(1);}
		}
		if (strcmp (arg, "-P") == 0 || strcmp (arg, "--Pattern") == 0) {
			write_fpga_data=1;
			if(i < (argc-2)) {
				write_fpga_data=2;
				data1 = (unsigned int) strtoul(argv[i+1],0,0);
				data2 = (unsigned int) strtoul(argv[i+2],0,0);					
			//	printf("\n DIMM[%d] write_fpga_data=%d Data1= 0x%x, Data2= 0x%x", 
			//				D, write_fpga_data, fpga1_data1, fpga1_data2);
			}				
		}
		if (strcmp (arg, "-p") == 0 || strcmp (arg, "--pattern") == 0) {
			if(i < (argc-2)) {
				SetPattern=1;
				data1 = strtoul(argv[i+1],0,0);
				data2 = strtoul(argv[i+2],0,0);					
			}				
		}		
		if (strcmp (arg, "-B") == 0 || strcmp (arg, "--bcom_sw") == 0) bcomsw = 1;
		if (strcmp (arg, "-E") == 0 || strcmp (arg, "--Enable") == 0) slaveON = 2;
		if (strcmp (arg, "-D") == 0 || strcmp (arg, "--display") == 0) debug_flag = atoi(argv[i+1]);

		if (strcmp (arg, "-off") == 0 || strcmp (arg, "--offset") == 0) {
			SetOffset = 1;
			if(i < (argc-2) && isdigit((int)argv[i+1][0]) ) {
				Training = strtoul(argv[i+1],0,0);
				data = strtoul(argv[i+2],0,0);
			} else exit(1);
		}
		if ((strcmp (arg, "-5") == 0 || strcmp (arg, "--fpga1") == 0) ||
			(strcmp (arg, "-7") == 0 || strcmp (arg, "--fpga2") == 0) ||
			(strcmp (arg, "-f") == 0 || strcmp (arg, "--fpga") == 0)
			) {
			if (strcmp (arg, "-5") == 0 || strcmp (arg, "--fpga1") == 0) dti = 5;
			if (strcmp (arg, "-7") == 0 || strcmp (arg, "--fpga2") == 0) dti = 7;
			if (strcmp (arg, "-f") == 0 || strcmp (arg, "--fpga") == 0) dti = 5+7;
			if(i < (argc-1) && (isdigit((int)argv[i+1][0])) ) {
				i2cRdWr = 1;
				addr = strtoul(argv[i+1],0,0);
			}
			if(i < (argc-2) && (isdigit((int)argv[i+1][0]) && isdigit((int)argv[i+2][0]))) {
				i2cRdWr = 2;
				addr = strtoul(argv[i+1],0,0);
				data = strtoul(argv[i+2],0,0);					
			}
		//	printf("\n DIMM[%d] i2cRdWr=%d dti= %d Addr= 0x%x, Write Data= 0x%02X\n\n", D, i2cRdWr, dti, addr, data);
		//	exit(1);
		}

		if (strcmp (arg, "-ecc") == 0 || strcmp (arg, "--ECC") == 0) initEcc=1;
		
		if (strcmp (arg, "-T") == 0 || strcmp (arg, "--test") == 0) {
			wrCmdtest = 1;
			if((i < (argc-1)) && (isdigit((int)argv[i+1][0])) ) {
				wrCmdtest = 2;
				pattern1 = strtoul(argv[i+1],0,0);
				pattern2 = 0;
			} 	
			if((i < (argc-2)) && (isdigit((int)argv[i+2][0]))) {
				wrCmdtest = 3;
				pattern1 = strtoul(argv[i+1],0,0);
				pattern2 = strtoul(argv[i+2],0,0);
			} 
		//	printf("\n wrCmdtest= %d, pattern1= %d\n\n", wrCmdtest, pattern1);
		//	exit(1);
		}
		if (strcmp (arg, "-CMD") == 0 || strcmp (arg, "--Command") == 0) {
			sendCmdtest = 1;
			if((i < (argc-1)) && (isdigit((int)argv[i+1][0])) ) {
			//	sendCmdtest = 2;
				pattern1 = strtoul(argv[i+1],0,0);
				pattern2 = 0;
			}
#if 0	
			if((i < (argc-2)) && (isdigit((int)argv[i+2][0]))) {
				sendCmdtest = 3;
				pattern1 = strtoul(argv[i+1],0,0);
				pattern2 = strtoul(argv[i+2],0,0);
			} 
#endif
		}		
		if (strcmp (arg, "-FW") == 0 || strcmp (arg, "--FakeWr") == 0 ||
		    strcmp (arg, "-GWS") == 0 || strcmp (arg, "--G.W.S") == 0) {
			fakeWrtest = 1;
			if((i < (argc-1)) && (isdigit((int)argv[i+1][0])) ) {
				pattern1 = strtoul(argv[i+1],0,0);
				pattern2 = 0;
			} 	
		}
		if (strcmp (arg, "-A") == 0 || strcmp (arg, "--Address") == 0) {
			if((i < (argc-1)) && (isdigit((int)argv[i+1][0])) ) {
				target = strtoul(argv[i+1],0,0);
			} else {
				SingleDimm = 1;
			}
		}		
		if (strcmp (arg, "-Page") == 0 || strcmp (arg, "--PageAddress") == 0) {
			if((i < (argc-1)) && (isdigit((int)argv[i+1][0])) ) {
				targetPage = strtoul(argv[i+1],0,0);
			} 
		}		
		if (strcmp (arg, "-V") == 0 || strcmp (arg, "--vref") == 0) {
			SetVref = 1;	
			data = strtoul(argv[i+1],0,0);					
		}		
		if (strcmp (arg, "-K") == 0 || strcmp (arg, "--clock") == 0) {
			SetClkDelay = 1;	
			data = strtoul(argv[i+1],0,0);					
		}		
		if (strcmp (arg, "-M") == 0 || strcmp (arg, "--cmd_dq_mux") == 0) {
			cmdMuxCyc = 1;					
			if(i < (argc-2)) {
				data = strtoul(argv[i+1],0,0) & 0x0F;
				expectedOffset = strtoul(argv[i+2],0,0);
			} else {
				printf("\n Input error CMD-Mux-Cycle testing ... data= 0x%x, expectedOffset= %d\n\n",
							   	data, expectedOffset);			
				exit(1);
			}
		}
		if (strcmp (arg, "-M2") == 0 || strcmp (arg, "--fakeRd_dq_mux") == 0) {
			cmdMuxCyc = 2;					
			if(i < (argc-2)) {
				data = strtoul(argv[i+1],0,0) & 0x0F;
				expectedOffset = strtoul(argv[i+2],0,0);
			} else {
				printf("\n Input error CMD-Mux-Cycle testing ... data= 0x%x, expectedOffset= %d\n\n",
							   	data, expectedOffset);			
				exit(1);
			}
		}		
		if (strcmp (arg, "-MS") == 0 || strcmp (arg, "--msdelay") == 0) MemDelay=strtoul(argv[i+1],0,0)*1000;
	}

	if(strstr(system_info,"x3650")) lenovo = 1;
	else							lenovo = 0;

	if(debug)
		printf("\n Options: update_hw= %d logging= %d eccMode= %d debug_flag= %d TestTime= %d "
			   " pattern1= %x pattern2= %x SingleDimm= %d lenovo=%d\n",
	   					update_hw,logging,eccMode,debug_flag,TestTime,
						pattern1,pattern2,SingleDimm, lenovo);

	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) FATAL;
	map_base = mmap(0, MAP_SIZE2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, target & ~MAP_MASK2);
	if(map_base == (void *) -1) FATAL;

	virt_addr = map_base + (target & MAP_MASK2);

	if(targetPage) {
		if((fd1 = open("/dev/mem", O_RDWR | O_SYNC)) == -1) FATAL;
		map_page = mmap(0, MAP_SIZE2, PROT_READ | PROT_WRITE, MAP_SHARED, fd1, targetPage & ~MAP_MASK2);
		if(map_page == (void *) -1) FATAL;
		virt_page = map_page + (targetPage & MAP_MASK2);
	}

	if(argc < 4) help(argc, argv); 

	if(SingleDimm) {
		if(Training & TX_DQ_DQS_1)
			virt_addr = map_base + (target & MAP_MASK2) + CMD_OFFSET;
	}

	N =  D<12?0:1;
	C = (int)(D)<12?(int)(D/3):(int)(D/3)-4;
	
	getSN(D);

	if(listing) list( D );	

	if(i2cRdWr) {
		if(dti==5+7) {
			if( i2cRdWr == 1 ) printf("\n DIMM[%d] Addr= 0x%x, ", D, addr);
			if( i2cRdWr == 2 ) printf("\n DIMM[%d] Addr= 0x%x, Write Data= 0x%02X\n\n", D, addr, data);	
			if(addr>0xff) 
				Set_i2c_Page( 1 );
			
			if( i2cRdWr == 1 ) printf(" Read Data= 0x%02X, 0x%02X\n\n", 
										ReadSMBus(FPGA1, D, addr&0xFF), ReadSMBus(FPGA2, D, addr&0xFF));
			if( i2cRdWr == 2 ) {
				WriteSMBus(FPGA1, D, addr&0xFF, data);
				WriteSMBus(FPGA2, D, addr&0xFF, data);
			}
			if(addr>0xff)
				Set_i2c_Page( 0 );
		} else {
			if( i2cRdWr == 1 ) printf("\n DIMM[%d] dti= %d Addr= 0x%x, ", D, dti, addr);
			if( i2cRdWr == 2 ) printf("\n DIMM[%d] dti= %d Addr= 0x%x, Write Data= 0x%02X\n\n", D, dti, addr, data);	
			if(addr>0xff)
				WriteSMBus(dti, D, 0xA0, 1);
			usleep(FPGA_SETPAGE_DLY);

			if( i2cRdWr == 1 ) printf(" Read Data= 0x%02X\n\n",ReadSMBus(dti, D, addr&0xFF));
			if( i2cRdWr == 2 ) WriteSMBus(dti, D, addr&0xFF, data);

			usleep(FPGA_SETPAGE_DLY);
			if(addr>0xff)
				WriteSMBus(dti, D, 0xA0, 0);
			usleep(FPGA_SETPAGE_DLY);
		}
		if(addr == 0x66) PLLReset=1;
		else			 exit(1);
	}
	if(ModeReset) {
		printf("\n DIMM[%d] Training Mode set to Nomal & Reset Counters ... ");
		WriteSMBus(FPGA1, D, 0, 0);
		WriteSMBus(FPGA2, D, 0, 0);
		ResetCounter();
		printf("Done\n\n");
		exit(1);
	}
	if(PLLReset) {
		printf("\n DIMM[%d] PLL Reset & load i/o delay ... ", D);
		fpga_pll_reset_high_low();
#if LOAD_IO_DELAY
		fpga_i_Dly_load_signal();		// 9.28.2016
		fpga_o_Dly_load_signal();		// 9.28.2016
#endif
		printf("Done\n\n");
		exit(1);
	}	
	if(SetVref) {
		printf("\n DIMM[%d] Set TxVREF ... ");
		if(data < 0 || data > 72) {
			printf(" out of Vref Range %d Abort!! \n\n", data);
			exit(1);
		}		
		Set_i2c_Page( 1 );
		for(i=0;i<16+eccMode*2;i++) {
			WriteSMBus( (i2cvRefDQS[i]>>12), D, i2cvRefDQS[i]&0xFF, data);
			usleep(FPGA_WRITE_DLY);				
		}
		Set_i2c_Page( 0 );
		printf("Done\n\n");
		exit(1);
	}
	if(SetClkDelay) {
		printf("\n DIMM[%d] Set CMD-to-CLK Delay %d tick(s)... ", D, data);

		WriteSMBus(FPGA1,D,CMD_CK_ADDR,data);		
		WriteSMBus(FPGA2,D,CMD_CK_ADDR,data);
		usleep(FPGA_CMD_WR_DLY);

		fpga_pll_reset_high_low();
#if LOAD_IO_DELAY
		fpga_i_Dly_load_signal();		// 9.1.2016
		fpga_o_Dly_load_signal();		// 9.1.2016
#endif

		printf("Done\n\n");
		exit(1);
	}	
	if(SetOffset) {
		printf("\n DIMM[%d] Set test %d offset %d tick(s) ... ", D, Training, data);	

		if(Training == 4 || Training == 8) {
			for(i=0;i<64+eccMode*8;i++) {
				if(Training==4) tdata = ReadSMBus( (i2cTXDQS1p[i]>>12), D, i2cTXDQS1p[i]&0xFF);
				if(Training==8) tdata = ReadSMBus( (i2cRXDQS1p[i]>>12), D, i2cRXDQS1p[i]&0xFF);
				tdata += data;
				if(tdata<0) { 
					printf(" addr= 0x%x, adj value(%d) is negative ... ERRO !!!\n\n",
								(Training==4)?i2cTXDQS1p[i]&0xFF:i2cRXDQS1p[i]&0xFF, tdata ); exit(1);
				}
				if(Training==4) WriteSMBus(i2cTXDQS1p[i]>>12, D, i2cTXDQS1p[i]&0xFF, tdata);	
				if(Training==8) WriteSMBus(i2cRXDQS1p[i]>>12, D, i2cRXDQS1p[i]&0xFF, tdata);	
				usleep(10000);
#if LOAD_IO_DELAY
				fpga_i_Dly_load_signal();			
#endif
			}
		} else if(Training==16) {
			for(i=0;i<16+eccMode*2;i++) {
				tdata = ReadSMBus( (i2cTXDQS2[i]>>12), D, i2cTXDQS2[i]&0xFF);
				tdata += data;
				if(tdata<0) { 
					printf(" addr= 0x%x, adj value(%d) is negative ... ERRO !!!\n\n",
								i2cTXDQS2[i]&0xFF, tdata ); exit(1);
				}
				WriteSMBus(i2cTXDQS2[i]>>12, D, i2cTXDQS2[i]&0xFF, tdata);	
				usleep(10000);
#if LOAD_IO_DELAY
				fpga_o_Dly_load_signal();			
#endif
			}
		}		
		printf("Done\n\n");		
		exit(1);
	}
	
	if(viewspd) dumpspd( D );
	if(savetofile) WritetoFile( D );
	if(loadfromfile) LoadfromFile( D );
	if(clearspd) ClearSpd( D );
	if(savetospd) WritetoSpd( D );
	if(loadfromspd) LoadfromSpd( D );

	if(write_fpga_data) { writeInitialPatternToFPGA(write_fpga_data, data1, data2); exit(1); }
	if(bcomsw)  enable_bcom_switch(bcomsw); 
	if(slaveON) enable_bcom_switch(slaveON); 
	if(initEcc) initEcctest( D ); 
	if(wrCmdtest) WrCmdtest( wrCmdtest );
	if(sendCmdtest) SendCmdtest( (unsigned char)(pattern1&0xFF) );
	if(fakeWrtest) FakeWrtest( (int)pattern1 );
	if(cmdMuxCyc) cmdMuxCycTest( D, data, expectedOffset, cmdMuxCyc );

	fprintf(stderr,"\n SYS Platform %s, hostname - %s", strstr(system_info,"Name:"), hostname);
	fprintf(stderr,"\n                  : HA# %d, dataScramble: %s(%X), ECC: %s, ", 
					HA+1, (dataScramble==0)?"OFF":"ON", mcscramble_seed_sel, (eccMode==0)?"OFF":"ON");
	fprintf(stderr,"TOHM: %#.9lx(%dGB), Training Addr= %p, ",
					tohm ,tohm>>30, (SingleDimm)?target + CMD_OFFSET:target);

	if(logging) {
		printf("\n logfile= %s",logfile);	
		fp = fopen(logfile,"a+");
		fprintf( fp,"\n %s - ", hostname);
	}

	printf("\n\n DIMM[%d] N%d.C%d.D%d : P/N %-18s S/N %s %6.1fC FPGA H/W %08X, %08X",
				D,N,C,(int)(D%3),dimm[D].sPN,dimm[D].sSN,dimm[D].Temp/16.,HWrev[0],HWrev[1]);

	if(ReadSMBus(FPGA1,D,13)) {					// Byte 13 - '1' Training mode is locked
		printf("\n Training Mode on FPGA1 is locked\n");	
		WriteSMBus(FPGA1, D, 13, 0);			// '0' to unlock.
	} 
#if BOTH_FPGA
	if(ReadSMBus(FPGA2,D,13)) {
		printf("\n Training Mode on FPGA2 is locked\n");	
		WriteSMBus(FPGA2, D, 13, 0);			// '0' to unlock.
	} 
#endif

	ResetCounter();		// FPGA-V47 9/20/2016
	
	memset(IoCycDly,0,sizeof(IoCycDly));

	if(Training & CMD_CK) CMDCK(WR,start_lp1, end_lp1, step1);
//FIXME
//	if(Training & RD_CMD_CK) CMDCK(RD,start_lp1, end_lp1, step1);
	if(Training & TX_DQ_DQS_0) {

		if(CycDly !=0 || SetIoCycDly == 0) TxDQDQS1v0(start_lp400, end_lp400, step400);
	//	int delay=1;
	//	if(CycDly) delay = CycDly;

		if(result1||result2) {
			printf("\n\n Training Fail !! - 0x%08X 0x%08X\n",result1,result2);
			for(i=0;i<64;i++) {
				if( i<32 ) {
					if((result1 >>i)&1) 
						IoCycDly[(int)(i/4)] |= CycDly << 2*(i%4);
				} else {
					if((result2 >>i)&1) {
						IoCycDly[(int)(i/4)] |= CycDly << 2*(i%4);
					//	printf("\n i=%2d IoCycDly[%d] = 0x%x",i,(int)(i/4),IoCycDly[(int)(i/4)]);
					}
				}		
			}			
		}
		if(SetIoCycDly) {
			Set_i2c_Page(1);
			usleep(FPGA_SETPAGE_DLY);
			int i2cCycDly[18];
			
			if(!CycDly) printf("\n Reset Write i2c - ");
			
			printf("\n    0x176    0x177    0x178    0x179    0x17A    0x17B    0x17C    0x17D    0x17E    0x17F");

			for(i=0;i<16;i++) {
				if(i%8==0) printf("\n   ");
				i2cCycDly[i] = ReadSMBus( i2cIoCycDly[i]>>12, D, i2cIoCycDly[i]&0xFF);
				if(CycDly==0) {
					IoCycDly[i] = 0;
					WriteSMBus( (i2cIoCycDly[i]>>12), D, i2cIoCycDly[i]&0xFF, (int)0);
					usleep(FPGA_WRITE_DLY);
				}
				printf(" 0x%02X(%02X)", IoCycDly[i] ,i2cCycDly[i]);
			}			
			
			if(CycDly) printf("\n Write i2c - ");

			for(i=0;i<16;i++) {
				if(IoCycDly[i]) {
					for(data=0, j=0;j<4;j++) {
						tdata = (IoCycDly[i]>> (j*2) ) & 0x3;
						if(tdata) data |= tdata << (j*2);
						else	  data |= ((i2cCycDly[i]>> (j*2) ) & 0x3) << (j*2);
					//	printf("\n   j=%d data= %02X tdata= %02X ", j, data, tdata);
					}		
					printf("\n   DTI= %0X Reg= 0x1%02X Data= %02X",(i2cIoCycDly[i]>>12), i2cIoCycDly[i]&0xFF, data);
					WriteSMBus( (i2cIoCycDly[i]>>12), D, i2cIoCycDly[i]&0xFF, data);
					usleep(FPGA_WRITE_DLY);				
				}
			}			
			Set_i2c_Page(0);
		
			if((CycDly)&&(result1||result2)) {	
				printf("\n\n Retest ");
				TxDQDQS1v0(start_lp400, end_lp400, step400);
				if(result1||result2) 
					printf("\n\n Training Fail !! - 0x%08X 0x%08X\n",result1,result2);
			}
		}
	}
	if(Training & TX_DQ_DQS_1) {
		TxDQS1(start_lp4, end_lp4, step4);
		TxDQDQS1(start_lp4, end_lp4, step4);
	}
	if(Training & RX_DQ_DQS_0) {
		RxDQDQS1v0(start_lp800, end_lp800, step800);
	}
	if(Training & RX_DQ_DQS_1) {
		RxDQS1(start_lp8, end_lp8, step8);
		RxDQDQS1(start_lp8, end_lp8, step8);
	}
	if(Training & TX_DQ_DQS_2_0) {
		if(eccMode) {
			printf("\n\n ECC Mode is ON. FakeWR training is not ready!!!\n\n");
			return 0;
		}		
		TxDQDQS2v0(start_lp1600, end_lp1600, step1600);
	}
	if(Training & TX_DQ_DQS_2) {
		if(eccMode) {
			printf("\n\n ECC Mode is ON. FakeWR training is not ready!!!\n\n");
			return 0;
		}		
		TxDQDQS2(start_lp16, end_lp16, step16);
	}	
	if(Training & TX_VREF) {
	 	TxVREF(start_lp32, end_lp32, step32);
	}
	printf("\n\n");
	
	WriteSMBus(FPGA1,D,13,1); // '1' to I2C Bus lock to prevent from accidental accessing
#if BOTH_FPGA	
	WriteSMBus(FPGA2,D,13,1); 
#endif
	usleep(FPGA_WRITE_DLY);

	if(munmap(map_base, MAP_SIZE2) == -1) FATAL;
	close(fd);

	if(logging) {
		fprintf(fp,"\t N%d.C%d.D%d : %s",
					//	D<12?0:1, (int)(D)<12?(int)(D/3):(int)(D/3)-4,(int)(D%3), sSN[D]);
						N, C, (int)(D%3), dimm[D].sSN);
		fprintf(fp," [ %02d:%02d:%02d %02d-%02d-%02d ]\n",
					tim->tm_hour, tim->tm_min, tim->tm_sec,tim->tm_mon+1, tim->tm_mday,tim->tm_year+1900);

		fclose(fp);
	}

	return 0;
}


