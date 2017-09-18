/*
 * File: spd-*.c
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>		// mmap()
#include <inttypes.h>		// define  PRIx32 	lx
							// typedef uint32_t uint_farptr_t

#define Byte				unsigned char
#define Word				unsigned short int

#define MAP_SIZE 4096UL
#define MAP_MASK (MAP_SIZE - 1)

#define  SMB_TIMEOUT    100  	// 100 ms
#define  SMB_SPD		0xA
#define  SMB_NV 		0xB		// 0r 0x8
#define  SMB_TSOD		3
#define  SMB_PAGE		6

		//         0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
		// Node:   0  0  0  0  0  0  0  0  0  0  0  0  1  1  1  1  1  1  1  1  1  1  1  1
		// Channel:0  0  0  1  1  1  2  2  2  3  3  3  0  0  0  1  1  1  2  2  2  3  3  3
		// Dimm:   0  1  2  0  1  2  0  1  2  0  1  2  0  1  2  0  1  2  0  1  2  0  1  2
#if 0
// SuperMicro-Haswell DDR4
int SMBsad[24]=	{ 0, 1, 2, 4, 5, 6, 0, 1, 2, 4, 5, 6, 0, 1, 2, 4, 5, 6, 0, 1, 2, 4, 5, 6};
int SMBctl[24]= { 0, 0, 0, 0, 0, 0,16,16,16,16,16,16, 0, 0, 0, 0, 0, 0,16,16,16,16,16,16};
int SMBdev[24]=	{19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19};
#endif
// Intel SDP S2600WTT
int SMBsad[24]=	{ 0, 1, 2, 4, 5, 6, 0, 1, 2, 4, 5, 6, 0, 1, 2, 4, 5, 6, 0, 1, 2, 4, 5, 6};
int SMBctl[24]= { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int SMBdev[24]=	{19,19,19,19,19,19,22,22,22,22,22,22,19,19,19,19,19,19,22,22,22,22,22,22};

int Bus;
off_t mmBase = 0x80000000, mmioAddr;
uint64_t nominal_frequency;

uint64_t rdmsr(int cpu, uint32_t reg)
{
	uint64_t data;
	int fd;
	char msr_file_name[64];

	sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
	fd = open(msr_file_name, O_RDONLY);
	if (fd < 0) {
		if (errno == ENXIO) {
			fprintf(stderr, "rdmsr: No CPU %d\n", cpu);
			exit(2);
		} else if (errno == EIO) {
			fprintf(stderr, "rdmsr: CPU %d doesn't support MSRs\n",	cpu);
			exit(3);
		} else {
			perror("rdmsr: open"); exit(127);
		}
	}
			
	pread(fd, &data, sizeof data, reg);	

	close(fd);

	return data;	
}

uint64_t getTickCount()
{
	return 1000000ULL * rdmsr(0, 0x10); // uS
}

double elapsedTime(uint64_t start)
{
	uint64_t stop = getTickCount();

	return (((stop - start)/nominal_frequency)/1000);	// return MS
}

int MMioReadDword(int bus, int dev, int Func, int off)
{
	int md;
	int *map_Dword, value;

	md = open("/dev/mem", O_RDWR|O_SYNC);  
	if (md == -1) {  return (-1);  }  	

	mmioAddr=(off_t)(mmBase+(bus<<20)+(dev<<15)+(Func<<12)+(off>>2));
	
	map_Dword = mmap(0, MAP_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, md, mmioAddr&~MAP_MASK);

	if (map_Dword == MAP_FAILED) {  printf("mmap failed !!\n"); return 1; }
	
	value = map_Dword[off>>2];

	munmap(map_Dword, MAP_SIZE); 
	close(md);
	return value;
}

int MMioWriteDword(int bus, int dev, int Func, int off, int Data)
{
	int md;
	int *map_Dword, value;

	md = open("/dev/mem", O_RDWR|O_SYNC);  
	if (md == -1) {  return (-1);  }  	

	mmioAddr=(off_t)(mmBase+(bus<<20)+(dev<<15)+(Func<<12)+(off>>2));
	
	map_Dword = mmap(0, MAP_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, md, mmioAddr&~MAP_MASK);

	if (map_Dword == MAP_FAILED) {  printf("mmap failed !!\n"); return 1; }

	map_Dword[off>>2] = Data;
//	usleep(1*1000);
	value = map_Dword[off>>2];
	munmap(map_Dword, MAP_SIZE); 
	close(md);
	return value;
}

int smbCmd, smbCfg, smbStat;
int smbD, smbC, smbA;

int ReadSMBus(int smb_dti, int d, int off)
{
	int Data, startCount=0;
	int N;

//	usleep(10000);	// 10MS delay
	
	smbA = SMBsad[d];
	smbC = SMBctl[d];
	smbD = SMBdev[d];
		
	smbCfg = (smb_dti<<28) | 0x08000000;
	if (smb_dti==3) smbCmd  = 0xA0000000|(smbA<<24)|(off<<16);
	else	 	 	smbCmd  = 0x80000000|(smbA<<24)|(off<<16);

	if (d<(12)) N=0;
	else		N=1;	

	MMioWriteDword(Bus+N*(Bus+1),smbD,0,0x188+smbC,smbCfg);	
	Data = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x188+smbC);

	if (Data&0x100) {								// 
		usleep(10*1000);
		MMioWriteDword(Bus+N*(Bus+1),smbD,0,0x188+smbC,smbCfg);	
		Data = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x188+smbC);
	}		
	if (Data&0x100) return (-1);

	startCount = getTickCount();
	do {
		usleep(1);
		smbStat = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x180+smbC);	
		if (!(smbStat&0x10000000)) break;			// Busy?
	} while(elapsedTime(startCount) < SMB_TIMEOUT);

	MMioWriteDword(Bus+N*(Bus+1),smbD,0,0x184+smbC,smbCmd);	

	startCount = getTickCount();
	do {
		usleep(1);
		smbStat = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x180+smbC);	
		if (!(smbStat&0x10000000)) break;			// Busy?
	} while(elapsedTime(startCount) < SMB_TIMEOUT);
	
	while(!(smbStat&0xA0000000)) 					// Read Data Valid & No SMBus error
		smbStat = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x180+smbC);

	if (smb_dti==3) return((Word)(smbStat&0xFFFF));
	else		    return((Byte)(smbStat&0xFF));
}

int WriteSMBus(int smb_dti, int d, int off, int Data)
{
	int startCount;
	int N;

//	usleep(10000);	// 10MS delay
	
	smbA = SMBsad[d];
	smbC = SMBctl[d];
	smbD = SMBdev[d];
		
	smbCfg = (smb_dti<<28) | 0x08000000;
	smbCmd = 0x88000000 | (smbA<<24) | (off<<16) | Data;

	if (d<(12)) N=0;
	else		N=1;	

	MMioWriteDword(Bus+N*(Bus+1),smbD,0,0x188+smbC,smbCfg);	
	Data = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x188+smbC);

	if (Data&0x100) {								// 
		usleep(10*1000);
		MMioWriteDword(Bus+N*(Bus+1),smbD,0,0x188+smbC,smbCfg);	
		Data = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x188+smbC);
	}		
	if (Data&0x100) return (-1);

	startCount = getTickCount();
	do {
		usleep(1);
		smbStat = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x180+smbC);	
		if (!(smbStat&0x10000000)) break;			// Busy?
	} while(elapsedTime(startCount) < SMB_TIMEOUT);

	MMioWriteDword(Bus+N*(Bus+1),smbD,0,0x184+smbC,smbCmd);	

	startCount = getTickCount();
	do {
		usleep(1);
		smbStat = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x180+smbC);	
		if (!(smbStat&0x60000000)) break;			// Busy?
	} while(elapsedTime(startCount) < SMB_TIMEOUT);

}

Word SetPageAddress(int d, int page)
{
	int Data,startCount=0;
	int N;

	smbA = page + 6;	// 6: Set Page 0, 7: Set Page 1
	smbC = SMBctl[d];
	smbD = SMBdev[d];
		
	smbCfg = (SMB_PAGE<<28) | 0x08000000;
	smbCmd = 0x80000000|(smbA<<24);

	if (d<(12)) N=0;
	else		N=1;	

	MMioWriteDword(Bus+N*(Bus+1),smbD,0,0x188+smbC,smbCfg);	
	Data = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x188+smbC);

	if (Data&0x100) {								// 
		usleep(10*1000);
		MMioWriteDword(Bus+N*(Bus+1),smbD,0,0x188+smbC,smbCfg);	
		Data = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x188+smbC);
	}		
	if (Data&0x100) return (-1);

	startCount = getTickCount();
	do {
		usleep(1);
		smbStat = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x180+smbC);	
		if (!(smbStat&0x10000000)) break;			// Busy?
	} while(elapsedTime(startCount) < SMB_TIMEOUT);

	MMioWriteDword(Bus+N*(Bus+1),smbD,0,0x184+smbC,smbCmd);	

	startCount = getTickCount();
	do {
		usleep(1000);
		smbStat = MMioReadDword(Bus+N*(Bus+1),smbD,0,0x180+smbC);	
		if (!(smbStat&0x10000000)) break;			// Busy?
	} while(elapsedTime(startCount) < SMB_TIMEOUT);
	
	return 0;

}

#if 0
int main( int argc, char **argv ) {

	Word Wvalue;	
	int i, j, dimm, page;

	// exit with useful diagnostic if iopl() causes a segfault
	if ( iopl( 3 ) ) { perror( "iopl" ); return 1; }
	Wvalue = ioperm( 0xCF8, 8, 1);	// set port input/output permissions
	if(Wvalue) printf("ioperm return value is: 0x%x\n", Wvalue);

	Bus = (MMioReadDword(0,5,0,0x108)>>8) & 0xFF;
	
    if(argc == 2) dimm = strtoul(argv[1],0,0);
    else          dimm = 0;

	printf("\n DIMM[%d]\n\n", dimm);

	page=0;
	SetPageAddress(dimm, page);

//	
//	WriteSMBus(SMB_SPD, dimm, 0xf0, 0x00);
//

	for(i=0;i<16;i++) {
		printf(" %03X :",i*16+256*page);
		for(j=0;j<16;j++) printf(" %02X",ReadSMBus(SMB_SPD, dimm, i*16+j));
		printf("\n");
	}

	printf("\n");

#if 1
	page=1;
	SetPageAddress(dimm, page);

	for(i=0;i<16;i++) {
		printf(" %03X :",i*16+256*page);
		for(j=0;j<16;j++) printf(" %02X",ReadSMBus(SMB_SPD, dimm, i*16+j));
		printf("\n");
	}

	printf("\n\n");
#endif

	return 0;

}
#endif



