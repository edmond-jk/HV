/*
 * HVDIMM DDR4 Slave IO training
 *
 */
#define VERSION "-7.31.16"
char version[] = VERSION;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>	// mmap()
#include <sys/time.h>
#include <sys/io.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include "hv_training.h"

#define PER_BIT_TX_DQDQS1	1
#define PER_BIT_RX_DQDQS1	1
#define PER_BIT_TX_DQDQS2	0

#define PLOT_SYMBOL_ZERO 	'0'
#define PLOT_SYMBOL_ONE 	' '

#if (PER_BIT_TX_DQDQS1) || (PER_BIT_RX_DQDQS1) || (PER_BIT_TX_DQDQS2)
#undef	CMD_CK_ADDR				
#define	CMD_CK_ADDR			102
#endif

#define BITS_PER_LONG_LONG 64
#define GENMASK_ULL(h, l) \
	(((~0ULL) << (l)) & (~0ULL >> (BITS_PER_LONG_LONG - 1 - (h))))
#define GET_BITFIELD(v, lo, hi)	\
	(((v) & GENMASK_ULL(hi, lo)) >> (lo))

#define PLATFORM_INFO_ADDR          (0xCE)

//MK-begin
#define MMIO_WINDOW_SIZE			0x100000
#define MMIO_WINDOW_SIZE_2			0x40000000
//MK-end
#define CMD_OFFSET					0x10000
#define BCOM_SW_OFFSET				0x18000
#define MAP_SIZE2 1024*1024*1UL
#define MAP_MASK2 (MAP_SIZE2 - 1)
#define TX_DQDQS1_DATA_SIZE			4096
#define RX_DQDQS1_DATA_SIZE			4096
#define TX_DQDQS2_DATA_SIZE			64
//#define MMIO_START_ADDR 0x480000000		// for 10.5.3.187
//#define MMIO_START_ADDR 0x480000000		// for 10.5.3.105
#define DATA_LOOP	4096/16				// 64*4 = 4KB, 64*64*4 = 256KB
#define FATAL do { fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", \
  __LINE__, __FILE__, errno, strerror(errno)); exit(1); } while(0)

#include "spd-3-16-2016.c"

#define CMD_CK			0x01	// CMD-CK: Post RCD Signal Setup/Hold Time for FPGA
#define RD_CMD_CK		0x02	//
#define TX_DQ_DQS_1		0x04	// Tx DQ-DQS(1) - DB-to-FPGA Write Timing
#define RX_DQ_DQS_1		0x08	// Rx DQ-DQS(1) - DDR4-to-FPGA Fake Read Timing
#define TX_DQ_DQS_2		0x10	// Tx DQ-DQS(2) - FPGA-to-DDR4 Fake Write Timing

#define TX_DQ_DQS_2_TEST	7	// experimental version of Tx DQ-DQS(2)
#define WRT_INIT_PATTERN	9	// Write initial pattern to SPD
//MK-end

#define RD_CMD_CK_MASK  0xc0
#define RD				0
#define WR				1
#define HV_MMIO_CMD		2

#define SPD				0xA
#define TSOD			0x3
#define FPGA1			0x5
#define FPGA2			0x7

#define FPGA_TIMEOUT		100		// 100 = 100MS
//
//#define FPGA_WRITE_DLY	1000	// usleep(), 1000 = 1ms ... Lenovo X3650M5
//
#define FPGA_WRITE_DLY		1000	// 1000 = 1ms 
#define FPGA_CMD_WR_DLY		10000	// 10000 = 10ms 
#define FPGA_SETPAGE_DLY	10000	// 10000 = 10ms 

int rDQS[256][18], reDQS[18], feDQS[18], cpDQS[18], lwDQS[18];
int rDQ[256][9], RE[72], FE[72], CP[72], PW[72], PC[72];
int i2cRESULT[9] = {
					0x5069, 0x506a, 0x506b, 0x506c,
					0x7069, 0x706a, 0x706b, 0x706c,
					0x506d
				};

int i2cTXDQS1[18] = {
					0x5010, 0x5011, 0x5012, 0x5013, 0x5014, 0x5015, 0x5016, 0x5017,
					0x7010, 0x7011, 0x7012, 0x7013, 0x7014, 0x7015, 0x7016, 0x7017,
					0x5018, 0x5019
				};

int i2cRXDQS1[18] = {
					0x5022, 0x5023, 0x5024, 0x5025, 0x5026, 0x5027, 0x5028, 0x5029,
					0x7022, 0x7023, 0x7024, 0x7025, 0x7026, 0x7027, 0x7028, 0x7029,
					0x502a, 0x502b
				};
#if 0
int i2cTXDQS2[18] = {
					0x5034, 0x5035, 0x5036, 0x5037, 0x5038, 0x5039, 0x503a, 0x503b,
					0x7034, 0x7035, 0x7036, 0x7037, 0x7038, 0x7039, 0x703a, 0x703b,
					0x503c, 0x503d
				};
#else
int i2cTXDQS2[18] = {
					0x5106, 	//  0 to 3
 					0x5107, 	//  4 -
 					0x5108, 	//  8 -
 					0x5109, 	// 12 -
 					0x510a, 	// 16 -
 					0x510b, 	// 20 -
 					0x510c, 	// 24 -
 					0x510d, 	// 28 -
					0x7106, 	// 32 -
 					0x7107, 	// 36 -
 					0x7108, 	// 40 -
 					0x7109, 	// 44 -
 					0x710a, 	// 48 -
 					0x710b, 	// 52 -
 					0x710c, 	// 56 -
 					0x710d, 	// 60 -
 					0x510e, 	//cb0 -
 					0x510f, 	//cb4 -
				};
#endif
int i2cTXDQS1_perBit[72] = { 
					0x5010, 0x501A, 0x5024, 0x502E,	//  0 to 3
 					0x5011, 0x501B, 0x5025, 0x502F,	//  4 -
 					0x5012, 0x501C, 0x5026, 0x5030,	//  8 -
 					0x5013, 0x501D, 0x5027, 0x5031,	// 12 -
 					0x5014, 0x501E, 0x5028, 0x5032,	// 16 -
 					0x5015, 0x501F, 0x5029, 0x5033,	// 20 -
 					0x5016, 0x5020, 0x502A, 0x5034,	// 24 -
 					0x5017, 0x5021, 0x502B, 0x5035,	// 28 -
 
 					0x7010, 0x701A, 0x7024, 0x702E,	// 32 -
 					0x7011, 0x701B, 0x7025, 0x702F,	// 36 -
 					0x7012, 0x701C, 0x7026, 0x7030,	// 40 -
 					0x7013, 0x701D, 0x7027, 0x7031,	// 44 -
 					0x7014, 0x701E, 0x7028, 0x7032,	// 48 -
 					0x7015, 0x701F, 0x7029, 0x7033,	// 52 -
 					0x7016, 0x7020, 0x702A, 0x7034,	// 56 -
 					0x7017, 0x7021, 0x702B, 0x7035,	// 60 -
					0x5018, 0x5022, 0x502C, 0x5036,	//cb0 -
 					0x5019, 0x5023, 0x502D, 0x5037	//cb4 -

				};

int i2cRXDQS1_perBit[72] = { 
					0x5038, 0x5042, 0x504c, 0x5056,	//  0 to 3
 					0x5039, 0x5043, 0x504d, 0x5057,	//  4 -
 					0x503a, 0x5044, 0x504e, 0x5058,	//  8 -
 					0x503b, 0x5045, 0x504f, 0x5059,	// 12 -
 					0x503c, 0x5046, 0x5050, 0x505a,	// 16 -
 					0x503d, 0x5047, 0x5051, 0x505b,	// 20 -
 					0x503e, 0x5048, 0x5052, 0x505c,	// 24 -
 					0x503f, 0x5049, 0x5053, 0x505d,	// 28 -
 
 					0x7038, 0x7042, 0x704c, 0x7056,	// 32 -
 					0x7039, 0x7043, 0x704d, 0x7057,	// 36 -
 					0x703a, 0x7044, 0x704e, 0x7058,	// 40 -
 					0x703b, 0x7045, 0x704f, 0x7059,	// 44 -
 					0x703c, 0x7046, 0x7050, 0x705a,	// 48 -
 					0x703d, 0x7047, 0x7051, 0x705b,	// 52 -
 					0x703e, 0x7048, 0x7052, 0x705c,	// 56 -
 					0x703f, 0x7049, 0x7053, 0x705d,	// 60 -
					0x5040, 0x504a, 0x5054, 0x505e,	//cb0 -
 					0x5041, 0x504b, 0x5055, 0x505f	//cb4 -
				};

int i2cTXDQS2_perBit[72] = { 
					0x5106, 0x5116, 0x5126, 0x5136,	//  0 to 3
 					0x5107, 0x5117, 0x5127, 0x5137,	//  4 -
 					0x5108, 0x5118, 0x5128, 0x5138,	//  8 -
 					0x5109, 0x5119, 0x5129, 0x5139,	// 12 -
 					0x510a, 0x511a, 0x512a, 0x513a,	// 16 -
 					0x510b, 0x511b, 0x512b, 0x513b,	// 20 -
 					0x510c, 0x511c, 0x512c, 0x513c,	// 24 -
 					0x510d, 0x511d, 0x512d, 0x513d,	// 28 -
					0x7106, 0x7116, 0x7126, 0x7136,	// 32 -
 					0x7107, 0x7117, 0x7127, 0x7137,	// 36 -
 					0x7108, 0x7118, 0x7128, 0x7138,	// 40 -
 					0x7109, 0x7119, 0x7129, 0x7139,	// 44 -
 					0x710a, 0x711a, 0x712a, 0x713a,	// 48 -
 					0x710b, 0x711b, 0x712b, 0x713b,	// 52 -
 					0x710c, 0x711c, 0x712c, 0x713c,	// 56 -
 					0x710d, 0x711d, 0x712d, 0x713d,	// 60 -
 					0x510e, 0x511e, 0x512e, 0x513e,	//cb0 -
 					0x510f, 0x511f, 0x512f, 0x513f	//cb4 -
				};

char sSN[24][12];
char sPN[24][18];
char logfile[128], system_info[80], hostname[80];
int DimmTemp[24];

int dti=0, N, C, D, Training=0, Status, update_hw=0, eccMode=0;
int start_lp=0, end_lp=256, step_size=1;
int fd, logging=0, listing=0, write_i2c=0, showsummary=0;
int loadfromfile=0, savetofile=0, viewspd=0;
int write_fpga_data=0, bcomsw=0, wrCmdtest=0, cmdMuxCyc=0;
int CPmin, CPmax, FEmin, FEmax, REmin, REmax, PWmin, PWmax;
int colorlimit=20, TestTime=0, SetPattern=0, slaveON=0, ModeReset=0;
unsigned long Data, pattern1, pattern2;

#if (PER_BIT_TX_DQDQS1) || (PER_BIT_RX_DQDQS1) || (PER_BIT_TX_DQDQS2)
unsigned long data1 = -1;//0x5555555555555555;
unsigned long data2 = 0;//0xAAAAAAAAAAAAAAAA;
#else
unsigned long data1 = -1;//0x5555555555555555;
unsigned long data2 = 0;//0xAAAAAAAAAAAAAAAA;
#endif

struct timeval start, end, start1, end1;
unsigned long elapsedtime, elapsedtime1;

void *map_base, *virt_addr;
off_t target;
//MK-begin
unsigned long *virtual_mmio_base;
char *debug_str=NULL;
const char *fpga1_data1_str=NULL, *fpga1_data2_str=NULL;
const char *fpga2_data1_str=NULL, *fpga2_data2_str=NULL;
unsigned int debug_flag=0;
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
void writeInitialPatternToFPGA(int write_fpga_data);
//MK-end

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

//MK-begin
void enable_bcom_switch(int mode)
{
	unsigned int i;

	if(mode==2) {
		printf("\n Set bit7 of Byte14 to '1'");
		WriteSMBus(FPGA1, D, 14, ReadSMBus(FPGA1,D,14) | 0x80);
		WriteSMBus(FPGA2, D, 14, ReadSMBus(FPGA2,D,14) | 0x80);
	}

	printf("\n Write 64B data for BCOM_MUX_SW @ %p ...", target + BCOM_SW_OFFSET);

	virtual_mmio_base = map_base + (target & MAP_MASK2) + BCOM_SW_OFFSET;

	for (i=0; i < 8; i++)
	{
		*((unsigned long *) (virtual_mmio_base + i)) = 0x123456789ABCDEF0;
	}
	clflush_cache_range(map_base + (target & MAP_MASK2), 8*16);
	usleep(100);

	printf(" Done\n\n");
	
}
//MK-end

//
// V_0722
//
void fpga_i_Dly_load_signal(void)
{
//0729	int Byte14_1, Byte14_2;
	unsigned char Byte14_1, Byte14_2;

	Byte14_1 =ReadSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR);
	Byte14_2 =ReadSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR);

	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR, (Byte14_1 & 0xDF));	// reset bit 5
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR, (Byte14_2 & 0xDF));
	usleep(FPGA_WRITE_DLY);									// 1ms

	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR, (Byte14_1 | 0x20));	// Set Bit5 '1'
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR, (Byte14_2 | 0x20));
	usleep(FPGA_WRITE_DLY);	
	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR, (Byte14_1 & 0xDF));
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR, (Byte14_2 & 0xDF));
	usleep(FPGA_WRITE_DLY);
		
}

void fpga_o_Dly_load_signal(void)	// Fake-WR output delay
{
	unsigned char Byte14_1, Byte14_2;

	Byte14_1 =ReadSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR);
	Byte14_2 =ReadSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR);

	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR, (Byte14_1 & 0xBF));	// reset bit 6
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR, (Byte14_2 & 0xBF));
	usleep(FPGA_WRITE_DLY);									// 1ms

	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR, (Byte14_1 | 0x40));	// Set Bit6 '1'
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR, (Byte14_2 | 0x40));
	usleep(FPGA_WRITE_DLY);	
	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR, (Byte14_1 & 0xBF));
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR, (Byte14_2 & 0xBF));
	usleep(FPGA_WRITE_DLY);
		
}

//MKvoid MemCpy(int dest, unsigned long data1, unsigned long data2)
//MK-begin
void MemCpy(int dest, unsigned long data1, unsigned long data2, unsigned int byteCnt)
//MK-end
{
	void *tmp;
	unsigned long read_result, writeval, qwordData;
	int i;
//MK-begin
	unsigned int loop_count=byteCnt >> 4;	// 16 bytes (two 64-bit accesses per iteration)
//MK-end

#if MMAP_DEBUG
	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) FATAL;
	map_base = mmap(0, MAP_SIZE2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, target & ~MAP_MASK2);
	if(map_base == (void *) -1) FATAL;
#endif
	virt_addr = map_base + (target & MAP_MASK2);

//MK	for (i=0; i<DATA_LOOP; i++) {
//MK-begin
	for (i=0; i < loop_count; i++) {
//MK-end
		if(dest == WR) {
			*((unsigned long *) virt_addr) = data1;
			virt_addr = (void *) ((unsigned long *)virt_addr + 1);
			*((unsigned long *) virt_addr) = data2;
			virt_addr = (void *) ((unsigned long *)virt_addr + 1);
		} else if(dest == HV_MMIO_CMD) {
			*((unsigned long *) (virt_addr + CMD_OFFSET)) = data1;
			virt_addr = (void *) ((unsigned long *)virt_addr + 1);
			*((unsigned long *) (virt_addr + CMD_OFFSET)) = data2;
			virt_addr = (void *) ((unsigned long *)virt_addr + 1);
		} else {
			/* We just want to generate read transactions. Not interested in actual values */
			qwordData = *((unsigned long *) virt_addr);
			virt_addr = (void *) ((unsigned long *)virt_addr + 1);
			qwordData = *((unsigned long *) virt_addr);
			virt_addr = (void *) ((unsigned long *)virt_addr + 1);
		}
	}
#if 1
//MK	clflush_cache_range(map_base + (target & MAP_MASK2), DATA_LOOP*16);
//MK-begin
	clflush_cache_range(map_base + (target & MAP_MASK2), loop_count*16);
//MK-end
#endif	
#if MMAP_DEBUG
	if(munmap(map_base, MAP_SIZE2) == -1) FATAL;
	close(fd);
#endif
}

//MK-begin
/*
 * This routine expects byteCnt to be multiple of 64-bytes. It will divide
 * the requested amount of data in multiple of 64-bytes and write each
 * 64-byte block at virt_addr.
 * */
void MemCpy2(int dest, unsigned long data1, unsigned long data2, unsigned int byteCnt)
{
	void *tmp;
	unsigned long qwordData;
	int i, j;
	unsigned int loop_count=byteCnt >> 6;	// 64 bytes per iteration


#if MMAP_DEBUG
	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) FATAL;
	map_base = mmap(0, MAP_SIZE2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, target & ~MAP_MASK2);
	if(map_base == (void *) -1) FATAL;
#endif
//	virt_addr = map_base + (target & MAP_MASK2);

	for (i=0; i < loop_count; i++) {
		/* We will be writing the next 64-byte block at the same location */
		virt_addr = map_base + (target & MAP_MASK2);
		for (j=0; j < 4; j++) {
			if(dest == WR) {
//				printf("virt_addr=%#.16lx\n", (unsigned long *) virt_addr);
				*((unsigned long *) virt_addr) = data1;
				virt_addr = (void *) ((unsigned long *)virt_addr + 1);
//				printf("virt_addr=%#.16lx\n", (unsigned long *) virt_addr);
				*((unsigned long *) virt_addr) = data2;
				virt_addr = (void *) ((unsigned long *)virt_addr + 1);
			} else {
				/* We just want to generate read transactions. Not interested in actual values */
				qwordData = *((unsigned long *) virt_addr);
				virt_addr = (void *) ((unsigned long *)virt_addr + 1);
				qwordData = *((unsigned long *) virt_addr);
				virt_addr = (void *) ((unsigned long *)virt_addr + 1);
			}
		}
	}
#if 1
	clflush_cache_range(map_base + (target & MAP_MASK2), loop_count*16);
#endif
#if MMAP_DEBUG
	if(munmap(map_base, MAP_SIZE2) == -1) FATAL;
	close(fd);
#endif
}

//MK-end

/*
 * per JS, find the falling edge first and then rising 
 */
void FindCenterPoint(int test, int perBit, int start_lp, int end_lp, int step_size)
{
	int i, j, strobe, foundzero, perDQ, perDQmax, BITMASK;
	int longestzero=0, tmpfeDQS;

	if(logging) {
		if(test==TX_DQ_DQS_1) fprintf(fp, "TxDQDQS1:");
		if(test==RX_DQ_DQS_1) fprintf(fp, "RxDQDQS1:");
		if(test==TX_DQ_DQS_2) fprintf(fp, "TxDQDQS2:");
	}

	if (perBit) perDQmax = 4;
	else		perDQmax = 1;

	for(perDQ=0;perDQ<perDQmax;perDQ++) 
	{
		CPmax = REmax = FEmax = PWmax = 0;
	   	CPmin = REmin = FEmin = PWmin = 256;
		BITMASK = 1 << perDQ;

		for(strobe=0;strobe<16+eccMode*2;strobe++) {
			reDQS[strobe] = 0;
			longestzero = 0;
			foundzero = 0;
			/* find the start of longest continuous zero as falling point */
			for(i=start_lp;i<end_lp;i=i+step_size) {
			//	printf("\ni=%3d rDQS=%x", i, rDQS[i][strobe]&0x1);
			//	if(rDQS[i][strobe]<0) continue;
				if((rDQS[i][strobe] & BITMASK)==0) {
					if (!foundzero)
						tmpfeDQS = i;
					foundzero++;
				} else {
					if (foundzero > longestzero) {
						feDQS[strobe] = tmpfeDQS;
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
			for(i=feDQS[strobe];i<end_lp;i=i+step_size) {
			//	printf("\n bitmask=%x i=%3d feDQS=%x", BITMASK, i, rDQS[i][strobe]);	
			//	if(rDQS[i][strobe]<0) continue;
				if((rDQS[i][strobe] & BITMASK) && 
			   	(rDQS[i+step_size][strobe] & BITMASK) && 
		       	(rDQS[i+2*step_size][strobe] & BITMASK) ) {
					reDQS[strobe] = i;
					break;
				}
			}

			/* if rising point is not found, set it to 255 */
			if (reDQS[strobe] < feDQS[strobe])
				reDQS[strobe] = 255;

			lwDQS[strobe] = reDQS[strobe] - feDQS[strobe];

			cpDQS[strobe] = (feDQS[strobe] + reDQS[strobe])/2;
			if(feDQS[strobe] < FEmin ) FEmin = feDQS[strobe];
			if(feDQS[strobe] > FEmax ) FEmax = feDQS[strobe];
			if(reDQS[strobe] < REmin ) REmin = reDQS[strobe];
			if(reDQS[strobe] > REmax ) REmax = reDQS[strobe];
			if(cpDQS[strobe] < CPmin ) CPmin = cpDQS[strobe];
			if(cpDQS[strobe] > CPmax ) CPmax = cpDQS[strobe];
			if(lwDQS[strobe] < PWmin ) PWmin = lwDQS[strobe];
			if(lwDQS[strobe] > PWmax ) PWmax = lwDQS[strobe];

			FE[strobe*4 + perDQ] = feDQS[strobe];
			RE[strobe*4 + perDQ] = reDQS[strobe];
			CP[strobe*4 + perDQ] = cpDQS[strobe];
			PW[strobe*4 + perDQ] = lwDQS[strobe];
		}		

		printf("\n\n  DQ:");
		for (j=0; j < 16+eccMode*2; j++)
			printf("%5d", j*4+perDQ);

		printf("\n  FE:");
		for (j=0; j < 16+eccMode*2; j++)
			printf("%5d", feDQS[j]);
	//	printf("  [%3d - %3d] %ups", FEmax, FEmin, (FEmax-FEmin)*1250/256);
		printf("  [%3d - %3d]", FEmax, FEmin);

		printf("\n  RE:");
		for (j=0; j < 16+eccMode*2; j++)
			printf("%5d", reDQS[j]);
	//	printf("  [%3d - %3d] %ups", REmax, REmin, (REmax-REmin)*1250/256);
		printf("  [%3d - %3d]", REmax, REmin);

		printf("\n  PW:");
		for (j=0; j < 16+eccMode*2; j++)
			printf(" \033[%d;%dm%4d\033[0m",
							lwDQS[j]<=colorlimit?1:0,	// 1: Bold, 5: Blink, 7: reverse
							lwDQS[j]<=colorlimit?31:38,lwDQS[j]);
	//	printf("  [%3d - %3d] %ups", PWmax, PWmin, (PWmax-PWmin)*1250/256);
		printf("  [%3d - %3d]", PWmax, PWmin);

		printf("\n  CP:");
		if((update_hw) && (test==TX_DQ_DQS_2)) {
			WriteSMBus(FPGA1, D, 0xA0, 0x1);	// Set Page '1'
			WriteSMBus(FPGA2, D, 0xA0, 0x1);	// Set Page '1'				
			usleep(FPGA_SETPAGE_DLY);
		}		
		for (j=0; j < 16 + eccMode*2; j++) {
			printf("%5d", cpDQS[j]);	
			if(logging)
				fprintf(fp, "%5d", cpDQS[j]);
			if (update_hw) { 
#if !(PER_BIT_TX_DQDQS1)
				if(test==TX_DQ_DQS_1) WriteSMBus( (i2cTXDQS1[j]>>12), D, i2cTXDQS1[j]&0xFF, cpDQS[j]);
#endif
#if !(PER_BIT_RX_DQDQS1)
				if(test==RX_DQ_DQS_1) WriteSMBus( (i2cRXDQS1[j]>>12), D, i2cRXDQS1[j]&0xFF, cpDQS[j]);
#endif
#if !(PER_BIT_TX_DQDQS2)
				if(test==TX_DQ_DQS_2) WriteSMBus( (i2cTXDQS2[j]>>12), D, i2cTXDQS2[j]&0xFF, cpDQS[j]);
		//	 printf("\n dq=%d dti= %d, addr= %d data= %d", j, (i2cTXDQS2[j]>>12), i2cTXDQS2[j]&0xFF, cpDQS[j]);
#endif
				usleep(FPGA_WRITE_DLY);
			}
		}
		if((update_hw) && (test==TX_DQ_DQS_2)) {
			WriteSMBus(FPGA1, D, 0xA0, 0x0);	// Set Page '0'
			WriteSMBus(FPGA2, D, 0xA0, 0x0);	// Set Page '0'
			usleep(FPGA_SETPAGE_DLY);
		}
	//	printf("  [%3d - %3d] %ups", CPmax, CPmin, (CPmax-CPmin)*1250/256);
		printf("  [%3d - %3d]", CPmax, CPmin);

	}

#if (PER_BIT_TX_DQDQS1) || (PER_BIT_RX_DQDQS1) || (PER_BIT_TX_DQDQS2)
	if (update_hw) { 
#if PER_BIT_TX_DQDQS2
		WriteSMBus(FPGA1, D, 0xA0, 0x1);	// Set Page '1'
		WriteSMBus(FPGA2, D, 0xA0, 0x1);	// Set Page '1'
		usleep(FPGA_SETPAGE_DLY);
#endif
		for (j=0; j < 64 + eccMode*8; j++) {
#if PER_BIT_TX_DQDQS1
			if(test==TX_DQ_DQS_1) WriteSMBus( (i2cTXDQS1_perBit[j]>>12), D, i2cTXDQS1_perBit[j]&0xFF, CP[j]);
#endif
#if PER_BIT_RX_DQDQS1
			if(test==RX_DQ_DQS_1) WriteSMBus( (i2cRXDQS1_perBit[j]>>12), D, i2cRXDQS1_perBit[j]&0xFF, CP[j]);
#endif
#if PER_BIT_TX_DQDQS2
			if(test==TX_DQ_DQS_2) WriteSMBus( (i2cTXDQS2_perBit[j]>>12), D, i2cTXDQS2_perBit[j]&0xFF, CP[j]);
#endif			 
			usleep(FPGA_WRITE_DLY);
			// printf("\n dq=%d dti= %d, addr= %d data= %d", j, (i2cTXDQS1_perBit[j]>>8), i2cTXDQS1_perBit[j]&0xFF, CP[j]);
		}
#if PER_BIT_TX_DQDQS2
		WriteSMBus(FPGA1, D, 0xA0, 0x0);	// Set Page '0'
		WriteSMBus(FPGA2, D, 0xA0, 0x0);	// Set Page '0'
		usleep(FPGA_SETPAGE_DLY);
#endif
		if(test==TX_DQ_DQS_2) fpga_o_Dly_load_signal();		
		else				  fpga_i_Dly_load_signal();	
	}
#endif

#if 1
	if(showsummary) {	
		printf("\n");
		for(i=start_lp;i<end_lp;i=i+step_size) {
			printf("\n %3d:",i);	
			for (j=0; j < 8 + eccMode; j++) {
				printf(" ");
				printf("\033[0;%dm%c\033[0m",(i==(CP[j*8+0]&0xFE))?31:38,((rDQ[i][j]>>0)&0x1)?' ':'0');
				printf("\033[0;%dm%c\033[0m",(i==(CP[j*8+1]&0xFE))?31:38,((rDQ[i][j]>>1)&0x1)?' ':'0');
				printf("\033[0;%dm%c\033[0m",(i==(CP[j*8+2]&0xFE))?31:38,((rDQ[i][j]>>2)&0x1)?' ':'0');
				printf("\033[0;%dm%c\033[0m",(i==(CP[j*8+3]&0xFE))?31:38,((rDQ[i][j]>>3)&0x1)?' ':'0');
				printf(" ");
				printf("\033[0;%dm%c\033[0m",(i==(CP[j*8+4]&0xFE))?31:38,((rDQ[i][j]>>4)&0x1)?' ':'0');
				printf("\033[0;%dm%c\033[0m",(i==(CP[j*8+5]&0xFE))?31:38,((rDQ[i][j]>>5)&0x1)?' ':'0');
				printf("\033[0;%dm%c\033[0m",(i==(CP[j*8+6]&0xFE))?31:38,((rDQ[i][j]>>6)&0x1)?' ':'0');
				printf("\033[0;%dm%c\033[0m",(i==(CP[j*8+7]&0xFE))?31:38,((rDQ[i][j]>>7)&0x1)?' ':'0');
			}	
		}

		printf("\n\n  DQ:"); for (j=0; j < 64; j++) printf("%5d", j);
		printf("\n  FE:");	for (j=0; j < 64; j++)	printf("%5d", FE[j]);
		printf("\n  RE:");	for (j=0; j < 64; j++)	printf("%5d", RE[j]);
		printf("\n  PW:");	for (j=0; j < 64; j++)	printf("%5d", PW[j]);
		printf("\n  CP:");	for (j=0; j < 64; j++)	printf("%5d", CP[j]);
	}
#endif

}

/*
 * per JS, find the falling edge first and then rising 
 */
void FindCMDCenterPoint(int *cs0, int *reCS0, int *feCS0, int *cpCS0)
{
	int i, found;
	int pfeCS0;
	int longestzero, tmpfeCS0;

	*reCS0 = 0;
	*feCS0 = 0;
	found = 0;
	longestzero = 0;
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

void PrintBinary(unsigned char D)
{
	int i=8;
	printf(" ");	
	while(i) {
		if(D&0x80)  printf("   1");
		else		printf("   0");
		D <<=1;
		i--;
	}
}

void ReadDQResultFromFPGA(int test, int i, int perBit, int BitMask)
{
	unsigned char dqByte;
	int j;
#if 0
	// Byte 0, 1, 2 ,3
	for (j=0; j < TRN_RESULTS_DQ_SZ; j++) {
		dqByte = ReadSMBus(FPGA1, D, TRN_RESULTS_DQ_START+j);
		rDQS[i][2*j] = dqByte & 0x0F;
		rDQS[i][2*j+1] = (dqByte >> 4 ) & 0x0F;
		rDQ[i][j] = dqByte;
	}

#if BOTH_FPGA
	// Byte 4, 5, 6, 7
	for (j=0; j < TRN_RESULTS_DQ_SZ; j++) {
		dqByte = ReadSMBus(FPGA2, D, TRN_RESULTS_DQ_START+j);
		rDQS[i][2*j+8] = dqByte & 0x0F;
		rDQS[i][2*j+9] = (dqByte >> 4 ) & 0x0F;
		rDQ[i][j+4] = dqByte;
	}
#endif

#if ECC_ON	
	// Byte ECC
	if (eccMode) {
		dqByte = ReadSMBus(FPGA1, D, TRN_RESULTS_ECC);
		rDQS[i][ECC_OFFSET] = dqByte & 0x0F;
		rDQS[i][ECC_OFFSET+1] = (dqByte >> 4 ) & 0x0F;
		rDQ[i][8] = dqByte;
	}
#endif
#else
	for (j=0; j < 8 + eccMode; j++) {
		dqByte = ReadSMBus((i2cRESULT[j]>>12), D, i2cRESULT[j]&0xFF);
		rDQS[i][2*j] = dqByte & BitMask;
		rDQS[i][2*j+1] = (dqByte >> 4 ) & BitMask;
		rDQ[i][j] = dqByte;
	}		
#endif
//	if ( ( Training != TX_DQ_DQS_2 ) || (debug_str == NULL || (debug_flag & 0x03) == 0 ) )
	if ( ( test != TX_DQ_DQS_2 ) || (debug_flag & 0x03) == 0 ) 
	{
		if(perBit) {
			for (j=0; j < 8 + eccMode; j++) {
				printf(" %c%c%c%c %c%c%c%c", 
							((rDQ[i][j]>>0)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
							((rDQ[i][j]>>1)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
							((rDQ[i][j]>>2)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
							((rDQ[i][j]>>3)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
							((rDQ[i][j]>>4)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
							((rDQ[i][j]>>5)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
							((rDQ[i][j]>>6)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
							((rDQ[i][j]>>7)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO);
			}		
		} else {		
			for (j=0; j < 8 + eccMode; j++) {
				printf(" %c%c%c%c %c%c%c%c", 
							((rDQ[i][j]>>0)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
							' ',
							' ',
							' ',
							((rDQ[i][j]>>4)&0x1)?PLOT_SYMBOL_ONE:PLOT_SYMBOL_ZERO,
							' ',
							' ',
							' ');
			}
		}
		if(!eccMode) printf("%10s"," ");		
	}
}

//MK-begin
/**
 * cmpTxDQDQS64b - Performs 64-bit data  comparison for Tx-DQDQS(2), which
 * is FPGA-to-DDR4 Write Timing calibration.
 * @i: Row number of the array (loop index from the caller)
 * @rDQSt: Pointer to a table
 * @qwExpData: 64-bit data read from I2C registers
 * 				upper 32-bit = data2; lower 32-bit = data1
 * @qwFPGA1: 64-bit data, upper 32-bit = lower 32-bit data from addr X+1
 * 				lower 32-bit = lower 32-bit data from addr X
 * @qwFPGA2: 64-bit data, upper 32-bit = upper 32-bit data from addr X+1
 * 				lower 32-bit = upper 32-bit data from addr X
 *
 * For FPGA1, qwFPGA1 is compared against qwExpData nibble-by-nibble.
 * For FPGA2, qwFPGA2 is compared against qwExpData nibble-by-nibble.
 * The result is stored in the input table.
  */
void cmpTxDQDQS64b(int i, unsigned long qwExpData, unsigned long qwFPGA1, unsigned long qwFPGA2)
{
	unsigned long loNibbleMask=0x000000000000000F;
	unsigned long hiNibbleMask=0x0000000F00000000;
	unsigned long qwTemp0, qwTemp1;
	int j, eccByte;

	/* Store test results in byte 0 ~ 7 for FPGA1 and byte 8 ~ 15 for FPGA2 */
	for (j=0; j < 8; j++) {
		/*
		 * If qwExpData[x:y] == qwFPGA1[x:y] where [3:0], [7:4], [11:8],
		 * [15:12], ..... [31:27] AND
		 * store 0, otherwise, store 1.
		 */
		qwTemp0 = (qwExpData & (loNibbleMask << (j*4))) ^ (qwFPGA1 & (loNibbleMask << (j*4)));
		qwTemp1 = (qwExpData & (hiNibbleMask << (j*4))) ^ (qwFPGA1 & (hiNibbleMask << (j*4)));
		if (qwTemp0 == 0 && qwTemp1 == 0) {
			rDQS[i][j] = 0;
		} else {
			rDQS[i][j] = 1;
		}

#if BOTH_FPGA
		qwTemp0 = (qwExpData & (loNibbleMask << (j*4))) ^ (qwFPGA2 & (loNibbleMask << (j*4)));
		qwTemp1 = (qwExpData & (hiNibbleMask << (j*4))) ^ (qwFPGA2 & (hiNibbleMask << (j*4)));
		if (qwTemp0 == 0 && qwTemp1 == 0) {
			rDQS[i][j+8] = 0;
		} else {
			rDQS[i][j+8] = 1;
		}
#endif

	} // end of for

	/* Draw a line of results */
	for (j=0; j < 9; j++) {
		printf("%5d", rDQS[i][j*2]);
		printf("%5d", rDQS[i][j*2+1]);
	}
}

/**
 * cmpTxDQDQS64b_2 - Performs 64-bit data comparison for Tx-DQDQS(2), which
 * is FPGA-to-DDR4 Write Timing calibration.
 * @i: Row number of the array (loop index from the caller)
 * @rDQSt: Pointer to a table
 * @qwExpData: 64-bit data read from I2C registers
 * 				upper 32-bit = data2; lower 32-bit = data1
 * @qwFPGA1: 64-bit data, upper 32-bit = lower 32-bit data from addr X+1
 * 				lower 32-bit = lower 32-bit data from addr X
 * @qwFPGA2: 64-bit data, upper 32-bit = upper 32-bit data from addr X+1
 * 				lower 32-bit = upper 32-bit data from addr X
 *
 * For FPGA1, qwFPGA1 is compared against qwExpData nibble-by-nibble.
 * For FPGA2, qwFPGA2 is compared against qwExpData nibble-by-nibble.
 * The result is stored in the input table.
  */
void cmpTxDQDQS64b_2(int i, unsigned long qwExpData, unsigned long qwFPGA1, unsigned long qwFPGA2)
{
	unsigned long loNibbleMask=0x000000000000000F;
	unsigned long hiNibbleMask=0x0000000F00000000;
	unsigned long qwTemp0, qwTemp1;
	int j, eccByte;

	/* Store test results in byte 0 ~ 7 for FPGA1 and byte 8 ~ 15 for FPGA2 */
	for (j=0; j < 8; j++) {
		/*
		 * If qwExpData[x:y] == qwFPGA1[x:y] where [3:0], [7:4], [11:8],
		 * [15:12], ..... [31:27] AND
		 * store 0, otherwise, store 1.
		 */
		qwTemp0 = (qwExpData & (loNibbleMask << (j*4))) ^ (qwFPGA1 & (loNibbleMask << (j*4)));
		qwTemp1 = (qwExpData & (hiNibbleMask << (j*4))) ^ (qwFPGA1 & (hiNibbleMask << (j*4)));
		if (qwTemp0 == 0 && qwTemp1 == 0) {
			rDQS[i][j] &= 0xFFFFFFF7;
		} else {
			rDQS[i][j] |= 0x00000008;	
		}

#if BOTH_FPGA
		qwTemp0 = (qwExpData & (loNibbleMask << (j*4))) ^ (qwFPGA2 & (loNibbleMask << (j*4)));
		qwTemp1 = (qwExpData & (hiNibbleMask << (j*4))) ^ (qwFPGA2 & (hiNibbleMask << (j*4)));
		if (qwTemp0 == 0 && qwTemp1 == 0) {
			rDQS[i][j+8] &= 0xFFFFFFF7;
		} else {
			rDQS[i][j+8] |= 0x00000008;
		}
#endif

	} // end of for

	/* Draw a line of results */
	for (j=0; j < 9; j++) {
		printf("\033[0;%dm%5x\033[0m", (rDQS[i][j*2])?38:36, rDQS[i][j*2]);
		printf("\033[0;%dm%5x\033[0m", (rDQS[i][j*2+1])?38:36,rDQS[i][j*2+1]);
	}
}
//MK-end

int check_fpga_status(int dti, int addr, int bitmask, double timeout)
{
	uint64_t start;
//0729	int Data;
	unsigned char Data;

	start = getTickCount();

	do {
//		usleep(1000);
		usleep(10);
		Data = ReadSMBus(dti, D, addr);
		if(Data & bitmask)
			break;
	} while(elapsedTime(start) < timeout);

	return Data;
}		

void CMDCK(int cmd)
{
	int cs0a[256], cs0b[256];		// for CMD-CK training
	int reCS0a, feCS0a, cpCS0a;
	int reCS0b, feCS0b, cpCS0b;
	int i;
	long data1 = 0;
	long data2 = 0;
	int ts_5, ts_7;

	if(cmd == RD) {
//MK		MemCpy(WR, data1, data2);	// first prefill the 4k data pattern
//MK-begin
		MemCpy(WR, data1, data2, 4096);	// first prefill the 4k data pattern
//MK-end
	}		
	printf("\n ::: %s, CMD= %s, Data pattern= 0x%lx\n",
					"CMD_CK Training", cmd==RD?"RD":"WR",data1);
	printf("\nloop: FPGA_1 FPGA_2");
	// Ste 1. set training mode
	WriteSMBus(FPGA1,D,TRN_MODE_ADDR,CMD_CK_MODE);		// Set Training mode to '9'
#if BOTH_FPGA
	WriteSMBus(FPGA2,D,TRN_MODE_ADDR,CMD_CK_MODE);		// Set Training mode to '9'
#endif
	usleep(FPGA_CMD_WR_DLY);

//	for (i=start_lp; i<end_lp; i++) {
	for (i=start_lp; i<end_lp; i=i+step_size) {

		gettimeofday(&start, NULL);	
		printf("\n %3d:",i);	

		// Step 2. set clk delay time to 0
		ts_5 = check_fpga_status(FPGA1,TRN_STATUS,TRN_MODE,FPGA_TIMEOUT);
#if BOTH_FPGA
		ts_7 = check_fpga_status(FPGA2,TRN_STATUS,TRN_MODE,FPGA_TIMEOUT);
#endif
		if ( ((ts_5 & TRN_MODE)==TRN_MODE) && ((ts_5 & TRN_ERR)==0) 
#if BOTH_FPGA
		  && ((ts_7 & TRN_MODE)==TRN_MODE) && ((ts_7 & TRN_ERR)==0) 
#endif
		) {
			WriteSMBus(FPGA1,D,CMD_CK_ADDR,i);			// 88
			WriteSMBus(FPGA2,D,CMD_CK_ADDR,i);
			usleep(FPGA_CMD_WR_DLY);
		} else {
			printf("CMDCK:: FPGA1/2 is not in test mode(A[103] bit0 is 0)! %#.8x, %#.8x\n", ts_5, ts_7);
			exit (-1);
		}

		// Step 3. set FPGA Reset to High
		WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR,1);
#if BOTH_FPGA
		WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR,1);	
#endif
		usleep(FPGA_CMD_WR_DLY);

		// Step 4. Write 4KB data=0
		gettimeofday(&start1, NULL);	
//MK		MemCpy(cmd, data1, data2);				// Issue command to FPGA
//MK-begin
		MemCpy(cmd, data1, data2, 4096);
//MK-end
		gettimeofday(&end1, NULL);

		// Step 5. set FPGA Reset to Low
		WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR,0);
#if BOTH_FPGA
		WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR,0);	
#endif
		usleep(30000);
		// Step 6. read cs0 status from FPGA-1
		ts_5 = check_fpga_status(FPGA1, TRN_STATUS, CAPTURE_DN, FPGA_TIMEOUT);
#if BOTH_FPGA
		ts_7 = check_fpga_status(FPGA2, TRN_STATUS, CAPTURE_DN, FPGA_TIMEOUT);
#endif		
		if ( ((ts_5 & CAPTURE_DN) == CAPTURE_DN) && ((ts_5 & TRN_ERR) == 0) 
#if BOTH_FPGA
		  && ((ts_7 & CAPTURE_DN) == CAPTURE_DN) && ((ts_7 & TRN_ERR) == 0)
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
			printf("CMDCK:: I2C error on FPGA1/2: status = %#.8x, %#.8x\n", ts_5, ts_7);
			exit (-1);
		}	

		gettimeofday(&end, NULL);
		
		elapsedtime = ((end.tv_sec * 1000000 + end.tv_usec)
				     - (start.tv_sec * 1000000 + start.tv_usec));
		elapsedtime1 = ((end1.tv_sec * 1000000 + end1.tv_usec)
				     - (start1.tv_sec * 1000000 + start1.tv_usec));
	
		if(TestTime) printf(" ... %lduS, %ldmS", elapsedtime1, elapsedtime/1000);
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
	}

	// Step 9. set FPGA Reset to High
	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR,1);
#if BOTH_FPGA
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR,1);	
#endif
	usleep(FPGA_CMD_WR_DLY);

	// Step 10. set FPGA Reset to Low
	WriteSMBus(FPGA1,D,FPGA_PLL_RESET_ADDR,0);
#if BOTH_FPGA
	WriteSMBus(FPGA2,D,FPGA_PLL_RESET_ADDR,0);	
#endif
	usleep(FPGA_CMD_WR_DLY);

	/* Set FPGA back to NORMAL mode per jsung's request */
	WriteSMBus(FPGA1, D, TRN_MODE_ADDR, NORMAL_MODE);
#if BOTH_FPGA
	WriteSMBus(FPGA2, D, TRN_MODE_ADDR, NORMAL_MODE);
#endif
	usleep(FPGA_CMD_WR_DLY);
}

void TxDQDQS1(int start_lp, int end_lp, int step_size)
{
	int i, j, ts_5, ts_7, perBit;

	printf("\n\n TxDQDQS1:: %s, Data=%#.16lx & %#.16lx\n", "WR DB-to-FPGA Training", data1, data2);

	// Set training mode
	WriteSMBus(FPGA1, D, TRN_MODE_ADDR, TX_DQDQS_1_MODE);
#if BOTH_FPGA
	WriteSMBus(FPGA2, D, TRN_MODE_ADDR, TX_DQDQS_1_MODE);
#endif	
	usleep(FPGA_CMD_WR_DLY);

	printf("\n  DQ: 0-------7 8------15 16-----23 24-----31 32-----39 40-----47 48-----55 56-----63 64-----71");
	for (i=start_lp; i < end_lp; i=i+step_size) {

		gettimeofday(&start, NULL);	
		printf("\n %3d:",i);	

		// Set CLK delay time A[33:16]
		ts_5 = check_fpga_status(FPGA1,TRN_STATUS,TRN_MODE,FPGA_TIMEOUT);
#if BOTH_FPGA
		ts_7 = check_fpga_status(FPGA2,TRN_STATUS,TRN_MODE,FPGA_TIMEOUT);
#endif
		if ( ((ts_5 & TRN_MODE)==TRN_MODE) && ((ts_5 & TRN_ERR)==0) 
#if BOTH_FPGA
		  && ((ts_7 & TRN_MODE)==TRN_MODE) && ((ts_7 & TRN_ERR)==0) 
#endif
		 ) {
#if PER_BIT_TX_DQDQS1
			perBit = 1;
			for (j=0; j < 64 + 8*eccMode; j++) {
				WriteSMBus( (i2cTXDQS1_perBit[j]>>12), D, i2cTXDQS1_perBit[j]&0xFF, i);
				usleep(FPGA_WRITE_DLY);
			}
#else				
			perBit = 0;
			for (j=0; j < 16 + 2*eccMode; j++) {
				WriteSMBus( (i2cTXDQS1[j]>>12), D, i2cTXDQS1[j]&0xFF, i);
				usleep(FPGA_WRITE_DLY);
			}
		
#endif		
			fpga_i_Dly_load_signal();		// TOP level image

		} else {
			printf("\n\tTxDQDQS1:: FPAG1/2 is not in test mode (A[103] bit0 is 0)! %#.8x, %#.8x\n", ts_5, ts_7);
			exit (-1);
		}

		// Write 4KB data pattern
		gettimeofday(&start1, NULL);	
//MK-begin
		MemCpy(WR, data1, data2, TX_DQDQS1_DATA_SIZE);
//MK-end
		gettimeofday(&end1, NULL);

		// Read training results DQ0-71
		ts_5 = check_fpga_status(FPGA1, TRN_STATUS, CAPTURE_DN, FPGA_TIMEOUT);
#if BOTH_FPGA
		ts_7 = check_fpga_status(FPGA2, TRN_STATUS, CAPTURE_DN, FPGA_TIMEOUT);
#endif		
		if ( ((ts_5 & CAPTURE_DN) == CAPTURE_DN) && ((ts_5 & TRN_ERR) == 0) 
#if BOTH_FPGA
		  && ((ts_7 & CAPTURE_DN) == CAPTURE_DN) && ((ts_7 & TRN_ERR) == 0)
#endif
		   ) {
			ReadDQResultFromFPGA(TX_DQ_DQS_1, i, perBit, 0xF);
		} else {
			printf("\n\tTxDQDQS1:: I2C error on FPGA1/2: status = %#.8x, %#.8x\n", ts_5, ts_7);
			exit (-1);
		}

		gettimeofday(&end, NULL);
		elapsedtime = ((end.tv_sec * 1000000 + end.tv_usec)
				     - (start.tv_sec * 1000000 + start.tv_usec));
		elapsedtime1 = ((end1.tv_sec * 1000000 + end1.tv_usec)
				     - (start1.tv_sec * 1000000 + start1.tv_usec));
	
		if(TestTime) printf(" ... %lduS, %ldmS", elapsedtime1, elapsedtime/1000); 
	}

	// Find middle passing point
	FindCenterPoint(TX_DQ_DQS_1, perBit, start_lp, end_lp, step_size);

	
	/* Set FPGA back to NORMAL mode per jsung's request */
	WriteSMBus(FPGA1, D, TRN_MODE_ADDR, NORMAL_MODE);
#if BOTH_FPGA
	WriteSMBus(FPGA2, D, TRN_MODE_ADDR, NORMAL_MODE);
#endif
	usleep(FPGA_CMD_WR_DLY);
}

void RxDQDQS1(int start_lp, int end_lp, int step_size)
{
	int i, j, ts_5, ts_7, perBit;

	printf("\n\n RxDQDQS1:: %s, Data=%#.16lx & %#.16lx\n", "Fake-RD DDR4-to-FPGA Training", data1, data2);

	// Prefill the 4KB data pattern
//MK	MemCpy(WR, data1, data2);
//MK-begin
	MemCpy(WR, data1, data2, RX_DQDQS1_DATA_SIZE);
//MK-end

	// Set training mode
	WriteSMBus(FPGA1, D, TRN_MODE_ADDR, RX_DQDQS_1_MODE);
#if BOTH_FPGA
	WriteSMBus(FPGA2, D, TRN_MODE_ADDR, RX_DQDQS_1_MODE);
#endif	
	usleep(FPGA_CMD_WR_DLY);

	printf("\n  DQ: 0-------7 8------15 16-----23 24-----31 32-----39 40-----47 48-----55 56-----63 64-----71");
	for(i=start_lp; i<end_lp; i=i+step_size) {

		gettimeofday(&start, NULL);	
		printf("\n %3d:",i);	

		// Set DQ delay time A[51:34]
		ts_5 = check_fpga_status(FPGA1,TRN_STATUS,TRN_MODE,FPGA_TIMEOUT);
#if BOTH_FPGA
		ts_7 = check_fpga_status(FPGA2,TRN_STATUS,TRN_MODE,FPGA_TIMEOUT);
#endif
		if ( ((ts_5 & TRN_MODE)==TRN_MODE) && ((ts_5 & TRN_ERR)==0) 
#if BOTH_FPGA
		  && ((ts_7 & TRN_MODE)==TRN_MODE) && ((ts_7 & TRN_ERR)==0) 
#endif
		) {
#if PER_BIT_RX_DQDQS1
			perBit = 1;
			for (j=0; j < 64 + 8*eccMode; j++) {
				WriteSMBus( (i2cRXDQS1_perBit[j]>>12), D, i2cRXDQS1_perBit[j]&0xFF, i);
				usleep(FPGA_WRITE_DLY);
			}
#else
			perBit = 0;
			for (j=0; j < 16 + 2*eccMode; j++) {
				WriteSMBus( (i2cRXDQS1[j]>>12), D, i2cRXDQS1[j]&0xFF, i);
				usleep(FPGA_WRITE_DLY);
			}
#endif			
			fpga_i_Dly_load_signal();		// TOP level image 7/25/2016

		} else {
			printf("RxDQDQS1:: FPAG1/2 is not in test mode (A[103] bit0 is 0)! %#.8x, %#.8x\n", ts_5, ts_7);
			exit (-1);
		}

		// Read 4KB data pattern
		gettimeofday(&start1, NULL);	
//MK		MemCpy(RD, data1, data2);
//MK-begin
		MemCpy(RD, data1, data2, RX_DQDQS1_DATA_SIZE);
//MK-end
		gettimeofday(&end1, NULL);

		// Read training results DQ0-71
		ts_5 = check_fpga_status(FPGA1, TRN_STATUS, CAPTURE_DN, FPGA_TIMEOUT);
#if BOTH_FPGA
		ts_7 = check_fpga_status(FPGA2, TRN_STATUS, CAPTURE_DN, FPGA_TIMEOUT);
#endif		
		if ( ((ts_5 & CAPTURE_DN) == CAPTURE_DN) && ((ts_5 & TRN_ERR) == 0) 
#if BOTH_FPGA
		  && ((ts_7 & CAPTURE_DN) == CAPTURE_DN) && ((ts_7 & TRN_ERR) == 0)
#endif
		   ) {
			ReadDQResultFromFPGA(RX_DQ_DQS_1, i, perBit, 0xF);
		} else {
			printf("RxDQDQS1:: I2C error on FPGA1/2: status = %#.8x, %#.8x\n", ts_5, ts_7);
			exit (-1);
		}

		gettimeofday(&end, NULL);
		elapsedtime = ((end.tv_sec * 1000000 + end.tv_usec)
				     - (start.tv_sec * 1000000 + start.tv_usec));
		elapsedtime1 = ((end1.tv_sec * 1000000 + end1.tv_usec)
				     - (start1.tv_sec * 1000000 + start1.tv_usec));

		if(TestTime) printf(" ... %lduS, %ldmS", elapsedtime1, elapsedtime/1000); 
	}

	// Find middle passing point
	FindCenterPoint(RX_DQ_DQS_1, perBit, start_lp, end_lp, step_size);

	/* Set FPGA back to NORMAL mode per jsung's request */
	WriteSMBus(FPGA1, D, TRN_MODE_ADDR, NORMAL_MODE);
#if BOTH_FPGA
	WriteSMBus(FPGA2, D, TRN_MODE_ADDR, NORMAL_MODE);
#endif
	usleep(FPGA_CMD_WR_DLY);
}

//MK-begin
/**
 * TxDQDQS2 - FPGA-to-DDR4 Write Training for fake-write operation
 *
 **/
void TxDQDQS2(int start_lp, int end_lp, int step_size)
{
	unsigned int i, j, bl, ts_5, ts_7, dwordData1, perBit;
	unsigned long qwExpData, qwFPGA1, qwFPGA2, qw1, qw2, qw1xor, qw2xor, qwresult;
	unsigned long qRd[8], exp1, exp2;
	unsigned char byteData;
	void *v_addr;
	unsigned long data3, data4;

	ts_5 = ReadSMBus(FPGA1,D,14);
#if BOTH_FPGA
	ts_7 = ReadSMBus(FPGA2,D,14);
#endif
	
	if(!(ts_5&0x80)) {		// Byte14.bit7 - '1' Enable FakeWR operation
		printf("\n FPGA1 Fake WR Training Mode is not ready");
		printf("\t Set  Byte14 bit 7 to '1' ... ");
		WriteSMBus(FPGA1, D, 14, ts_5 | 0x80);
		printf("Done");
	}	
#if BOTH_FPGA
	if(!(ts_7&0x80)) {		// Byte14.bit7 - '1' Enable FakeWR operation
		printf("\n FPGA2 Fake WR Training Mode is not ready");
		printf("\t Set Byte14 bit 7 to '1' ... ");
		WriteSMBus(FPGA2, D, 14, ts_7 | 0x80);
		printf("Done");
	}
#endif
//	enable_bcom_switch();

	/*
	 * 1. Read I2C A[118:115] for Data1 (32-bit) and A[122:119] for Data2 (32-bit)
	 * This step is no longer needed in this routine since we generate
	 * the table with the result from FPGA.
	 */
	
//	data1 = data2 = data3 = data4 = 0;

	if(SetPattern) {
		printf("\n data1=%x data2=%x data3=%x data4=%x", data1, data2, data3, data4);
		writeInitialPatternToFPGA(2);
		data1 = data1 & 0xffffffff;
		data2 = data2 & 0xffffffff;
		data3 = data1;
		data4 = data2;
	} else {		
		data1 = data2 = data3 = data4 = 0;
		for (i=0; i < 4; i++) {
			dwordData1 = (unsigned int)ReadSMBus(FPGA1, D, TX_DQDQS2_FPGA_DATA1+i);
			data1 = data1 | (dwordData1 << (i*8));
			dwordData1 = (unsigned int)ReadSMBus(FPGA1, D, TX_DQDQS2_FPGA_DATA2+i);
			data2 = data2 | (dwordData1 << (i*8));
#if BOTH_FPGA
			dwordData1 = (unsigned int)ReadSMBus(FPGA2, D, TX_DQDQS2_FPGA_DATA1+i);
			data3 = data3 | (dwordData1 << (i*8));
			dwordData1 = (unsigned int)ReadSMBus(FPGA2, D, TX_DQDQS2_FPGA_DATA2+i);
			data4 = data4 | (dwordData1 << (i*8));
#endif
	}
		printf("\n data1=%x data2=%x data3=%x data4=%x", data1, data2, data3, data4);
	}

#if BOTH_FPGA
	printf("\n\n TxDQDQS2:: %s, Data1,2= %#.8x & %#.8x / %#.8x & %#.8x\n",
			"Fake-WR FPGA-to-DDR4 Training", data1, data2, data3, data4);
#else
	printf("\n\nTxDQDQS2:: %s, Data1,2= %#.8x & %#.8x\n", "Fake-WR FPGA-to-DDR4 Training", data1, data2);
#endif

//	if ( debug_str != NULL && (debug_flag & 0x00000002) != 0 ) {
	if ( (debug_flag & 0x00000002) != 0 ) {
		printf("\n  DQ:  0:63       [BL0] 0:63       [BL1] 0:63       [BL2] 0:63       [BL3]"
				   " 0:63       [BL4] 0:63       [BL5] 0:63       [BL6] 0:63       [BL7]     0:63   '0': Pass");
	} else {
		printf("\n  DQ: 0-------7 8------15 16-----23 24-----31 32-----39 40-----47 48-----55 56-----63 64-----71");
		printf("  0123456789ABCDEF");
	}		
	for (i=start_lp; i < end_lp; i=i+step_size)
	{
		/* Get the base addr of training_data window */
		v_addr = map_base + (target & MAP_MASK2);

		gettimeofday(&start, NULL);
		printf("\n %3d:",i);

		/* 2. Write a pattern to training_data window ... Background */
		MemCpy2(WR, 0x5555555555555555, 0x5555555555555555, TX_DQDQS2_DATA_SIZE);

		/* 3. Set training mode to 3 for Tx DQ-DQS(2) */
		WriteSMBus(FPGA1, D, TRN_MODE_ADDR, TX_DQDQS_2_MODE);
#if BOTH_FPGA
		WriteSMBus(FPGA2, D, TRN_MODE_ADDR, TX_DQDQS_2_MODE);
#endif
		usleep(FPGA_CMD_WR_DLY);

		/*
		 * 4. IF ( A[103].bit0 == 1 )
		 *    THEN Set "Int. DQ/DQS Delay" time (A[69:52]) to value in i
		 *    ENDIF
		 */
//		byteData = (unsigned char)ReadSMBus(FPGA1, D, TRN_STATUS);
//		if (byteData & 0x01 == 1) {
		ts_5 = check_fpga_status(FPGA1,TRN_STATUS,TRN_MODE,FPGA_TIMEOUT);
#if BOTH_FPGA
		ts_7 = check_fpga_status(FPGA2,TRN_STATUS,TRN_MODE,FPGA_TIMEOUT);
#endif
		if ( ((ts_5 & TRN_MODE)==TRN_MODE) && ((ts_5 & TRN_ERR)==0) 
#if BOTH_FPGA
		  && ((ts_7 & TRN_MODE)==TRN_MODE) && ((ts_7 & TRN_ERR)==0) 
#endif
		) {
			WriteSMBus(FPGA1, D, 0xA0, 0x1);	// Set Page '1'
			WriteSMBus(FPGA2, D, 0xA0, 0x1);	// Set Page '1'
			usleep(FPGA_SETPAGE_DLY);
#if PER_BIT_TX_DQDQS2
			perBit = 1;
			for (j=0; j < 64 + 8*eccMode; j++) {
				WriteSMBus( (i2cTXDQS2_perBit[j]>>12), D, i2cTXDQS2_perBit[j]&0xFF, i);
				usleep(FPGA_WRITE_DLY);
			}
#else				
			perBit = 0;
			for (j=0; j < 16 + 2*eccMode; j++) {
				WriteSMBus( (i2cTXDQS2[j]>>12), D, i2cTXDQS2[j]&0xFF, i);
				usleep(FPGA_WRITE_DLY);
			} 
#endif
			WriteSMBus(FPGA1, D, 0xA0, 0x0);	// Set Page '0'
			WriteSMBus(FPGA2, D, 0xA0, 0x0);	// Set Page '0'
			usleep(FPGA_SETPAGE_DLY);

			fpga_o_Dly_load_signal();		// TOP level image 7/29/2016

		} else {
			printf("TxDQDQS2:: FPGA1/2 is not in test mode (A[103] bit0 is 0)! %#.8x, %#.8x\n", ts_5, ts_7);
			exit (-1);
		}

		/* 5. Write a pattern to training_data window for Fake WR operation */
		gettimeofday(&start1, NULL);
		MemCpy2(WR, 0x6666666666666666, 0x9999999999999999, TX_DQDQS2_DATA_SIZE);
		gettimeofday(&end1, NULL);

		/* 6. Set training mode to NORMAL mode */
		WriteSMBus(FPGA1, D, TRN_MODE_ADDR, NORMAL_MODE);
#if BOTH_FPGA
		WriteSMBus(FPGA2, D, TRN_MODE_ADDR, NORMAL_MODE);
#endif
		usleep(FPGA_CMD_WR_DLY);

		/*
		 * 7. IF ( A[103].bit2 == 1 && bit7 == 0 )
		 *    THEN
		 *		fill up the table with calibration results from FPGA
		 *    ENDIF
		 */
		ts_5 = (unsigned char) check_fpga_status(FPGA1, TRN_STATUS, CAPTURE_DN, FPGA_TIMEOUT);
#if BOTH_FPGA
		ts_7 = (unsigned char) check_fpga_status(FPGA2, TRN_STATUS, CAPTURE_DN, FPGA_TIMEOUT);
#endif
	//	if ( ((ts_5 & 0x84) ^ 0x80 == 0x84)
		if ( ((ts_5 & CAPTURE_DN) == CAPTURE_DN) && ((ts_5 & TRN_ERR) == 0) 
#if BOTH_FPGA
	//	  && ((ts_7 & 0x84) ^ 0x80 == 0x84)
		  && ((ts_7 & CAPTURE_DN) == CAPTURE_DN) && ((ts_7 & TRN_ERR) == 0)
#endif
		   ) {
			/* Draw the graph based on the result from FPGA */
			ReadDQResultFromFPGA(TX_DQ_DQS_2, i, perBit, 0xF);

			/* Read two 64-bit data from DRAM */
			for(j=0;j<8;j++) 
				qRd[j] = *((unsigned long *) v_addr+j);

			qw1 = qRd[0] | qRd[2] | qRd[4] | qRd[6];	// data1-data1
			qw2 = qRd[1] | qRd[3] | qRd[5] | qRd[7];	// data2-data2
										
			//
			// data2 => data1 order, HW 01.07.28.16  V7.31.16, SJ
			//
			exp1 = data4 << 32 | data2;	//
			exp2 = data3 << 32 | data1;	// 
			qw1xor = qw1 ^ exp1;		// RD data1 ^ expected Data1
			qw2xor = qw2 ^ exp2;		// RD data2 ^ expected Data2
			// 
			// final result should be ORed not XORed.    V7.31.16, SJ
			//
			qwresult = qw1xor | qw2xor;
#if 0
			printf("\n  exp1= %016lx,   exp2= %016lx\n",exp1, exp2);		
			printf("   qw1= %016lx,    qw2= %016lx\n",qw1, qw2);
			printf("qw1xor= %016lx, qw2xor= %016lx\n",qw1xor, qw2xor);
			printf("qwresult= %016lx\n\n",qwresult);
#endif
		//	if ( debug_str != NULL && (debug_flag & 0x00000001) != 0 ) {
			if ( (debug_flag & 0x00000001) != 0 ) {
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
		//	if ( debug_str != NULL && (debug_flag & 0x00000002) != 0 ) {
			if ( (debug_flag & 0x00000002) != 0 ) {
#if 0
				/* Two 32-bit data from FPGA are merged in a 64-bit value */
				qwExpData = ((unsigned long)data2 << 32) | (unsigned long)data1;

				/* Two 32-bit data in DRAM fake-written by FPGA0 are merged in a 64-bit value */
				qwFPGA1 = (qw2 << 32) | (qw1 & 0x00000000FFFFFFFF);

				/* Two 32-bit data in DRAM fake-written by FPGA1 are merged in a 64-bit value */
				qwFPGA2 = (qw1 >> 32) | (qw2 & 0xFFFFFFFF00000000);

			//	printf("  [%lx-%lx]  ", qwExpData, qwFPGA1);
				cmpTxDQDQS64b_2(i, qwExpData, qwFPGA1, qwFPGA2);
#endif
			//	printf("  %016lx %016lx %016lx %016lx %016lx %016lx %016lx %016lx XOR:",
		//				qRd[0], qRd[1], qRd[2], qRd[3], qRd[4], qRd[5], qRd[6], qRd[7]);
				
				printf(" ");
				for(bl=0;bl<8;bl++) {
					printf(" ");
					for(j=0;j<16;j++) {
						printf("%01x",(qRd[bl]>>j*4)&0xF);
					}
				}
				printf(" XOR:");
				for(j=0;j<16;j++) {
					rDQS[i][j] = (qwresult>>j*4)&0xF;
					printf("\033[0;%dm%1X\033[0m", 
							(qwresult>>j*4)&0xF?31:34,
							(qwresult>>j*4)&0xF);
				}
			}
		
		} else {
			printf("TxDQDQS2:: I2C error on FPGA1/2: status = %#.8x, %#.8x\n", ts_5, ts_7);
			exit (-1);
		}

		gettimeofday(&end, NULL);

		/* Calculate abd display elapsed time */
		elapsedtime = ((end.tv_sec * 1000000 + end.tv_usec)
				     - (start.tv_sec * 1000000 + start.tv_usec));
		elapsedtime1 = ((end1.tv_sec * 1000000 + end1.tv_usec)
				     - (start1.tv_sec * 1000000 + start1.tv_usec));

		if(TestTime) printf(" ... %lduS, %ldmS", elapsedtime1, elapsedtime/1000);

	} // end of for

	/* Find middle passing point */
	FindCenterPoint(TX_DQ_DQS_2, perBit, start_lp, end_lp, step_size);

	/* Set FPGA back to NORMAL mode per jsung's request */
	WriteSMBus(FPGA1, D, TRN_MODE_ADDR, NORMAL_MODE);
#if BOTH_FPGA
	WriteSMBus(FPGA2, D, TRN_MODE_ADDR, NORMAL_MODE);
#endif
	usleep(FPGA_CMD_WR_DLY);
}

void writeInitialPatternToFPGA(int write_fpga_data)
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
	} else if(write_fpga_data==2) {
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
	}		
}

//MK-end

void list(int D)
{
	int i, j;
	unsigned char data;

	printf("\n DIMM[%d] N%d.C%d.D%d : %-18s S/N - %s %6.1fC",
					D, D<12?0:1, (int)(D)<12?(int)(D/3):(int)(D/3)-4, (int)(D%3),
				   	sPN[D], sSN[D], DimmTemp[D]/16.);

	printf("   FPGA H/W - %02X-%02X.%02X.%02X, %02X-%02X.%02X.%02X",
					ReadSMBus(FPGA1,D,2), ReadSMBus(FPGA1,D,3), ReadSMBus(FPGA1,D,4),ReadSMBus(FPGA1,D,5),
					ReadSMBus(FPGA2,D,2), ReadSMBus(FPGA2,D,3), ReadSMBus(FPGA2,D,4),ReadSMBus(FPGA2,D,5));

	printf("\n\n");
	printf("           | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\t");
	printf(" 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
	printf(" ----------+------------------------------------------------\t");
	printf("------------------------------------------------\n");

	for(i=0;i<8;i++) {
		printf(" %03d(0x%02X) :",i*16,i*16);
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

	WriteSMBus(FPGA1, D, 0xA0, 0x1);	// Set Page '1'
	WriteSMBus(FPGA2, D, 0xA0, 0x1);	// Set Page '1'
	usleep(FPGA_SETPAGE_DLY);
	for(i=0;i<8;i++) {
		printf(" %03d(0x%02X):",i*16+256,i*16+256);
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
	WriteSMBus(FPGA1, D, 0xA0, 0x0);	// Set Page '0'
	WriteSMBus(FPGA2, D, 0xA0, 0x0);	// Set Page '0'
	usleep(FPGA_SETPAGE_DLY);

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

	printf("\n\n CMD_CK Delay Addr: %3d(0x%X)", CMD_CK_ADDR, CMD_CK_ADDR);

#if PER_BIT_TX_DQDQS1
	printf("   TxDQDQS1(WR) Addr: %3d(0x%X)", i2cTXDQS1_perBit[0]&0xFFF, i2cTXDQS1_perBit[0]&0xFFF);
#else
	printf("   TxDQDQS1(WR) Addr: %3d(0x%X)", i2cTXDQS1[0]&0xFFF, i2cTXDQS1[0]&0xFFF);
#endif
#if PER_BIT_RX_DQDQS1
	printf("\n RxDQDQS1(FakeRD) : %3d(0x%X)", i2cRXDQS1_perBit[0]&0xFFF, i2cRXDQS1_perBit[0]&0xFFF);
#else
	printf("\n RxDQDQS1(FakeRD) : %3d(0x%X)", i2cRXDQS1[0]&0xFFF, i2cRXDQS1[0]&0xFFF);
#endif
#if PER_BIT_TX_DQDQS2
	printf("   TxDQDQS2(FakeWR) : %3d(0x%X)", i2cTXDQS2_perBit[0]&0xFFF, i2cTXDQS2_perBit[0]&0xFFF);
#else
	printf("   TxDQDQS2(FakeWR) : %3d(0x%X)", i2cTXDQS2[0]&0xFFF, i2cTXDQS2[0]&0xFFF);
#endif

	printf("\n\n");
	exit(1);
}

void getSN(int D)
{
	int i, ww, wrem;
	char SN[6];

	SetPageAddress(D, 1);
	usleep(FPGA_SETPAGE_DLY);

	for(i=0;i<18;i++) {
		sPN[D][i]=ReadSMBus(SPD,D,(329-256+i));
	}

	for(i=0;i<6;i++) {
		SN[i] = ReadSMBus(SPD,D,(323-256+i));
	}
	SetPageAddress(D, 0);
	usleep(FPGA_SETPAGE_DLY);

	SN[0] = SN[0] - 8;
	ww = 10*(SN[1]>>4) + SN[1]&0xF -1;
	wrem = SN[2]>>4;
	sprintf(sSN[D],"%X%03d%X%02X%02X%02X",
					SN[0]&0xF,ww*7+wrem,SN[2]&0xF, SN[3]&0xFF, SN[4]&0xFF, SN[5]&0xFF);

	DimmTemp[D] = ReadSMBus(TSOD, D, 5) & 0xFFF;

}

void dumpspd(int D)
{
	int i, j;

	printf(" DIMM[%d] spd data\n\n", D);
#if 1	
	SetPageAddress(D, 0);

	for(i=0;i<16;i++) {
		printf(" %03X :",i*16);
		for(j=0;j<16;j++) printf(" %02X", ReadSMBus(SPD, D, i*16+j));
		printf("\n");
	}
	printf("\n");	
#endif
	SetPageAddress(D, 1);

	for(i=0;i<16;i++) {
		printf(" %03X :",i*16+256);
		for(j=0;j<16;j++) printf(" %02X",ReadSMBus(SPD, D, i*16+j));
		printf("\n");
	}

	SetPageAddress(D, 0);	

	printf("\n");

	exit(1);
}	

void LoadfromFile(int D)
{
	int i, j;
	int tx1[72], rx1[72], tx2[72], cmd[2], rd[2];
	char *ptr, sline[512], tname[20];
	FILE *fp;
	

	if ((fp=fopen(logfile,"r"))==NULL) {
		fprintf(stdout,"\n\n Cannot open file %s !! \n\n",logfile); exit(1); 
	}

	fprintf(stdout,"\n Load training data from File - %s ...", logfile);

	while (fgets(sline,512,fp)!=NULL) {
		if ( strstr(sline,"TxDQDQS1:") != NULL) {
			printf("\nTxDQDQS1:");
			ptr = strstr(sline,":");	
			ptr++;
#if PER_BIT_TX_DQDQS1		
			for(i=0;i<64 + 8*eccMode;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &tx1[i]);
				printf(" %d",tx1[i]);
				WriteSMBus( (i2cTXDQS1_perBit[i]>>12), D, i2cTXDQS1_perBit[i]&0xFF, tx1[i]);
				usleep(FPGA_WRITE_DLY);
			}
#else
			for(i=0;i<16 + 2*eccMode;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &tx1[i]);
				printf(" %d",tx1[i]);
				WriteSMBus( (i2cTXDQS1[i]>>12), D, i2cTXDQS1[i]&0xFF, tx1[i]);
				usleep(FPGA_WRITE_DLY);
			}			
#endif
			printf("\n");
		}	
		if ( strstr(sline,"RxDQDQS1:") != NULL) {
			printf("\nRxDQDQS1:");
			ptr = strstr(sline,":");	
			ptr++;
#if PER_BIT_RX_DQDQS1		
			for(i=0;i<64 + 8*eccMode;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &rx1[i]);
				printf(" %d",rx1[i]);
				WriteSMBus( (i2cRXDQS1_perBit[i]>>12), D, i2cRXDQS1_perBit[i]&0xFF, rx1[i]);
				usleep(FPGA_WRITE_DLY);
			}
#else
			for(i=0;i<16 + 2*eccMode;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &rx1[i]);
				printf(" %d",rx1[i]);
				WriteSMBus( (i2cRXDQS1[i]>>12), D, i2cRXDQS1[i]&0xFF, rx1[i]);
				usleep(FPGA_WRITE_DLY);
			}			
#endif
			printf("\n");
		}	
		if ( strstr(sline,"TxDQDQS2:") != NULL) {
			printf("\nTxDQDQS2:");
			ptr = strstr(sline,":");	
			ptr++;

			WriteSMBus(FPGA1, D, 0xA0, 0x1);	// Set Page '1'
			WriteSMBus(FPGA2, D, 0xA0, 0x1);	// Set Page '1'
			usleep(FPGA_SETPAGE_DLY);
			
#if PER_BIT_TX_DQDQS2
			for(i=0;i<64 + 8*eccMode;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &tx2[i]);
				printf(" %d",tx2[i]);
				WriteSMBus( (i2cTXDQS2_perBit[i]>>12), D, i2cTXDQS2_perBit[i]&0xFF, tx2[i]);
				usleep(FPGA_WRITE_DLY);
			}
#else
			for(i=0;i<16 + 2*eccMode;i++,ptr=ptr+4) {
				sscanf(ptr,"%4d", &tx2[i]);
				printf(" %d",tx2[i]);
				WriteSMBus( (i2cTXDQS2[i]>>12), D, i2cTXDQS2[i]&0xFF, tx2[i]);
				usleep(FPGA_WRITE_DLY);
			}			
#endif
			WriteSMBus(FPGA1, D, 0xA0, 0x0);	// Set Page '0'
			WriteSMBus(FPGA2, D, 0xA0, 0x0);	// Set Page '0'
			usleep(FPGA_SETPAGE_DLY);

			printf("\n");
		}			

		fpga_i_Dly_load_signal();		// 8.2.2016
		fpga_o_Dly_load_signal();		// 8.2.2016

	}

	printf("Done\n\n");

	fclose(fp);

	exit(1);
}

void WritetoFile(int D)
{
	int i, j;
	unsigned char tx1[72], rx1[72], tx2[72], cmd[2], rd[2];
	time_t  timer;
	struct tm *tim;
	FILE *fp;
	
	timer = time(0);
	tim = localtime(&timer);

	if(logging==0) {
		sprintf(logfile, "%s-%s-%02d%02d-%02d%02d%02d.log", 
						sSN[D], hostname, tim->tm_hour, tim->tm_min, 
						tim->tm_mon+1, tim->tm_mday,tim->tm_year+1900);
	}

	fp = fopen(logfile,"w");
	fprintf(stdout,"\n Save training data to File - %s ...", logfile);

	fprintf(fp,"     DQ :");
	for (j=0; j < 64 + 8*eccMode; j++) fprintf(fp," %3d",j);
			
	fprintf(fp,"\nTxDQDQS1:");	
#if PER_BIT_TX_DQDQS1
	for (j=0; j < 64 + 8*eccMode; j++) {
		tx1[j] = ReadSMBus( (i2cTXDQS1_perBit[j]>>12), D, i2cTXDQS1_perBit[j]&0xFF);
		usleep(FPGA_WRITE_DLY);
		fprintf(fp," %3d",tx1[j]);
	}
#else				
	for (j=0; j < 16 + 2*eccMode; j++) {
		tx1[j] = ReadSMBus( (i2cTXDQS1[j]>>12), D, i2cTXDQS1[j]&0xFF);
		usleep(FPGA_WRITE_DLY);
		fprintf(fp," %3d",tx1[j]);
	}
#endif	

	fprintf(fp,"\nRxDQDQS1:");	
#if PER_BIT_RX_DQDQS1
	for (j=0; j < 64 + 8*eccMode; j++) {
		rx1[j] = ReadSMBus( (i2cRXDQS1_perBit[j]>>12), D, i2cRXDQS1_perBit[j]&0xFF);
		usleep(FPGA_WRITE_DLY);
		fprintf(fp," %3d",rx1[j]);
	}
#else				
	for (j=0; j < 16 + 2*eccMode; j++) {
		rx1[j] = ReadSMBus( (i2cRXDQS1[j]>>12), D, i2cRXDQS1[j]&0xFF);
		usleep(FPGA_WRITE_DLY);
		fprintf(fp," %3d",rx1[j]);
	}
#endif	

	fprintf(fp,"\nTxDQDQS2:");	
	
	WriteSMBus(FPGA1, D, 0xA0, 0x1);	// Set Page '1'
	WriteSMBus(FPGA2, D, 0xA0, 0x1);	// Set Page '1'
	usleep(FPGA_SETPAGE_DLY);
#if PER_BIT_TX_DQDQS2
	for (j=0; j < 64 + 8*eccMode; j++) {
		tx2[j] = ReadSMBus( (i2cTXDQS2_perBit[j]>>12), D, i2cTXDQS2_perBit[j]&0xFF);
		usleep(FPGA_WRITE_DLY);
		fprintf(fp," %3d",tx2[j]);
	}
#else				
	for (j=0; j < 16 + 2*eccMode; j++) {
		tx2[j] = ReadSMBus( (i2cTXDQS2[j]>>12), D, i2cTXDQS2[j]&0xFF);
		usleep(FPGA_WRITE_DLY);
		fprintf(fp," %3d",tx2[j]);
	}
#endif
	WriteSMBus(FPGA1, D, 0xA0, 0x0);	// Set Page '0'
	WriteSMBus(FPGA2, D, 0xA0, 0x0);	// Set Page '0'
	usleep(FPGA_SETPAGE_DLY);

	fprintf(fp,"\nCMD-CLK :");
	cmd[0] = ReadSMBus(FPGA1, D, CMD_CK_ADDR);
	cmd[1] = ReadSMBus(FPGA2, D, CMD_CK_ADDR);
	usleep(10000);
	fprintf(fp," %3d %3d\n",cmd[0],cmd[1]);

	printf("Done\n\n");

	fclose(fp);

	exit(1);
}

void wr_cmd_test(int D, int testmode)
{
	int i, j;	
	int read1, read2, pat1, pat2;
	
	printf("\n Write 64B data 0x%016lx, 0x%016lx to HV_MMIO_CMD window - %p ...", 
					pattern1, pattern2, target + CMD_OFFSET);
#if 0
	MemCpy(HV_MMIO_CMD, pattern1, pattern2, 64);
#else
	virt_addr = map_base + (target & MAP_MASK2);
	
	for (i=0; i < 4; i++) {
		*((unsigned long *) (virt_addr + CMD_OFFSET)) = data1;
		virt_addr = (void *) ((unsigned long *)virt_addr + 1);
		*((unsigned long *) (virt_addr + CMD_OFFSET)) = data2;
		virt_addr = (void *) ((unsigned long *)virt_addr + 1);

	}
//MK-begin
	clflush_cache_range(map_base + (target & MAP_MASK2 + CMD_OFFSET), 4*16);
//MK-end

#endif
	printf(" Done\n");

#if 0
	WriteSMBus(FPGA1, D, 0xA0, 0x1);	// Set Page '1'
	WriteSMBus(FPGA2, D, 0xA0, 0x1);	// Set Page '1'
	usleep(FPGA_SETPAGE_DLY);

	for(i=0;i<8;i=i+2) {
		for(read1=0,j=0;j<4;j++) 
			read1 |= (ReadSMBus(FPGA1, D, i*16+j) << (j*8));
		pat1 = (int)(pattern1 & 0xFFFFFFFF);	
		printf("\n %02x BL%d: read1= %08X, pat1= %08X, xor= %8X", i*16, i, read1, pat1, read1^pat1);

		for(read1=0,j=0;j<4;j++) 
			read1 |= (ReadSMBus(FPGA2, D, i*16+j) << (j*8));
		pat1 = (int)((pattern1>>32) & 0xFFFFFFFF);	
		printf("\t read1= %08X, pat1= %08X, xor= %8X", read1, pat1, read1^pat1);

		for(read2=0,j=0;j<4;j++) 
			read2 |= (ReadSMBus(FPGA1, D, i*16+j+16) << (j*8));
		pat2 = (int)(pattern2 & 0xFFFFFFFF);	
		printf("\n %02x BL%d: read2= %08X, pat2= %08X, xor= %8X", i*16+16, i+1, read2, pat2, read2^pat2);

		for(read2=0,j=0;j<4;j++) 
			read2 |= (ReadSMBus(FPGA2, D, i*16+j+16) << (j*8));
		pat2 = (int)((pattern2>>32) & 0xFFFFFFFF);	
		printf("\t read2= %08X, pat2= %08X, xor= %8X", read2, pat2, read2^pat2);		
	}

	WriteSMBus(FPGA1, D, 0xA0, 0x0);	// Set Page '0'
	WriteSMBus(FPGA2, D, 0xA0, 0x0);	// Set Page '0'
	usleep(FPGA_SETPAGE_DLY);
#endif
	printf("\n\n");
	exit(1);
}

int i2cMuxCmd[9] = { 0x5100, 0x5101, 0x5102, 0x5103, 0x7100, 0x7101, 0x7102, 0x7103, 0x5104 };

int i2cMuxSet[9] = { 0x5008, 0x5009, 0x500a, 0x500b, 0x7008, 0x7009, 0x700a, 0x700b, 0x500c };

void cmd_mux_cycle_test(int D, int expectedOffset)
{
	int i, j, bl, cmdMuxOld[18], cmdMuxNew[18], rdata, cdata;

	printf("\n CMD-Mux-Cycle testing ... Data= 0x%x, expectedOffset= %d", Data, expectedOffset); 
	printf("\n");

	for(i=0; i < 8 + eccMode ; i++) {
		cdata = ReadSMBus( i2cMuxSet[i]>>12, D, (i2cMuxSet[i] & 0xFF));
		cmdMuxOld[i*2] = cdata & 0xF;
		cmdMuxOld[i*2+1] = (cdata>>4) & 0xF;
	//	printf(" %2X", cmdMuxOld[i]);
	}

	WriteSMBus(FPGA1, D, 0xA0, 0x1);	// Set Page '1'
	WriteSMBus(FPGA2, D, 0xA0, 0x1);	// Set Page '1'
	usleep(FPGA_SETPAGE_DLY);

	for(i=0; i < 16 + eccMode * 2 ; i++) {
		cmdMuxNew[i] = cmdMuxOld[i];
		j=(i>>1);
		
		for(bl=0;bl<8;bl++) {
			rdata = ReadSMBus( i2cMuxCmd[j]>>12, D, (i2cMuxCmd[j] & 0xFF) + bl*16 );
			if(i%2==0) rdata = rdata & 0xF;
			else       rdata = (rdata>>4) & 0xF;
			if (Data == rdata) {
				cmdMuxNew[i] = cmdMuxOld[i] + (expectedOffset - bl);
				if(cmdMuxNew[i] < 0) {
					printf("\n\t Error!! Nibble %d, new target value is %d", cmdMuxNew[i]);
					cmdMuxNew[i] = cmdMuxOld[i];
				}		
				break;
			}
		}
			
	}

	WriteSMBus(FPGA1, D, 0xA0, 0x0);	// Set Page '0'
	WriteSMBus(FPGA2, D, 0xA0, 0x0);	// Set Page '0'
	usleep(FPGA_SETPAGE_DLY);

	printf("\n  FROM: ");
	for(i=0; i < 8 + eccMode ; i++) {
		printf("  0x%02x", (cmdMuxOld[i*2+1]<<4) + cmdMuxOld[i*2]);
	}
	printf("\n    TO: ");	
	for(i=0; i < 8 + eccMode ; i++) {
		printf("  0x%02x", (cmdMuxNew[i*2+1]<<4) + cmdMuxNew[i*2]);
		if(update_hw) {
			WriteSMBus( i2cMuxSet[i]>>12, D, (i2cMuxSet[i] & 0xFF), (cmdMuxNew[i*2+1]<<4) + cmdMuxNew[i*2] );
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
		"\n              1= CMD-CK"
		"\n              2= RD-CMD-CK"
		"\n              4= Tx-DQDQS1(WR)"
		"\n              8= Rx-DQDQS1(FakeRD)"
		"\n             16= Tx-DQDQS2(FakeWR)  ex) 0x1F= all"
		"\n  -s --start [0~256]"
		"\n  -e --end   [0~256]"
		"\n  -z --step  [1~2^n]"
		"\n  -p --pattern <data1> <data2>          | Set Training data1, data2"
		"\n  -u --update                           | update_hw, default - no update"
		"\n  -U --Update                           | update_hw & show summary"
		"\n  -l --log                              | enable logging, default - no logging"
		"\n  -f --logfile <filename>               | default filename - result.log"
#if 0		
		"\n  -off --offset <tid> <value>"
#endif
		"\n  -A --Address                          | Target Training Address"
		"\n  -P --Pattern {<data1> <data2>}        | Program Tx-DQDQS2(FakeWR) data pattern"
	    "\n                                        | <data1> <data2> or HVT_FPGA[1|2]_DATA[1|2]"
		"\n  -B --bcom_sw                          | {re}set BCOM[3:0] MUX to Low"
		"\n  -E --Enable                           | Enable Slave I/O i/f for FakeWR"
		"\n  -R --Reset                            | Reset training mode"
		"\n  -D --debug [0|1|2]                    | TxDQDQS2 Debug(display) flag, default= 0"
		"\n  -M --cmd_dq_mux [0~0xF] <0~7>         | cmd-dq-mux delay setting, - <pattern>, <bl>"
		"\n  -T --test <data1> <data2>             | MMIO Write command test"
		"\n  -5 --fpga1 <addr> <data>"
		"\n  -7 --fpga2 <addr> <data>"
		"\n"
		"\n  export HVT_DEBUG=[0|1|2]              | Tx-DQDQS2(FakeWR) - 0= FPGA, 1/2= DDR4 RD result"
		"\n  export HVT_FPGA[1|2]_DATA[1|2]=<data> | set Tx-DQDQS2(FakeWR) training data"
		"\n  printenv | grep HVT_                  | Check Environment Variable"
		"\n"
		"\n  -v --viewspd                          | view spd data" 
		"\n  -S --save {<filename>}                | [S]ave training data to file"
		"\n  -L --load {<filename>}                | Load training data from file"
		"\n  -ts --time                            | Show test time/loop"
		"\n  -cl --colorlimit [0~255]              | Set PW color limit"
		"\n\n";

//	printf("\n Scan DIMM \n");
	for(D=0;D<24;D++) {
		SetPageAddress(D, 0);	
		usleep(FPGA_SETPAGE_DLY);		// 8-2-2016, Lenovo
  		if(ReadSMBus(SPD,D,0) == 0x23) {
			printf("\n DIMM[%d] N%d.C%d.D%d : ",
							D,D<12?0:1, (int)(D)<12?(int)(D/3):(int)(D/3)-4,(int)(D%3));

			getSN(D);

			printf("%-18s S/N - %s %6.1fC", sPN[D], sSN[D], DimmTemp[D]/16.);

			if(ReadSMBus(FPGA1,D,2) != 0xFF) {
				printf("   FPGA H/W - %02X-%02X.%02X.%02X, %02X-%02X.%02X.%02X",
					ReadSMBus(FPGA1,D,2), ReadSMBus(FPGA1,D,3), ReadSMBus(FPGA1,D,4),ReadSMBus(FPGA1,D,5),
					ReadSMBus(FPGA2,D,2), ReadSMBus(FPGA2,D,3), ReadSMBus(FPGA2,D,4),ReadSMBus(FPGA2,D,5));
			}
		}
	}

	fprintf (stdout, mesg, argv[0], version);

	exit(1);
}

int main( int argc, char **argv ) {

	Word Wvalue;
	int i, j, addr, dti, data, mcmtr, toffadj=0, tdata;
	time_t test_time;
	struct tm *tim;
	unsigned long tohm, tohm0, tohm1;
	int start_lp4=0, end_lp4=256, step_size4=1;
	int start_lp8=0, end_lp8=256, step_size8=1;
	int start_lp16=0, end_lp16=256, step_size16=1;
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
	if(mcmtr&0x4) {					// if MCMTR bit2 == 1, ECC mode is ON
		eccMode = 1;
#ifndef ECC_ON
//		MMioWriteDword(Bus,19,0,0x7c,mcmtr&0xfffffffB);	
//		printf("\n MCMTR : %08X ==> %08X \n\n", mcmtr,mcmtr&0xfffffffB);
#endif	
	}

	tohm0 = MMioReadDword(0,5,0,0xd4);
	tohm1 = MMioReadDword(0,5,0,0xd8);
	tohm0 = GET_BITFIELD(tohm0, 26, 31);

	tohm = ((tohm1 << 6) | tohm0) << 26;
	tohm = (tohm | 0x3FFFFFF) + 1;

	target = tohm - MMIO_WINDOW_SIZE_2;	

	time(&test_time);
	tim = localtime(&test_time);

	for (i = 1; i < argc; i++) {
    	char *arg = argv[i];
		if (strcmp (arg, "-h") == 0 || strcmp (arg, "--help") == 0) help(argc, argv);
		if (strcmp (arg, "-i") == 0 || strcmp (arg, "--i2c") == 0) listing=1;
		if (strcmp (arg, "-u") == 0 || strcmp (arg, "--update") == 0) update_hw=1;
		if (strcmp (arg, "-R") == 0 || strcmp (arg, "--Reset") == 0) ModeReset=1;
		if (strcmp (arg, "-U") == 0 || strcmp (arg, "--Update") == 0) {update_hw=1; showsummary=1;}
		if (strcmp (arg, "-l") == 0 || strcmp (arg, "--log") == 0) logging=1;
		if (strcmp (arg, "-ts") == 0 || strcmp (arg, "--time") == 0) TestTime=1;
		if (strcmp (arg, "-cl") == 0 || strcmp (arg, "--colorlimit") == 0) colorlimit=atoi(argv[i+1]);
		if (strcmp (arg, "-t") == 0 || strcmp (arg, "--tid") == 0) {
			Training |= strtoul(argv[i+1],0,0);
			if((i < (argc-3)) && (isdigit((int)argv[i+2][0]) && isdigit((int)argv[i+3][0]))) {
				if(atoi(argv[i+1]) & 0x04) {
					start_lp4 = atoi(argv[i+2]);
					end_lp4 = atoi(argv[i+3]);
				}
				if(atoi(argv[i+1]) & 0x08) {			
					start_lp8 = atoi(argv[i+2]);
					end_lp8 = atoi(argv[i+3]);
				}	
				if(atoi(argv[i+1]) & 0x10) {
					start_lp16 = atoi(argv[i+2]);
					end_lp16 = atoi(argv[i+3]);
				}	
			}
			if((i < (argc-4)) && (isdigit((int)argv[i+2][0]) && isdigit((int)argv[i+3][0]) && isdigit((int)argv[i+4][0]))) {
				if(atoi(argv[i+1]) & 0x04) {
					start_lp4 = atoi(argv[i+2]);
					end_lp4 = atoi(argv[i+3]);
					step_size4 = atoi(argv[i+4]);
				}
				if(atoi(argv[i+1]) & 0x08) {			
					start_lp8 = atoi(argv[i+2]);
					end_lp8 = atoi(argv[i+3]);
					step_size8 = atoi(argv[i+4]);
				}	
				if(atoi(argv[i+1]) & 0x10) {
					start_lp16 = atoi(argv[i+2]);
					end_lp16 = atoi(argv[i+3]);
					step_size16 = atoi(argv[i+4]);
				}	
			}		
		}
		if (strcmp (arg, "-s") == 0 || strcmp (arg, "--start") == 0) {
			start_lp = start_lp4 = start_lp8 = start_lp16 = atoi(argv[i+1]);
//			start_lp4 = atoi(argv[i+1]);
//			start_lp8 = atoi(argv[i+1]);
//			start_lp16 = atoi(argv[i+1]);
		}
		if (strcmp (arg, "-e") == 0 || strcmp (arg, "--end") == 0) {
			end_lp = end_lp4 = end_lp8 = end_lp16 = atoi(argv[i+1]);
//			end_lp4 = atoi(argv[i+1]);
//			end_lp8 = atoi(argv[i+1]);
//			end_lp16 = atoi(argv[i+1]);
		}
		if (strcmp (arg, "-z") == 0 || strcmp (arg, "--step") == 0) {
			step_size = step_size4 = step_size8 = step_size16 = atoi(argv[i+1]);
//			step_size4 = atoi(argv[i+1]);
//			step_size8 = atoi(argv[i+1]);
//			step_size16 = atoi(argv[i+1]);
		}
		if (strcmp (arg, "-v") == 0 || strcmp (arg, "--viewspd") == 0) viewspd=1;
		if (strcmp (arg, "-L") == 0 || strcmp (arg, "--load") == 0) {
			loadfromfile=1;
			if(i <(argc-1)) {
				sprintf(logfile, argv[i+1]);
				if(strlen(logfile)==0) {printf("\n Error --logfile '%s'\n",argv[i+1]); exit(1);}
			}
		}
		if (strcmp (arg, "-S") == 0 || strcmp (arg, "--save") == 0) {
			savetofile=1;
			if(i <(argc-1)) {
				sprintf(logfile, argv[i+1]);
				if(strlen(logfile)==0) {printf("\n Error --logfile '%s'\n",argv[i+1]); exit(1);}
				logging = 1;
			}
		}
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
				printf("\n DIMM[%d] Training Pattern Data1= 0x%x, Data2= 0x%x", 
							D, data1, data2);
			}				
		}		
		if (strcmp (arg, "-B") == 0 || strcmp (arg, "--bcom_sw") == 0) {
			bcomsw = 1;
			target = tohm - MMIO_WINDOW_SIZE;
		}
		if (strcmp (arg, "-E") == 0 || strcmp (arg, "--Enable") == 0) {
			slaveON = 2;
			target = tohm - MMIO_WINDOW_SIZE;
		}
		if (strcmp (arg, "-D") == 0 || strcmp (arg, "--debug") == 0) {
				debug_flag = atoi(argv[i+1]);
		}
#if 0
		if (strcmp (arg, "-om") == 0 || strcmp (arg, "--offset_minus") == 0) {
			toffadj = 1;
			Training = strtoul(argv[i+1],0,0);
			data = strtoul(argv[i+2],0,0);
		}
#endif
		if (strcmp (arg, "-off") == 0 || strcmp (arg, "--offset") == 0) {
			toffadj = 1;
			Training = strtoul(argv[i+1],0,0);
			data = strtoul(argv[i+2],0,0);
		}		
		if (strcmp (arg, "-f") == 0 || strcmp (arg, "--logfile") == 0) {
			sprintf(logfile, argv[i+1]);
			if(strlen(logfile)==0) {printf("\n Error --logfile '%s'\n",argv[i+1]); exit(1);}
			logging = 1;
		}
		if (strcmp (arg, "-5") == 0 || strcmp (arg, "--fpga1") == 0) {
			if(i < (argc-1) && (isdigit((int)argv[i+1][0])) ) {
				write_i2c = 1;
				dti = 5;	
				addr = strtoul(argv[i+1],0,0);
			}
			if(i < (argc-2) && (isdigit((int)argv[i+1][0]) && isdigit((int)argv[i+2][0]))) {
				write_i2c = 2;
				dti = 5;	
				addr = strtoul(argv[i+1],0,0);
				data = strtoul(argv[i+2],0,0);					
			}
		//	printf("\n DIMM[%d] write_i2c=%d dti= %d Addr= 0x%x, Write Data= 0x%02X\n\n", D, write_i2c, dti, addr, data);
		//	exit(1);
		}		
		if (strcmp (arg, "-7") == 0 || strcmp (arg, "--fpga2") == 0) {
			if(i < (argc-1) && (isdigit((int)argv[i+1][0])) ) {
				write_i2c = 1;
				dti = 7;	
				addr = strtoul(argv[i+1],0,0);
			}
			if(i < (argc-2) && (isdigit((int)argv[i+1][0]) && isdigit((int)argv[i+2][0]))) {
				write_i2c = 2;
				dti = 7;	
				addr = strtoul(argv[i+1],0,0);
				data = strtoul(argv[i+2],0,0);					
			}
		}		
		if (strcmp (arg, "-T") == 0 || strcmp (arg, "--test") == 0) {
			if(i < (argc-1)) {
				wrCmdtest = 1;
				pattern1 = strtoul(argv[i+1],0,0);
				pattern2 = 0;
				target = tohm - MMIO_WINDOW_SIZE;
			} 	
			if(i < (argc-2)) {
				wrCmdtest = 2;
				pattern1 = strtoul(argv[i+1],0,0);
				pattern2 = strtoul(argv[i+2],0,0);
				target = tohm - MMIO_WINDOW_SIZE;
			} else {
				fprintf(stderr,"\n Input error: -T --test  pattern1= 0x%x pattern2= 0x%x\n\n",
								 pattern1, pattern2);
				exit(1);
			}
		}
		if (strcmp (arg, "-A") == 0 || strcmp (arg, "--Address") == 0) {
			target = strtoul(argv[i+1],0,0);					
		}		
		if (strcmp (arg, "-M") == 0 || strcmp (arg, "--cmd_dq_mux") == 0) {
			cmdMuxCyc = 1;					
			if(i < (argc-2)) {
				Data = strtoul(argv[i+1],0,0) & 0x0F;
				expectedOffset = strtoul(argv[i+2],0,0);
			} else {
				printf("\n Input error CMD-Mux-Cycle testing ... Data= 0x%x, expectedOffset= %d\n\n",
							   	Data, expectedOffset);			
				exit(1);
			}
		}
	}

#if (!MMAP_DEBUG)
	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) FATAL;
	map_base = mmap(0, MAP_SIZE2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, target & ~MAP_MASK2);
	if(map_base == (void *) -1) FATAL;
#endif

	if(argc < 4) help(argc, argv); 

	getSN(D);

	if(listing) list( D );	

	if(write_i2c) {
		if(write_i2c == 1 ) printf("\n DIMM[%d] dti= %d Addr= 0x%x, ", D, dti, addr);
		if(write_i2c == 2 ) printf("\n DIMM[%d] dti= %d Addr= 0x%x, Write Data= 0x%02X\n\n", D, dti, addr, data);	
		if(addr>0xff)
			WriteSMBus(dti, D, 0xA0, 1);
		usleep(FPGA_SETPAGE_DLY);

		if(write_i2c == 1 ) printf(" Read Data= 0x%02X\n\n",ReadSMBus(dti, D, addr&0xFF));
		if(write_i2c == 2 ) WriteSMBus(dti, D, addr&0xFF, data);
		usleep(FPGA_SETPAGE_DLY);
		if(addr>0xff)
			WriteSMBus(dti, D, 0xA0, 0);
		usleep(FPGA_SETPAGE_DLY);
		exit(1);
	}
	if(ModeReset) {
		printf("\n DIMM[%d] Training Mode set to Nomal ... ");
		WriteSMBus(FPGA1, D, 0, 0);
		WriteSMBus(FPGA2, D, 0, 0);
		printf("Done\n\n");
		exit(1);
	}		
	if(toffadj) {
		printf("\n DIMM[%d] Training(%d) Data offset adjust %d tick(s) ... ", 
						D, Training, data);	
		if( Training ==4 ) {
			for(i=0;i<64+eccMode*8;i++) {
				tdata = ReadSMBus( (i2cTXDQS1_perBit[i]>>12), D, i2cTXDQS1_perBit[i]&0xFF);
				tdata += data;
				if(tdata<0) { 
					printf(" addr= 0x%x, adj value(%d) is negative ... ERRO !!!\n\n",
								i2cTXDQS1_perBit[i]&0xFF, tdata ); exit(1);
				}
				WriteSMBus( (i2cTXDQS1_perBit[i]>>12), D, i2cTXDQS1_perBit[i]&0xFF, tdata);	
				usleep(10000);
				fpga_i_Dly_load_signal();			// 8-2-2016
			}
		}
		if( Training == 8 ) {
			for(i=0;i<64+eccMode*8;i++) {
				tdata = ReadSMBus( (i2cRXDQS1_perBit[i]>>12), D, i2cRXDQS1_perBit[i]&0xFF);
				tdata += data;
				if(tdata<0) { 
					printf(" addr= 0x%x, adj value(%d) is negative ... ERRO !!!\n\n",
								i2cRXDQS1_perBit[i]&0xFF, tdata ); exit(1);
				}				
				WriteSMBus( (i2cRXDQS1_perBit[i]>>12), D, i2cRXDQS1_perBit[i]&0xFF, tdata);	
				usleep(10000);
				fpga_i_Dly_load_signal();			// 8-2-2016
			}
		}
		if( Training == 0x10 ) {
			WriteSMBus(FPGA1, D, 0xA0, 0x1);	// Set Page '1'
			WriteSMBus(FPGA2, D, 0xA0, 0x1);	// Set Page '1'
			usleep(10000);
			for(i=0;i<16 + 2*eccMode;i++) {
				tdata = ReadSMBus( (i2cTXDQS2[i]>>12), D, i2cTXDQS2[i]&0xFF);
				tdata += data;
				if(tdata<0) { 
					printf(" addr= 0x%x, adj value(%d) is negative ... ERRO !!!\n\n",
								i2cTXDQS2[i]&0xFF, tdata ); exit(1);
				}				
				WriteSMBus( (i2cTXDQS2_perBit[i]>>12), D, i2cTXDQS2_perBit[i]&0xFF, tdata);	
				usleep(10000);
			}
			WriteSMBus(FPGA1, D, 0xA0, 0x0);	// Set Page '0'
			WriteSMBus(FPGA2, D, 0xA0, 0x0);	// Set Page '0'
			usleep(10000);
			fpga_o_Dly_load_signal();			// 8-2-2016
		}		
		printf("Done\n\n");		
		exit(1);
	}
	
	if(viewspd) dumpspd( D );
	if(savetofile) WritetoFile( D );
	if(loadfromfile) LoadfromFile( D );
	if(write_fpga_data) { writeInitialPatternToFPGA(write_fpga_data); exit(1); }
	if(bcomsw) { enable_bcom_switch(bcomsw); exit(1); }
	if(slaveON) { enable_bcom_switch(slaveON); exit(1); }
	if(wrCmdtest) wr_cmd_test( D, wrCmdtest );
	if(cmdMuxCyc) cmd_mux_cycle_test( D , expectedOffset);

	fprintf(stderr,"\n Platform %s, hostname - %s", strstr(system_info,"Product"), hostname);
	fprintf(stderr,"\n Memory Information : TOHM= %#.9lx(%3dGB), Training Addr= %p", 
					tohm, tohm>>30, target);

	if(logging) {
		printf("\n logfile= %s",logfile);	
		fp = fopen(logfile,"a+");
		fprintf( fp,"\n %s - ", hostname);
	}

	printf("\n DIMM[%d] N%d.C%d.D%d : P/N - %-18s S/N - %s  %6.1fC",
					D, D<12?0:1, (int)(D)<12?(int)(D/3):(int)(D/3)-4, (int)(D%3),
				   	sPN[D], sSN[D], DimmTemp[D]/16.);

	printf("  FPGA H/W - %02X-%02X.%02X.%02X, %02X-%02X.%02X.%02X",
					ReadSMBus(FPGA1,D,2), ReadSMBus(FPGA1,D,3), ReadSMBus(FPGA1,D,4),ReadSMBus(FPGA1,D,5),
					ReadSMBus(FPGA2,D,2), ReadSMBus(FPGA2,D,3), ReadSMBus(FPGA2,D,4),ReadSMBus(FPGA2,D,5));
	
	printf("\n Selected options : update_hw= %d, logging= %d,"
		   " eccMode= %d, debug_flag= %d\n", update_hw, logging, eccMode, debug_flag);

	if(ReadSMBus(FPGA1,D,13)) {					// Byte 13 - '1' Training mode is locked
		printf("\n Training Mode on FPGA1 is locked\n");	
		WriteSMBus(FPGA1, D, 13, 0);			// '0' to unlock.
		usleep(FPGA_WRITE_DLY);
	} 
#if BOTH_FPGA
	if(ReadSMBus(FPGA2,D,13)) {
		printf("\n Training Mode on FPGA2 is locked\n");	
		WriteSMBus(FPGA2, D, 13, 0);			// '0' to unlock.
		usleep(FPGA_WRITE_DLY);
	} 
#endif

	if(Training & CMD_CK) CMDCK( WR );
	if(Training & RD_CMD_CK) CMDCK( RD );
	if(Training & TX_DQ_DQS_1) TxDQDQS1(start_lp4, end_lp4, step_size4);
	if(Training & RX_DQ_DQS_1) RxDQDQS1(start_lp8, end_lp8, step_size8);
	if(Training & TX_DQ_DQS_2) TxDQDQS2(start_lp16, end_lp16, step_size16);

	printf("\n\n");
	
	WriteSMBus(FPGA1,D,13,1); // '1' to I2C Bus lock to prevent from accidental accessing
#if BOTH_FPGA	
	WriteSMBus(FPGA2,D,13,1); 
#endif
	usleep(FPGA_WRITE_DLY);

#if (!MMAP_DEBUG)
	if(munmap(map_base, MAP_SIZE2) == -1) FATAL;
	close(fd);
#endif

	if(logging) {
		fprintf(fp,"\t N%d.C%d.D%d : %s",
						D<12?0:1, (int)(D)<12?(int)(D/3):(int)(D/3)-4,(int)(D%3), sSN[D]);
		fprintf(fp," [ %02d:%02d:%02d %02d-%02d-%02d ]\n",
					tim->tm_hour, tim->tm_min, tim->tm_sec,tim->tm_mon+1, tim->tm_mday,tim->tm_year+1900);

		fclose(fp);
	}

	return 0;
}
