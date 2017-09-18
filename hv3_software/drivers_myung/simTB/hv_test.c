/*
 *
 *  HVDIMM Command driver for BSM/MMLS.
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
*/
#include <stdio.h>
#ifdef USER_SPACE_CMD_DRIVER
#include <fcntl.h>
#include <string.h>
#endif

#include "hv_mmio.h"
#include "hv_cmd.h"

#ifdef USER_SPACE_CMD_DRIVER
extern unsigned long bsm_stress_size;
extern unsigned long mmls_stress_size;
extern unsigned long stress_type;
extern unsigned long stress_block_size;
extern unsigned long stress_data_compare;
extern int async;

static int fd;
#endif

#define SIXTEEN_MB	(16*1024*1024)

static char test_buf[SIXTEEN_MB];
int main(int argc, char **argv)
{
#ifdef USER_SPACE_CMD_DRIVER
	int i, val;
	char *valp;
	if (argc == 2 && 
	    (!strcmp(argv[1], "h") || !strcmp(argv[1], "?") || !strcmp(argv[1], "man") || !strcmp(argv[1], "help"))) {
		printf("\n");
		printf("        hv_u_cmd : user space stress test\n");
		printf("                   [h|?|man|help] : display help\n");
		printf("                   [b=block size] : default 4096\n");
		printf("                   [a=async mode] : 0/1, default 0, sync mode\n");
		printf("                   [t=test type]  : 0/1, default 0, seq+random\n");
		printf("                   [i=integrity]  : 0/1, default 0, disabled\n");
		printf("                   [B=BSM sectors]  : default 0x40000\n");
		printf("                   [M=MMLS sectors] : default 0x40000\n\n");
		return 0;
	}

	for (i=1; i<argc; i++) {
		valp = strstr(argv[i], "=");
		if (valp) {
			*valp = 0;
			valp++;
			val = strtoul(valp,0,0);
			if (!strcmp(argv[i], "b"))
				stress_block_size = val;
			else if (!strcmp(argv[i], "a"))
				async = val;
			else if (!strcmp(argv[i], "t"))
				stress_type = val;
			else if (!strcmp(argv[i], "i"))
				stress_data_compare = val;
			else if (!strcmp(argv[i], "B"))
				bsm_stress_size = val;
			else if (!strcmp(argv[i], "M"))
				mmls_stress_size = val;
		}
	}

	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1)
		printf("open /dev/mem failed\n");
	hv_io_init();

	hv_cmd_stress_test(GROUP_BSM);
	hv_cmd_stress_test(GROUP_MMLS);

	hv_io_release();
	close(fd);
#else
	printf("perform bsm_write_command(1, 8, 0, test_buf, 0, NULL)\n");
	bsm_write_command(1, 8, 0, test_buf, 0, NULL);
	printf("perform bsm_read_command(1, 8, 0, test_buf, 0, NULL)\n");
	bsm_read_command(1, 8, 0, test_buf, 0, NULL);
	printf("perform mmls_write_command(1, 8, 0, test_buf, 0, NULL)\n");
	mmls_write_command(1, 8, 0, (long)test_buf, 0, NULL);
	printf("perform mmls_read_command(1, 8, 0, test_buf, 0, NULL)\n");
	mmls_read_command(1, 8, 0, (long)test_buf, 0, NULL);
#endif

	return 0;
}

#ifdef USER_SPACE_CMD_DRIVER
int get_fd()
{
	return fd;
}
#endif

