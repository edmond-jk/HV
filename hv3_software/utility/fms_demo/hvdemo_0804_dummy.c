#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/io.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include "hv_training.h"


int D, demo_flag=0;
unsigned long demo_id=0, demo_action=3, demo_repeat_count=1;
unsigned long demo_qwdata1, demo_qwdata2, target;
char logfile[128];
FILE *fp;


int main( int argc, char **argv ) {
	int i, j;
	int ret_code=0;



//	if ((fp=fopen(logfile,"r"))==NULL) {
//		fprintf(stdout,"\n\n Cannot open file %s !! \n\n",logfile); exit(1);
//	}

//	fprintf(stdout,"\n Load training data from File - %s ...", logfile);

	for (i = 1; i < argc; i++)
	{
    	char *arg = argv[i];

		if (strcmp (arg, "-demo") == 0) {
    		if (argc < 5) {
    			printf("[-demo error]: not enough input argument\n");
    			ret_code = -1;
				goto exit;
    		}
    		else {
        		demo_flag = 1;
        		D = (int) strtoul(argv[i+1], 0, 0);
        		demo_id = strtoul(argv[i+2], 0, 0);
    			demo_action = strtoul(argv[i+3], 0, 0);
    			if (demo_id == 0) {
    				if (demo_action == 1) {
    					if (argc == 7) {
    						demo_qwdata1 = strtoul(argv[i+4], 0, 0);
    						demo_qwdata2 = strtoul(argv[i+5], 0, 0);

    						sprintf(logfile, "pattern.log");
    						(void) remove(logfile);
    						fp = fopen(logfile,"a+");
    						fprintf(fp,"0x%lx 0x%lx\n", demo_qwdata1, demo_qwdata2);
    						fclose(fp);

    					} else {
                			demo_flag = 0;
    						printf("[-demo error]: not enough input argument\n");
    		    			ret_code = -1;
    						goto exit;
    					}
    				} else if (demo_action == 2) {
    					if (argc == 6) {
    						if ((strtoul(argv[i+4], 0, 0) >= 0x380000000) && (strtoul(argv[i+4], 0, 0) <= 0x440000000)) {
    							target = strtoul(argv[i+4], 0, 0);

        						sprintf(logfile, "pattern.log");
        						fp = fopen(logfile,"r");
        						fscanf(fp, "%lx %lx", &demo_qwdata1, &demo_qwdata2);
        						printf("[0x%.16lx] 0x%.16lx\n", target, demo_qwdata2);
        						printf("[0x%.16lx] 0x%.16lx\n", target+8, demo_qwdata1);
        						fclose(fp);

    						} else {
                    			demo_flag = 0;
        						printf("[-demo error]: target physical address (0x%.16lx) should be in 0x3_0000_0000 ~ 0x4_4000_0000\n", strtoul(argv[i+4],0,0));
        		    			ret_code = -1;
        						goto exit;
    						}
    					} else {
                			demo_flag = 0;
    						printf("[-demo error]: -demo dimm# demo_id demo_action physical_addr\n");
    		    			ret_code = -1;
    						goto exit;
    					}
    				} else if (demo_action == 3) {
    					if (argc != 5) {
                			demo_flag = 0;
    						printf("[-demo error]: input argument is not correct\n");
    		    			ret_code = -1;
    						goto exit;
    					}
    					else {
    						sprintf(logfile, "pattern.log");
    						fp = fopen(logfile,"r");
    						fscanf(fp, "%lx %lx", &demo_qwdata1, &demo_qwdata2);
    						printf("0x%.16lx\n0x%.16lx\n", demo_qwdata1, demo_qwdata2);
    						fclose(fp);
    					}
    				} else if (demo_action == 4) {
    					if (argc == 6) {
           					demo_repeat_count = strtoul(argv[i+4], 0, 0);
    					} else {
                			demo_flag = 0;
    						printf("[-demo error]: input argument is not correct\n");
    		    			ret_code = -1;
    		    			goto exit;
    					}
    				} else {
            			demo_flag = 0;
            			printf("[-demo error]: demo_action %d not supported\n", demo_action);
            			ret_code = -1;
            			goto exit;
    				}
    			} else {
        			demo_flag = 0;
        			printf("[-demo error]: demo_id %d not supported\n", demo_id);
        			ret_code = -1;
        			goto exit;
    			}
    		}

    		if (demo_flag) {
    			break;	// let's get out of the loop since we found what we want
    		}
    	}
	}

exit:
	return ret_code;
}
