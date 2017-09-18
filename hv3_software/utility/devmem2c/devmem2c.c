/*
 * devmem2.c: Simple program to read/write from/to any location in memory.
 *
 *  Copyright (C) 2000, Jan-Derk Bakker (J.D.Bakker@its.tudelft.nl)
 *
 *
 * This software has been developed for the LART computing board
 * (http://www.lart.tudelft.nl/). The development has been sponsored by
 * the Mobile MultiMedia Communications (http://www.mmc.tudelft.nl/)
 * and Ubiquitous Communications (http://www.ubicom.tudelft.nl/)
 * projects.
 *
 * The author can be reached at:
 *
 *  Jan-Derk Bakker
 *  Information and Communication Theory Group
 *  Faculty of Information Technology and Systems
 *  Delft University of Technology
 *  P.O. Box 5031
 *  2600 GA Delft
 *  The Netherlands
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/mman.h>
  
#define FATAL do { fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", \
  __LINE__, __FILE__, errno, strerror(errno)); exit(1); } while(0)
 
#define MAP_SIZE 1024*1024*1024*8UL   // 8G size

#define MAP_MASK (MAP_SIZE - 1)

int main(int argc, char **argv) {
    int fd;
    void *map_base, *virt_addr;
    void *tmp;
    unsigned long read_result, writeval;
    off_t target;
    int access_type = 'w';
    unsigned long loop_cnt=1;
    unsigned long loop_cnt_saved;
    unsigned long i;
    unsigned long start_addr;
    unsigned char comp_result;
	
    if(((argc != 4) && (argc != 5) && (argc != 6))) {
	fprintf(stderr, "\nUsage:\t%s {address} {type} [data] {count} [c] \n", argv[0]);
	fprintf(stderr,	"\taddress : memory address to act upon\n");
	fprintf(stderr,	"\ttype    : access operation type: [b]yte, [h]alfword, [w]ord, [l]ongword\n");
	fprintf(stderr,	"\tdata    : data to be written\n");
	fprintf(stderr,	"\tcount   : repeat count\n");
	fprintf(stderr,	"\tc       : comparison of write/Read\n");
	exit(1);
    }
    target = strtoul(argv[1], 0, 0);
    tmp = (void *)target;
    start_addr = (unsigned long)tmp;

    // if(argc > 2)
	access_type = tolower(argv[2][0]);


    if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) FATAL;
    printf("/dev/mem opened.\n"); 
    fflush(stdout);
    
    /* Map one page */
    map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, target & ~MAP_MASK);
    if(map_base == (void *) -1) FATAL;
    printf("Memory mapped at address %p.\n", map_base); 
    fflush(stdout);

    if(argc == 4) {    
	virt_addr = map_base + (target & MAP_MASK);
	loop_cnt = strtoul(argv[3], 0, 0);

	switch(access_type) {
		case 'b':
			while (loop_cnt) {
				read_result = *((unsigned char *) virt_addr);
				printf("Value at address 0x%lX (%p): 0x%lX\n", tmp, virt_addr, read_result);
				fflush(stdout);
				virt_addr = (void *) ((unsigned char *)virt_addr + 1);
				tmp = (void *) ((unsigned char *)tmp +1);
				loop_cnt--;
			}
			break;
		case 'h':
			while (loop_cnt) {
				read_result = *((unsigned short *) virt_addr);
				printf("Value at address 0x%lX (%p): 0x%lX\n", tmp, virt_addr, read_result);
				fflush(stdout);
				virt_addr = (void *) ((unsigned short *)virt_addr + 1);
				tmp = (void *) ((unsigned short *)tmp +1);
				loop_cnt--;
			}
			break;
		case 'w':
			while (loop_cnt) {
				read_result = *((unsigned int *) virt_addr);
				printf("Value at address 0x%lX (%p): 0x%lX\n", tmp, virt_addr, read_result);
				fflush(stdout);
				virt_addr = (void *) ((unsigned int *)virt_addr + 1);
				tmp = (void *) ((unsigned int *)tmp +1);
				loop_cnt--;
			}
			break;
		case 'l':
			while (loop_cnt) {
				read_result = *((unsigned long *) virt_addr);
				printf("Value at address 0x%lX (%p): 0x%16lX\n", tmp, virt_addr, read_result);
				fflush(stdout);
				virt_addr = (void *) ((unsigned long *)virt_addr + 1);
				tmp = (void *) ((unsigned long *)tmp +1);
				loop_cnt--;
			}
			break;
		default:
			fprintf(stderr, "Illegal data type '%c'.\n", access_type);
			exit(2);
	}
    }
    // printf("Value at address 0x%X (%p): 0x%lX\n", target, virt_addr, read_result); 

    //fflush(stdout);

    if((argc == 5)||(argc==6)) {
 		virt_addr = map_base + (target & MAP_MASK);
		writeval = strtoul(argv[3], 0, 0);
		loop_cnt = strtoul(argv[4], 0, 0);

		switch(access_type) {
			case 'b':
				loop_cnt_saved = loop_cnt;
				while (loop_cnt)
				{
					*((unsigned char *) virt_addr) = writeval;
					loop_cnt--;
					read_result = *((unsigned char *) virt_addr);
					virt_addr = (void *) ((unsigned char *)virt_addr + 1);
				}
				break;
			case 'h':
				loop_cnt_saved = loop_cnt;
				while (loop_cnt)
				{
					*((unsigned short *) virt_addr) = writeval;
					loop_cnt--;
					read_result = *((unsigned short *) virt_addr);
					virt_addr = (void *) ((unsigned short *)virt_addr + 1);
				}
				break;
			case 'w':
				loop_cnt_saved = loop_cnt;
				while (loop_cnt)
				{
					*((unsigned int *) virt_addr) = writeval;
					loop_cnt--;
					read_result = *((unsigned int *) virt_addr);
					virt_addr = (void *) ((unsigned int *)virt_addr + 1);
				}
				break;
			case 'l':
				loop_cnt_saved = loop_cnt;
				comp_result = 0;
				printf("loop cnt = 0x%lX\n", loop_cnt);
				for (i=0; i<loop_cnt; i++)
				{
					*((unsigned long *) virt_addr) = writeval;
					read_result = *((unsigned long *) virt_addr);
					virt_addr = (void *) ((unsigned long *)virt_addr + 1);
					if (argc==6)
					{
					   if (read_result != writeval)
					   {
						comp_result = 1;
						printf("Comparison failed at address: 0x%lX!!!\n", start_addr+i*8);
						printf("loop_cnt=%ld;  WriteData=0x%lX, ReadData=0x%lX\n", i, writeval, read_result);
						break;
					   }
					}
				}
				if (argc == 6)
				    if (comp_result ==0)
					printf("PASS!!! *** Read data are the same as Wite data ***\n");


				break;
		}
		printf("Written 0x%lX; readback 0x%lX\n", writeval, read_result); 
		fflush(stdout);
	}
	
	if(munmap(map_base, MAP_SIZE) == -1) FATAL;
    close(fd);
    return 0;
}

