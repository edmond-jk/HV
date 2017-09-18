/*
 *
 *  User-space REGRESSION test program for HVDIMM char driver on MMLS
 *
 *  (C) 2016 Netlist, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Author: S.Gosali <sgosali@netlist.com>
 *  August 2016
 *
 *  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *  NOTE: To build, need to copy hv_cdev_uapi.h into /usr/include/uapi/linux
 *  		It has user-space IOCTL definition.
 *  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define _USE_LARGEFILE64
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <uapi/linux/hv_cdev_uapi.h>

#define VERBOSE_LOG 0	/* enable for more logging info */

#define PATTERN         0x1F
#define PATTERN2        0x9D
#define BUF_SIZE        1024*1024	/* 1MB */
#define MB_SHIFT        20
#define BYTES_PER_MB    256*4096   /* 1MB */

FILE* fd_reportfile;
int failed = 0;
int fd;

static int get_dev_size(uint64_t* size)
{
	int res;

	uint64_t dev_size;

	printf("Issue ioctl HV_MMLS_SIZE\n");
	res = ioctl(fd, HV_MMLS_SIZE, &dev_size);
	if (res < 0) {
		perror("ioctl HV_MMLS_SIZE failed");
		return -1;
	}

	*size = dev_size;

	return 0;
}

/*
* test_wr_rd_verivy()
*
*    Write pattern with lenght of num_bytes, read it back to verify
*
*    starting_offset = byte offset to start writing pattern
**/
static test_wr_rd_verify(char pattern, off_t starting_offset, int num_bytes)
{
	char* ptr_wr_buf;
	char* ptr_rd_buf;
	int i, res;

	ptr_wr_buf = (char*) calloc(num_bytes, sizeof(char));
	ptr_rd_buf = (char*) calloc(num_bytes, sizeof(char));

	fflush(NULL);

	/* Position the write position */
	lseek(fd, starting_offset, SEEK_SET);

#if VERBOSE_LOG
	printf("----Running write-read-verify test------\n");
	printf("Writing pattern=%x into file offset=%x with length=%d bytes\n",
            pattern,
            starting_offset,
            num_bytes);
#endif

	memset(ptr_wr_buf, pattern, num_bytes);

	write(fd, ptr_wr_buf, num_bytes);

#if 0
    /* Dump a few bytes of what has been written to check */
	printf("Dump a few bytes of written data for sanity check:\n");

	if (num_bytes < 8) {
        for (i = 0; i < num_bytes; i++)
            printf("p[%d]=0x%2X\n",i, ptr_wr_buf[i]);
	}
	else {
        for (i = 0; i < 8; i++)
            printf("p[%d]=0x%2X\n",i, ptr_wr_buf[i]);
    }
#endif // 0

	/* Read back and verify */
	lseek(fd, starting_offset, SEEK_SET);

	read(fd, ptr_rd_buf, num_bytes);


#if 0  /* Enable if want to dump buffer to check */
	printf("The data in the device is:\n");

	for(i=0; i < num_bytes; i++) {
		printf("0x%02X", ptr_rd_buf[i]);
	}
#endif

	/* Do memory compare to verify */
	/* Memory test to check if whatever written was good */
	//printf("\nTesting each byte of previously written data: \n\n");

	res = memcmp(ptr_wr_buf, ptr_rd_buf, num_bytes);
	if (res != 0) {
		fprintf(fd_reportfile, "FAILED: block (file_offset)=0x%X\n", starting_offset);
		printf("FAILED: Memory compare failed at block (file_offset)=0x%X\n", starting_offset);
		failed = 1;
		goto error;
	}
	else
        fprintf(fd_reportfile, "PASSED OK: block (file_offset)=0x%X\n", starting_offset);

error:
    free(ptr_rd_buf);
    free(ptr_wr_buf);
}

int main (int argc, char *argv[])
{
    	int i, j, res;
    	char ch;
    	char input[30];
    	int nbytes;
    	uint64_t dev_size;
    	uint64_t mb_size;
    	int count;

#if 0
    	printf ("This program was called with \"%s\".\n",argv[0]);

    	if (argc > 1) {
        	for (count = 1; count < argc; count++) {
            		printf("argv[%d] = %s\n", count, argv[count]);
        	}
    	}
    	else {
        	printf("The command had no other arguments.\n");
    	}
#endif // 0

    	printf("Opening /dev/hv_cdev_mmls0 file\n");
    	fd = open64("/dev/hv_cdev_mmls0", O_RDWR);

    	if (fd == -1) {
        	printf("Error in opening file \n");
        	printf("1. Make sure the file exists (driver was not installed properly)\n");
        	printf("2. User should be in root or use sudo\n");
        	return(-1);
    	}

    	fd_reportfile = fopen("chardrv_test_result.txt", "w");
    	if (fd_reportfile == NULL) {
        	printf("Error in opening chardrv_test_result.txt file\n");
        	close(fd);
        	return(-1);
    	}

    	fprintf(fd_reportfile, "WR/RD/Verify test every 4K block in MMLS: ...\n");
    	fprintf(fd_reportfile, "---------------------------------------------\n\n");

    	get_dev_size(&dev_size);
    	printf("mmls size: %llu\n", dev_size);

    	printf("Running regression test for chardrv on mmls...\n\n");

    	mb_size = dev_size >> MB_SHIFT;
    	printf("# MB segments=%d\n", mb_size);

    	/* Knowing # of MB segments, run full test for each 4K block for entire storage */
    	for(j = 0; j < mb_size; j++) {
        	/* Write 1M block by writing/reading/verifying every 4K continuously */
        	for(i = 0; i < 256; i++){
            		if(i%2) {
                		test_wr_rd_verify(PATTERN, (i * 4096) + (j * BYTES_PER_MB), 4096);
//				printf(".");
			}	
            		else
                		test_wr_rd_verify((char)0x28, (i * 4096) + (j * BYTES_PER_MB), 4096);
        	}
    	}

	printf("\nTest is DONE\n");
	printf("\nTest result is presented in ./chardrv_test_result.txt\n");

	if (failed)
		printf("ERROR: There is one or more failures in the test. Please check chardrv_test_result.txt\n");
    	else
        	printf("\nRegression Test PASSED.  All data is verified OK.\n");

    	close(fd);
    	fclose(fd_reportfile);

    	return 0;
}
