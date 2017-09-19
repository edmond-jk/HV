/* Netlist Copyright 2015, All Rights Reserved */
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h> // mmap
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "../netlist_ev3_ioctl.h"
#include "ev3util.h"
#include "ev3_json.h"
#include "ev3_json_cmd.h"

// #define ALL_PIO_ACCESS_SIZES_SUPPORTED
// #define ALL_PIO_ACCESS_ALIGNMENTS_SUPPORTED
// #define MEMSET_MEMCMP_MEMCPY_ACCESS_SUPPORTED
// #define I2C_SUPPORTED  
// #define FACTORY_PMU_CMD_SUPPORTED

// #define COMPILE_EXTENDED_COMMANDS // Debug only - do not use. 

#define EV_DEFAULT_SIZE ((unsigned long long)8*1024*1024*1024) // 8GB
#define MAX_ERRORS_REPORTED (0x20)

SINT32 s32_evfd;
SINT8 dev_name[32];

// Set the default values for these for now. We will read the driver value and adjust these if needed.
int ev_skip_sectors = EV_DEFAULT_SKIP_SECTORS; // This will be read from the driver
UINT64 start_address_for_testing = (EV_DEFAULT_SKIP_SECTORS * EV_HARDSECT); // Skip the DDR training area if there is one.
UINT64 ev_size_in_bytes = EV_DEFAULT_SIZE; // This will be read from driver

#define MAX_COMMAND_NAME_LENGTH 255
#define MAX_COMMAND_ARGUMENTS 10
#define EV_PROMPT "EV3UTIL>"
#define min(a,b) (a) < (b) ? a : b
#define MAX_STR 256
#define LAST_VALID_REG_ADDR (0xAFF)
#define LAST_VALID_CONFIG_REG_ADDR (0x200) // The extended config area could potentially go to 0x1000
#define LAST_VALID_PIO_ADDR (((uint64_t)16*1024*1024*1024)-1)   // TBD - this needs to be tracked based on device id. 4 or 8 or 16 GB
#define LAST_VALID_I2C_SPD_ADDR (0xff) // Last I2C address

// NVDIMM Control register bit field
#define BACKUP_ENABLE 0x2

// User actions
#define SAVE_ACTION 0
#define RESTORE_ACTION 1
#define DISARM_ACTION 2
#define ARM_ACTION 3


#define DEFAULT_CPU_FREQ 2800.0


// NV States

char *nv_state[17] = 
    {"INIT state",
     "undefined state",
     "undefined state",
     "undefined state",
     "DISARMED state",
     "undefined state",
     "RESTORE state",
     "undefined state",
     "undefined state",
     "undefined state",
     "ARMED state",
     "HALT_DMA state",
     "BACKUP state",
     "FATAL state",
     "undefined state",
     "DONE state",
     "undefined state"
     };


char *current_fpga_image[3] = 
    { "",   // Unknown image - display nothing
      "Factory image",
      "Application image",
    };

static struct CpuInfo cpu_info = { DEFAULT_CPU_FREQ };  // Frequency is defaulted in case value cannot be read

typedef struct EV_Menu_s
{
    SINT8 commandName[MAX_COMMAND_NAME_LENGTH];
    UINT32 (*command)(SINT32 argc, SINT8 *argv[], output_format form);
    SINT8 helpString[255];
    SINT32 hide;
}EV_Menu_t;

/* different Patterns for filling and verifying the memory */
uint64_t patterns[] = 
{
    0,
    0x1111111111111111ULL,
    0x2222222222222222ULL,
    0x3333333333333333ULL,
    0x4444444444444444ULL,
    0x5555555555555555ULL,
    0x6666666666666666ULL,
    0x7777777777777777ULL,
    0x8888888888888888ULL,
    0x9999999999999999ULL,
    0xaaaaaaaaaaaaaaaaULL,
    0xbbbbbbbbbbbbbbbbULL,
    0xccccccccccccccccULL,
    0xddddddddddddddddULL,
    0xeeeeeeeeeeeeeeeeULL,
    0xffffffffffffffffULL,
    0x7a6c7258554e494cULL,
};

void *aligned_malloc(size_t size, size_t align_size);
void aligned_free(void *ptr);

#define ALIGN_VALUE 4096            // Data 
#define VECTOR_ALIGN_VALUE 4096     // Vector


uint64_t counter = 0;

EV_Menu_t TestList[]=
{
    {"fill_pattern", EV_fill_pattern, "\t<pattern number> [<num_sgl> <size_in_kb>]",0},
    {"verify_pattern", EV_verify_pattern, "\t<pattern number> [<num_sgl> <size_in_kb>]",0},
    {"fill_user_pattern", EV_fill_user_pattern, "\t<pattern> <start> <end> <num_sgl> <size_in_kb>",0},
    {"verify_user_pattern", EV_verify_user_pattern, "\t<pattern> <start> <end> <num_sgl> <size_in_kb>",0},
    {"load_evram", EV_load_evram, " <filename> \t--Use full pathname",0},
    {"save_evram", EV_save_evram, " <filename> \t--Use full pathname",0},
    {"fill_inc_pattern", EV_fill_inc_pattern, " [start_address end_address num_vec buffer_size] \t--Fills memory range with incrementing pattern",0},
    {"verify_inc_pattern", EV_verify_inc_pattern, " [start_address end_address num_vec buffer_size] \t--Verifies memory range with incrementing pattern",0},
    {"fill_pattern_async", EV_fill_pattern_async, " <pattern number> [<num_sgl> <size_in_kb>]",0},
    {"program_fpga", EV_program_fpga, " <filename> [force]\t--Use full pathname of properly named RBF bit file, use 'force' to allow any named file",0},
#ifdef COMPILE_EXTENDED_COMMANDS
    {"rb", EV_rb, " <hex offset>\t--read register using byte access",1},
    {"wb", EV_wb, " <hex value> <hex offset>\t--write register using byte access",1},
#endif
    {"rd", EV_rd, " <hex offset>\t--read register using double word access",0},
    {"wd", EV_wd, " <hex value> <hex offset>\t--write register using double word access",0},
#ifdef COMPILE_EXTENDED_COMMANDS
    {"rw", EV_rw, " <hex offset>\t--read register using word access",1},
    {"ww", EV_ww, " <hex value> <hex offset>\t--write register using word access",1},
    {"rq", EV_rq, " <hex offset>\t--read register using quad word access",1},
    {"wq", EV_wq, " <hex value> <hex offset>\t--write register using quad word access",1},
#endif
    {"dr", EV_dr, "\t\t--dump all registers using double word access",0},
    {"crb", EV_crb, " <hex offset>\t--config space read using byte access",0},
    {"crw", EV_crw, " <hex offset>\t--config space read using word access",0},
    {"crd", EV_crd, " <hex offset>\t--config space read using double word access",0},
    {"cwb", EV_cwb, " <hex value> <hex offset>\t--config space write using byte access",0},
    {"cww", EV_cww, " <hex value> <hex offset>\t--config space write using word access",0},
    {"cwd", EV_cwd, " <hex value> <hex offset>\t--config space write using double word access",0},
    {"dc", EV_dc, "\t\t--Dump all PCI Space Configuration registers",0},
    {"prb", EV_prb, " <hex offset>\t--read pio (BAR1) using byte access",1},
    {"pwb", EV_pwb, " <hex value> <hex offset>   --write pio (BAR1) using byte access",1},
    {"prw", EV_prw, " <hex offset>\t--read pio (BAR1) using word access",1},
    {"pww", EV_pww, " <hex value> <hex offset>   --write pio (BAR1) using word access",1},
    {"prd", EV_prd, " <hex offset>\t--read pio (BAR1) using double word access",0},
    {"pwd", EV_pwd, " <hex value> <hex offset>   --write pio (BAR1) using double word access",0},
    {"prq", EV_prq, " <hex offset>   --read pio (BAR1) using quad word access",1},
    {"pwq", EV_pwq, " <hex value> <hex offset>   --write pio (BAR1) using quad word access",1},
    {"dp", EV_dp, "[<start addr>]\t--dump pio (BAR1) using double word access",0},
    {"beacon", EV_beacon, "[<0..1>]\t--Optional parameter sets BEACON value (LEDs blinking), otherwise read it",0},
#ifdef I2C_SUPPORTED
    {"irb", EV_irb, " <hex offset>               --read i2c using byte access",1},
    {"iwb", EV_iwb, " <hex value> <hex offset>   --write i2c using byte access",1},
    {"di", EV_di, " --Dump all I2C SPD registers using byte word access",1},
    {"passcode", EV_passcode, "--Enter passcode",1},
#endif
#ifdef COMPILE_EXTENDED_COMMANDS
    {"chip_reset", EV_chip_reset, "\t--Issue soft reset to device)",0},
#endif
    {"log", EV_dbg_log_state, "\t\t--Capture device internal debug data to log",0},
    {"rst_stats", EV_reset_stats, "\t--Reset the debug statistics and performance counters",0},
    {"perf_stats", EV_get_perf_stats, "\t--Show performance counters and calculations",0},
    {"enable_stats", EV_enable_stats, "[<0..1>] \t--Enable performance stats or read current value",0},
    {"char_driver_test", EV_char_driver_test, "--Execute character driver READ/WRITE (DMA) test",0},
    {"pio_test", EV_pio_test, "[<0..4>] | [5 <start sector> <sector count>] --no parameter or 0=all subtests 1=PIO size/align using IOCTL, 2=MMAP - flow control using pointers 3=MMAP flow control using memcpy 4=Quick test",0},
    {"max_dmas", EV_max_dmas, "[<1..31>]\t--Sets MAX outstanding DMAS to HW, otherwise read it",0},
    {"max_descriptors", EV_max_descriptors, "[<1..7936>]\t--Sets MAX outstanding DESCRIPTORS to HW, otherwise read it",1},
    {"memory_window", EV_memory_window, "[<0..511>] --Optional parameter sets MEMORY WINDOW for config 2 only, otherwise read it",0},
    {"rst_ecc", EV_ecc_reset, " --Clears the ECC error counters",0},
    {"ecc_status", EV_ecc_status, " --Read the ECC counters and the last failed ECC address",0},
    {"sbe", EV_ecc_sbe, "[<0..1>] --Optional parameter sets Single Bit Error injection to on or off, otherwise read it",0},
    {"mbe", EV_ecc_mbe, "[<0..1>] --Optional parameter sets Double Bit Error injection to on or off, otherwise read it",0},
    {"?", DisplayHelp, "\t\t--Show Help",0},
    {"help", DisplayHelp, "\t\t--Show Help",0},
    {"get_version", EV_get_version, "\t--Show hardware and software versions",0},
    {"get_model", EV_get_model, "\t--Show device and vendor id",0},
    {"get_size", EV_get_size, "\t--Show memory size",0},
    {"force_save", EV_force_save, "\t--Start save of DDR memory to flash",0},
    {"force_restore", EV_force_restore, "\t--Start restore of DDR memory from flash",0},
    {"arm_nv", EV_arm_nv, "\t\t--ARM NV for backup",0},
    {"disarm_nv", EV_disarm_nv, "\t--DISARM NV for backup",0},
    {"nv", EV_nv, "\t\t--Get a consolidated status of various system parameters",0},
    {"card_ready", EV_card_ready, "\t--Show if card is ARMed for backup and ready for DMA\n",0},
    {"write_access", EV_write_access, "[<0..1>] --Allow WRITE ACCESS when DISARMED",1},
#ifdef FACTORY_PMU_CMD_SUPPORTED
    {"pmu_read", EV_pmu_read, " <hex offset>   --Read PMU SPD register",0},
    {"pmu_write", EV_pmu_write, " <hex offset> <hex offset>   --Write data to PMU SPD register",0},
    {"pmu_info", EV_pmu_info, "  --Print the PMU information",0},
    {"pmu_update", EV_pmu_update, " <filename>   --Update the PMU SPD register",0},
#endif
    {"cardCtrl", EV_CardCtrl, "Card control fields",0},
    {"cardInfo", EV_CardInfo, "Card information fields",0},
    {"cardStatus", EV_CardStatus, "Card status fields",0},
    {"errorGroup", EV_ErrorGroup, "Card error fields",0},
    {"warningGroup", EV_WarningGroup, "Card warning fields",0},
    {"errorThresholds", EV_ErrorThresholds, "Card error thresholds",0},
    {"warningThresholds", EV_WarningThresholds, "Card warning thresholds",0},
    {"flashStatus", EV_FlashStatus, "Flash status",0},
    {"pmuConfig", EV_PmuConfig, "Pmu Configuration",0},
    {"pmuInfo", EV_PmuInfo, "Pmu Information",0},
    {"pmuStatus", EV_PmuStatus, "Pmu Status",0},
    {"logData", EV_LogData, "Log data",0},
    {"all", EV_All, "Retrieve all JSON objects",0},
};

#define MAX_NUMBER_OF_COMMANDS sizeof(TestList)/sizeof(EV_Menu_t)

void display_version( char * me )
{
    fprintf( stderr, "%s: version %d.%d of %s\n", me, APP_REV, APP_VER, APP_DATE );
}

void display_usage( char * me )
{
    display_version( me );
    fprintf( stderr, "\n"
             "usage: %s device [noprompt <command>]\n"
             "where:\n"
             "        device is /dev/evmap0 or /dev/evmema\n"
             "\n"
             "optional input arguments:\n"
             "       noprompt\n"
             "             for script based use\n"
             "             <command> is required\n"
             "\n"
             "NOTE: order of arguments is pertinent\n"
             "\n"
             "Please send comments and bug reports to ev-support@netlist.com\n"
             ,me );
}

static int get_version_settings(int *is_special_release, int *is_legacy_release)
{
    SINT32 result = TEST_PASS;
    SINT32 rc;
    ioctl_arg_t *u_arg;
    ver_info_t version_info;
    float version_number; // Used to test for greater, equal, or less than a particular version. 

    // Get version information from the card. We need to change register offsets based on version
    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    
    version_info.driver_rev = 0xFF;
    version_info.driver_ver = 0xFF;
    version_info.fpga_rev = 0xFF;
    version_info.fpga_ver = 0xFF;
    version_info.rtl_version = 0xFF;
    version_info.rtl_sub_version = 0xFF;
    version_info.rtl_sub_sub_version = 0xFF;
    version_info.fw_version = 0xFF;
    version_info.fw_sub_version = 0xFF;
    version_info.fw_sub_sub_version = 0xFF;
    version_info.current_fpga_image = IOCTL_FPGA_IMAGE_UNKNOWN;
    sprintf(version_info.extra_info," ");

    u_arg->ioctl_union.ver_info = version_info;
    u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;
    rc = ioctl(s32_evfd, EV_IOC_GET_VERSION, (void *) u_arg);

    if ((rc < 0) || (u_arg->errnum != 0))
    {
        perror("ioctl(EV_IOC_GET_VERSION)");
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        version_info = u_arg->ioctl_union.ver_info;

        if (u_arg->errnum)
        {
            printf("Error getting Data from Driver : Err No : %d\n", u_arg->errnum);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            // Special release
            if ((version_info.rtl_version == 0x00) && (version_info.rtl_sub_version == 0x02) && (version_info.rtl_sub_sub_version == 0x0C) &&
                (version_info.fw_version == 0x00) && (version_info.fw_sub_version == 0x02) && (version_info.fw_sub_sub_version == 0x0E))
            {
                *is_special_release = TRUE;
            }
            else
            {
                version_number = (1.0 * version_info.fw_version * 65536.0) + (1.0 * version_info.fw_sub_version * 256.0) + (1.0 * version_info.fw_sub_sub_version);

                if (version_number < 768.0)
                {
                    // Older than 0.3.0 - legacy release
                    *is_legacy_release = TRUE;
                }
            }
        }
    }    

    free(u_arg);
    return result;
}

/* Main function for the Application */
SINT32 main(SINT32 argc, SINT8 **argv)
{
    SINT32 result = TEST_COULD_NOT_EXECUTE;
    int use_prompt = TRUE;
    int cmd_position;
    int use_json = FALSE;

    if (argc < 2) 
    {
        display_usage( argv[0] );
        exit( result ); // Return an exit status to the calling script
    }

    if (argc >= 3)
    {
        if (strcmp(argv[2], "noprompt") == 0)
        {
            if (argc < 4)
            {
                display_usage( argv[0] );
                exit( result );
            }
            use_prompt = FALSE;
            cmd_position = 3;
        }

        if (strcmp(argv[2], JSON_COMMAND_STRING) == 0)
        {
            if (argc < 4)
            {
                display_usage( argv[0] );
                exit( result );
            }
            use_prompt = FALSE;
            use_json = TRUE;
            cmd_position = 3;
        }
    }

    if (argc >= 4)
    {
        if (strcmp(argv[3], "noprompt") == 0)
        {
            if (argc < 5)
            {
                display_usage( argv[0] );
                exit( result );
            }
            use_prompt = FALSE;
            cmd_position = 4;
        }
    }

    (void) signal(SIGINT, exit_program);

    strcpy(dev_name, argv[1]);

    s32_evfd = open(dev_name, 0);
    if (s32_evfd < 0) 
    {
        printf("\nERROR: Opening device name %s, Error code is %d\n",dev_name, errno);
        perror(dev_name);
        result=TEST_DEVICE_DOES_NOT_EXIST;
        exit(result); // Return an exit status to the calling script
    }

    // We need to set a couple of globals based on driver and FPGA capabilities.

    get_skip_sectors(&ev_skip_sectors, &start_address_for_testing);
    get_size(&ev_size_in_bytes);

    if (!use_prompt)
    {
        if (!use_json)
        {
            result=evscript(argc-cmd_position, &argv[cmd_position]);
        }
        else
        {
            result=evjson(argc-cmd_position, &argv[cmd_position]);
        }
            
        close(s32_evfd);
        exit(result); // Return an exit status to the calling script
    }
    else
    {
        close(s32_evfd);
        return evshell(); // This will only exit
    }
}

// Invoked during a CTRL-C
VOID exit_program()
{
    printf("Exit\n");
    exit(TEST_PASS);
}

// Creates user prompt and wait for the commands
UINT32 evshell()
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    UINT32 strSize;
    UINT32 leadingSpace;
    UINT8 *commandLine;
    UINT8 *input;    
    commandLine = (UINT8*)malloc(MAX_COMMAND_NAME_LENGTH);
    UINT8 s8_chr;
    int status;
    struct timeval start;
    struct timeval end;
    double diff = 0.0; // Time in milliseconds

    // Sign-on
    printf("\nEV3UTIL v%d.%.2d Netlist Inc. Copyright 2011-2016 All Rights Reserved\n",APP_REV,APP_VER);
    status = get_cpuinfo(&cpu_info); // Get the local CPU info.
    if (status == TEST_PASS)
    {
        printf("CPU Frequency: %.2f Mhz  ", cpu_info.freq);
    }
    else
    {
        printf("WARNING: Defaulted CPU Frequency: %.2f Mhz  ", cpu_info.freq);
    }
    printf("Size : %lld GiB (%lld bytes)\n", ev_size_in_bytes / (1024*1024*1024), ev_size_in_bytes);
    printf("\n\n\n");


    while(1)    
    {     
        // loop forever
        printf("\n\r");
        printf(EV_PROMPT); // display the prompt to the user

        strSize = 0;
        leadingSpace = TRUE;
        input = commandLine;
        s8_chr = getc( stdin );
        while( (s8_chr != '\n') && (s8_chr != ';') )
        {
            // ignore leading spaces
            if( s8_chr != ' ' )
            {
                leadingSpace = FALSE;
            }
            if( (s8_chr <= 0x7F) && !leadingSpace )
            {
                strSize++;
                if( strSize < MAX_COMMAND_NAME_LENGTH )
                {
                    *input++ = s8_chr;
                }
                else
                {
                    printf( "Buffer overrun!\n" );
                    free( commandLine );    // free command line memory
                    exit_program();
                }
            }
            s8_chr = getc( stdin );
        }

        *input = '\0';                

        if(!strcmp(commandLine, "exit"))    // break from loop when user types 'exit'
        {
            free( commandLine );    // free command line memory
            exit_program();
        }
        if(!strlen(commandLine))    
        {
            continue;
        }
        s32_evfd = open(dev_name, 0);
        if (s32_evfd < 0) 
        {
            perror(dev_name);
            exit(TEST_DEVICE_DOES_NOT_EXIST);
        }

        gettimeofday(&start, NULL);
        result = invokeCommand(commandLine);
        gettimeofday(&end, NULL);

        diff = 0.0;
        diff += (((end.tv_sec - start.tv_sec) * 1000) + ((end.tv_usec - start.tv_usec)/1000.0));

        switch(result)
        {
            case TEST_PASS:
                printf("PASSED - execution time = %f Seconds\n", diff/1000.0);
                break;
            case TEST_FAIL:
                printf("FAILED - execution time = %f Seconds\n", diff/1000.0);
                break;
            case TEST_COULD_NOT_EXECUTE:
                printf("Unable to execute command due to parameter error or card state\n");
                break;
            case TEST_DEVICE_DOES_NOT_EXIST:
                printf("Device does not exist\n");
                break;
            case TEST_COMMAND_DOES_NOT_EXIST:
                printf("Command does not exist\n");
                break;
            default:
                printf("UNKNOWN ERROR\n");
                break;
        }
        close(s32_evfd);
    }

    free( commandLine );    // free command line memory
    exit_program();
}

// Invoke the commands called using a script
UINT32 evscript(SINT32 argc, SINT8 **argv)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    int i;
    struct timeval start;
    struct timeval end;
    double diff = 0.0; // Time in milliseconds

    printf("\nEV3UTIL v%d.%.2d ",APP_REV,APP_VER);
    for (i=0; i<argc;i++)
    {
        printf(" %s", argv[i]);
    }
    printf(": ");

    get_cpuinfo(&cpu_info); // Get the local CPU info.

    gettimeofday(&start, NULL);
    result = processCommand(argc, argv, FORM_HUMAN_READABLE);
    gettimeofday(&end, NULL);

    diff = 0.0;
    diff += (((end.tv_sec - start.tv_sec) * 1000) + ((end.tv_usec - start.tv_usec)/1000.0));
    printf("Execution time = %f Seconds\n", diff/1000.0);
    return(result);
}

// Invoke the commands called using a script, make the output JSON compatible
UINT32 evjson(SINT32 argc, SINT8 **argv)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    int i;
    struct timeval start;
    struct timeval end;
    double diff = 0.0; // Time in milliseconds

    get_cpuinfo(&cpu_info); // Get the local CPU info.

    result = processCommand(argc, argv, FORM_JSON);
    return(result);
}

/* Get the command and send it to process the command */
UINT32 invokeCommand(SINT8 *str)
{
    UINT32 result = TEST_COMMAND_DOES_NOT_EXIST;
    SINT8 *tok = " ";
    SINT8 *uargv[10];
    SINT32 i;
    SINT32 uargc;

    uargc = 0;
    uargv[0] = strtok(str, tok);

    //printf("%s\n", uargv[0]);
    if (uargv[0] != NULL)
    {
        uargc++;
        for (i = 1; i < 10; i++)
        {
            uargv[i] = strtok(NULL, tok);
            if (uargv[i] != NULL)    
            {
                uargc++;
            }
            else
                break;
        }
    }

    result = processCommand(uargc, uargv, FORM_HUMAN_READABLE);
    return result;
}

/* Process the command */
UINT32 processCommand(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COMMAND_DOES_NOT_EXIST;
    SINT32 commandNumber;

    for(commandNumber = 0; commandNumber < MAX_NUMBER_OF_COMMANDS; commandNumber++)
    {
        if(!strcmp(TestList[commandNumber].commandName, argv[0]))
        {
            if (argc > 1)
            {
                if(!strcmp(argv[1],"?") || !(strlen(argv[1])))
                {
                    printf("\n\r%s\n\r", TestList[commandNumber].helpString);
                    result = TEST_COULD_NOT_EXECUTE; 
                    return result;
                }
            }            

            result = TestList[commandNumber].command(argc, argv, form); /* Invoke Command handler */

            return(result);
        }
    }

    return result;
}

UINT32 DisplayHelp(SINT32 argc, SINT8 *argv[], output_format form)
{
    SINT32 commandNumber;

    printf("\n\rUsage: \n\r");
    for(commandNumber = 0; commandNumber < MAX_NUMBER_OF_COMMANDS; commandNumber++)
    {
        if (!TestList[commandNumber].hide)
        {
            printf("\n\r%s %s",TestList[commandNumber].commandName, TestList[commandNumber].helpString);
        }
    }
    printf("\n\r");

    return TEST_PASS; 
}


void display_version_info(ver_info_t version_info)
{
    printf("\n");
    if (version_info.fpga_build == 0)
    {
        printf("FPGA Version               : %.2X.%.2X\n",version_info.fpga_ver, version_info.fpga_rev);
    }
    else
    {
        printf("FPGA Version               : %.2X.%.2X   Build %.2X\n", version_info.fpga_ver, version_info.fpga_rev, version_info.fpga_build);
    }
    printf("FPGA Image                 : %s\n", current_fpga_image[version_info.current_fpga_image]);
    printf("NV RTL Version             : %.2X.%.2X.%.2X\n", version_info.rtl_version, version_info.rtl_sub_version, version_info.rtl_sub_sub_version);
    printf("NV FW Version              : %.2X.%.2X.%.2X\n", version_info.fw_version, version_info.fw_sub_version, version_info.fw_sub_sub_version);
    printf("FPGA Configuration Number  : %d\n",version_info.fpga_configuration);
    printf("FPGA Board Code            : %d\n",version_info.fpga_board_code);
    printf("Driver Version/Date/Status : %d.%.2d %s\n", version_info.driver_rev, version_info.driver_ver, version_info.extra_info);
    printf("EV3UTIL Version/Date/Status: %d.%.2d %s\n", APP_REV,APP_VER, APP_DATE);
    printf("\n");
}

/* Function to get the EVRAM model */
UINT32 EV_get_version(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    ioctl_arg_t *u_arg;
    SINT32 rc;
    ver_info_t version_info;
    UINT64 reg_offset = 2;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_REG;
    UINT8 access_size = sizeof(int32_t);  // Use DWORD access only.


    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    
    version_info.driver_rev = 0xFF;
    version_info.driver_ver = 0xFF;
    version_info.fpga_rev = 0xFF;
    version_info.fpga_ver = 0xFF;
    version_info.rtl_version = 0xFF;
    version_info.rtl_sub_version = 0xFF;
    version_info.rtl_sub_sub_version = 0xFF;
    version_info.fw_version = 0xFF;
    version_info.fw_sub_version = 0xFF;
    version_info.fw_sub_sub_version = 0xFF;
    version_info.current_fpga_image = IOCTL_FPGA_IMAGE_UNKNOWN;

    sprintf(version_info.extra_info," ");

    u_arg->ioctl_union.ver_info = version_info;
    u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;
    rc = ioctl(s32_evfd, EV_IOC_GET_VERSION, (void *) u_arg);
    if (rc < 0) 
    {
        perror("ioctl(EV_IOC_GET_MODEL)");
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        if (u_arg->errnum)
        {
            printf("Error getting Data from Driver : Err No : %d\n", u_arg->errnum);
        }
        else
        {
            version_info = u_arg->ioctl_union.ver_info;
            display_version_info(version_info);
            result = TEST_PASS;
        }
    }    
    
    free(u_arg);
    return result;    
}

/* Function to get the EVRAM model */
UINT32 EV_get_model(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    ioctl_arg_t *u_arg;
    SINT32 rc;
    dev_info_t device_info;
    int is_special_release = FALSE;
    int is_legacy_release = FALSE;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_REG;
    UINT8 access_size = 4;
    unsigned char output_str[25]; // Largest string plus NULL terminator.
    int str_index = 0;
    int i;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    
    device_info.dev_id = 0x1234;
    device_info.ven_id = 0x5678;
    u_arg->ioctl_union.dev_info = device_info;
    u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    result = get_version_settings(&is_special_release, &is_legacy_release);

    if (result == TEST_PASS)
    {
        rc = ioctl(s32_evfd, EV_IOC_GET_MODEL, (void *) u_arg);
        if (rc < 0) 
        {
            perror("ioctl(EV_IOC_GET_MODEL)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            if (u_arg->errnum)
            {
                printf("Error getting Data from Driver : Err No : %d\n", u_arg->errnum);
                result = TEST_COULD_NOT_EXECUTE;
            }
            else
            {
                device_info = u_arg->ioctl_union.dev_info;

                printf("\nVendor ID      : %.4X\n", device_info.ven_id);
                printf("Device ID      : %.4X\n", device_info.dev_id);
                if (MFG_NETLIST == device_info.mfg_info)
                {
                    printf("Manufacturer   : NETLIST\n");
                }
                else
                {
                    printf("Manufacturer   : Unknown\n");
                }

                printf("Cards detected : %d\n\n", device_info.total_cards_detected);
            }
        }
    }

    if ((result == TEST_PASS) && !is_special_release && !is_legacy_release)
    {
        reg_offset = EV3_PART_NUM0;
        str_index = 0;
        while ((reg_offset <= EV3_PART_NUM5) && (result == TEST_PASS))
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

            if (result == 0)
            {
                // Decode
                output_str[str_index] = (unsigned char)(value & 0xff);
                str_index++;
                output_str[str_index] = (unsigned char)((value >> 8) & 0xff);
                str_index++;
                output_str[str_index] = (unsigned char)((value >> 16) & 0xff);
                str_index++;
                output_str[str_index] = (unsigned char)((value >> 24) & 0xff);
                str_index++;
            }
            else
            {
                printf("ERROR: Unable to read part number\n");
                result = TEST_COULD_NOT_EXECUTE;
            }
            
            reg_offset += access_size;
        }
        // Terminate the string
        output_str[str_index] = 0;

        // Workaround processing. If there are any LF or CF replace them with spaces.
        for (i = 0; i < str_index; i++)
        {
            if ((output_str[i] == 0xd) || (output_str[i] == 0xa))
            {
                output_str[i] = ' ';
            }
        }

        printf("Card P/N       : %s\n", output_str);

        reg_offset = EV3_SERIAL_NUM0;
        str_index = 0;
        while ((reg_offset <= EV3_SERIAL_NUM1) && (result == TEST_PASS))
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

            if (result == 0)
            {
                // High byte of EV3_SERIAL_NUM0 is decoded as a character
                if (reg_offset == EV3_SERIAL_NUM0)
                {
                    output_str[str_index] = (unsigned char)(value & 0xff);
                    str_index++;
                    output_str[str_index] = (unsigned char)((value >> 8) & 0xff);
                    str_index++;
                    output_str[str_index] = (unsigned char)((value >> 20) & 0xf);  // Upper nibble is valid only
                    str_index++;
                    output_str[str_index] = (unsigned char)((value >> 24) & 0xff); 
                    str_index++;
                }
                else
                {
                    // Skip this unused byte (2 nibbles) for the first read
                    if (reg_offset == EV3_SERIAL_NUM1)
                    {
                        output_str[str_index] = (unsigned char)(value & 0xff);
                        str_index++;
                        output_str[str_index] = (unsigned char)((value >> 8) & 0xff);
                        str_index++;
                        output_str[str_index] = (unsigned char)((value >> 16) & 0xff);
                        str_index++;
                        output_str[str_index] = (unsigned char)((value >> 28) & 0xf); // Upper nibble is valid only
                        str_index++;
                    }
                }
            }
            else
            {
                printf("ERROR: Unable to read serial number\n");
                result = TEST_COULD_NOT_EXECUTE;
            }
            
            reg_offset += access_size;
        }

        // This output_str is not yet formatted we need to format each character.
        printf("Card S/N       : ");
        for (i=0;i<str_index;i++)
        {
            if ((i==0) || (i==3) || (i==4))
            {
                // These are encoded differently
                if (output_str[i] != 0)
                {
                    // Some protection - Printing out non-character will cause ev3load script to fail.
                    printf("%c", (unsigned char)output_str[i]);
                }
            }
            else
            {
                if ((i==2) | (i==7))
                {
                    printf("%1X", (unsigned char)output_str[i]);
                }
                else
                {
                    printf("%02X", (unsigned char)output_str[i]);
                }
            }
        }
        printf("\n");
        // PMU info next
        reg_offset = PMU_PART_NUM0;
        str_index = 0;
        while ((reg_offset <= PMU_PART_NUM5) && (result == TEST_PASS))
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

            if (result == 0)
            {
                // Decode
                output_str[str_index] = (unsigned char)(value & 0xff);
                str_index++;
                output_str[str_index] = (unsigned char)((value >> 8) & 0xff);
                str_index++;
                output_str[str_index] = (unsigned char)((value >> 16) & 0xff);
                str_index++;
                output_str[str_index] = (unsigned char)((value >> 24) & 0xff);
                str_index++;
            }
            else
            {
                printf("ERROR: Unable to read part number\n");
                result = TEST_COULD_NOT_EXECUTE;
            }
            
            reg_offset += access_size;
        }
        // Terminate the string
        output_str[str_index] = 0;
        printf("PMU P/N        : %s\n", output_str);

        reg_offset = PMU_SERIAL_NUM0;
        str_index = 0;
        while ((reg_offset <= PMU_SERIAL_NUM1) && (result == TEST_PASS))
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

            if (result == 0)
            {
                output_str[str_index] = (unsigned char)(value & 0xff);
                str_index++;
                output_str[str_index] = (unsigned char)((value >> 8) & 0xff);
                str_index++;
                if (reg_offset == PMU_SERIAL_NUM0)
                {
                    output_str[str_index] = (unsigned char)((value >> 20) & 0xf); // Upper nibble wanted only
                }
                else
                {
                    output_str[str_index] = (unsigned char)((value >> 16) & 0xff); 
                }
                str_index++;
                if (reg_offset == PMU_SERIAL_NUM1)
                {
                    output_str[str_index] = (unsigned char)((value >> 28) & 0xf); // Upper nibble wanted only
                }
                else
                {
                    output_str[str_index] = (unsigned char)((value >> 24) & 0xff); 
                }
                str_index++;
            }
            else
            {
                printf("ERROR: Unable to read serial number\n");
                result = TEST_COULD_NOT_EXECUTE;
            }
            
            reg_offset += access_size;
        }

        // This output_str is not yet formatted we need to format each character.
        printf("PMU S/N        : ");
        for (i=0;i<str_index;i++)
        {
            if ((i==0) || (i==3) || (i==4))
            {
                // These are encoded differently
                if (output_str[i] != 0)
                {
                    // Some protection - Printing out non-character will cause ev3load script to fail.
                    printf("%c", (unsigned char)output_str[i]);
                }
            }
            else
            {
                if ((i==2) || (i==7))
                {
                    printf("%1X", (unsigned char)output_str[i]);
                }
                else
                {
                    printf("%02X", (unsigned char)output_str[i]);
                }
            }
        }

        printf("\n\n\n");
    }

    free(u_arg);
    return result;    
}

UINT32 get_size(UINT64 *card_size_in_bytes)
{
    UINT32 result = TEST_PASS;
    ioctl_arg_t *u_arg;
    SINT32 rc;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    u_arg->ioctl_union.get_set_val.is_read = TRUE;
    u_arg->ioctl_union.get_set_val.value = 0;
    u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = ioctl(s32_evfd, EV_IOC_GET_MSZKB, (void *) u_arg);
    if (rc < 0) 
    {
        perror("ioctl(EV_IOC_GET_MSZKB)");
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        if (u_arg->errnum)
        {
            printf("Error getting Data from Driver : Err No : %d\n", u_arg->errnum);
        }
        else
        {
            *card_size_in_bytes = u_arg->ioctl_union.get_set_val.value*1024;
        }
    }
    free(u_arg);
    return result;
}

/* Function to get the size of the memory used in EVRAM */
UINT32 EV_get_size(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    ioctl_arg_t *u_arg;
    SINT32 rc;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    u_arg->ioctl_union.get_set_val.is_read = TRUE;
    u_arg->ioctl_union.get_set_val.value = 0;
    u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = ioctl(s32_evfd, EV_IOC_GET_MSZKB, (void *) u_arg);
    if (rc < 0) 
    {
        perror("ioctl(EV_IOC_GET_MSZKB)");
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        if (u_arg->errnum)
            printf("Error getting Data from Driver : Err No : %d\n", u_arg->errnum);
        else
            printf("Size : %lld GiB (%lld bytes)\n", u_arg->ioctl_union.get_set_val.value/1024/1024, (unsigned long long)u_arg->ioctl_union.get_set_val.value*1024);
    }
    free(u_arg);
    return result;
}

UINT32 nvdimm_reset()
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = FALSE;
    int space = SPACE_REG;
    UINT8 access_size = 1;

    reg_offset = 1; 
    value = 0;

    result = TEST_COULD_NOT_EXECUTE;

    // TBD - implement this function.

    return result;
}

UINT32 get_nvdimm_state(int *state, int show_result)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_REG;
    UINT8 access_size = 4;

    reg_offset = STATE_REG;

    result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

    if (result == 0)
    {
        if (value > NV_STATE_DONE) // value is unsigned, set index limit
        {
            value = NV_STATE_UNDEFINED; 
        }

        if (show_result)
        {
            printf("Current NVDIMM state is %s (%lld)\n", nv_state[value], value);
        }
    }
    else
    {
        if (show_result)
        {
            printf("ERROR: Unable to read NVDIMM state\n");
        }

        value = NV_STATE_UNDEFINED; 
        result = TEST_COULD_NOT_EXECUTE;
    }

    *state = (int)(value & 0xff);  // value is guaranteed to be in range of the nv_state array.
    return result;
}

// This prints out a recommended action
static void print_recommendation(int state, int action)
{
    switch(state)
    {
        case NV_STATE_INIT:
            switch(action)
            {
                case SAVE_ACTION:
                    printf("ERROR: NVvault is not in ARMED state\n");
                    break;
                case RESTORE_ACTION:
                    // This is permitted.
                    break;
                case DISARM_ACTION:
                    printf("ERROR: NVvault is not in ARMED state\n");
                    break;
                case ARM_ACTION:
                    printf("ERROR: NVvault is not in DISARMED state\n");
                    break;
                default:
                    break;
            }
            break;
        case NV_STATE_ARMED:
            switch(action)
            {
                case SAVE_ACTION:
                    // This is permitted.
                    break;
                case RESTORE_ACTION:
                    printf("ERROR: NVvault is not in INIT state\n");
                    break;
                case DISARM_ACTION:
                    // This is permitted.
                    break;
                case ARM_ACTION:
                    printf("ERROR: NVvault is already in ARMED state\n");
                    break;
                default:
                    break;
            }
        case NV_STATE_DISARMED:
            switch(action)
            {
                case SAVE_ACTION:
                    printf("ERROR: NVvault is not in ARMED state\n");
                    break;
                case RESTORE_ACTION:
                    // This is permitted.
                    break;
                case DISARM_ACTION:
                    printf("ERROR: NVvault is already in DISARMED state\n");
                    break;
                case ARM_ACTION:
                    // This is permitted.
                    break;
                default:
                    break;
            }
            break;
        default:
            printf("Unknown state = %d\n",state);
            break;
    }
}

UINT32 EV_force_save(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    ioctl_arg_t *u_arg;
    SINT32 rc;
    int state = 0xff;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED; 

    rc = get_nvdimm_state(&state, FALSE);

    if ((rc==0) && (state==NV_STATE_ARMED))
    {
        printf("Force Save started\n");
        rc = ioctl(s32_evfd, EV_IOC_FORCE_SAVE, (void *) u_arg); // There is no value sent to the driver. 
        if (rc < 0) 
        {
            perror("ioctl(EV_IOC_FORCE_SAVE)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            if (u_arg->errnum)
            {
                result = get_test_result(u_arg->errnum);
            }
            else
            {
                result = TEST_PASS;
            }
        }
    }
    else
    {
        print_recommendation(state, SAVE_ACTION);
    }

    free(u_arg);
    return result;
}


UINT32 EV_force_restore(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    ioctl_arg_t *u_arg;
    SINT32 rc;
    int state = 0xff;

    // To get autoerase setting
    UINT32 result2 = TEST_PASS;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = get_nvdimm_state(&state, FALSE);

    if ((rc==0) && (state==NV_STATE_INIT))
    {
        printf("Force Restore started\n");
        rc = ioctl(s32_evfd, EV_IOC_FORCE_RESTORE, (void *) u_arg);  // No value is passed to the driver
        if (rc < 0) 
        {
            perror("ioctl(EV_FORCE_RESTORE)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            if (u_arg->errnum)
            {
                result = get_test_result(u_arg->errnum);
            }
            else
            {
                result = TEST_PASS;
            }
        }
    }
    else
    {
        print_recommendation(state, RESTORE_ACTION);
    }

    free(u_arg);
    return result;
}


UINT32 EV_arm_nv(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    ioctl_arg_t *u_arg;
    SINT32 rc;
    int state = 0xff;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = get_nvdimm_state(&state, FALSE);

    if ((rc==0) && (state==NV_STATE_DISARMED))
    {
        printf("Arming NV\n");
        rc = ioctl(s32_evfd, EV_IOC_ARM_NV, (void *) u_arg);
        if (rc < 0) 
        {
            perror("ioctl(EV_ARM_NV)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            if (u_arg->errnum)
            {
                result = get_test_result(u_arg->errnum);
            }
            else
            {
                result = TEST_PASS;
            }
        }
    }
    else
    {
        print_recommendation(state, ARM_ACTION);
    }

    free(u_arg);
    return result;
}

UINT32 EV_disarm_nv(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    ioctl_arg_t *u_arg;
    SINT32 rc;
    int state = 0xff;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = get_nvdimm_state(&state, FALSE);

    if ((rc==0) && (state==NV_STATE_ARMED))
    {
        printf("Disarming NV\n");
        rc = ioctl(s32_evfd, EV_IOC_DISARM_NV, (void *) u_arg);
        if (rc < 0) 
        {
            perror("ioctl(EV_DISARM_NV)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            if (u_arg->errnum)
            {
                result = get_test_result(u_arg->errnum);
            }
            else
            {
                result = TEST_PASS;
            }
        }
    }
    else
    {
        print_recommendation(state, DISARM_ACTION);
    }

    free(u_arg);
    return result;
}

UINT32 EV_fill_pattern(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    SINT32 idx;
    UINT32 num_vec = 32; // Default
    UINT32 size_in_kB = 64; // Default

    if ((argc != 2) && (argc !=4))
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        idx = atoi(argv[1]);
            
        if (argc == 4)
        {
            num_vec = atoi(argv[2]);
            size_in_kB = atoi(argv[3]);
        }

        result = fill_pattern(idx, num_vec, size_in_kB);
        if(result)
        {
            result = TEST_FAIL;
        }
    }
    return result;    
}

UINT32 fill_pattern(UINT32 idx, UINT32 num_vec, UINT32 size_in_kB)
{
    UINT32 result = TEST_PASS;
    struct timeval start;
    struct timeval end;
    double diff = 0.0;
    struct EvIoReq req;
    const int NUM_vec = num_vec;
    const uint64_t BUF_sz = size_in_kB * 1024;
    uint64_t mem_sz = get_mem_sz();
    uint64_t sz;
    uint64_t offset;
    struct SgVec *vec = (struct SgVec *) aligned_malloc(sizeof(struct SgVec) * NUM_vec, VECTOR_ALIGN_VALUE);
    int rc = 0;
    ev_buf_t ev_buf;
    ioctl_arg_t *u_arg;
    ioctl_arg_t *u_arg2;
    int isSynchronous = TRUE;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    u_arg2 = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    // Fill up write buffer with one pattern
    char *buf = (char *)aligned_malloc(BUF_sz, ALIGN_VALUE);

    if (buf == NULL)
    {
        perror("malloc");
        result = TEST_COULD_NOT_EXECUTE;
    }

    for (offset = 0; offset < BUF_sz; offset += sz) 
    {
        sz = min(sizeof(patterns[idx]), BUF_sz - offset);
        memcpy(buf + offset, &patterns[idx], sz);
    }

    printf("Fill memory pattern %lx, started at %llx\n", patterns[idx], start_address_for_testing);

    uint64_t bytes = 0;
    offset = start_address_for_testing;

    gettimeofday(&start, NULL);
    while (offset < mem_sz) 
    {
        int ii;
        uint64_t len;
        if (bytes > mem_sz/50) 
        {
            bytes = 0;
            printf(".");
            fflush(stdout);
        }

        for (ii = 0; ii < NUM_vec; ii++) 
        {
            len = min((unsigned long)BUF_sz, mem_sz - offset);
            if (len == 0)
                break;
            vec[ii].ram_base = buf;
            vec[ii].dev_addr = offset;
            vec[ii].length = len;
            offset += len;
            bytes += len;
        }

        if(ii == 0)
            printf("Number of vec can't be zero");

        req.vec = (struct SgVec *)vec;
        req.nvec = ii;
        req.status = 1;
        req.cookie = &req;

        ev_buf.type = SGIO;
        ev_buf.buf = (void *)&req;
        ev_buf.len = len; 
        ev_buf.offset = 0; 
        ev_buf.dest_addr = 0;
        ev_buf.sync = isSynchronous;

        u_arg->ioctl_union.ev_buf = ev_buf;
        u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;
     
        rc = ioctl(s32_evfd, EV_IOC_WRITE, (void *)u_arg);
        if ((rc < 0) || (u_arg->errnum != 0))
        {
            perror("ioctl(EV_IOC_WRITE)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            while (1) 
            {
                u_arg2->ioctl_union.ev_io_event_ioc.count = 1;
                rc = ioctl(s32_evfd, EV_IOC_GET_IOEVENTS, (void *)u_arg2);

                if ((rc < 0) || (u_arg2->errnum != 0))
                {
                    perror("ioctl(EV_IOC_GET_IOEVENTS)");
                    result = TEST_FAIL;
                    continue;
                }

                if (u_arg2->ioctl_union.ev_io_event_ioc.count == 0) 
                {
                    result = TEST_FAIL;
                } 
                else  
                {
                    if(!(u_arg2->ioctl_union.ev_io_event_ioc.count == 1))
                    {
                        printf("More than one events received");
                        result = TEST_FAIL;
                    }
                    else
                    {
                        if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count -1].status == 0))
                        {
                            printf("Evram IO failed");
                            result = TEST_FAIL;
                        }
                        else
                        {
                            if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie == &req))
                            {
                                printf("\nCookie does not match request cookie = %p  req = %p at index = %d", 
                                    u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie, &req, 
                                    u_arg2->ioctl_union.ev_io_event_ioc.count - 1);
                                result = TEST_FAIL;
                            }
                            else
                            {
                                //printf("\nCookie matches request\n");
                            }
                        }
                    }
                    break;
                }
            }
        }
    }

    gettimeofday(&end, NULL);
    diff += (((end.tv_sec - start.tv_sec) * 1000) + ((end.tv_usec - start.tv_usec)/1000.0));

    aligned_free(buf);
    aligned_free(vec);
    free(u_arg);
    free(u_arg2);

    printf("Done\n");
    printf("%lu bytes filled\n", mem_sz);
    printf("Time taken to Write: %10.5f milli sec\n", diff);
    printf("Throughput: %10.5f MB/S\n", (mem_sz/1000)/(diff));

    return result;
}

#define IOCTL_RETRY_COUNT 20

UINT32 EV_fill_pattern_async(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    SINT32 idx;
    UINT32 num_vec = 32; // Default
    UINT32 size_in_kB = 64; // Default

    if ((argc != 2) && (argc !=4))
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        idx = atoi(argv[1]);
            
        if (argc == 4)
        {
            num_vec = atoi(argv[2]);
            size_in_kB = atoi(argv[3]);
        }

        result = fill_pattern_async(idx, num_vec, size_in_kB);
        if(result)
        {
            result = TEST_FAIL;
        }
    }
    return result;    
}

UINT32 fill_pattern_async(UINT32 idx, UINT32 num_vec, UINT32 size_in_kB)
{
    UINT32 result = TEST_PASS;
    struct timeval start;
    struct timeval end;
    double diff = 0.0;
    struct EvIoReq req;
    const int NUM_vec = num_vec;
    const uint64_t BUF_sz = size_in_kB * 1024;
    uint64_t mem_sz = get_mem_sz();
    uint64_t sz, offset;
    struct SgVec *vec = (struct SgVec *) aligned_malloc(sizeof(struct SgVec) * NUM_vec, VECTOR_ALIGN_VALUE);
    int rc = 0;
    ev_buf_t ev_buf;
    ioctl_arg_t *u_arg;
    ioctl_arg_t *u_arg2;
    int isSynchronous = TRUE;
    int retryCount = IOCTL_RETRY_COUNT;
    int error_code = 0;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    u_arg2 = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    // Fill up write buffer with one pattern
    char *buf = (char *)aligned_malloc(BUF_sz, ALIGN_VALUE);

    if (buf == NULL)
        perror("malloc");

    for (offset = 0; offset < BUF_sz; offset += sz) 
    {
        sz = min(sizeof(patterns[idx]), BUF_sz - offset);
        memcpy(buf + offset, &patterns[idx], sz);
    }

    printf("Fill memory pattern %lx, started at %llx\n", patterns[idx], start_address_for_testing);

    uint64_t bytes = 0;
    offset = start_address_for_testing;

    gettimeofday(&start, NULL);
    while (offset < mem_sz) 
    {
        int ii;
        uint64_t len;
        if (bytes > mem_sz/50) 
        {
            bytes = 0;
            printf(".");
            fflush(stdout);
        }

        for (ii = 0; ii < NUM_vec; ii++) 
        {
            len = min((unsigned long)BUF_sz, mem_sz - offset);
            if (len == 0)
                break;
            vec[ii].ram_base = buf;
            vec[ii].dev_addr = offset;
            vec[ii].length = len;
            offset += len;
            bytes += len;
        }

        if(ii == 0)
            printf("Number of vec can't be zero");

        ev_buf.sync = TRUE;  // SYNC - last DMA

        retryCount = IOCTL_RETRY_COUNT;
        do 
        {
            req.vec = (struct SgVec *)vec;
            req.nvec = ii;
            req.status = 1;
            req.cookie = &req;

            ev_buf.type = SGIO;
            ev_buf.buf = (void *)&req;
            ev_buf.len = len; 
            ev_buf.offset = 0; 
            ev_buf.dest_addr = 0;

            // Check to see if last DMA            
            if (offset < mem_sz)
            {
                ev_buf.sync = FALSE; // ASYNC
            }
            else
            {
                ev_buf.sync = TRUE;  // SYNC - last DMA
            }

            u_arg->ioctl_union.ev_buf = ev_buf;
            u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

            error_code = 0;

            rc = ioctl(s32_evfd, EV_IOC_WRITE, (void *)u_arg);
            if ((rc < 0) || (u_arg->errnum != 0))
            {
                error_code = errno;

                if (error_code == EAGAIN)
                {
                    usleep (500);
                    //printf ("retry retryCount=%d\n", retryCount);
                }
                else
                {
                    // Some non-resource related issue, mark as not executed
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }
            retryCount--;
        } while ((error_code == EAGAIN) && (retryCount>0));


        if ((rc < 0) || (u_arg->errnum != 0))
        {
            perror("ioctl(EV_IOC_WRITE)");
            result = TEST_COULD_NOT_EXECUTE;
        }
    }

    // Done with while loop last DMA is SYNCHRONOUS, use IOC_GET_EVENTS to wait on that completion
    {
        u_arg2->ioctl_union.ev_io_event_ioc.count = 1;
        rc = ioctl(s32_evfd, EV_IOC_GET_IOEVENTS,  (void *)u_arg2);

        if (rc < 0) 
        {
            perror("ioctl(EV_IOC_GET_IOEVENTS)");
            result = TEST_FAIL;
        }
        else
        {
            if (u_arg2->ioctl_union.ev_io_event_ioc.count == 0) 
            {
                result = TEST_FAIL;
            } 
            else 
            {
                if(!(u_arg2->ioctl_union.ev_io_event_ioc.count == 1))
                {
                    printf("More than one events received");
                    result = TEST_FAIL;
                }
                else
                {
                    if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count -1].status == 0))
                    {
                        printf("Evram IO failed");
                        result = TEST_FAIL;
                    }
                    else
                    {
                        if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count -1].cookie == &req))
                        {
                            printf("Cookie does not match request");
                            result = TEST_FAIL;
                        }
                        else
                        {
                            //printf("\nCookie matches request\n");
                        }
                    }
                }
            }
        }
    }

    gettimeofday(&end, NULL);
    diff += (((end.tv_sec - start.tv_sec) * 1000) + ((end.tv_usec - start.tv_usec)/1000.0));

    aligned_free(buf);
    aligned_free(vec);
    free(u_arg);
    free(u_arg2);

    printf("Done\n");
    printf("%lu bytes filled\n", mem_sz);
    printf("Time taken to Write: %10.5f milli sec\n", diff);
    printf("Throughput: %10.5f MB/S\n", (mem_sz/1000)/(diff));

    return result;
}

UINT32 EV_card_ready(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    ioctl_arg_t *u_arg;
    SINT32 rc;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    u_arg->ioctl_union.get_set_val.is_read = TRUE;
    u_arg->ioctl_union.get_set_val.value = 0;
    u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = ioctl(s32_evfd, EV_IOC_CARD_READY, (void *) u_arg);
    if (rc < 0) 
    {
        perror("ioctl(EV_IOC_CARD)");
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        if (u_arg->errnum)
            printf("Error getting Data from Driver : Err No : %d\n", u_arg->errnum);
        else
        {
            if (u_arg->ioctl_union.get_set_val.value)
            {
                printf("Card is ARMED for backup and ready, data is protected\n");
            }
            else
            {
                printf("Card is not ready, data is not protected\n");
            }
        }
    }
    
    free(u_arg);
    return result;
}


// Get evram memory size in bytes
// What this routine really does is to get the last memory address plus one regardless of starting offset.
// We need a get "raw memory" size interfacefrom the driver. For now we round off to the nearest 1G.
UINT64 get_mem_sz(VOID)
{
    SINT32 result=TEST_COULD_NOT_EXECUTE;
    ioctl_arg_t *u_arg;
    SINT32 rc;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    u_arg->ioctl_union.get_set_val.is_read = TRUE;
    u_arg->ioctl_union.get_set_val.value = 0;
    u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = ioctl(s32_evfd, EV_IOC_GET_MSZKB, (void *) u_arg);
    if (rc < 0) 
    {
        perror("ioctl(EV_IOC_GET_MSZKB)");
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        if (u_arg->errnum)
        {
            printf("Error getting Data from Driver : Err No : %d\n", u_arg->errnum);
            result = TEST_FAIL;
        }
        else
        {
            printf("Size : %llu KB\n", u_arg->ioctl_union.get_set_val.value);
            free(u_arg);
            return (UINT64)(u_arg->ioctl_union.get_set_val.value*1024);
        }
    }

    free(u_arg);
    return result;
}

// This sets the globals related to the skip area that was needed on the older FPGA versions
UINT32 get_skip_sectors(int *skip_sectors, UINT64 *start_address)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    SINT32 rc;
    ioctl_arg_t u_arg;
    get_set_val_t get_set_val;

    memset(&get_set_val,0x00,sizeof(get_set_val_t));

    // READ
    get_set_val.is_read = TRUE;
    result = 0; // Okay to proceed

    if (result == 0)
    {
        u_arg.ioctl_union.get_set_val = get_set_val;
        u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

        rc = ioctl(s32_evfd, EV_IOC_GET_SKIP_SECTORS, (void *)&u_arg);
        if ((rc < 0) || (u_arg.errnum != 0))
        {
            perror("ioctl(EV_IOC_GET_SKIP_SECTORS)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            get_set_val = u_arg.ioctl_union.get_set_val;

            if (get_set_val.is_read)
            {
                *skip_sectors = (int)(get_set_val.value & 0xffffffff);
                *start_address = (ev_skip_sectors * EV_HARDSECT);
            }
        }
    }

    return result;
}

/* Register access routines */
UINT32 EV_rb(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_REG;
    UINT8 access_size = 1;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 2)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        reg_offset = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }            
    }

    return result;
}

UINT32 EV_rw(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_REG;
    UINT8 access_size = 2;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 2)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        reg_offset = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }
    }            

    return result;
}

UINT32 EV_rd(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_REG;
    UINT8 access_size = 4;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 2)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        reg_offset = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }            
    }
    return result;
}

UINT32 EV_rq(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_REG;
    UINT8 access_size = 8;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 2)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        reg_offset = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }            
    }
    return result;
}


UINT32 EV_wb(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = FALSE;
    int space = SPACE_REG;
    UINT8 access_size = 1;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 3)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        value = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }

        reg_offset = strtoul(argv[2], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[2]) != (endptr-argv[2]))
        {
            printf("Input error: argument not valid <%s>\n", argv[2]);
            result = TEST_COULD_NOT_EXECUTE;
        }

        if (result == TEST_PASS)
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }            
    }
    return result;
}

UINT32 EV_ww(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = FALSE;
    int space = SPACE_REG;
    UINT8 access_size = 2;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 3)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        errno=0;
        endptr=str;
        value = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }

        errno=0;
        endptr=str;
        reg_offset = strtoul(argv[2], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[2]) != (endptr-argv[2]))
        {
            printf("Input error: argument not valid <%s>\n", argv[2]);
            result = TEST_COULD_NOT_EXECUTE;
        }

        if (result == TEST_PASS)
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }            
    }

    return result;
}

UINT32 EV_wd(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = FALSE;
    int space = SPACE_REG;
    UINT8 access_size = 4;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 3)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {    
        /* Always hex */
        value = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }

        reg_offset = strtoul(argv[2], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[2]) != (endptr-argv[2]))
        {
            printf("Input error: argument not valid <%s>\n", argv[2]);
            result = TEST_COULD_NOT_EXECUTE;
        }

        if (result == TEST_PASS)
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }            
    }
    return result;
}

UINT32 EV_wq(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = FALSE;
    int space = SPACE_REG;
    UINT8 access_size = 8;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 3)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        value = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }

        reg_offset = strtoul(argv[2], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[2]) != (endptr-argv[2]))
        {
            printf("Input error: argument not valid <%s>\n", argv[2]);
            result = TEST_COULD_NOT_EXECUTE;
        }

        if (result == TEST_PASS)
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }            
    }

    return result;
}

UINT32 EV_dr(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    UINT64 reg_offset = 0;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_REG;
    UINT8 access_size = 4;

    if (argc != 1)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        while((reg_offset<LAST_VALID_REG_ADDR) && (result==TEST_PASS))
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
            reg_offset+=access_size;
        }            
    }
    return result;
}

/* Configuration space register access routines */
UINT32 EV_crb(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_PCI;
    UINT8 access_size = 1;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 2)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        reg_offset = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }            
    }
    return result;
}

UINT32 EV_crw(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_PCI;
    UINT8 access_size = 2;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 2)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {    
        /* Always hex */
        reg_offset = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }            
    }

    return result;
}

UINT32 EV_crd(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_PCI;
    UINT8 access_size = 4;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 2)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        reg_offset = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }
    }            

    return result;
}

UINT32 EV_cwb(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = FALSE;
    int space = SPACE_PCI;
    UINT8 access_size = 1;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 3)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        value = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            reg_offset = strtoul(argv[2], &endptr, 16);
            /* Check to make sure the entire string was parsed */
            if (strlen(argv[2]) != (endptr-argv[2]))
            {
                printf("Input error: argument not valid <%s>\n", argv[2]);
                result = TEST_COULD_NOT_EXECUTE;
            }
            else
            {
                result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
                if (result == 0)
                {
                    result = TEST_PASS;
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }
        }            
    }
    return result;
}

UINT32 EV_cww(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = FALSE;
    int space = SPACE_PCI;
    UINT8 access_size = 2;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 3)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {    
        /* Always hex */
        errno=0;
        endptr=str;
        value = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            errno=0;
            endptr=str;
            reg_offset = strtoul(argv[2], &endptr, 16);
            /* Check to make sure the entire string was parsed */
            if (strlen(argv[2]) != (endptr-argv[2]))
            {
                printf("Input error: argument not valid <%s>\n", argv[2]);
                result = TEST_COULD_NOT_EXECUTE;
            }
            else
            {
                result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
                if (result == 0)
                {
                    result = TEST_PASS;
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }
        }            
    }
    return result;
}

UINT32 EV_cwd(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = FALSE;
    int space = SPACE_PCI;
    UINT8 access_size = 4;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 3)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        value = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            reg_offset = strtoul(argv[2], &endptr, 16);
            /* Check to make sure the entire string was parsed */
            if (strlen(argv[2]) != (endptr-argv[2]))
            {
                printf("Input error: argument not valid <%s>\n", argv[2]);
                result = TEST_COULD_NOT_EXECUTE;
            }

            if (result == 0)
            {
                result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
                if (result == 0)
                {
                    result = TEST_PASS;
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }
        }
    }            

    return result;
}

UINT32 EV_dc(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset = 0;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_PCI;
    UINT8 access_size = 4;

    if (argc != 1)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        while((reg_offset<LAST_VALID_CONFIG_REG_ADDR) && (result==0))
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
            reg_offset+=access_size;
        }
    }            

    return result;
}

/* PIO Access routines next */
/* Register access routines */
UINT32 EV_prb(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_PIO;
    UINT8 access_size = 1;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 2)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        reg_offset = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }            
    }
    return result;
}

UINT32 EV_prw(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_PIO;
    UINT8 access_size = 2;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 2)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        reg_offset = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }

        if (result == 0)
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }
    }            

    return result;
}

UINT32 EV_prd(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_PIO;
    UINT8 access_size = 4;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 2)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        reg_offset = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }            
    }

    return result;
}

UINT32 EV_prq(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_PIO;
    UINT8 access_size = 8;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 2)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        reg_offset = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }            
    }

    return result;
}

UINT32 EV_pwb(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = FALSE;
    int space = SPACE_PIO;
    UINT8 access_size = 1;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 3)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        value = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            reg_offset = strtoul(argv[2], &endptr, 16);
            /* Check to make sure the entire string was parsed */
            if (strlen(argv[2]) != (endptr-argv[2]))
            {
                printf("Input error: argument not valid <%s>\n", argv[2]);
                result = TEST_COULD_NOT_EXECUTE;
            }
            else
            {
                result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
                if (result == 0)
                {
                    result = TEST_PASS;
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }
        }            
    }

    return result;
}

UINT32 EV_pww(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = FALSE;
    int space = SPACE_PIO;
    UINT8 access_size = 2;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 3)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        errno=0;
        endptr=str;
        value = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            errno=0;
            endptr=str;
            reg_offset = strtoul(argv[2], &endptr, 16);
            /* Check to make sure the entire string was parsed */
            if (strlen(argv[2]) != (endptr-argv[2]))
            {
                printf("Input error: argument not valid <%s>\n", argv[2]);
                result = TEST_COULD_NOT_EXECUTE;
            }
            else
            {
                result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
                if (result == 0)
                {
                    result = TEST_PASS;
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }
        }            
    }

    return result;
}

UINT32 EV_pwd(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = FALSE;
    int space = SPACE_PIO;
    UINT8 access_size = 4;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 3)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        value = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            reg_offset = strtoul(argv[2], &endptr, 16);
            /* Check to make sure the entire string was parsed */
            if (strlen(argv[2]) != (endptr-argv[2]))
            {
                printf("Input error: argument not valid <%s>\n", argv[2]);
                result = TEST_COULD_NOT_EXECUTE;
            }
            else
            {
                result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
                if (result == 0)
                {
                    result = TEST_PASS;
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }
        }
    }            

    return result;
}

UINT32 EV_pwq(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = FALSE;
    int space = SPACE_PIO;
    UINT8 access_size = 8;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 3)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        value = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            reg_offset = strtoul(argv[2], &endptr, 16);
            /* Check to make sure the entire string was parsed */
            if (strlen(argv[2]) != (endptr-argv[2]))
            {
                printf("Input error: argument not valid <%s>\n", argv[2]);
                result = TEST_COULD_NOT_EXECUTE;
            }
            else
            {
                result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
                if (result == 0)
                {
                    result = TEST_PASS;
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }
        }
    }            

    return result;
}

UINT32 EV_dp(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset = 0;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_PIO;
    UINT8 access_size = 4;
    char str[MAX_STR];
    char *endptr=str;
    static unsigned long current_start = 0;  // track last value for the next dp call
    int max_dump = 64;
    int count = 0;

    if (argc > 2)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }    
    else
    {    
        if (argc == 2)
        {
            /* Always hex */
            current_start = strtoul(argv[1], &endptr, 16);
            /* Check to make sure the entire string was parsed */
            if (strlen(argv[1]) != (endptr-argv[1]))
            {
                printf("Input error: argument not valid <%s>\n", argv[1]);
                result = TEST_COULD_NOT_EXECUTE;
            }
            else
            {
                reg_offset = current_start;
                while((reg_offset<LAST_VALID_PIO_ADDR) && (result==TEST_PASS) && (count<max_dump))
                {
                    result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
                    if (result == 0)
                    {
                        result = TEST_PASS;
                    }
                    else
                    {
                        result = TEST_COULD_NOT_EXECUTE;
                    }
                    reg_offset+=access_size;
                    count++;
                }            
                current_start = reg_offset;
            }
        }
    }

    return result;
}

// I2C access next

UINT32 EV_irb(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_I2C;
    UINT8 access_size = 1;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 2)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        reg_offset = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
             result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
        }
    }            

    return result;
}


UINT32 EV_iwb(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = 0;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = FALSE;
    int space = SPACE_I2C;
    UINT8 access_size = 1;
    char str[MAX_STR];
    char *endptr=str;

    if (argc != 3)
    {
         result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        /* Always hex */
        value = strtoul(argv[1], &endptr, 16);
        /* Check to make sure the entire string was parsed */
        if (strlen(argv[1]) != (endptr-argv[1]))
        {
            printf("Input error: argument not valid <%s>\n", argv[1]);
             result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            reg_offset = strtoul(argv[2], &endptr, 16);
            /* Check to make sure the entire string was parsed */
            if (strlen(argv[2]) != (endptr-argv[2]))
            {
                printf("Input error: argument not valid <%s>\n", argv[2]);
                 result = TEST_COULD_NOT_EXECUTE;
            }
            else
            {
                result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
                if (result == 0)
                {
                    result = TEST_PASS;
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }
        }
    }            

    return result;
}


UINT32 EV_di(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    UINT64 reg_offset = 0;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_I2C;
    UINT8 access_size = 1; // Byte access only is allowed for I2C
    int max_dump = 2048; // A random large number.
    int count = 0;

    if (argc > 1)
    {
         result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        count = 0;
        reg_offset = I2C_SPD_ADDR;
        while((reg_offset<=LAST_VALID_I2C_SPD_ADDR) && (result==0) && (count<max_dump))
        {
            result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
            if (result == 0)
            {
                result = TEST_PASS;
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }
            reg_offset+=access_size;
            count++;
        }            

    }
    return result;
}

UINT32 aux_hw_access(UINT64 reg_offset, UINT64 *value, UINT64 length, int is_read, UINT8 access_size, int space, int silent)
{
    UINT32 ret_val = 0;
    ioctl_arg_t *u_arg;
    SINT32 rc;
    mem_addr_size_t access_request;
    UINT64 max_value=0;
    int i;
    access_request.addr = reg_offset;
    access_request.val = *value; // For reads this is a value that means "uninitialized".
    access_request.size = length;
    access_request.access_size = access_size;
    access_request.is_read = is_read;
    access_request.space = space;

    /* Do some value checks for size, alignment, etc and return if invalid */
    /* Make sure only one of the expected access sizes are used */
    switch(access_request.access_size)
    {
        case 1:
        case 2:
        case 4:
            break;
        case 8: // For registers and PIO only
            if (space == SPACE_PCI)
            {
                printf("Input error: Config Space access size can be 1,2,4, but is 0x%X\n", access_request.access_size);
                ret_val+=1;
            }
            break;
        default:
            printf("Input error: Access size can be 1,2,4,8 but is 0x%X\n", access_request.access_size);
            ret_val+=1;
    }

    /* Calculate max value for writes */
    max_value = 0x100;
    for (i=1;i<access_request.access_size;i++)
    {
        max_value *= 0x100;
    }
    max_value -= 1;

    if (access_request.val > max_value)
    {
        printf("Input error: value cannot exceed 0x%llX, but is 0x%llX\n",max_value, access_request.val);
        ret_val+=1;
    }
        
    /* Check alignment for the requested access size. We do not want to break a larger access into smaller ones,  */
    /* except for PIO mode access. */
    if (((access_request.addr % access_request.access_size) !=0) && (space != SPACE_PIO))
    {
        printf("Input error: Incorrect address alignment for the current access size of 0x%X\n", 
                access_request.access_size);
        ret_val+=1;
    }

    // Check the offset range
    switch(space)
    {
        case SPACE_PCI:
            if ((access_request.addr + ((access_request.size*access_request.access_size)-1)) > LAST_VALID_CONFIG_REG_ADDR)
            {
                printf("Input error: Access is out of range, last valid byte address=0x%X\n", LAST_VALID_CONFIG_REG_ADDR); 
                ret_val++;
            }
            break;
        case SPACE_PIO:
            if ((access_request.addr + ((access_request.size*access_request.access_size)-1)) > LAST_VALID_PIO_ADDR)
            {
                printf("Input error: Access is out of range, last valid byte address=0x%lX\n", LAST_VALID_PIO_ADDR); 
                ret_val++;
            }
            break;
        case SPACE_REG:
            if ((access_request.addr + ((access_request.size*access_request.access_size)-1)) > LAST_VALID_REG_ADDR)
            {
                printf("Input error: Access is out of range, last valid byte address=0x%X\n", LAST_VALID_REG_ADDR); 
                ret_val++;
            }
            break;
        case SPACE_I2C:
            // TBD - figure out the range for this register set.
            break;
        default:
            printf("Input error: default case value=%d\n", space); 
            ret_val++;
    }

    /* if no errors found */
    if (ret_val == 0)
    {
        u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
        u_arg->ioctl_union.mem_addr_size = access_request;
        u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

        /* For WRITE access, print the value before we actually issue the command. */
        /* In case it fails we can tell what the last access before the failure was. */

        if (!is_read)
        {
            if (space != SPACE_PIO)
            {
                switch (access_request.access_size)
                {
                    case 1:    /* Byte */
                        if (!silent)
                        {
                            printf("%.02llX-->%.04X\n", access_request.val, (int)access_request.addr);
                        }
                        break;
                    case 2:    /* Word */
                        if (!silent)
                        {
                            printf("%.04llX-->%.04X\n", access_request.val, (int)access_request.addr);
                        }
                        break;
                    case 4: /* DWord */
                        if (!silent)
                        {
                            printf("%.08llX-->%.04X\n", access_request.val, (int)access_request.addr);
                        }
                        break;
                    case 8: /* QWord */
                        if (!silent)
                        {
                            printf("%.016llX-->%.04X\n", access_request.val, (int)access_request.addr);
                        }
                        break;
                    default:
                        printf("Invalid access_size =%d\n", (int)access_request.access_size);
                        ret_val = 1;
                }
            }
            else
            {
                // For memory space show more hex digits
                switch (access_request.access_size)
                {
                    case 1:    /* Byte */
                        if (!silent)
                        {
                            printf("%.02llX-->%.08X\n", access_request.val, (int)access_request.addr);
                        }
                        break;
                    case 2:    /* Word */
                        if (!silent)
                        {
                            printf("%.04llX-->%.08X\n", access_request.val, (int)access_request.addr);
                        }
                        break;
                    case 4: /* DWord */
                        if (!silent)
                        {
                            printf("%.08llX-->%.08X\n", access_request.val, (int)access_request.addr);
                        }
                        break;
                    case 8: /* QWord */
                        if (!silent)
                        {
                            printf("%.016llX-->%.08X\n", access_request.val, (int)access_request.addr);
                        }
                        break;
                    default:
                        printf("Invalid access_size =%d\n", access_request.access_size);
                        ret_val = 1;
                }
            }

        }

        if (ret_val == 0)
        {
            rc = ioctl(s32_evfd, EV_IOC_HW_ACCESS, (void *) u_arg);
            if (rc != 0) 
            {
                printf("IOCTL(EV_IOC_HW_ACCESS) result = %d\n", rc);
                ret_val = 1; // Any non-zero value will be interpreted as "could not execute"
            }
            else
            {
                if (u_arg->errnum)
                {
                    ret_val = get_test_result(u_arg->errnum); // A fail will make ret_val non-zero
                    if (ret_val != 0)
                    {
                        printf("ERROR: IOCTL returned errnum=%d\n", u_arg->errnum); 
                    }
                }
                else
                {
                    access_request = u_arg->ioctl_union.mem_addr_size;
                }
            }
        
            if (ret_val == 0)
            {
                if (is_read)
                {
                    if (space != SPACE_PIO)
                    {
                        switch (access_size)
                        {
                            case 1:    /* Byte */
                                *value = access_request.val;
                                if (!silent)
                                {
                                    printf("%.02llX<--%.04llX\n", access_request.val, access_request.addr);
                                }
                                break;
                            case 2:    /* Word */
                                *value = access_request.val;
                                if (!silent)
                                {
                                    printf("%.04llX<--%.04llX\n", access_request.val, access_request.addr);
                                }
                                break;
                            case 4: /* DWord */
                                *value = access_request.val;
                                if (!silent)
                                {
                                    printf("%.08llX<--%.04llX\n", access_request.val, access_request.addr);
                                }
                                break;
                            case 8: /* QWord */
                                *value = access_request.val;
                                if (!silent)
                                {
                                    printf("%.016llX<--%.04llX\n", access_request.val, access_request.addr);
                                }
                                break;
                            default:
                                printf("Invalid access_size =%d\n", access_request.access_size);
                                ret_val = 2;
                        }
                    }
                    else
                    {
                        // For memory space show more hex digits
                        switch (access_size)
                        {
                            case 1:    /* Byte */
                                *value = access_request.val;
                                if (!silent)
                                {
                                    printf("%.02llX<--%.08llX\n", access_request.val, access_request.addr);
                                }
                                break;
                            case 2:    /* Word */
                                *value = access_request.val;
                                if (!silent)
                                {
                                    printf("%.04llX<--%.08llX\n", access_request.val, access_request.addr);
                                }                            
                                break;
                            case 4: /* DWord */
                                *value = access_request.val;
                                if (!silent)
                                {
                                    printf("%.08llX<--%.08llX\n", access_request.val, access_request.addr);
                                }
                                break;
                            case 8: /* QWord */
                                *value = access_request.val;
                                if (!silent)
                                {
                                    printf("%.016llX<--%.08llX\n", access_request.val, access_request.addr);
                                }
                                break;
                            default:
                                printf("Invalid access_size =%d\n", access_request.access_size);
                                ret_val = 2;
                        }
                    }
                }
            }
        }
        free(u_arg);
    }

    return ret_val;
}

UINT32 EV_beacon(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    ioctl_arg_t u_arg;
    get_set_val_t get_set_val;
    SINT32 rc;

    memset(&get_set_val,0x00,sizeof(get_set_val_t));

    if (argc == 1)
    {
        // READ
        get_set_val.is_read = TRUE;
        result = 0; // Okay to proceed
    }
    else
    {
        // WRITE - We'll only use the second argument
        get_set_val.is_read = FALSE;
        get_set_val.value = atoi(argv[1]);
        if (!((get_set_val.value>=0) && (get_set_val.value<=1)))
        {
            printf("Value is out of range s/b 0..1 but is %lld\n", get_set_val.value);
        }
        else
        {    
            result = 0; // Okay to proceed
        }
    }

    if (result == 0)
    {
        u_arg.ioctl_union.get_set_val = get_set_val;
        u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

        rc = ioctl(s32_evfd, EV_IOC_GET_SET_BEACON, (void *)&u_arg);
        if ((rc < 0) || (u_arg.errnum != 0))
        {
            perror("ioctl(EV_IOC_GET_SET_BEACON)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            get_set_val = u_arg.ioctl_union.get_set_val; // Get the response

            if (get_set_val.is_read)
            {
                printf("Current beacon value = %lld\n", get_set_val.value);
            }
            else
            {
                printf("Set beacon value to %lld\n", get_set_val.value);
            }
        }
    }

    return result;
}

UINT32 EV_passcode(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    SINT32 rc;
    ioctl_arg_t u_arg;
    get_set_val_t get_set_val;
    char str[MAX_STR];
    char *endptr=str;

    memset(&get_set_val,0x00,sizeof(get_set_val_t));

    // We will allow reading the driver's current passcode setting.
    result = TEST_PASS;     // Allow testing to continue.
    if ((argc == 1) || (argc ==2))
    {
        if (argc == 2)
        {
            // WRITE - We'll only use the second argument
            get_set_val.is_read = FALSE;
            // Always hex
            get_set_val.value = strtoul(argv[1], &endptr, 16);
            // Check to make sure the entire string was parsed
            if (strlen(argv[1]) != (endptr-argv[1]))
            {
                printf("Input error: argument not valid <%s>\n", argv[1]);
                result = TEST_COULD_NOT_EXECUTE;
            }
            else
            {
                if (!((get_set_val.value>=0) && (get_set_val.value<=0xffffffff)))
                {
                    printf("Value is out of range s/b 0..0xffffffff but is %llx\n", get_set_val.value);
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }
        }
        else
        {
            get_set_val.is_read = TRUE;
            get_set_val.value = UNINITIALIZED_VALUE;
        }
        
        if (result == TEST_PASS) // ok so far
        {
            u_arg.ioctl_union.get_set_val = get_set_val;
            u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

            rc = ioctl(s32_evfd, EV_IOC_GET_SET_PASSCODE, (void *)&u_arg);
            if ((rc < 0) || (u_arg.errnum != 0))
            {
                perror("ioctl(EV_IOC_GET_SET_PASSCODE)");
                result = TEST_COULD_NOT_EXECUTE;
            }
            else
            {
                get_set_val = u_arg.ioctl_union.get_set_val; // Get the response
            }
        }
        else
        {
            if (get_set_val.is_read)
            {
                printf("Current passcode value is %llx\n", get_set_val.value);
            }
            else
            {
                printf("Set passcode value to %llx\n", get_set_val.value);
            }
        }
    }

    return result;
}



UINT32 EV_chip_reset(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    ioctl_arg_t u_arg;
    SINT32 rc;

    u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = ioctl(s32_evfd, EV_IOC_CHIP_RESET, (void *)&u_arg);
    if (rc < 0) 
    {
        perror("ioctl(EV_IOC_CHIP_RESET)");
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        printf("Chip Reset done\n");
    }
    return result;
}

// Debug only
// This will cause the driver to output certain fields of interest in the device's structure
UINT32 EV_dbg_log_state(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    ioctl_arg_t u_arg;
    SINT32 rc;

    u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = ioctl(s32_evfd, EV_IOC_DBG_LOG_STATE, (void *)&u_arg);
    if ((rc < 0) || (u_arg.errnum !=0))
    {
        perror("ioctl(EV_IOC_DBG_LOG_STATE)");
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        printf("Debug Log snapshot taken - see /var/log/messages file\n");
    }

    return result;
}


UINT32 EV_reset_stats(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    ioctl_arg_t u_arg;
    SINT32 rc;

    u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = ioctl(s32_evfd, EV_IOC_DBG_RESET_STATS, (void *)&u_arg);
    if ((rc < 0) || (u_arg.errnum !=0))
    {
        perror("ioctl(EV_IOC_DBG_RESET_STATS)");
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        printf("Debug stats counters have been reset\n");
    }
    return result;
}

   
// This is not really a 'test' but it is called every time a evutil is called, script or no script.
// It should not print anything that may interfere with the script based test output.
static int get_cpuinfo(struct CpuInfo *info)
{
    int retVal = TEST_COULD_NOT_EXECUTE;
    FILE *cpuInfo;
    int done = FALSE;
    int num_found = 0;
    char str[STR_LEN]; 
    char *res;
    char *name_str;
    char *value_str;

    cpuInfo = fopen("/proc/cpuinfo", "r");
    if (cpuInfo == NULL)
    {
          info->freq = DEFAULT_CPU_FREQ; // Default on local machine.
        printf("ERROR: Cannot open /proc/cpuinfo, defaulting CPU frequency to %f Mhz\n", info->freq);
    }
    else 
    {
          //"cpu Mhz"
          info->freq = DEFAULT_CPU_FREQ; // Default of local machine frequency - in case we don't find it.

        // Read the file one line at a time and search for the wanted fields and extract the data
        while (!done)
        {
            res = fgets (str, sizeof(str), cpuInfo); 

            if (res!=NULL)
            {
                // Extract the name field
                name_str = strtok(str, ":");

                // If it is the one we are looking for, then extract the value
                if (strstr(name_str,"cpu MHz") !=NULL)
                {
                    // Found it
                    value_str = strtok(NULL, ":");
                    info->freq = atof(value_str);
                    num_found++;
                }
            }
            else
            {
                done = TRUE;
            }

            if (num_found==1)
            {
                done = TRUE;
            }
        }

        fclose(cpuInfo);

        if (num_found != 0)
        {
            retVal = TEST_PASS;
        }
        else
        {
            retVal = TEST_FAIL;
        }
    }
    return retVal;
}

UINT32 EV_enable_stats(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    ioctl_arg_t u_arg;
    get_set_val_t get_set_val;
    SINT32 rc;

    memset(&get_set_val,0x00,sizeof(get_set_val_t));

    if (argc == 1)
    {
        // READ
        get_set_val.is_read = TRUE;
        result = 0; // Okay to proceed
    }
    else
    {
        // WRITE - We'll only use the second argument
        get_set_val.is_read = FALSE;
        get_set_val.value = atoi(argv[1]);
        if (!((get_set_val.value>=0) && (get_set_val.value<=1)))
        {
            printf("Value is out of range s/b 0..1 but is %lld\n", get_set_val.value);
        }
        else
        {    
            result = 0; // Okay to proceed
        }
    }

    if (result == 0)
    {
        u_arg.ioctl_union.get_set_val = get_set_val;
        u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

        rc = ioctl(s32_evfd, EV_IOC_GET_SET_CAPTURE_STATS, (void *)&u_arg);
        if ((rc < 0) || (u_arg.errnum != 0))
        {
            perror("ioctl(EV_IOC_GET_SET_CAPTURE_STATS)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            get_set_val = u_arg.ioctl_union.get_set_val; // Get the response

            if (get_set_val.is_read)
            {
                printf("Current capture_stats value = %lld\n", get_set_val.value);
            }
            else
            {
                printf("Set capture_stats value to %lld\n", get_set_val.value);
            }
        }
    }

    return result;
}


UINT32 EV_get_perf_stats(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    ioctl_arg_t u_arg;
    SINT32 rc;
    performance_stats_t ps;
    float delta_time_in_secs = 0.0;
    float iops = 0.0;
    float throughput = 0.0;
    unsigned long long cpu_ticks = 0;

    memset(&ps,0xff,sizeof(performance_stats_t));

    u_arg.ioctl_union.performance_stats = ps;
    u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = ioctl(s32_evfd, EV_IOC_GET_PERF_STATS, (void *)&u_arg);
    if ((rc < 0) || (u_arg.errnum != 0))
    {
        perror("ioctl(EV_IOC_GET_PERF_STATS)");
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        ps = u_arg.ioctl_union.performance_stats;
        if (ps.is_enabled)
        {
            // Some local processing is needed.
            // Get the local CPU frequency since the time stamps provided are based on that frequency.

            // Do the calculations
            cpu_ticks = (ps.end_time - ps.start_time);
            delta_time_in_secs = cpu_ticks/(1000000.0*cpu_info.freq);
            if (delta_time_in_secs != 0.0)
            {
                iops = ps.ios_completed/delta_time_in_secs;
                throughput = ps.bytes_transferred/delta_time_in_secs; 
            }

            printf("\nPerformance Information since last rst_stats command\n");
            printf("\nRaw Data\n");
            printf("dmas completed ISR calls   %lld\n", ps.completion_interrupts);
            printf("dmas completed processed   %lld\n", ps.ios_completed);
            printf("Total interrupt calls      %lld\n", ps.total_interrupts);
            printf("start_time                 %lld\n", ps.start_time);
            printf("end_time                   %lld\n", ps.end_time);
            printf("bytes transferred          %lld\n", ps.bytes_transferred);
               printf("delta time in cpu ticks    %lld CPU ticks\n", cpu_ticks);
               printf("delta time seconds         %f S\n", delta_time_in_secs);
               printf("CPU MHZ                    %f Mhz\n", cpu_info.freq);

            if (delta_time_in_secs != 0.0)
            {
                   printf("\n\n");
                printf("\nCalculated quantities\n");
                   printf("DMAs per second            %-10.2f DMAs/S (%5.2f KIOPS for small IO sizes)\n", iops, iops/1000.0);
                printf("Throughput                 %-10.2f MB/S\n", throughput/(1024*1024));
                printf("Latency                    %-5.4f     uS\n", 1000000.0/iops);
            }
        }
        else
        {
            printf("\nPerformance capture is not currently enabled - use \"enable_stats 1\" to enable\n");
        }
    }
    return result;
}

UINT32 EV_max_dmas(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    SINT32 rc;
    ioctl_arg_t u_arg;
    get_set_val_t get_set_val;

    memset(&get_set_val,0x00,sizeof(get_set_val_t));

    if (argc == 1)
    {
        // READ
        get_set_val.is_read = TRUE;
    }
    else
    {
        // WRITE - We'll only use the second argument
        get_set_val.is_read = FALSE;
        get_set_val.value = atoi(argv[1]);
        if (!((get_set_val.value>0) && (get_set_val.value<=MAX_DMAS_QUEUED_TO_HW)))
        {
            printf("Value is out of range s/b 1..%d but is %lld\n", MAX_DMAS_QUEUED_TO_HW, get_set_val.value);
            result = TEST_COULD_NOT_EXECUTE;
        }
    }

    u_arg.ioctl_union.get_set_val = get_set_val;
    u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    if (result == TEST_PASS)
    {
        rc = ioctl(s32_evfd, EV_IOC_GET_SET_MAX_DMAS, (void *)&u_arg);
        if ((rc < 0) || (u_arg.errnum != 0))
        {
            perror("ioctl(EV_IOC_GET_SET_MAX_DMAS)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            get_set_val = u_arg.ioctl_union.get_set_val;
            result = TEST_PASS;

            if (get_set_val.is_read)
            {
                printf("Current max_dmas_queued_to_hw = %lld\n", get_set_val.value);
            }
            else
            {
                printf("Set max_dmas_queued_to_hw to %lld\n", get_set_val.value);
            }
        }
    }

    return result;
}

UINT32 EV_max_descriptors(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    SINT32 rc;
    ioctl_arg_t u_arg;
    get_set_val_t get_set_val;

    memset(&get_set_val,0x00,sizeof(get_set_val_t));

    if (argc == 1)             
    {
        // READ
        get_set_val.is_read = TRUE;
    }
    else
    {
        // WRITE - We'll only use the second argument
        get_set_val.is_read = FALSE;
        get_set_val.value = atoi(argv[1]);
        if (!((get_set_val.value>0) && (get_set_val.value<=7936)))
        {
            printf("Value is out of range s/b 1..8192 but is %lld\n", get_set_val.value);
            result = TEST_COULD_NOT_EXECUTE;
        }
    }

    u_arg.ioctl_union.get_set_val = get_set_val;
    u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = ioctl(s32_evfd, EV_IOC_GET_SET_MAX_DESCRIPTORS, (void *)&u_arg);
    if ((rc < 0) || (u_arg.errnum != 0))
    {
        perror("ioctl(EV_IOC_GET_SET_MAX_DESCRIPTORS)");
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        get_set_val = u_arg.ioctl_union.get_set_val; // Get the response

        result = TEST_PASS;

        if (get_set_val.is_read)
        {
            printf("Current max_descriptors_queued_to_hw = %lld\n", get_set_val.value);
        }
        else
        {
            printf("Set max_descriptors_queued_to_hw to %lld\n", get_set_val.value);
        }
    }

    return result;
}



UINT32 EV_memory_window(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    SINT32 rc;
    ioctl_arg_t u_arg;
    get_set_val_t get_set_val;

    memset(&get_set_val,0x00,sizeof(get_set_val_t));

    if (argc == 1)
    {
        // READ
        get_set_val.is_read = TRUE;
    }
    else
    {
        // WRITE - We'll only use the second argument
        get_set_val.is_read = FALSE;
        get_set_val.value = atoi(argv[1]);
        // TBD - get the total memory from the driver.
        if (!((get_set_val.value>=0) && (get_set_val.value<(EV_SIZE_16GB/WINDOW_SIZE_CONFIG2))))
        {
            printf("Value is out of range s/b 0..%ld but is %lld\n", 
                    (EV_SIZE_16GB/WINDOW_SIZE_CONFIG2),
                    get_set_val.value);
            result = TEST_COULD_NOT_EXECUTE;
        }
    }

    u_arg.ioctl_union.get_set_val = get_set_val;
    u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = ioctl(s32_evfd, EV_IOC_GET_SET_MEMORY_WINDOW, (void *)&u_arg);
    if ((rc < 0) || (u_arg.errnum != 0))
    {
        perror("ioctl(EV_IOC_GET_SET_MEMORY_WINDOW)");
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        get_set_val = u_arg.ioctl_union.get_set_val; // Get the response

        result = TEST_PASS;

        if (get_set_val.is_read)
        {
            printf("Current memory window is %lld\n", get_set_val.value);
        }
        else
        {
            printf("Memory window has been set to %lld\n", get_set_val.value);
        }
    }

    return result;
}

UINT32 EV_ecc_reset(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    SINT32 rc;
    ioctl_arg_t u_arg;

    if (argc == 1)
    {
        result = 0; // Okay to proceed
    }

    if (result == 0)
    {
        u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

        rc = ioctl(s32_evfd, EV_IOC_ECC_RESET, (void *)&u_arg);
        if ((rc < 0) || (u_arg.errnum != 0))
        {
            perror("ioctl(EV_IOC_ECC_RESET)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            printf("ECC counters have been reset\n");            
        }
    }

    return result;
}

UINT32 EV_ecc_status(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    SINT32 rc;
    ioctl_arg_t u_arg;
    ecc_status_t ecc_val;

    memset(&ecc_val,0x00,sizeof(ecc_status_t));

    if (argc == 1)
    {
        result = 0; // Okay to proceed
    }

    if (result == 0)
    {
        u_arg.ioctl_union.ecc_status = ecc_val;
        u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

        rc = ioctl(s32_evfd, EV_IOC_ECC_STATUS, (void *)&u_arg);
        if ((rc < 0) || (u_arg.errnum != 0))
        {
            perror("ioctl(EV_IOC_ECC_STATUS)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            ecc_val = u_arg.ioctl_union.ecc_status;

            printf("Auto correction enabled             = %d\n", ecc_val.is_auto_corr_enabled);
            printf("Single bit error detected           = %d\n", ecc_val.is_sbe);
            printf("Single bit error counter            = %d\n", (int)ecc_val.num_ddr_single_bit_errors);
            printf("Multi bit error detected            = %d\n", ecc_val.is_mbe);
            printf("Multi bit error counter             = %d\n", (int)ecc_val.num_ddr_multi_bit_errors);
            printf("Last ECC error Address Range        = 0x%llx to 0x%llx\n", 
                    ecc_val.last_ddr_error_range_start, 
                    ecc_val.last_ddr_error_range_end);
        }
    }

    return result;
}

UINT32 EV_ecc_sbe(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    SINT32 rc;
    ioctl_arg_t u_arg;
    get_set_val_t get_set_val;

    memset(&get_set_val,0x00,sizeof(get_set_val_t));

    if (argc == 1)
    {
        // READ
        get_set_val.is_read = TRUE;
        result = 0; // Okay to proceed
    }
    else
    {
        // WRITE - We'll only use the second argument
        get_set_val.is_read = FALSE;
        get_set_val.value = atoi(argv[1]);
        if (!((get_set_val.value>=0) && (get_set_val.value<=1)))
        {
            printf("Value is out of range s/b 0..1 but is %lld\n", get_set_val.value);
        }
        else
        {    
            result = 0; // Okay to proceed
        }
    }

    if (result == 0)
    {
        u_arg.ioctl_union.get_set_val = get_set_val;
        u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

        rc = ioctl(s32_evfd, EV_IOC_GET_SET_ECC_SINGLE_BIT_ERROR, (void *)&u_arg);
        if ((rc < 0) || (u_arg.errnum != 0))
        {
            perror("ioctl(EV_IOC_GET_SET_ECC_SINGLE_BIT_ERROR)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            get_set_val = u_arg.ioctl_union.get_set_val;
            if (get_set_val.is_read)
            {
                printf("Current Single Bit Error Generate = %lld\n", get_set_val.value);
            }
            else
            {
                printf("Set Single Bit Error Generate to %lld\n", get_set_val.value);
            }
        }
    }

    return result;
}

UINT32 EV_ecc_mbe(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    SINT32 rc;
    ioctl_arg_t u_arg;
    get_set_val_t get_set_val;

    memset(&get_set_val,0x00,sizeof(get_set_val_t));

    if (argc == 1)
    {
        // READ
        get_set_val.is_read = TRUE;
        result = 0; // Okay to proceed
    }
    else
    {
        // WRITE - We'll only use the second argument
        get_set_val.is_read = FALSE;
        get_set_val.value = atoi(argv[1]);
        if (!((get_set_val.value>=0) && (get_set_val.value<=1)))
        {
            printf("Value is out of range s/b 0..1 but is %lld\n", get_set_val.value);
        }
        else
        {    
            result = 0; // Okay to proceed
        }
    }

    if (result == 0)
    {
        u_arg.ioctl_union.get_set_val = get_set_val;
        u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

        rc = ioctl(s32_evfd, EV_IOC_GET_SET_ECC_MULTI_BIT_ERROR, (void *)&u_arg);
        if ((rc != 0) || (u_arg.errnum != 0))
        {
            perror("ioctl(EV_IOC_GET_SET_ECC_MULTI_BIT_ERROR)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            get_set_val = u_arg.ioctl_union.get_set_val;
            if (get_set_val.is_read)
            {
                printf("Current Multi Bit Error Generate = %lld\n", get_set_val.value);
            }
            else
            {
                printf("Set Multi Bit Error Generate to %lld\n", get_set_val.value);
            }
        }
    }

    return result;
}

UINT32 EV_write_access(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    SINT32 rc;
    ioctl_arg_t u_arg;
    get_set_val_t get_set_val;

    memset(&get_set_val,0x00,sizeof(get_set_val_t));

    if (argc == 1)
    {
        // READ
        get_set_val.is_read = TRUE;
        result = 0; // Okay to proceed
    }
    else
    {
        // WRITE - We'll only use the second argument
        get_set_val.is_read = FALSE;
        get_set_val.value = atoi(argv[1]);
        if (!((get_set_val.value>=0) && (get_set_val.value<=1)))
        {
            printf("Value is out of range s/b 0..1 but is %lld\n", get_set_val.value);
        }
        else
        {    
            result = 0; // Okay to proceed
        }
    }

    if (result == 0)
    {
        u_arg.ioctl_union.get_set_val = get_set_val;
        u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

        rc = ioctl(s32_evfd, EV_IOC_GET_SET_WRITE_ACCESS, (void *)&u_arg);
        if ((rc < 0) || (u_arg.errnum != 0))
        {
            perror("ioctl(EV_IOC_GET_SET_WRITE_ACCESS)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            get_set_val = u_arg.ioctl_union.get_set_val; // Get the response

            if (get_set_val.is_read)
            {
                printf("Current Write Access = %lld\n", get_set_val.value);
            }
            else
            {
                printf("Set Write Access to %lld\n", get_set_val.value);
            }
        }
    }

    return result;
}

UINT32 EV_verify_pattern(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    SINT32 idx;
    UINT32 num_vec = 32; // Default
    UINT32 size_in_kB = 64; // Default

    if ((argc != 2) && (argc !=4))
    {
        result=TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        idx = atoi(argv[1]);
        printf("Verify Pattern index=%x pattern=%lx argv[1]=(%s)\n", idx, patterns[idx],argv[1]);

        if (argc == 4)
        {
            num_vec = atoi(argv[2]);
            size_in_kB = atoi(argv[3]);
        }

        result = verify_pattern(idx, num_vec, size_in_kB);
        if(result)
        {
            printf("Verify Pattern Failed\n");
            result = TEST_FAIL;
        }
        else
        {
            printf("Verify Pattern SUCCESS !!!\n");
        }
    }
    return result;    
}
// Verify evram memory content matching pattern
UINT32 verify_pattern(UINT32 idx, UINT32 num_vec, UINT32 size_in_kB)
{
    UINT32 result = TEST_PASS;
    struct timeval start;
    struct timeval end;
    double diff = 0.0;
    struct EvIoReq req;
    const int NUM_vec = num_vec;
    const uint64_t BUF_sz = size_in_kB * 1024;
    const uint64_t TOTAL_buf_sz = BUF_sz * NUM_vec;
    uint64_t mem_sz = get_mem_sz();
    uint64_t sz;
    uint64_t offset;
    struct SgVec *vec = (struct SgVec *) aligned_malloc(sizeof(struct SgVec) * NUM_vec, VECTOR_ALIGN_VALUE);
    int rc = 0;
    ev_buf_t ev_buf;
    ioctl_arg_t *u_arg;
    ioctl_arg_t *u_arg2;
    int isSynchronous = TRUE;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    u_arg2 = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    // Fill up verify buffer with one pattern
    char *vbuf = (char *)malloc(TOTAL_buf_sz);

    if (vbuf == NULL)
    {
        perror("verify buffer malloc");
        result = TEST_COULD_NOT_EXECUTE;
    }

    for (offset = 0; offset < TOTAL_buf_sz; offset += sz) 
    {
        sz = min(sizeof(patterns[idx]), TOTAL_buf_sz - offset);
        memcpy(vbuf + offset, &patterns[idx], sz);
    }

    printf("Verify memory pattern %lx, started at %llx\n", patterns[idx], start_address_for_testing);

    uint64_t bytes = 0;
    char *rbuf = (char *)aligned_malloc(TOTAL_buf_sz, ALIGN_VALUE);

    if( rbuf == NULL)
        perror("read buffer  malloc");

    offset = start_address_for_testing;

    while (offset < mem_sz) 
    {
        int ii;
        uint64_t len;
        uint64_t verify_pos = offset;

        if (bytes > mem_sz/50) 
        {
            bytes = 0;
            printf(".");
            fflush(stdout);
        }

        uint64_t buf_used = 0;
        for (ii = 0; ii < NUM_vec; ii++) 
        {
            len = min((unsigned long)BUF_sz, mem_sz - offset);
            if (len == 0)
                break;
            vec[ii].ram_base = rbuf + buf_used;
            vec[ii].dev_addr = offset;
            vec[ii].length = len;
            offset += len;
            bytes += len;
            buf_used += len;
        }

        if (buf_used > TOTAL_buf_sz)
        {
            printf("use out of buffer\n");
        }

        if (ii == 0)
        {
            printf("Number of vec can't be zero\n");
        }

        req.vec = vec;
        req.nvec = ii;
        req.status = 1;
        req.cookie = &req;

        ev_buf.type = SGIO;
        ev_buf.buf = (void *)&req;
        ev_buf.sync = isSynchronous;

        u_arg->ioctl_union.ev_buf = ev_buf;
        u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

        gettimeofday(&start, NULL);
        rc = ioctl(s32_evfd, EV_IOC_READ, (void *)u_arg);
        if ((rc < 0) || (u_arg->errnum != 0))
        {
            perror("ioctl(EV_IOC_READ)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            while (1) 
            {
                u_arg2->ioctl_union.ev_io_event_ioc.count = 1;
                rc = ioctl(s32_evfd, EV_IOC_GET_IOEVENTS, (void *)u_arg2);

                if ((rc < 0) || (u_arg2->errnum != 0))
                {
                    perror("ioctl(EV_IOC_GET_IOEVENTS)");
                    result = TEST_FAIL;
                    continue;
                }

                if (u_arg2->ioctl_union.ev_io_event_ioc.count == 0) 
                {
                    result = TEST_FAIL;
                    continue;
                } 
                else 
                {
                    if(!(u_arg2->ioctl_union.ev_io_event_ioc.count == 1))
                    {
                        printf("More than one events received");
                        result = TEST_FAIL;
                    }
                    else
                    {
                        if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count -1].status == 0))
                        {
                            printf("Evram IO failed");
                            result = TEST_FAIL;
                        }
                        else
                        {
                            if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie == &req))
                            {
                                printf("\nCookie does not match request cookie = %p  req = %p at index = %d", 
                                    u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie, &req, 
                                    u_arg2->ioctl_union.ev_io_event_ioc.count - 1);
                                result = TEST_FAIL;
                            }
                            else
                            {
                                //printf("\nCookie matches request\n");
                            }
                        }
                    }

                    gettimeofday(&end, NULL);
                    diff += (((end.tv_sec - start.tv_sec) * 1000) + ((end.tv_usec - start.tv_usec)/1000.0));

                    // Verify pattern
                    if (memcmp(vbuf, rbuf, buf_used)) 
                    {
                        uint64_t ii, expected, found;
                        uint64_t error_count=0;
                        for (ii = 0; ii < buf_used; ii += 8, verify_pos += 8) 
                        {
                            expected = *((uint64_t *)(vbuf + ii));
                            found = *((uint64_t *)(rbuf + ii));
                            if (found != expected) 
                            {
                                printf("Mismatch at offset 0x%lx: expect %lx found %lx\n",
                                         verify_pos, expected, found);
                                result = TEST_FAIL;
                                error_count++;
                                if (error_count>32) 
                                {
                                    printf("Too many errors to report\n");
                                    break;
                                }
                            }
                        }
                        break;
                    } 
                    else 
                    {
                        break;
                    }
                }
            }
        }
    }

    free(vbuf);
    aligned_free(rbuf);
    aligned_free(vec);
    free(u_arg);
    free(u_arg2);

    printf("Done\n");
    printf("%lu bytes verified\n", mem_sz);
    printf("Time taken to Read: %10.5f milli sec\n", diff);
    
    return result;
}


UINT32 EV_save_evram(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;

    if (argc == 2)
    {
        result = save_evram(argv[1], get_mem_sz(), 32, 64*1024);
        if(result)
        {
            printf("Saving EVRAM to file FAILED !!\n");
            result = TEST_FAIL;
        }
        else
        {
            printf("Saving EVRAM to file SUCCESS !!!\n");
            result = TEST_PASS;
        }
    }
    return result;    
}

// Save evram memory content to file "fname".
UINT32 save_evram(const char *fname, uint64_t size_in_bytes, UINT32 num_vec, uint64_t buffer_size)
{
    UINT32 result = TEST_PASS;
    struct timeval start;
    struct timeval end;
    double diff = 0.0;
    struct EvIoReq req;
    const int NUM_vec = num_vec;
    const int BUF_sz = buffer_size;
    uint64_t offset = 0;
    uint64_t mem_sz = get_mem_sz();
    char *buf = (char *)aligned_malloc(BUF_sz * NUM_vec, ALIGN_VALUE);
    struct SgVec *vec = (struct SgVec *) aligned_malloc(sizeof(struct SgVec) * NUM_vec, VECTOR_ALIGN_VALUE);
    int rc = 0;
    ev_buf_t ev_buf;
    ioctl_arg_t *u_arg;
    ioctl_arg_t *u_arg2;
    int isSynchronous = TRUE;
    int jj;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    u_arg2 = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    mem_sz = size_in_bytes;

    printf ("buf address=%p buf size=0x%x SGL address=%p num vectors =%d\n", buf, BUF_sz * NUM_vec, vec, NUM_vec);

    int fd = open(fname, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (!(fd >= 0))
    {
        printf("Failed to open %s: %s", fname, strerror(errno));
    }

    printf("Saving\n");
    uint64_t bytes = 0;
    offset = 0;

    while (offset < mem_sz) 
    {
        int ii;
        uint64_t len;
        uint64_t buf_offset;
        if (bytes > mem_sz/50) 
        {
            bytes = 0;
            printf(".");
            fflush(stdout);
        }

        for (ii = 0, buf_offset = 0; ii < NUM_vec; ii++) 
        {
            len = min((unsigned long)BUF_sz, mem_sz - offset);
            if (len == 0)
                break;
            vec[ii].ram_base = buf + buf_offset;
            vec[ii].dev_addr = offset;
            vec[ii].length = len;
            offset += len;
            bytes += len;
            buf_offset += len;
        }

        if (ii == 0)
        {
            printf("Number of vec can't be zero");
        }

        req.vec = vec;
        req.nvec = ii;
        req.status = 1;
        req.cookie = &req;
    
        ev_buf.type = SGIO;
        ev_buf.sync = isSynchronous;
        ev_buf.buf = (void *)&req;

        u_arg->ioctl_union.ev_buf = ev_buf;
        u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;
     
        gettimeofday(&start, NULL);
        rc = ioctl(s32_evfd, EV_IOC_READ, (void *)u_arg);
        if (rc < 0)
        {
            printf("Submit io req failed: %s", strerror(errno));
            perror("ioctl(EV_IOC_READ)");
            result = TEST_FAIL;
        }
        else
        {
            if (u_arg->errnum)
            {
                result = get_test_result(u_arg->errnum);
                break;
            }
            else
            {
                while (1) 
                {
                    u_arg2->ioctl_union.ev_io_event_ioc.count = 1;
                    rc = ioctl(s32_evfd, EV_IOC_GET_IOEVENTS, (void *)u_arg2);
                    if ((rc < 0) || (u_arg2->errnum != 0))
                    {
                        perror("ioctl(EV_IOC_GET_IOEVENTS)");
                        continue;
                    }

                    if (u_arg2->ioctl_union.ev_io_event_ioc.count == 0) 
                    {
                        result = TEST_FAIL;
                        continue;
                    } 
                    else 
                    {
                        if(!(u_arg2->ioctl_union.ev_io_event_ioc.count == 1))
                        {
                            printf("More than one events received");
                            result = TEST_FAIL;
                        }
                        else
                        {
                            if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count -1].status == 0))
                            {
                                printf("Evram IO failed");
                                result = TEST_FAIL;
                            }
                            else
                            {
                                if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie == &req))
                                {
                                    printf("Cookie does not match request");
                                    result = TEST_FAIL;
                                }
                                else
                                {
                                    //printf("\nCookie matches request\n");
                                }
                            }
                        }
                        gettimeofday(&end, NULL);
                        diff += (((end.tv_sec - start.tv_sec) * 1000) + ((end.tv_usec - start.tv_usec)/1000.0));
                        break;
                    }
                }

                for (jj = 0; jj < ii; jj++) 
                {
                    ssize_t written = 0;
                    while (written != (ssize_t)vec[jj].length) 
                    {
                        ssize_t bytes = write(fd, vec[jj].ram_base, vec[jj].length);
                        if (bytes < 0) 
                        {
                            if (errno == EINTR)
                            {
                                printf("Failed to write: %s", strerror(errno));
                                result = TEST_FAIL;
                            }
                            continue;
                        } 
                        else 
                        {
                            written += bytes;
                        }
                    }
                }
            }
        }
    }

    aligned_free(buf);
    aligned_free(vec);
    free(u_arg);
    free(u_arg2);

    close(s32_evfd);
    printf("Done\n");
    printf("Time taken : %10.5f milli sec\n", diff);

    return result;
}

UINT32 EV_load_evram(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;

    if (argc == 2)
    {
        result = load_evram(argv[1], 32, 64*1024);
        if(result)
        {
            printf("Loading file to EVRAM FAILED !!\n");
            result = TEST_FAIL;
        }
        else
        {
            printf("Loading file to EVRAM SUCCESS !!\n");
            result = TEST_PASS;
        }
    }
    return result;    
}

// load file "fname" content to  evram memory.
UINT32 load_evram(const char *fname, UINT32 num_vec, uint64_t buffer_size)
{
    UINT32 result = TEST_PASS;
    struct timeval start;
    struct timeval end;
    double diff = 0.0;
    struct EvIoReq req;
    const int NUM_vec = num_vec;
    const int BUF_sz = buffer_size;
    uint64_t offset = 0;
    uint64_t mem_sz = get_mem_sz();
    char *buf = (char *)aligned_malloc(BUF_sz * NUM_vec, ALIGN_VALUE);
    struct SgVec *vec = (struct SgVec *) aligned_malloc(sizeof(struct SgVec) * NUM_vec, VECTOR_ALIGN_VALUE);
    int rc = 0;
    ev_buf_t ev_buf;
    ioctl_arg_t *u_arg;
    ioctl_arg_t *u_arg2;
    int isSynchronous = TRUE;
    struct stat st;
    int bytes_read;
    int curr_read;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    u_arg2 = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    stat(fname, &st);
    printf("Size of file : 0x%lX\n", st.st_size);
    mem_sz = st.st_size;


    printf ("buf address=%p buf size=0x%x SGL address=%p num vectors =%d\n", buf, BUF_sz * NUM_vec, vec, NUM_vec);

    int fd = open(fname, O_RDONLY, 0644);
    if (fd < 0)
    {
        printf("Failed to open %s: %s", fname, strerror(errno));
        result = TEST_FAIL;
    }
    else
    {
        printf("Loading\n");
        uint64_t bytes = 0;
        offset = 0;

        while (offset < mem_sz) 
        {
            int ii;
            uint64_t len;
            uint64_t buf_offset;
            if (bytes > mem_sz/50) 
            {
                bytes = 0;
                printf(".");
                fflush(stdout);
            }

            for (ii = 0, buf_offset = 0; ii < NUM_vec; ii++) 
            {
                len = min((unsigned long)BUF_sz, mem_sz - offset);
                if (len == 0)
                    break;
                bytes_read = 0;

                while(bytes_read < len)
                {
                    bytes_read = read(fd, buf + buf_offset + bytes_read, len - bytes_read);
                    if (curr_read < 0)
                    {
                        printf("Failed to read: %s", strerror(errno));
                        result = TEST_FAIL;
                    }
                    // printf("len : %d bytes read : %d\n", len, bytes_read);
                }

                vec[ii].ram_base = buf + buf_offset;
                vec[ii].dev_addr = offset;
                vec[ii].length = len;
                offset += len;
                bytes += len;
                buf_offset += len;
            }

            if (ii == 0)
            {
                printf("Number of vec can't be zero");
            }

            req.vec = vec;
            req.nvec = ii;
            req.status = 1;
            req.cookie = &req;
        
            ev_buf.type = SGIO;
            ev_buf.sync = isSynchronous;
            ev_buf.buf = (void *)&req;

            u_arg->ioctl_union.ev_buf = ev_buf;
            u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

            gettimeofday(&start, NULL);
            rc = ioctl(s32_evfd, EV_IOC_WRITE, (void *)u_arg);
            if (rc < 0)
            {
                printf("Submit io req failed: %s", strerror(errno));
                perror("ioctl(EV_IOC_WRITE)");
                result = TEST_FAIL;
            }
            else
            {
                if (u_arg->errnum)
                {
                    result = get_test_result(u_arg->errnum);
                    break;
                }
                else
                {
                    while (1) 
                    {
                        u_arg2->ioctl_union.ev_io_event_ioc.count = 1;
                        rc = ioctl(s32_evfd, EV_IOC_GET_IOEVENTS, (void *)u_arg2);
                        if ((rc < 0) || (u_arg2->errnum != 0))
                        {
                            perror("ioctl(EV_IOC_GET_IOEVENTS)");
                            result = TEST_FAIL;
                            continue;
                        }

                        if (u_arg2->ioctl_union.ev_io_event_ioc.count == 0) 
                        {
                            result = TEST_FAIL;
                            continue;
                        } 
                        else 
                        {
                            if(!(u_arg2->ioctl_union.ev_io_event_ioc.count == 1))
                            {
                                printf("More than one events received");
                                result = TEST_FAIL;
                            }
                            else
                            {
                                if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count -1].status == 0))
                                {
                                    printf("Evram IO failed");
                                    result = TEST_FAIL;
                                }
                                else
                                {
                                    if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie == &req))
                                    {
                                        printf("Cookie does not match request");
                                        result = TEST_FAIL;
                                    }
                                    else
                                    {
                                        //printf("\nCookie matches request\n");
                                    }
                                }
                            }
                            gettimeofday(&end, NULL);
                            diff += (((end.tv_sec - start.tv_sec) * 1000) + ((end.tv_usec - start.tv_usec)/1000.0));
                            break;
                        }
                    }
                }
            }
        }   

        aligned_free(buf);
        aligned_free(vec);
        free(u_arg);
        free(u_arg2);

        close(s32_evfd);
        printf("Done\n");
        printf("Time taken : %10.5f milli sec\n", diff);
    }
    
    return result;
}

UINT32 EV_fill_user_pattern(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    UINT64 pattern = 0;
    UINT64 start_address = 0;
    UINT64 size_in_bytes = 0;
    UINT64 buffer_size = 0;
    UINT32 num_vec = 1;

    if (argc != 6)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        if (argv[1][0] == '0' && argv[1][1] == 'x')
            pattern = strtoul(argv[1], NULL, 16);
        else
            pattern = atoi(argv[1]);
        
        if (argv[2][0] == '0' && argv[2][1] == 'x')
            start_address = strtoul(argv[2], NULL, 16);
        else
            start_address = atoi(argv[2]);

        if (argv[3][0] == '0' && argv[3][1] == 'x')
            size_in_bytes = strtoul(argv[3], NULL, 16);
        else
            size_in_bytes = atoi(argv[3]);

        if (argv[4][0] == '0' && argv[4][1] == 'x')
            num_vec = strtoul(argv[4], NULL, 16);
        else
            num_vec = atoi(argv[4]);

        if (argv[5][0] == '0' && argv[5][1] == 'x')
            buffer_size = strtoul(argv[5], NULL, 16);
        else
            buffer_size = atoi(argv[5]);

        result = fill_user_pattern(pattern, start_address, size_in_bytes, num_vec, buffer_size);
    }

    return result;    
}


UINT32 fill_user_pattern(uint64_t pattern, uint64_t start_address, uint64_t size_in_bytes, UINT32 num_vec, uint64_t buffer_size)
{
    UINT32 result = TEST_PASS;
    struct timeval start;
    struct timeval end;
    double diff = 0.0;
    struct EvIoReq req;
    const int NUM_vec = num_vec;
    const uint64_t BUF_sz = buffer_size;
    uint64_t mem_sz = get_mem_sz();
    uint64_t sz;
    uint64_t offset;
    struct SgVec *vec = (struct SgVec *) aligned_malloc(sizeof(struct SgVec) * NUM_vec, VECTOR_ALIGN_VALUE);
    int rc = 0;
    ev_buf_t ev_buf;
    ioctl_arg_t *u_arg;
    ioctl_arg_t *u_arg2;
    int isSynchronous = TRUE;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    u_arg2 = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    // Fill up write buffer with one pattern
    char *buf = (char *)aligned_malloc(BUF_sz, ALIGN_VALUE);
    if (buf == NULL)
        perror("malloc");

    for (offset = 0; offset < BUF_sz; offset += sz) 
    {
        sz = 8;
        memcpy(buf + offset, &pattern, sz);
    }

    printf("Fill memory pattern %lx\n", pattern);

    uint64_t bytes = 0;
    offset = start_address;

    while (offset < mem_sz) 
    {
        int ii;
        uint64_t len;
        if (bytes > mem_sz/50) 
        {
            bytes = 0;
            printf(".");
            fflush(stdout);
        }

        for (ii = 0; ii < NUM_vec; ii++) 
        {
            len = min((unsigned long)BUF_sz, mem_sz - offset);
            if (len == 0)
                break;
            vec[ii].ram_base = buf;
            vec[ii].dev_addr = offset;
            vec[ii].length = len;
            offset += len;
            bytes += len;
        }

        if(ii == 0)
        {
            printf("Number of vec can't be zero");
        }

        req.vec = (struct SgVec *)vec;
        req.nvec = ii;
        req.status = 1;
        req.cookie = &req;

        ev_buf.type = SGIO;
        ev_buf.sync = isSynchronous;
        ev_buf.buf = (void *)&req;

        u_arg->ioctl_union.ev_buf = ev_buf;
        u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;
     
        gettimeofday(&start, NULL);
        rc = ioctl(s32_evfd, EV_IOC_WRITE, (void *)u_arg);
        if ((rc < 0) || (u_arg->errnum != 0))
        {
            perror("ioctl(EV_IOC_WRITE)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            while (1) 
            {
                u_arg2->ioctl_union.ev_io_event_ioc.count = 1;
                rc = ioctl(s32_evfd, EV_IOC_GET_IOEVENTS, (void *)u_arg2);
                if ((rc < 0) || (u_arg2->errnum != 0))
                {
                    perror("ioctl(EV_IOC_GET_IOEVENTS)");
                    result = TEST_FAIL;
                    continue;
                }

                if (u_arg2->ioctl_union.ev_io_event_ioc.count == 0) 
                {
                    result = TEST_FAIL;
                    continue;
                } 
                else 
                {
                    if(!(u_arg2->ioctl_union.ev_io_event_ioc.count == 1))
                    {
                        printf("More than one events received");
                        result = TEST_FAIL;
                    }
                    else
                    {
                        if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count -1].status == 0))
                        {
                            printf("Evram IO failed");
                            result = TEST_FAIL;
                        }
                        else
                        {
                            if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie == &req))
                            {
                                printf("\nCookie does not match request cookie = %p  req = %p at index = %d", 
                                    u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie, &req, 
                                    u_arg2->ioctl_union.ev_io_event_ioc.count - 1);
                                result = TEST_FAIL;
                            }
                            else
                            {
                                //printf("\nCookie matches request\n");
                            }
                        }
                    }

                    gettimeofday(&end, NULL);
                    diff += (((end.tv_sec - start.tv_sec) * 1000) + ((end.tv_usec - start.tv_usec)/1000.0));
                    break;
                }
            }
        }
    }
    aligned_free(buf);
    aligned_free(vec);
    free(u_arg);
    free(u_arg2);

    printf("Done\n");
    printf("%lu bytes filled\n", size_in_bytes);
    printf("Time taken : %10.5f milli sec\n", diff);
    
    return result;
}

UINT32 EV_verify_user_pattern(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    UINT64 pattern = 0;
    UINT64 start_address = 0;
    UINT64 size_in_bytes = 0;
    UINT64 buffer_size = 0;
    UINT32 num_vec = 1;

    if (argc != 6)
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        if (argv[1][0] == '0' && argv[1][1] == 'x')
            pattern = strtoul(argv[1], NULL, 16);
        else
            pattern = atoi(argv[1]);
        
        if (argv[2][0] == '0' && argv[2][1] == 'x')
            start_address = strtoul(argv[2], NULL, 16);
        else
            start_address = atoi(argv[2]);

        if (argv[3][0] == '0' && argv[3][1] == 'x')
            size_in_bytes = strtoul(argv[3], NULL, 16);
        else
            size_in_bytes = atoi(argv[3]);

        if (argv[4][0] == '0' && argv[4][1] == 'x')
            num_vec = strtoul(argv[4], NULL, 16);
        else
            num_vec = atoi(argv[4]);

        if (argv[5][0] == '0' && argv[5][1] == 'x')
            buffer_size = strtoul(argv[5], NULL, 16);
        else
            buffer_size = atoi(argv[5]);

        result = verify_user_pattern(pattern, start_address, size_in_bytes, num_vec, buffer_size);
    }

    return result;    
}

UINT32 verify_user_pattern(uint64_t pattern, uint64_t start_address, uint64_t size_in_bytes, UINT32 num_vec, uint64_t buffer_size)
{
    UINT32 result = TEST_PASS;
    struct timeval start;
    struct timeval end;
    double diff = 0.0;
    struct EvIoReq req;
    const int NUM_vec = num_vec;
    const uint64_t BUF_sz = buffer_size;
    uint64_t mem_sz = get_mem_sz();
    uint64_t sz;
    uint64_t offset;
    struct SgVec *vec = (struct SgVec *) aligned_malloc(sizeof(struct SgVec) * NUM_vec, VECTOR_ALIGN_VALUE);
    int rc = 0;
    ev_buf_t ev_buf;
    ioctl_arg_t *u_arg;
    ioctl_arg_t *u_arg2;
    int isSynchronous = TRUE;
    const uint64_t TOTAL_buf_sz = BUF_sz * NUM_vec;
    uint64_t bytes = 0;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    u_arg2 = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    // Fill up verify buffer with one pattern
    char *vbuf = (char *)malloc(TOTAL_buf_sz);
    if (vbuf == NULL)
        perror("verify buffer malloc");

    for (offset = 0; offset < TOTAL_buf_sz; offset += sz) 
    {
        sz = 8;
        memcpy(vbuf + offset, &pattern, sz);
    }

    printf("Verify memory pattern %lx\n", pattern);

    char *rbuf = (char *)aligned_malloc(TOTAL_buf_sz, ALIGN_VALUE);
    if( rbuf == NULL)
        perror("read buffer  malloc");

    offset = start_address;

    while (offset < mem_sz) 
    {
        int ii;
        uint64_t len;
        uint64_t verify_pos = offset;
        if (bytes > mem_sz/50) 
        {
            bytes = 0;
            printf(".");
            fflush(stdout);
        }

        uint64_t buf_used = 0;
        for (ii = 0; ii < NUM_vec; ii++) 
        {
            len = min((unsigned long)BUF_sz, mem_sz - offset);
            if (len == 0)
                break;
            vec[ii].ram_base = rbuf + buf_used;
            vec[ii].dev_addr = offset;
            vec[ii].length = len;
            offset += len;
            bytes += len;
            buf_used += len;
        }

        if (buf_used > TOTAL_buf_sz)
        {
            printf("use out of buffer\n");
        }

        if (ii == 0)
        {
            printf("Number of vec can't be zero\n");
        }

        req.vec = vec;
        req.nvec = ii;
        req.status = 1;
        req.cookie = &req;

        ev_buf.type = SGIO;
        ev_buf.sync = isSynchronous;
        ev_buf.buf = (void *)&req;

        u_arg->ioctl_union.ev_buf = ev_buf;
        u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;
     
        gettimeofday(&start, NULL);
        rc = ioctl(s32_evfd, EV_IOC_READ, (void *)u_arg);
        if ((rc < 0) || (u_arg->errnum != 0))
        {
            perror("ioctl(EV_IOC_READ)");
            result = TEST_FAIL;
        }
        else
        {
            while (1) 
            {
                u_arg2->ioctl_union.ev_io_event_ioc.count = 1;
                rc = ioctl(s32_evfd, EV_IOC_GET_IOEVENTS, (void *)u_arg2);
                if ((rc != 0) || (u_arg2->errnum != 0))
                {
                    perror("ioctl(EV_IOC_GET_IOEVENTS)");
                    result = TEST_FAIL;
                    continue;
                }

                if (u_arg2->ioctl_union.ev_io_event_ioc.count == 0) 
                {
                    result = TEST_FAIL;
                    continue;
                } 
                else 
                {
                    if(!(u_arg2->ioctl_union.ev_io_event_ioc.count == 1))
                    {
                        printf("More than one events received");
                        result = TEST_FAIL;
                    }
                    else
                    {
                        if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count -1].status == 0))
                        {
                            printf("Evram IO failed");
                            result = TEST_FAIL;
                        }
                        else
                        {
                            if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie == &req))
                            {
                                printf("\nCookie does not match request cookie = %p  req = %p at index = %d", 
                                    u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie, &req, 
                                    u_arg2->ioctl_union.ev_io_event_ioc.count - 1);
                                result = TEST_FAIL;
                            }
                            else
                            {
                                //printf("\nCookie matches request\n");
                            }
                        }
                    }

                    gettimeofday(&end, NULL);
                    diff += (((end.tv_sec - start.tv_sec) * 1000) + ((end.tv_usec - start.tv_usec)/1000.0));
                    // Verify pattern
                    if (memcmp(vbuf, rbuf, buf_used)) 
                    {
                        uint64_t ii, expected, found;
                        for (ii = 0; ii < buf_used; ii += 8, verify_pos += 8) 
                        {
                            expected = *((uint64_t *)(vbuf + ii));
                            found = *((uint64_t *)(rbuf + ii));
                            if (found != expected) 
                            {
                                printf("Mismatch at offset 0x%lx: expect %lx found %lx\n",
                                       verify_pos, expected, found);
                                result = TEST_FAIL;
                            }
                        }
                        break;
                    } 
                    else 
                    {
                        result = TEST_FAIL;
                        break;
                    }
                }
            }
        }
    }

    free(vbuf);
    aligned_free(rbuf);
    aligned_free(vec);
    free(u_arg);
    free(u_arg2);

    printf("%lu bytes verified\n", size_in_bytes);
    printf("Time taken : %10.5f milli sec\n", diff);
    return result;
}

UINT32 EV_fill_inc_pattern(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    UINT64 start_address = start_address_for_testing;
    UINT64 size_in_bytes = get_mem_sz();
    UINT32 num_vec = 32;
    UINT64 buffer_size = 65536;

    if ((argc != 1) && (argc !=5))
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        if (argc == 5)
        {
            if (argv[1][0] == '0' && argv[1][1] == 'x')
                start_address = strtoul(argv[1], NULL, 16);
            else
                start_address = atoi(argv[1]);

            if (argv[2][0] == '0' && argv[2][1] == 'x')
                size_in_bytes = strtoul(argv[2], NULL, 16);
            else
                size_in_bytes = atoi(argv[2]);

            if (argv[3][0] == '0' && argv[3][1] == 'x')
                num_vec = strtoul(argv[3], NULL, 16);
            else
                num_vec = atoi(argv[3]);

            if (argv[4][0] == '0' && argv[4][1] == 'x')
                buffer_size = strtoul(argv[4], NULL, 16);
            else
                buffer_size = atoi(argv[4]);
        }

        result = fill_inc_pattern(start_address, size_in_bytes, num_vec, buffer_size);
        if(result)
        {
            result = TEST_FAIL;
        }
        else
        {
            result = TEST_PASS;
        }
    }

    return result;
}

UINT32 fill_inc_pattern(uint64_t start_address, uint64_t size_in_bytes, UINT32 num_vec, uint64_t buffer_size)
{
    UINT32 result = TEST_PASS;
    struct timeval start;
    struct timeval end;
    double diff = 0.0;
    struct EvIoReq req;
    const int NUM_vec = num_vec;
    const uint64_t BUF_sz = buffer_size;
    uint64_t mem_sz = get_mem_sz();
    uint64_t offset;
    struct SgVec *vec = (struct SgVec *) aligned_malloc(sizeof(struct SgVec) * NUM_vec, VECTOR_ALIGN_VALUE);
    int rc = 0;
    ev_buf_t ev_buf;
    ioctl_arg_t *u_arg;
    ioctl_arg_t *u_arg2;
    int isSynchronous = TRUE;
    int ival;
    uint64_t bytes = 0;
   
    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    u_arg2 = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    // Fill up write buffer with one pattern
    char *buf = (char *)aligned_malloc(BUF_sz, ALIGN_VALUE);

    if (buf == NULL)
    {
        perror("malloc");
        result = TEST_COULD_NOT_EXECUTE;
    }

    for (ival = 0; ival < BUF_sz; ival++)
    {
        *(buf + ival) = ival;
    }

    offset = start_address;
    while (offset < mem_sz) 
    {
        int ii;
        uint64_t len;
        if (bytes > mem_sz/50) 
        {
            bytes = 0;
            printf(".");
            fflush(stdout);
        }

        for (ii = 0; ii < NUM_vec; ii++) 
        {
            len = min((unsigned long)BUF_sz, mem_sz - offset);
            if (len == 0)
                break;
            vec[ii].ram_base = buf;
            vec[ii].dev_addr = offset;
            vec[ii].length = len;
            offset += len;
            bytes += len;
        }

        if (ii == 0)
        {
            printf("Number of vec can't be zero");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            req.vec = (struct SgVec *)vec;
            req.nvec = ii;
            req.status = 1;
            req.cookie = &req;

            ev_buf.type = SGIO;
            ev_buf.sync = isSynchronous;
            ev_buf.buf = (void *)&req;

            u_arg->ioctl_union.ev_buf = ev_buf;
            u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

            gettimeofday(&start, NULL);

            rc = ioctl(s32_evfd, EV_IOC_WRITE, (void *)u_arg);
            if (rc < 0)
            {
                perror("ioctl(EV_IOC_WRITE)");
                result = TEST_COULD_NOT_EXECUTE;
            }
            else
            {
                if (u_arg->errnum)
                {
                    if (IOCTL_ERR_COMMAND_NOT_SUPPORTED == u_arg->errnum)
                        printf("Not Supported\n");
                    else
                        printf("Error getting Data from Driver : Err No : %d\n", u_arg->errnum);
                    result = TEST_COULD_NOT_EXECUTE;
                    break;
                }
                else
                {
                    while (1) 
                    {
                        u_arg2->ioctl_union.ev_io_event_ioc.count = 1;
                        rc = ioctl(s32_evfd, EV_IOC_GET_IOEVENTS, (void *)u_arg2);
                        if ((rc < 0) || (u_arg2->errnum != 0))
                        {
                            perror("ioctl(EV_IOC_GET_IOEVENTS)");
                            result = TEST_FAIL;
                            continue;
                        }

                        if (u_arg2->ioctl_union.ev_io_event_ioc.count == 0) 
                        {
                            result = TEST_FAIL;
                        } 
                        else 
                        {
                            if(!(u_arg2->ioctl_union.ev_io_event_ioc.count == 1))
                            {
                                printf("More than one events received");
                                result = TEST_FAIL;
                            }
                            else
                            {
                                if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count -1].status == 0))
                                {
                                    printf("Evram IO failed");
                                    result = TEST_FAIL;
                                }
                                else
                                {
                                    if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie == &req))
                                    {
                                        printf("\nCookie does not match request cookie = %p  req = %p at index = %d", 
                                            u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie, &req, 
                                            u_arg2->ioctl_union.ev_io_event_ioc.count - 1);
                                        result = TEST_FAIL;
                                    }
                                    else
                                    {
                                        //printf("\nCookie matches request\n");
                                    }
                                }
                            }

                            gettimeofday(&end, NULL);
                            diff += (((end.tv_sec - start.tv_sec) * 1000) + ((end.tv_usec - start.tv_usec)/1000.0));
                            break;
                        }
                    }
                }
            }
        }
    }
    aligned_free(buf);
    aligned_free(vec);
    free(u_arg);
    free(u_arg2);

    printf("%lu bytes filled\n", size_in_bytes);
    printf("Time taken : %10.5f milli sec\n", diff);
    return result;
}

UINT32 EV_verify_inc_pattern(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    UINT64 start_address = start_address_for_testing;
    UINT64 size_in_bytes = get_mem_sz();
    UINT32 num_vec = 32;
    UINT64 buffer_size = 65536;

    if ((argc != 1) && (argc !=5))
    {
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        if (argc == 5)
        {
            if (argv[1][0] == '0' && argv[1][1] == 'x')
                start_address = strtoul(argv[1], NULL, 16);
            else
                start_address = atoi(argv[1]);

            if (argv[2][0] == '0' && argv[2][1] == 'x')
                size_in_bytes = strtoul(argv[2], NULL, 16);
            else
                size_in_bytes = atoi(argv[2]);

            if (argv[3][0] == '0' && argv[3][1] == 'x')
                num_vec = strtoul(argv[3], NULL, 16);
            else
                num_vec = atoi(argv[3]);

            if (argv[4][0] == '0' && argv[4][1] == 'x')
                buffer_size = strtoul(argv[4], NULL, 16);
            else
                buffer_size = atoi(argv[4]);
        }

        result = verify_inc_pattern(start_address, size_in_bytes, num_vec, buffer_size);
        if(result)
        {
            printf("Verify Pattern Failed\n");
            result = TEST_FAIL;
        }
        else
        {
            printf("Verify Pattern SUCCESS !!!\n");
            result = TEST_PASS;
        }
    }

    return result;
}

UINT32 verify_inc_pattern(uint64_t start_address, uint64_t size_in_bytes, UINT32 num_vec, uint64_t buffer_size)
{
    UINT32 result = TEST_PASS;
    struct timeval start;
    struct timeval end;
    double diff = 0.0;
    struct EvIoReq req;
    const int NUM_vec = num_vec;
    const uint64_t BUF_sz = buffer_size;
    uint64_t mem_sz = get_mem_sz();
    uint64_t offset;
    struct SgVec *vec = (struct SgVec *) aligned_malloc(sizeof(struct SgVec) * NUM_vec, VECTOR_ALIGN_VALUE);
    int rc = 0;
    ev_buf_t ev_buf;
    ioctl_arg_t *u_arg;
    ioctl_arg_t *u_arg2;
    int isSynchronous = TRUE;
    const uint64_t TOTAL_buf_sz = BUF_sz * NUM_vec;
    uint64_t ival;
    uint64_t ival1;
    int num_errors = 0;
    uint64_t bytes = 0;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    u_arg2 = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    // Fill up verify buffer with one pattern
    char *vbuf = (char *)malloc(TOTAL_buf_sz);

    if (vbuf == NULL)
    {
        perror("verify buffer malloc");
        result = TEST_COULD_NOT_EXECUTE;
    }

    for (ival = 0; ival < TOTAL_buf_sz; )
    {
        for (ival1=0; ival1 < BUF_sz; ival1++)
        {
            *(vbuf + ival + ival1) = ival1;
        }
        ival += ival1;
    }

    char *rbuf = (char *)aligned_malloc(TOTAL_buf_sz, ALIGN_VALUE);
    if( rbuf == NULL)
    {
        perror("read buffer  malloc");
        result = TEST_FAIL;
    }

    offset = start_address;
    while (offset < mem_sz) 
    {
        int ii;
        uint64_t len;
        uint64_t verify_pos = offset;
        uint64_t buf_used = 0;

        if (bytes > mem_sz/50) 
        {
            bytes = 0;
            printf(".");
            fflush(stdout);
        }

        for (ii = 0; ii < NUM_vec; ii++) 
        {
            len = min((unsigned long)BUF_sz, mem_sz - offset);
            if (len == 0)
                break;
            vec[ii].ram_base = rbuf + buf_used;
            vec[ii].dev_addr = offset;
            vec[ii].length = len;
            offset += len;
            bytes += len;
            buf_used += len;
        }

        if (buf_used > TOTAL_buf_sz)
        {
            printf("use out of buffer\n");
        }

        if (ii == 0)
        {
            printf("Number of vec can't be zero\n");
        }

        req.vec = vec;
        req.nvec = ii;
        req.status = 1;
        req.cookie = &req;

        ev_buf.type = SGIO;
        ev_buf.sync = isSynchronous;
        ev_buf.buf = (void *)&req;

        u_arg->ioctl_union.ev_buf = ev_buf;
        u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

        gettimeofday(&start, NULL);

        rc = ioctl(s32_evfd, EV_IOC_READ, (void *)u_arg);
        if (rc < 0) 
        {
            perror("ioctl(EV_IOC_READ)");
            result = TEST_COULD_NOT_EXECUTE; 
        }
        else
        {
            if (u_arg->errnum)
            {
                if (IOCTL_ERR_COMMAND_NOT_SUPPORTED == u_arg->errnum)
                    printf("Not Supported\n");
                else
                    printf("Error getting Data from Driver : Err No : %d\n", u_arg->errnum);
                result = TEST_COULD_NOT_EXECUTE; 
                break;
            }
            else
            {
                while (1) 
                {
                    u_arg2->ioctl_union.ev_io_event_ioc.count = 1;
                    rc = ioctl(s32_evfd, EV_IOC_GET_IOEVENTS, (void *)u_arg2);
                    if ((rc < 0) || (u_arg2->errnum != 0))
                    {
                        perror("ioctl(EV_IOC_GET_IOEVENTS)");
                        result = TEST_FAIL;
                        continue;
                    }

                    if (u_arg2->ioctl_union.ev_io_event_ioc.count == 0) 
                    {
                        result = TEST_FAIL;
                    } 
                    else 
                    {
                        if(!(u_arg2->ioctl_union.ev_io_event_ioc.count == 1))
                        {
                            printf("More than one events received");
                            result = TEST_FAIL;
                        }
                        else
                        {
                            if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count -1].status == 0))
                            {
                                printf("Evram IO failed");
                                result = TEST_FAIL;
                            }
                            else
                            {
                                if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie == &req))
                                {
                                    printf("\nCookie does not match request cookie = %p  req = %p at index = %d", 
                                        u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie, &req, 
                                        u_arg2->ioctl_union.ev_io_event_ioc.count - 1);
                                    result = TEST_FAIL;
                                }
                                else
                                {
                                    //printf("\nCookie matches request\n");
                                }
                            }
                        }

                        gettimeofday(&end, NULL);
                        diff += (((end.tv_sec - start.tv_sec) * 1000) + ((end.tv_usec - start.tv_usec)/1000.0));

                        // Verify pattern
                        if (memcmp(vbuf, rbuf, buf_used)) 
                        {
                            uint64_t ii, expected, found;

                            result = TEST_FAIL;
                            for (ii = 0; ii < buf_used; ii += 8, verify_pos += 8) 
                            {
                                expected = *((uint64_t *)(vbuf + ii));
                                found = *((uint64_t *)(rbuf + ii));
                                if (found != expected) 
                                {
                                    printf("Mismatch at offset 0x%lx: expect %.16lx found %.16lx\n",
                                           verify_pos, expected, found);

                                    num_errors++;
                                    if (num_errors > MAX_ERRORS_REPORTED)
                                    {
                                        break;
                                    }
                                }
                            }
                            break;
                        } 
                        else 
                        {
                            break;
                        }
                    }
                }
            }
        }
    }

    free(vbuf);
    aligned_free(rbuf);
    aligned_free(vec);
    free(u_arg);
    free(u_arg2);

    printf("%lu bytes verified\n", size_in_bytes);
    printf("Time taken : %10.5f milli sec\n", diff);
    return result;
}

UINT32 EV_program_fpga(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    int force_program = FALSE;

    if (argc == 3)
    {
        if (strcmp(argv[2], "force")==0)
        {
            force_program = TRUE;
        }
    }

    if ((argc == 2) || (argc == 3))
    {
        result = program_fpga(argv[1], force_program);
        if(result)
        {
            printf("FPGA Programming FAILED\n");
            result = TEST_FAIL;
        }
        else
        {
            printf("FPGA Programming SUCCESS\n");
            result = TEST_PASS;
        }
    }

    return result;    
}


#define FPGA_CHECKSUM_BYTES (16)
#define FPGA_CHECKSUM_DWORDS (FPGA_CHECKSUM_BYTES/sizeof(UINT32))

UINT32 program_fpga(const char *fname, int force_program)
{
    ver_info_t version_info;
    struct stat st;
    uint64_t file_sz;
    UINT32 bytes_to_read = 0;
    UINT32 bytes;
    UINT32 status = TEST_COULD_NOT_EXECUTE;
    ioctl_arg_t *u_arg;
    SINT32 rc;
    fpga_prog_t prog_file;
    UINT8 buf[8192];
    UINT8 signal_status;
    UINT8 got_first_interrupt = 0;
    UINT8 fpga_programming_completed = 0;
    int fn_version = 0;
    int fn_revision = 0;
    int fn_board_code = 0xff;
    int fn_configuration = 0;
    int fn_build_number = 0;
    char fn_str[MAX_STR];
    char *token;
    int i;
    int build_field = TRUE; // Set to false if optional build field is not present.
    int parsed_data_is_valid = FALSE; // If the file name parses correctly it will be set to TRUE.
    int okay_to_program = FALSE;
    char board_rev = 'A';
    UINT64 value = UNINITIALIZED_VALUE;
    UINT32 *pFW;
    UINT8 FIFOFW_9;
    UINT8 FIFOFW_11;
    struct timeval startFW;
    struct timeval endFW;
    long diffFW;
    UINT32 buf_checksum[FPGA_CHECKSUM_DWORDS];

    stat(fname, &st);
    //printf("\n\nSize of file : 0x%X\n\n", st.st_size);
    bytes_to_read = file_sz = st.st_size;
    
    if (!force_program)
    {
        // Get version information from the card.
        u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    
        version_info.driver_rev = 0xFF;
        version_info.driver_ver = 0xFF;
        version_info.fpga_rev = 0xFF;
        version_info.fpga_ver = 0xFF;
        version_info.rtl_version = 0xFF;
        version_info.rtl_sub_version = 0xFF;
        version_info.rtl_sub_sub_version = 0xFF;
        version_info.fw_version = 0xFF;
        version_info.fw_sub_version = 0xFF;
        version_info.fw_sub_sub_version = 0xFF;
        version_info.current_fpga_image = IOCTL_FPGA_IMAGE_UNKNOWN;
        sprintf(version_info.extra_info," ");

        u_arg->ioctl_union.ver_info = version_info;
        u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;
        rc = ioctl(s32_evfd, EV_IOC_GET_VERSION, (void *) u_arg);
        if (rc < 0) 
        {
            perror("ioctl(EV_IOC_GET_MODEL)");
            status = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            if (u_arg->errnum)
            {
                printf("Error getting Data from Driver : Err No : %d\n", u_arg->errnum);
            }
            else
            {
                version_info = u_arg->ioctl_union.ver_info; // Get the response
                display_version_info(version_info);
                status = TEST_PASS;
            }
        }    

        // Now we have the card's version info stored in version_info. 
        // Next we get the data encoded in the file name of the FPGA
        // bit file.

        if (status == TEST_PASS)
        {
            // This is the normal expected path. 
            // If not force_program we will do some checks based on the FPGA bit file naming convention. 
            // -----------------------------------------------------------------------------------
            // EV1_<version>_<revision>_<board code>_<configuration>[_<build>_<build number>].rbf
            // -----------------------------------------------------------------------------------
            // The main check is to ensure that FPGA bit files do not program boards that are not pin-wise 
            // compatible with the bit file.
            // Parse the file name.
            strcpy(fn_str, fname);

            // Set to upper
            for (i=0;i<strlen(fn_str);i++)
            {
                fn_str[i]=toupper(fn_str[i]);
            }

            // We just need the file name not the full path name, extract the file name

            // Get the last token in the path name
            token = strtok(fn_str, "/");
            while (token != NULL)
            {
                strcpy(fn_str, token);
                token = strtok( NULL, "/");
            }
    
            // fn_str will have the file name by itself here.

            // Parse the isolated file name.
            // Get the last token in the path name
            token = strtok(fn_str, "_");

            // We will ignore case
            // This should be EV1
            if (strcmp(token, "EV3") == 0)
            {
                token = strtok( NULL, "_");
                if (token != NULL)
                {
                    fn_version = atoi(token);        
                    token = strtok( NULL, "_");
                    if (token != NULL)
                    {
                        fn_revision = atoi(token);        
                        token = strtok( NULL, "_");
                        if (token != NULL)
                        {
                            fn_board_code = atoi(token);        
                            token = strtok( NULL, "_");
                            if (token != NULL)
                            {
                                // If there are any non digit characters we may have encountered
                                // no "build_" field something like "EV1_4_34_1_2.rbf" which is a valid
                                // string.
                                for (i=0;i<strlen(token);i++)
                                {
                                    if (!isdigit(token[i]))
                                    {
                                        build_field = FALSE;
                                    }
                                }

                                if (build_field)
                                {
                                    fn_configuration = atoi(token);        
                                    token = strtok( NULL, "_");
                                }
                                else
                                {
                                    // At this point token is something like "2.rbf", extract the file extension part
                                    token = strtok( token, ".");
                                    token = strtok( NULL, ".");
                                }

                                // "build" is an optional field
                                if (token != NULL)
                                {
                                    if (strcmp(token, "BUILD")==0)
                                    {
                                        // build field was found - get the number
                                        token = strtok( NULL, ".");
                                        if (token != NULL)
                                        {
                                            fn_build_number = atoi(token);
                                    
                                            // We now care about the file extension and make sure it end in ".rbf"        
                                            token = strtok( NULL, ".");
                                            if (token != NULL)
                                            {
                                                if (strcmp(token, "RBF")==0)
                                                {
                                                    parsed_data_is_valid = TRUE;
                                                    // If we get here the file was parsed properly.
                                                    printf("File parsed properly: %d.%d board code %d configuration %d build %d\n",
                                                            fn_version,
                                                            fn_revision,
                                                            fn_board_code,
                                                            fn_configuration,
                                                            fn_build_number);
                                                }
                                            }
                                        }
                                    }
                                    else
                                    {
                                        if (token != NULL)
                                        {
                                            if (strcmp(token, "RBF")==0)
                                            {
                                                parsed_data_is_valid = TRUE;
                                                // If we get here the file was parsed properly.
                                                printf("File parsed properly: %d.%d board code %d configuration %d build %d\n",
                                                        fn_version,
                                                        fn_revision,
                                                        fn_board_code,
                                                        fn_configuration,
                                                        fn_build_number);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        free(u_arg);

        // If  parsed_data_is_valid then we can do some checks before we decide that we can use this 
        // file for programming the FPGA
        if (parsed_data_is_valid)
        {
            // Compare the new file and the card and make some decisions.
            okay_to_program = FALSE;
            
            // Check for 2.x backup images first. There is currently no way to distinguish between Rev B and Rev C
            // cards if the image version.revision is 2.0. For this case we will ask the end user to look at the card
            // physically. 
            if (version_info.driver_ver == 2)
            {
                printf("Please enter the card Rev letter. This is printed near the top center of the card\n");
                version_info.fpga_board_code = 0xff;
                    
                do
                {
                    board_rev = getchar();
                    board_rev = toupper(board_rev);

                    switch (board_rev)
                    {
                        case 'B':
                            version_info.fpga_board_code = 0;
                            break;
                        case 'C':
                        case 'D':
                        default:
                            version_info.fpga_board_code = 1;
                            break;
                    }
                } while (version_info.fpga_board_code != 0xff);
            }
            else
            {
                // Note that the driver returns a proper board code based on the version and revision of the FPGA.
                // This value may not exist in the version register but reported as such based on 
                // the FPGA version and revision.
            }

            // Check the board codes.
            if (version_info.fpga_board_code == fn_board_code)
            {
                okay_to_program = TRUE;
            }
            else
            {
                printf("Error: FPGA file is incompatible with board, FPGA code=%d board code=%d, use proper FPGA file\n", 
                        fn_board_code, version_info.fpga_board_code);
            }
        }
        else
        {
            printf("File naming convention is incorrect, use the \"force\" option to program using this FPGA bit file\n");
        }
    }
    else
    {
        // Force programming of the FPGA using a file that does not follow the defined conventions
    }

    if (okay_to_program || force_program)
    {
        //int fd = open(fname, O_RDONLY | O_BINARY);   // For Windoes use this
        int fd = open(fname, O_RDONLY, 0644);

        if (fd < 0)
        {
            printf("Failed to open %s: %s", fname, strerror(errno));
            status = TEST_FAIL;
        }
        else
        {
            // read the checksum from file
            //printf("\nChecksum is : \n");
            bytes = read(fd, buf_checksum, FPGA_CHECKSUM_BYTES);

            if (bytes != FPGA_CHECKSUM_BYTES)
            {
                printf("Failed to read checksum bytes= read %d bytes, should be %d bytes \n", bytes, FPGA_CHECKSUM_BYTES);
                status = TEST_FAIL;
            }
    
            if (status != TEST_FAIL)
            {
                value = (UINT32)buf_checksum[0];
                rc = aux_hw_access(SIGNATURE_TMP0, &value, 4, FALSE, 4, SPACE_REG, TRUE);
                if (rc != 0)
                {   
                    printf("Failed to read checksum index 0\n");
                    status = TEST_FAIL;
                }
            }

            if (status != TEST_FAIL)
            {
                // write the singchecksum to register
                //value =  buf_checksum[4] + (buf_checksum[5] << 8) + (buf_checksum[6] << 16) + (buf_checksum[7] << 24);
                value = (UINT32)buf_checksum[1];
                rc = aux_hw_access(SIGNATURE_TMP1, &value, 4, FALSE, 4, SPACE_REG, TRUE);
                if (rc != 0)
                {   
                    printf("Failed to read checksum index 1\n");
                    status = TEST_FAIL;
                }
                sleep(1);
            }

            // DisARM
            //read data from register
            rc = aux_hw_access(CONTROL_REG, &value, 4, TRUE, 4, SPACE_REG, TRUE);
            if (rc == 0)
            {
                //printf ("Register \t(0x%.3x) = 0x%04X\n", 0x204, (int)value);
            }

            if (status != TEST_FAIL)
            {
                //read data from register
                rc = aux_hw_access(STATUS_REG, &value, 4, TRUE, 4, SPACE_REG, TRUE);
                if (rc != 0)
                {
                    printf("Failed to read 0xA08\n");
                    status = TEST_FAIL;
                }
            }

            if (status != TEST_FAIL)
            {
                //write data to register
                value = 1;
                rc = aux_hw_access(CONTROL_REG, &value, 4, FALSE, 4, SPACE_REG, TRUE);
                if (rc != 0)
                {
                    printf("Failed to read 0x%x\n", CONTROL_REG);
                    status = TEST_FAIL;
                }
            }

            //read data from register
            rc = aux_hw_access(CONTROL_REG, &value, 4, TRUE, 4, SPACE_REG, TRUE);
            if (rc == 0)
            {
                //printf ("Register \t(0x%.3x) = 0x%04X\n", 0x204, (int)value);
            }

            //sleep(3);

            i = 0;
            do
            {       
                usleep(1000*100);
                i++;
                rc = aux_hw_access(STATE_REG, &value, 4, TRUE, 4, SPACE_REG, TRUE);
                if (rc == 0)
                {
                        if ( i > 100)
                        {
                            printf("\nEV card still in ARMed state, please run update again.\n");
                            printf("\nRead EV card state register used  %d x100ms\n", i);
                            return -1;
                        }
                    }
            } while ( (value & 0x0F) == 0x0A );
            printf("\n\nEV card current state is: 0x%X\n", (int) value);
            printf("\nRead EV card state register used  %d x100ms\n", i);



            //read data from register
            rc = aux_hw_access(IFP_CONTROL, &value, 4, TRUE, 4, SPACE_REG, TRUE);
            if (rc == 0)
            {
                //printf ("Register \t(0x%.3x) = 0x%04X\n", 0xa04, (int)(value & 0xffff));
            }

            //read data from register
            rc = aux_hw_access(IFP_COMMAND, &value, 1, TRUE, 1, SPACE_REG, TRUE);
            if (rc == 0)
            {
                //printf ("Register \t(0x%.3x) = 0x%04X\n", 0xa00, (int)(value & 0xffff));
            }

            //write data to register
            value |= 0x01;
            rc = aux_hw_access(IFP_COMMAND, &value, 4, FALSE, 4, SPACE_REG, TRUE);
            if (rc == 0)
            {
                //printf("\nWrite 0x%X to reg 0x%X\n", value, 0xa00);;
            }

            sleep(2);

            //read data from register
            rc = aux_hw_access(IFP_COMMAND, &value, 4, TRUE, 4, SPACE_REG, TRUE);
            if (rc == 0)
            {
                //printf ("Register \t(0x%.3x) = 0x%04X\n", 0xa00, (int)(value & 0xffff));
            }

            //read data from register
            rc = aux_hw_access(IFP_STATUS, &value, 4, TRUE, 4, SPACE_REG, TRUE);
            if (rc == 0)
            {
                //printf ("Register \t(0x%.3x) = 0x%04X\n", 0xa08, (int)(value & 0xffff));

                if ( (value & 0x40) != 0x40 )
                {
                    printf("The update image file is wrong, update exit.\n\n");
        
                    return -1;
                }
            }

            //read data from register
            rc = aux_hw_access(IFP_CONTROL, &value, 4, TRUE, 4, SPACE_REG, TRUE);
            if (rc == 0)
            {
                //printf ("Register \t(0x%.3x) = 0x%04X\n", 0xa04, (int)(value & 0xffff));
            }

            printf("\nErase the application image space:\n\n");
    
            i = 1;
            value = value & 0x01;
            while ( value != 0x01 )
            {
                sleep (1);
                i++;
                if ( i > 300 )
                {
                    break;
                }

                //printf("Erase has used %d seconds\r",i);
        
                //read data from register
                rc = aux_hw_access(IFP_CONTROL, &value, 4, TRUE, 4, SPACE_REG, TRUE);
                if (rc == 0)
                {
                    //printf ("Erase has used %d secnods. (register_0x%x) = 0x%04X)\r", i, 0xa04, (int)(value & 0xffff));
                    printf ("Erase has used %d seconds\r", i);
                    fflush(stdout);
                }

                value = value & 0x01;
            }

            printf("\n\n");
    

            //read data from register
            rc = aux_hw_access(IFP_CONTROL, &value, 4, TRUE, 4, SPACE_REG, TRUE);
            if (rc == 0)
            {
                if( (value & 0x01) != 0x01 )
                {
                    printf("The time for erase flash over 200 seconds, timeout, update exit.\n\n");
                    return -1;
                }
            }

            sleep(1);

            //printf("presse any to continue\n");
            //getchar();

            // write the checksum to register 0xA18
            //write data to register
            //value =  buf_checksum[8] + (buf_checksum[9] << 8) + (buf_checksum[10] << 16) + (buf_checksum[11] << 24);
            value =  buf_checksum[2]; 
            rc = aux_hw_access(IFP_AI_CHECKSUM, &value, 4, FALSE, 4, SPACE_REG, TRUE);
            if (rc == 0)
            {
                //printf("Write 0x%8X to reg 0x%X\n", value, 0xa18);
            }

            sleep(1);

            printf("Starting update the image:\n");


            bytes_to_read -= 16;

            //printf("bytes_to_read is : 0x%x\n", bytes_to_read);


            // start time
            gettimeofday(&startFW, NULL);

            //read data from register
            rc = aux_hw_access(IFP_STATUS, &value, 4, TRUE, 4, SPACE_REG, TRUE);
            if (rc == 0)
            {
                //printf ("Register \t(0x%.3x) = 0x%8X\n", 0xa08, (int)(value ));
                FIFOFW_9 = (value >> 8) & 0x02;
                //printf("FIFOFW_9 is : 0x%X\n\n", FIFOFW_9);
            }

            //printf("Press any key to continue.\n");
            //getchar();


            while (bytes_to_read > 0) 
            {

                if (bytes_to_read <= 256)
                {
                    for(i=0; i<256; i++)
                    {
                        buf[i]=0;
                    }
            
                    bytes = read(fd, buf, bytes_to_read);

            
                    //printf("print the last buffer content: \n");
                    //for(i=0; i<256; i++)
                    //{
                    //  if ( (i > 0) && ((i%0x10) == 0) )
                    //  {
                    //      printf("\n");
                    //  }

                    //  printf(" %X ", buf[i]);
                    //}
                    //printf("print the last buffer content end. \n\n\n");

                } 
                else
                {
                    bytes = read(fd, buf, 256);
                }
        
                for(i=0; i<64; i++)
                {
                    if ( (i > 0) && ((i%0x4) == 0) )
                    {
                        //printf("\n");
                    }

                    pFW = (UINT32 *) (buf+(i*4));
            
                    //value = (UINT32) *( (UINT32 *) (buf+(i*4)) );
                    value =(UINT32) *pFW;
    
                    //printf(" %8X ", value);

                    // write data to register 0xa24
                    rc = aux_hw_access(IFP_FIFO_DATA, &value, 4, FALSE, 4, SPACE_REG, TRUE);
                }
                //printf("\n\n");
                bytes_to_read -= bytes;

                do
                {
                    rc = aux_hw_access(IFP_STATUS, &value, 4, TRUE, 4, SPACE_REG, TRUE);
                    if (rc == 0)
                    {
                        //printf ("Register \t(0x%.3x) = 0x%8X\n", 0xa08, (int)(value ));
                        FIFOFW_9 = (value >> 8) & 0x02;
                        //printf("FIFOFW_9 is : 0x%X\n\n", FIFOFW_9);
                    }
                } while (FIFOFW_9 != 0x02);

                do 
                {
                    rc = aux_hw_access(IFP_STATUS, &value, 4, TRUE, 4, SPACE_REG, TRUE);
                    if (rc == 0)
                    {
                        //printf ("Register \t(0x%.3x) = 0x%8X\n", 0xa08, (int)(value ));
                        FIFOFW_11 = (value >> 8) & 0x08;
                        //printf("FIFOFW_11 is : 0x%X\n\n", FIFOFW_11);
                    }
                } while ( FIFOFW_11 == 0x08 );

                printf("\rProgramming... %d%%", (int)(100 - ((bytes_to_read * 100)/file_sz)));
                fflush(stdout);

                gettimeofday(&endFW, NULL);
                diffFW = endFW.tv_sec - startFW.tv_sec;
                //printf(" diffFW: %d", diffFW);
                if ( diffFW > 50 )
                {
                    printf("Programming took too long, update image error, update exit.\n");

                    rc = aux_hw_access(IFP_STATUS, &value, 4, TRUE, 4, SPACE_REG, TRUE);
                    if (rc == 0)
                    {
                        printf("0x%x is: 0x%llX\n", IFP_STATUS, value);
                        value = value | 0x04000;
        
                        sleep(2);
            
                        rc = aux_hw_access(IFP_STATUS, &value, 4, FALSE, 4, SPACE_REG, TRUE);
            
                        return -1;
                    }
                }
            }
    
            printf("\n\nProgramming finished.\n");  
            printf("Programming took %ld seconds\n\n", diffFW);

            rc = aux_hw_access(IFP_STATUS, &value, 4, TRUE, 4, SPACE_REG, TRUE);
            if (rc == 0)
            {
                //printf("0xa08 is: 0x%X\n", value);
                value = value | 0x02000;
        
                sleep(2);
    
                rc = aux_hw_access(IFP_STATUS, &value, 4, FALSE, 4, SPACE_REG, TRUE);
            }

            sleep(2);

            rc = aux_hw_access(IFP_STATUS, &value, 4, TRUE, 4, SPACE_REG, TRUE);
            //printf("After write 1 to 0xa08.13, read value is: 0x%X\n", value);

            sleep(2);

            rc = aux_hw_access(IFP_STATUS, &value, 4, TRUE, 4, SPACE_REG, TRUE);
            printf("Register_0xa08 is: 0x%llX\n", value);

            // read the checksum from register 0xA18_0xA27
            printf("\n\nRead checksum of the image file from register: \n");
                
            //read data from register
            rc = aux_hw_access(IFP_AI_CHECKSUM, &value, 4, TRUE, 4, SPACE_REG, TRUE);
            if (rc == 0)
            {
                printf ("Register \t(0x%.3x) = 0x%8X\n", IFP_AI_CHECKSUM, (int)(value ));
            }
        
            //read data from register
            rc = aux_hw_access(FIFO_CHECK_SUM, &value, 4, TRUE, 4, SPACE_REG, TRUE);
            if (rc == 0)
            {
                printf ("Register \t(0x%.3x) = 0x%8X\n\n", FIFO_CHECK_SUM, (int)(value ));
            }

            close(fd);
            status = TEST_PASS;
        }
    }
    else
    {
        status = TEST_COULD_NOT_EXECUTE;
    }

    return status;
}


UINT32 EV_pmu_read(SINT32 argc, SINT8 *argv[], output_format form)
{

    char *endptr;

    UINT8 spdRegisterRead;
    UINT8 spdValueRead=0xA5;



    if (argc != 2)
    {
        printf(" parameter is wrong ");
        return -1;
    }
    
    spdRegisterRead = strtoul(argv[1], &endptr, 16);

    
    spdValueRead = (UINT8)pmu_spd_access(spdRegisterRead, spdValueRead, 1);

    printf("\nRead PMU SPD register 0x%X is: 0x%X\n\n", spdRegisterRead, spdValueRead);
    
    return 0;

}



UINT32 EV_pmu_write(SINT32 argc, SINT8 *argv[], output_format form)
{

    char *endptr;

    UINT8 spdRegisterWrite;
    UINT8 spdValueWrite;

    if (argc != 3)
    {
        printf(" parameter is wrong ");
        return -1;
    }

    spdValueWrite = strtoul(argv[1], &endptr, 16);
    spdRegisterWrite = strtoul(argv[2], &endptr, 16);

    
    pmu_spd_access(spdValueWrite, spdRegisterWrite, 0);

    printf("\nWrite 0x%X  to PMU SPD register 0x%X finised.\n\n", spdValueWrite, spdRegisterWrite);

    return 0;

}


UINT32 EV_pmu_info(SINT32 argc, SINT8 *argv[], output_format form)
{

    UINT8 TempByteA, TempByteB, TempByteC, TempByteD;
    UINT32 TempWordA, TempWordB, TempWordC;
    UINT32 TempDWordA, TempDWordB, TempDWordC, TempDWordD, TempDWordE;
    UINT8 TempByteRead;
    UINT8 pmuj;
    UINT8 SN[24];   
                    
                    printf("\nThe PMU information is:\n");
                    
                    // Number of Bytes CRC coverage 
                    TempByteA = pmu_spd_access(0x0, TempByteRead, 1);
                    printf(" Number of Bytes CRC Coverage: 0x%X\n", TempByteA);
                    
                    // PMU SPD Revision
                    //TempByteA = NLPMUReadRegA(i, 0x1);
                    TempByteA = pmu_spd_access(0x1, TempByteRead, 1);
                    TempByteB = TempByteA >> 4;
                    printf(" PMU SPD Revision: %d.%d, ", TempByteB, TempByteA);
                    
                    // Type/Form Factor
                    //TempByteA = NLPMUReadRegA(i, 0x2);
                    TempByteA = pmu_spd_access(0x2, TempByteRead, 1);
                    printf("Type/Form Factor: 0x%X\n", TempByteA);
                    
                    // Nominal Array Capacitance
                    //TempByteA = NLPMUReadRegA(i, 0x3);
                    TempByteA = pmu_spd_access(0x3, TempByteRead, 1);
                    //TempByteB = NLPMUReadRegA(i, 0x4);
                    TempByteB = pmu_spd_access(0x4, TempByteRead, 1);
                    TempWordA = TempByteB;
                    TempWordB = TempWordA << 8;
                    TempWordC = TempWordB + TempByteA;
                    printf(" Nominal Array Capacitance: %d dF\n", TempWordC);       
                    
                    // Array Organization - Series
                    //TempByteA = NLPMUReadRegA(i, 0x5);
                    TempByteA = pmu_spd_access(0x5, TempByteRead, 1);
                    printf(" Array Organization - Series: 0x%X, ", TempByteA);              
                    
                    // Array Organization - Parallel
                    //TempByteA = NLPMUReadRegA(i, 0x6);
                    TempByteA = pmu_spd_access(0x6, TempByteRead, 1);
                    printf("Parallel: 0x%X\n", TempByteA);              
                    
                    // Maximum Array Voltage
                    //TempByteA = NLPMUReadRegA(i, 0x7);
                    TempByteA = pmu_spd_access(0x7, TempByteRead, 1);
                    //TempByteB = NLPMUReadRegA(i, 0x8);
                    TempByteB = pmu_spd_access(0x8, TempByteRead, 1);
                    TempWordA = TempByteB;
                    TempWordB = TempWordA << 8;
                    TempWordC = TempWordB + TempByteA;
                    printf(" Maximum Array Voltage: %d mV\n", TempWordC);               
                    
                    // Maximum Array Operating Temperature
                    //TempByteA = NLPMUReadRegA(i, 0x9);
                    TempByteA = pmu_spd_access(0x9, TempByteRead, 1);
                    printf(" Maximum Array Operating Temperature: %d C\n", TempByteA);
                    
                    // Measured Capacitance - First
                    //TempByteA = NLPMUReadRegA(i, 0xA);
                    TempByteA = pmu_spd_access(0xA, TempByteRead, 1);
                    //TempByteB = NLPMUReadRegA(i, 0xB);
                    TempByteB = pmu_spd_access(0xB, TempByteRead, 1);
                    TempWordA = TempByteB;
                    TempWordB = TempWordA << 8;
                    TempWordC = TempWordB + TempByteA;
                    printf(" Measured Capacitance on First: %d dF, ", TempWordC);               
                    
                    // Measured Capacitance - Last
                    //TempByteA = NLPMUReadRegA(i, 0xC);
                    TempByteA = pmu_spd_access(0xC, TempByteRead, 1);
                    //TempByteB = NLPMUReadRegA(i, 0xD);
                    TempByteB = pmu_spd_access(0xD, TempByteRead, 1);
                    TempWordA = TempByteB;
                    TempWordB = TempWordA << 8;
                    TempWordC = TempWordB + TempByteA;
                    printf("Last: %d dF, ", TempWordC);
                    
                    // Measured Capacitance - Lowest
                    //TempByteA = NLPMUReadRegA(i, 0xE);
                    TempByteA = pmu_spd_access(0xE, TempByteRead, 1);
                    //TempByteB = NLPMUReadRegA(i, 0xF);
                    TempByteB = pmu_spd_access(0xF, TempByteRead, 1);
                    TempWordA = TempByteB;
                    TempWordB = TempWordA << 8;
                    TempWordC = TempWordB + TempByteA;
                    printf("Lowest: %d dF\n", TempWordC);
                    
                    // Measured Temperature - Highest
                    //TempByteA = NLPMUReadRegA(i, 0x10);
                    TempByteA = pmu_spd_access(0x10, TempByteRead, 1);
                    printf(" Measured Temperature - Highest: %d C, ", TempByteA);               
                    
                    // Measured Temperature - Average
                    //TempByteA = NLPMUReadRegA(i, 0x11);
                    TempByteA = pmu_spd_access(0x11, TempByteRead, 1);
                    printf("Average: %d C\n", TempByteA);               
                    
                    // Measured Temperature - Samples
                    //TempByteA = NLPMUReadRegA(i, 0x12);
                    TempByteA = pmu_spd_access(0x12, TempByteRead, 1);
                    //TempByteB = NLPMUReadRegA(i, 0x13);
                    TempByteB = pmu_spd_access(0x13, TempByteRead, 1);
                    //TempByteC = NLPMUReadRegA(i, 0x14);
                    TempByteC = pmu_spd_access(0x14, TempByteRead, 1);
                    //TempByteD = NLPMUReadRegA(i, 0x15);
                    TempByteD = pmu_spd_access(0x15, TempByteRead, 1);
                    TempWordA = TempByteB;
                    TempWordB = TempWordA << 8;
                    
                    TempDWordB = TempByteC;
                    TempDWordC = TempDWordB << 16;
                    
                    TempDWordB = TempByteD;
                    TempDWordD = TempDWordB << 24;
                    
                    TempDWordE = TempDWordD + TempDWordC + TempWordB + TempByteA;                   
                    printf(" Measured Temperature - Samples : %d\n", TempDWordE);               
                    
                    // Number of Backup Cycles
                    //TempByteA = NLPMUReadRegA(i, 0x16);
                    TempByteA = pmu_spd_access(0x16, TempByteRead, 1);
                    //TempByteB = NLPMUReadRegA(i, 0x17);
                    TempByteB = pmu_spd_access(0x17, TempByteRead, 1);
                    TempWordA = TempByteB;
                    TempWordB = TempWordA << 8;
                    TempWordC = TempWordB + TempByteA;
                    printf(" Number of Backup Cycles: %d\n", TempWordC);                
                    
                    
                    // Super Capacitor Manufacturer #1
                    //TempByteA = NLPMUReadRegA(i, 0x18);
                    TempByteA = pmu_spd_access(0x18, TempByteRead, 1);
                    printf(" Super Capacitor Manufacturer #1: 0x%X, ", TempByteA);              
                    
                    // Super Capacitor Value #1
                    //TempByteA = NLPMUReadRegA(i, 0x19);
                    TempByteA = pmu_spd_access(0x19, TempByteRead, 1);
                    //TempByteB = NLPMUReadRegA(i, 0x1A);
                    TempByteB = pmu_spd_access(0x1A, TempByteRead, 1);
                    TempWordA = TempByteB;
                    TempWordB = TempWordA << 8;
                    TempWordC = TempWordB + TempByteA;
                    printf("Value #1: 0x%X, ", TempWordC);              
                    
                    // Super Capacitor Quantity #1
                    //TempByteA = NLPMUReadRegA(i, 0x1B);
                    TempByteA = pmu_spd_access(0x1B, TempByteRead, 1);
                    printf("Quantity #1: 0x%X\n", TempByteA);               
                    
                    // Super Capacitor Manufacturer #2
                    //TempByteA = NLPMUReadRegA(i, 0x1C);
                    TempByteA = pmu_spd_access(0x1C, TempByteRead, 1);
                    printf(" Super Capacitor Manufacturer #2: 0x%X, ", TempByteA);              
                    
                    
                    // Super Capacitor Value #2
                    //TempByteA = NLPMUReadRegA(i, 0x1D);
                    TempByteA = pmu_spd_access(0x1D, TempByteRead, 1);
                    //TempByteB = NLPMUReadRegA(i, 0x1E);
                    TempByteB = pmu_spd_access(0x1E, TempByteRead, 1);
                    TempWordA = TempByteB;
                    TempWordB = TempWordA << 8;
                    TempWordC = TempWordB + TempByteA;
                    printf("Value #2: 0x%X, ", TempWordC);  
                    
                    // Super Capacitor Quantity #2
                    //TempByteA = NLPMUReadRegA(i, 0x1F);
                    TempByteA = pmu_spd_access(0x1F, TempByteRead, 1);
                    printf("Quantity #2: 0x%X\n", TempByteA);               

                    // Super Capacitor Manufacturer #3
                    //TempByteA = NLPMUReadRegA(i, 0x20);
                    TempByteA = pmu_spd_access(0x20, TempByteRead, 1);
                    printf(" Super Capacitor Manufacturer #3: 0x%X, ", TempByteA);              
                                        
                    // Super Capacitor Value #3
                    //TempByteA = NLPMUReadRegA(i, 0x21);
                    TempByteA = pmu_spd_access(0x21, TempByteRead, 1);
                    //TempByteB = NLPMUReadRegA(i, 0x22);
                    TempByteB = pmu_spd_access(0x22, TempByteRead, 1);
                    TempWordA = TempByteB;
                    TempWordB = TempWordA << 8;
                    TempWordC = TempWordB + TempByteA;
                    printf("Value #3: 0x%X, ", TempWordC);  
                    
                    // Super Capacitor Quantity #3
                    //TempByteA = NLPMUReadRegA(i, 0x23);
                    TempByteA = pmu_spd_access(0x23, TempByteRead, 1);
                    printf("Quantity #3: 0x%X\n", TempByteA);               
                
                
                    // PMU Manufacturer
                    //TempByteA = NLPMUReadRegA(i, 0x24);
                    TempByteA = pmu_spd_access(0x24, TempByteRead, 1);
                    //TempByteB = NLPMUReadRegA(i, 0x25);
                    TempByteB = pmu_spd_access(0x25, TempByteRead, 1);
                    printf(" PMU Manufacturer: 0x%X%X, ", TempByteB, TempByteA);                        
                    
                    // PMU Manufacturing Location
                    //TempByteA = NLPMUReadRegA(i, 0x26);
                    TempByteA = pmu_spd_access(0x26, TempByteRead, 1);
                    printf("Location: 0x%X, ", TempByteA);              
                    
                    
                    // PMU Manufacturer Date
                    printf("Manufacturing Date: ");
                    //TempByteA = NLPMUReadRegA(i, 0x27);
                    TempByteA = pmu_spd_access(0x27, TempByteRead, 1);
                    printf("%x", TempByteA);
                    
                    //TempByteB = NLPMUReadRegA(i, 0x28);
                    TempByteB = pmu_spd_access(0x28, TempByteRead, 1);
                    if ( TempByteB < 10 )
                    {
                        printf("0");                    
                    }
                    printf("%x\n", TempByteB);
                    
                    
                    // PMU Serial Number
                    printf(" PMU Serial Number: ");
                    //TempByteA = NLPMUReadRegA(i, 41 );
                    TempByteA = pmu_spd_access(0x29, TempByteRead, 1);
                    printf("%c", TempByteA);
                    
                    //TempByteA = NLPMUReadRegA(i, 42 );
                    TempByteA = pmu_spd_access(0x2A, TempByteRead, 1);
                    if( TempByteA < 0x0F ) printf("0");
                    printf("%x", TempByteA);

                    //TempByteA = NLPMUReadRegA(i, 43 );
                    TempByteA = pmu_spd_access(0x2B, TempByteRead, 1);
                    TempByteA = TempByteA >> 4;
                    printf("%X", TempByteA);                    
                    
                    //TempByteA = NLPMUReadRegA(i, 44 );
                    TempByteA = pmu_spd_access(0x2C, TempByteRead, 1);
                    printf("%c", TempByteA);
                    //TempByteA = NLPMUReadRegA(i, 45 );
                    TempByteA = pmu_spd_access(0x2D, TempByteRead, 1);
                    printf("%c", TempByteA);
                    
                    for ( pmuj=0; pmuj<2; pmuj++)
                    {                   
                        //TempByteA = NLPMUReadRegA(i, (0x46+pmuj) );
                        TempByteA = pmu_spd_access((0x2E + pmuj), TempByteRead, 1);
                        if( TempByteA < 0x0F )
                        printf("0");
                        printf("%X", TempByteA);
                    }

                    //TempByteA = NLPMUReadRegA(i, 48 );
                    TempByteA = pmu_spd_access(0x30, TempByteRead, 1);
                    TempByteA = TempByteA >> 4;
                    printf("%X", TempByteA);
                    printf("\n");
                    
                    
                    // PMU Part Number
                    printf(" PMU Part Number: ");
                    for ( pmuj=0; pmuj<24; pmuj++)
                    {
                        //TempByteA = NLPMUReadRegA(i, 0x33+pmuj);
                        SN[pmuj] = pmu_spd_access( (0x33 + pmuj), TempByteRead, 1);
                        //printf("%c", TempByteA);
                    }                   
                    printf("%s", SN);                   
                    
                    // PMU Revision
                    //TempByteA = NLPMUReadRegA(i, 75);
                    TempByteA = pmu_spd_access(0x4B, TempByteRead, 1);                  
                    printf("\n PMU Revision: %c\n\n", TempByteA);   
                    
                



    return 0;
}

#ifdef FACTORY_PMU_CMD_SUPPORTED
UINT32 EV_pmu_update(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT8 buffer[256];
    
    //UINT32 i;

    char *endptr;
    
    UINT32 pmuj, pmuk;
    UINT8 SNLength,SNLength2;
    UINT8 PNLength;
    
    char SNScan[15], SNScan1[7], SNScan2[7], SNScan3[4], SNScan4[3], SN[8];
    char PNScan[25], PN[25];
    char PN_file[15];
    
    UINT8 a2;
    UINT8 Year_39, Week_40;
    unsigned long long longHigh, longLow, Week_High;
    
    
    //FILE *fd;
   
    //char* FWfilename; 
    char FWpatch[200];

    


    UINT8 TempByteA, TempByteB;
    UINT32 TempWordA;
    UINT8 TempByteRead;
    UINT32 kFW;


    strcpy( FWpatch, "/root/Desktop/EV3_PMU_File/");
    //printf("\nFWpatch is: %s\n", FWpatch);

    printf("\nPlease scan the serial number on PMU label: ");
    //gets(SNScan);
    fgets(SNScan, 13, stdin);
    fflush(stdin);

    SNLength = strlen(SNScan);                  
    SNLength2 = SNLength - 5;
    
    //printf("PMU serial number is: %s, length: %d, length2: %d\n", SNScan, SNLength, SNLength2);


    for ( pmuj=0; pmuj<5; pmuj++)
    {
        SNScan1[pmuj]=SNScan[pmuj+SNLength2];   
        //printf("SNScan1: %c, ", SNScan1[pmuj]);                       
    }
    //printf("\n");
    
    
    for ( pmuj=0; pmuj<SNLength2; pmuj++)
    {                       
        SNScan2[pmuj]=SNScan[pmuj];
        //printf("SNScan2: %c, ", SNScan2[pmuj]);
    }
    //printf("\n");
    
    for ( pmuj=0; pmuj<3; pmuj++)
    {                       
        SNScan3[pmuj]=SNScan[pmuj+1];
        //printf("SNScan3: %c, ", SNScan3[pmuj]);
    }
    
    SNScan1[5]='\0';
    //SNScan2[6]='\0';
    SNScan2[SNLength2]='\0';
    SNScan3[3]='\0';
    //printf("\n");

    
    
    
    
    
    
    longHigh  = strtol(SNScan3, &endptr, 16);
    Week_High = strtol(SNScan3, &endptr, 10);
    //printf("\nlongHigh: %llX\n", longHigh);
    longHigh = longHigh << 4;
    //printf("longHigh: %llX\n", longHigh);             
    
    longLow  = strtol(SNScan1, &endptr, 16);
    //printf("longLow: %llX\n", longLow);
    
    longLow = longLow << 4;
    //printf("longLow: %llX\n", longLow);

    // SN[0,1,2](48, 47, 46)
    for ( pmuj=0; pmuj<3; pmuj++)
    {                   
        //a1 = (Byte) longHigh;
        a2 = (UINT8) longLow;
        SN[pmuj] = a2;
        //SN[pmuj+3] = a1;
        //printf("SN[%d]: %X\n", pmuj, SN[pmuj]);
        //printf("SN[%d]: %X\n", pmuj+3, SN[pmuj+3]);
        
        //longHigh = longHigh >> 8;
        longLow  = longLow >> 8;
        //printf("longHigh: %llX, longHigh: %llX\n\n", longHigh, longLow);
    }


    // SN[5,6](43, 42)
    for ( pmuj=5; pmuj<7; pmuj++)
    {                   
        //a1 = (Byte) longHigh;
        a2 = (UINT8) longHigh;
        SN[pmuj] = a2;
        //SN[pmuj+3] = a1;
        //printf("SN[%d]: %X\n", pmuj, SN[pmuj]);
        //printf("SN[%d]: %X\n", pmuj+3, SN[pmuj+3]);
        
        //longHigh = longHigh >> 8;
        longHigh  = longHigh >> 8;
        //printf("longHigh: %llX, longHigh: %llX\n\n", longHigh, longLow);
    }                   
    
    
    
    SN[3] = SNScan2[5];
    SN[4] = SNScan2[4];
    SN[7] = SNScan2[0];
    
    
    // PMU Manufacturing Date
    Year_39 = SN[7] - 0x2F;
    Week_40 = Week_High / 7;
    Week_40++;
    //printf("Year: 0x%x, Week: %d\n", Year_39, Week_40);
    //itoa(Week_40, SNScan4, 10);
    sprintf(SNScan4, "%d", Week_40);
    Week_40 = strtol(SNScan4, &endptr, 16);
    printf("Year: 0x%x, Week: 0x%x\n", Year_39, Week_40);




    printf("\nPlease scan the part number on PMU label: ");
    //gets(PNScan);
    fgets(PNScan, 23, stdin);
    PNLength = strlen(PNScan) - 1;
    //printf("PMU part number is: %s, length: %d\n", PNScan, PNLength);
    
    if ( PNLength < 8)
    {
        printf("\n The Part Number on Label is Wrong !!!\n");
        
        return -1;
    }
    
    
    for ( pmuj=0; pmuj<PNLength; pmuj++)
    {
        //PN[PNLength-pmuj-1]=PNScan[pmuj];
        PN[pmuj]=PNScan[pmuj];
        //printf("PN[%d]: %c\n", PNLength-pmuj-1, PN[PNLength-pmuj-1]);                 
        //printf("PN[%d]: %c\n", pmuj, PN[pmuj]);                   
    }
    
    
/*  
    for ( pmuj=(PNLength-8), pmuk=0; pmuj<PNLength; pmuj++, pmuk++)
    {
        //PN[PNLength-pmuj-1]=PNScan[pmuj];
        PN_file[pmuk]=PNScan[pmuj];
        //printf("PN[%d]: %c\n", PNLength-pmuj-1, PN[PNLength-pmuj-1]);                 
        //printf("PN[%d]: %c\n", pmuj, PN[pmuj]);                   
    }                   
*/  


    PNScan[PNLength]='.';
    PNScan[PNLength+1]='b';
    PNScan[PNLength+2]='i';
    PNScan[PNLength+3]='n';
    
    PNScan[PNLength+4]='\0';
    //printf("PNScan is: %s\n", PNScan);

    
    strcat(FWpatch, PNScan);
    //printf("\nPMU SPD update file is: %s\n", FWpatch);


    
    int fd = open(FWpatch, O_RDONLY, 0644);
    if (fd >= 0 )
    {                       

        pmuj = read(fd, buffer, 256);

        printf("\nSelected PMU SPD file is: %s \n", FWpatch);           
    
        //printf("Buffer is: \n");
        for(kFW=0; kFW<256; kFW++)
        {
            //printf(" %X ", buffer[kFW]);
        }
    
        close(fd);
    
    } else
    {
        printf("\nCannot open file: %s\n\n", FWpatch);
    
        return -1;
    }   
    
    

    // clear the spd 
    for ( pmuj=0; pmuj<75; pmuj++)
    {
        //NLPMUWriteRegA(i, 0x33+pmuj, PN[pmuj]);   
        pmu_spd_access(0x00, pmuj , 0);                 
    }
    

    
    // Update PMU Manufacturing Date
    //NLPMUWriteRegA(i, 39, Year_39);
    pmu_spd_access(Year_39, 0x27, 0);
    //NLPMUWriteRegA(i, 40, Week_40);
    pmu_spd_access(Week_40, 0x28, 0);

    
    // Update SN
    //NLPMUWriteRegA(i, 48, SN[0]);
    pmu_spd_access(SN[0], 0x30, 0);
    //NLPMUWriteRegA(i, 47, SN[1]);
    pmu_spd_access(SN[1], 0x2F, 0);
    //NLPMUWriteRegA(i, 46, SN[2]);
    pmu_spd_access(SN[2], 0x2E, 0);
    
    //NLPMUWriteRegA(i, 45, SN[3]);
    pmu_spd_access(SN[3], 0x2D, 0);
    //NLPMUWriteRegA(i, 44, SN[4]);
    pmu_spd_access(SN[4], 0x2C, 0);
    
    //NLPMUWriteRegA(i, 43, SN[5]);
    pmu_spd_access(SN[5], 0x2B, 0);
    //NLPMUWriteRegA(i, 42, SN[6]);
    pmu_spd_access(SN[6], 0x2A, 0);
    
    //NLPMUWriteRegA(i, 41, SN[7]);
    pmu_spd_access(SN[7], 0x29, 0);


    // PMU Manufacturing Date
    //TempByteA = NLPMUReadRegA(i, 39 );
    TempByteA = pmu_spd_access(0x27, TempByteRead, 1);
    //TempByteB = NLPMUReadRegA(i, 40 );
    TempByteB = pmu_spd_access(0x28, TempByteRead, 1);
    printf("\nPMU Manufacturing Date of Year: %x,  Week: %x\n", TempByteA, TempByteB);

/*  
    // PMU Serial Number
    printf("\n[ %2d ] PMU NEW Serial Number: 0x", i);
    //TempByteA = NLPMUReadRegA(i, 41 );
    TempByteA = pmu_spd_access(0x29, TempByteRead, 1);
    printf("%c", TempByteA);
    
    //TempByteA = NLPMUReadRegA(i, 42 );
    TempByteA = pmu_spd_access(0x2A, TempByteRead, 1);
    if( TempByteA < 0x0F ) printf("0");
    printf("%x", TempByteA);

    //TempByteA = NLPMUReadRegA(i, 43 );
    TempByteA = pmu_spd_access(0x2B, TempByteRead, 1);
    TempByteA = TempByteA >> 4;
    printf("%X", TempByteA);                    
    
    //TempByteA = NLPMUReadRegA(i, 44 );
    TempByteA = pmu_spd_access(0x2C, TempByteRead, 1);
    printf("%c", TempByteA);
    //TempByteA = NLPMUReadRegA(i, 45 );
    TempByteA = pmu_spd_access(0x2D, TempByteRead, 1);
    printf("%c", TempByteA);
    
    for ( pmuj=0; pmuj<2; pmuj++)
    {                   
        //TempByteA = NLPMUReadRegA(i, (46+pmuj) );
        TempByteB = pmu_spd_access( (0x2E + pmuj), TempByteRead, 1);
        if( TempByteA < 0x0F )
        printf("0");
        printf("%X", TempByteA);
    }

    //TempByteA = NLPMUReadRegA(i, 48 );
    TempByteA = pmu_spd_access(0x30, TempByteRead, 1);
    TempByteA = TempByteA >> 4;
    printf("%X", TempByteA);
                        
*/  



    // Update PN
    for ( pmuj=0; pmuj<PNLength; pmuj++)
    {
        //NLPMUWriteRegA(i, 0x33+pmuj, PN[pmuj]);   
        pmu_spd_access(PN[pmuj], (0x33 + pmuj) , 0);                    
    }                       
    
    
    // PMU Part Number
    printf("\nPMU NEW Part Number: ");

    for ( pmuj=0; pmuj<24; pmuj++)
    {
        //TempByteA = NLPMUReadRegA(i, 0x33+pmuj);
        TempByteA = pmu_spd_access( (0x33 + pmuj),  TempByteRead, 1);
        printf("%c", TempByteA);
    }                   
    
    printf("\n");

    
    
    for ( pmuj=0; pmuj<=0x26; pmuj++)
    {
        // Number of Bytes CRC coverage 
        //NLPMUWriteRegA(i, 0, 0x11);
        //NLPMUWriteRegA(i, pmuj, buffer[pmu]);
        pmu_spd_access(buffer[pmuj],  pmuj, 0);
        //printf("buffer[%d]: 0x%X\n", pmuj, buffer[pmuj]);
        //TempByteA = pmu_spd_access( pmuj,  TempByteRead, 1);                  
        //printf(" %d:  %X\n", pmuj, TempByteA);
    }

    

    // PMU Revision Number
    //NLPMUWriteRegA(i, 75, buffer[75]);
    pmu_spd_access(buffer[75],  0x4B, 0);
    

    
    
    // Checksum: Byte: 0-9, 24-37, 75
    TempByteA = 0;
    TempWordA = 0;
    
    for ( pmuj=0; pmuj<=9; pmuj++)
    {
        //TempByteA = NLPMUReadRegA(i, 0x0+pmuj);   
        TempByteA = pmu_spd_access( pmuj,  TempByteRead, 1);                    
        TempWordA = TempWordA + TempByteA;
        //printf(" %X _ %X,", TempByteA, TempWordA);
    }

    //printf("\n");
    for ( pmuj=0x18; pmuj<=0x25; pmuj++)
    {
        //TempByteA = NLPMUReadRegA(i, 0x0+pmuj);
        TempByteA = pmu_spd_access(pmuj, TempByteRead , 1);
        TempWordA = TempWordA + TempByteA;
        //printf(" %X _ %X,", TempByteA, TempWordA);
    }
    
    // PMU Revision Number
    //TempByteA = NLPMUReadRegA(i, 75);
    TempByteA = pmu_spd_access(0x4B, TempByteRead , 1);
    TempWordA = TempWordA + TempByteA;                  
    //printf("Checksum is : 0x%X\n", TempWordA);
    
    //NLPMUWriteRegA(i, 49, (TempWordA & 0xFF) );
    pmu_spd_access( (TempWordA & 0xFF), 0x31 , 0);
    //NLPMUWriteRegA(i, 50, (TempWordA>>8) & 0xFF );
    pmu_spd_access( ((TempWordA>>8) & 0xFF), 0x32 , 0);
    
    //TempByteA = NLPMUReadRegA(i, 49);
    //TempByteA = pmu_spd_access(0x31, TempByteRead , 1);
    //TempByteB = NLPMUReadRegA(i, 50);
    //TempByteB = pmu_spd_access(0x32, TempByteRead , 1);
    //printf("Read checksum is : 0x%X%X\n", TempByteB, TempByteA);


    // Write done!!!
    //NLPMUWriteRegA(i, 255, 'N');
    pmu_spd_access('N', 0xFF , 0);

    return 0;

}

#else

UINT32 EV_pmu_update(SINT32 argc, SINT8 *argv[], output_format form)
{

                    
                    UINT8 buffer[256];
                    
                    //UINT32 i;



                    char *endptr;
                    
                    UINT32 pmuj, pmuk;
                    UINT8 SNLength,SNLength2;
                    UINT8 PNLength;
                    
                    char SNScan[15], SNScan1[7], SNScan2[7], SNScan3[4], SNScan4[3], SN[8];
                    char PNScan[25], PN[25];
                    char PN_file[15];
                    
                    UINT8 a2;
                    UINT8 Year_39, Week_40;
                    unsigned long long longHigh, longLow, Week_High;
                    
                    
                    //FILE *fd;
   
                    //char* FWfilename; 
                    char FWpatch[200];

                    


                    UINT8 TempByteA, TempByteB;
                    UINT32 TempWordA;
                    UINT8 TempByteRead;
                    UINT32 kFW;


                    strcpy( FWpatch, "/root/Desktop/EV3_PMU_File/");
                    //printf("\nFWpatch is: %s\n", FWpatch);

                    printf("\nPlease scan the serial number on PMU label: ");
                    //gets(SNScan);
                    fgets(SNScan, 13, stdin);
                    fflush(stdin);

                    SNLength = strlen(SNScan);                  
                    SNLength2 = SNLength - 5;
                    
                    //printf("PMU serial number is: %s, length: %d, length2: %d\n", SNScan, SNLength, SNLength2);


                    for ( pmuj=0; pmuj<5; pmuj++)
                    {
                        SNScan1[pmuj]=SNScan[pmuj+SNLength2];   
                        //printf("SNScan1: %c, ", SNScan1[pmuj]);                       
                    }
                    //printf("\n");
                    
                    
                    for ( pmuj=0; pmuj<SNLength2; pmuj++)
                    {                       
                        SNScan2[pmuj]=SNScan[pmuj];
                        //printf("SNScan2: %c, ", SNScan2[pmuj]);
                    }
                    //printf("\n");
                    
                    for ( pmuj=0; pmuj<3; pmuj++)
                    {                       
                        SNScan3[pmuj]=SNScan[pmuj+1];
                        //printf("SNScan3: %c, ", SNScan3[pmuj]);
                    }
                    
                    SNScan1[5]='\0';
                    //SNScan2[6]='\0';
                    SNScan2[SNLength2]='\0';
                    SNScan3[3]='\0';
                    //printf("\n");

                
                    
                    
                    
                    
                    
                    longHigh  = strtol(SNScan3, &endptr, 16);
                    Week_High = strtol(SNScan3, &endptr, 10);
                    //printf("\nlongHigh: %llX\n", longHigh);
                    longHigh = longHigh << 4;
                    //printf("longHigh: %llX\n", longHigh);             
                    
                    longLow  = strtol(SNScan1, &endptr, 16);
                    //printf("longLow: %llX\n", longLow);
                    
                    longLow = longLow << 4;
                    //printf("longLow: %llX\n", longLow);

                    // SN[0,1,2](48, 47, 46)
                    for ( pmuj=0; pmuj<3; pmuj++)
                    {                   
                        //a1 = (Byte) longHigh;
                        a2 = (UINT8) longLow;
                        SN[pmuj] = a2;
                        //SN[pmuj+3] = a1;
                        //printf("SN[%d]: %X\n", pmuj, SN[pmuj]);
                        //printf("SN[%d]: %X\n", pmuj+3, SN[pmuj+3]);
                        
                        //longHigh = longHigh >> 8;
                        longLow  = longLow >> 8;
                        //printf("longHigh: %llX, longHigh: %llX\n\n", longHigh, longLow);
                    }


                    // SN[5,6](43, 42)
                    for ( pmuj=5; pmuj<7; pmuj++)
                    {                   
                        //a1 = (Byte) longHigh;
                        a2 = (UINT8) longHigh;
                        SN[pmuj] = a2;
                        //SN[pmuj+3] = a1;
                        //printf("SN[%d]: %X\n", pmuj, SN[pmuj]);
                        //printf("SN[%d]: %X\n", pmuj+3, SN[pmuj+3]);
                        
                        //longHigh = longHigh >> 8;
                        longHigh  = longHigh >> 8;
                        //printf("longHigh: %llX, longHigh: %llX\n\n", longHigh, longLow);
                    }                   
                    
                    
                    
                    SN[3] = SNScan2[5];
                    SN[4] = SNScan2[4];
                    SN[7] = SNScan2[0];
                    
                    
                    // PMU Manufacturing Date
                    Year_39 = SN[7] - 0x2F;
                    Week_40 = Week_High / 7;
                    Week_40++;
                    //printf("Year: 0x%x, Week: %d\n", Year_39, Week_40);
                    //itoa(Week_40, SNScan4, 10);
                    sprintf(SNScan4, "%d", Week_40);
                    Week_40 = strtol(SNScan4, &endptr, 16);
                    printf("Year: 0x%x, Week: 0x%x\n", Year_39, Week_40);




                    printf("\nPlease scan the part number on PMU label: ");
                    //gets(PNScan);
                    fgets(PNScan, 23, stdin);
                    PNLength = strlen(PNScan) - 1;
                    //printf("PMU part number is: %s, length: %d\n", PNScan, PNLength);
                    
                    if ( PNLength < 8)
                    {
                        printf("\n The Part Number on Label is Wrong !!!\n");
                        
                        return -1;
                    }
                    
                    
                    for ( pmuj=0; pmuj<PNLength; pmuj++)
                    {
                        //PN[PNLength-pmuj-1]=PNScan[pmuj];
                        PN[pmuj]=PNScan[pmuj];
                        //printf("PN[%d]: %c\n", PNLength-pmuj-1, PN[PNLength-pmuj-1]);                 
                        //printf("PN[%d]: %c\n", pmuj, PN[pmuj]);                   
                    }
                    
                    
/*          
                    for ( pmuj=(PNLength-8), pmuk=0; pmuj<PNLength; pmuj++, pmuk++)
                    {
                        //PN[PNLength-pmuj-1]=PNScan[pmuj];
                        PN_file[pmuk]=PNScan[pmuj];
                        //printf("PN[%d]: %c\n", PNLength-pmuj-1, PN[PNLength-pmuj-1]);                 
                        //printf("PN[%d]: %c\n", pmuj, PN[pmuj]);                   
                    }                   
*/                  


                    PNScan[PNLength]='.';
                    PNScan[PNLength+1]='b';
                    PNScan[PNLength+2]='i';
                    PNScan[PNLength+3]='n';
                    
                    PNScan[PNLength+4]='\0';
                    //printf("PNScan is: %s\n", PNScan);

                    
                    strcat(FWpatch, PNScan);
                    //printf("\nPMU SPD update file is: %s\n", FWpatch);


    
                    int fd = open(FWpatch, O_RDONLY, 0644);
                    if (fd >= 0 )
                    {                       

                        pmuj = read(fd, buffer, 256);

                        printf("\nSelected PMU SPD file is: %s \n", FWpatch);           
            
                        //printf("Buffer is: \n");
                        for(kFW=0; kFW<256; kFW++)
                        {
                            //printf(" %X ", buffer[kFW]);
                        }
            
                        close(fd);
            
                    } else
                    {
                        printf("\nCannot open file: %s\n\n", FWpatch);
            
                        return -1;
                    }   
                    
                    

                    // clear the spd 
                    for ( pmuj=0; pmuj<75; pmuj++)
                    {
                        //NLPMUWriteRegA(i, 0x33+pmuj, PN[pmuj]);   
                        pmu_spd_access(0x00, pmuj , 0);                 
                    }
                    

                    
                    // Update PMU Manufacturing Date
                    //NLPMUWriteRegA(i, 39, Year_39);
                    pmu_spd_access(Year_39, 0x27, 0);
                    //NLPMUWriteRegA(i, 40, Week_40);
                    pmu_spd_access(Week_40, 0x28, 0);

                    
                    // Update SN
                    //NLPMUWriteRegA(i, 48, SN[0]);
                    pmu_spd_access(SN[0], 0x30, 0);
                    //NLPMUWriteRegA(i, 47, SN[1]);
                    pmu_spd_access(SN[1], 0x2F, 0);
                    //NLPMUWriteRegA(i, 46, SN[2]);
                    pmu_spd_access(SN[2], 0x2E, 0);
                    
                    //NLPMUWriteRegA(i, 45, SN[3]);
                    pmu_spd_access(SN[3], 0x2D, 0);
                    //NLPMUWriteRegA(i, 44, SN[4]);
                    pmu_spd_access(SN[4], 0x2C, 0);
                    
                    //NLPMUWriteRegA(i, 43, SN[5]);
                    pmu_spd_access(SN[5], 0x2B, 0);
                    //NLPMUWriteRegA(i, 42, SN[6]);
                    pmu_spd_access(SN[6], 0x2A, 0);
                    
                    //NLPMUWriteRegA(i, 41, SN[7]);
                    pmu_spd_access(SN[7], 0x29, 0);


                    // PMU Manufacturing Date
                    //TempByteA = NLPMUReadRegA(i, 39 );
                    TempByteA = pmu_spd_access(0x27, TempByteRead, 1);
                    //TempByteB = NLPMUReadRegA(i, 40 );
                    TempByteB = pmu_spd_access(0x28, TempByteRead, 1);
                    printf("\nPMU Manufacturing Date of Year: %x,  Week: %x\n", TempByteA, TempByteB);

/*                  
                    // PMU Serial Number
                    printf("\n[ %2d ] PMU NEW Serial Number: 0x", i);
                    //TempByteA = NLPMUReadRegA(i, 41 );
                    TempByteA = pmu_spd_access(0x29, TempByteRead, 1);
                    printf("%c", TempByteA);
                    
                    //TempByteA = NLPMUReadRegA(i, 42 );
                    TempByteA = pmu_spd_access(0x2A, TempByteRead, 1);
                    if( TempByteA < 0x0F ) printf("0");
                    printf("%x", TempByteA);

                    //TempByteA = NLPMUReadRegA(i, 43 );
                    TempByteA = pmu_spd_access(0x2B, TempByteRead, 1);
                    TempByteA = TempByteA >> 4;
                    printf("%X", TempByteA);                    
                    
                    //TempByteA = NLPMUReadRegA(i, 44 );
                    TempByteA = pmu_spd_access(0x2C, TempByteRead, 1);
                    printf("%c", TempByteA);
                    //TempByteA = NLPMUReadRegA(i, 45 );
                    TempByteA = pmu_spd_access(0x2D, TempByteRead, 1);
                    printf("%c", TempByteA);
                    
                    for ( pmuj=0; pmuj<2; pmuj++)
                    {                   
                        //TempByteA = NLPMUReadRegA(i, (46+pmuj) );
                        TempByteB = pmu_spd_access( (0x2E + pmuj), TempByteRead, 1);
                        if( TempByteA < 0x0F )
                        printf("0");
                        printf("%X", TempByteA);
                    }

                    //TempByteA = NLPMUReadRegA(i, 48 );
                    TempByteA = pmu_spd_access(0x30, TempByteRead, 1);
                    TempByteA = TempByteA >> 4;
                    printf("%X", TempByteA);
                                        
*/                  



                    // Update PN
                    for ( pmuj=0; pmuj<PNLength; pmuj++)
                    {
                        //NLPMUWriteRegA(i, 0x33+pmuj, PN[pmuj]);   
                        pmu_spd_access(PN[pmuj], (0x33 + pmuj) , 0);                    
                    }                       
                    
                    
                    // PMU Part Number
                    printf("\nPMU NEW Part Number: ");

                    for ( pmuj=0; pmuj<24; pmuj++)
                    {
                        //TempByteA = NLPMUReadRegA(i, 0x33+pmuj);
                        TempByteA = pmu_spd_access( (0x33 + pmuj),  TempByteRead, 1);
                        printf("%c", TempByteA);
                    }                   
                    
                    printf("\n");

                    
                    
                    for ( pmuj=0; pmuj<=0x26; pmuj++)
                    {
                        // Number of Bytes CRC coverage 
                        //NLPMUWriteRegA(i, 0, 0x11);
                        //NLPMUWriteRegA(i, pmuj, buffer[pmu]);
                        pmu_spd_access(buffer[pmuj],  pmuj, 0);
                        //printf("buffer[%d]: 0x%X\n", pmuj, buffer[pmuj]);
                        //TempByteA = pmu_spd_access( pmuj,  TempByteRead, 1);                  
                        //printf(" %d:  %X\n", pmuj, TempByteA);
                    }

                

                    // PMU Revision Number
                    //NLPMUWriteRegA(i, 75, buffer[75]);
                    pmu_spd_access(buffer[75],  0x4B, 0);
                    

                
                    
                    // Checksum: Byte: 0-9, 24-37, 75
                    TempByteA = 0;
                    TempWordA = 0;
                    
                    for ( pmuj=0; pmuj<=9; pmuj++)
                    {
                        //TempByteA = NLPMUReadRegA(i, 0x0+pmuj);   
                        TempByteA = pmu_spd_access( pmuj,  TempByteRead, 1);                    
                        TempWordA = TempWordA + TempByteA;
                        //printf(" %X _ %X,", TempByteA, TempWordA);
                    }

                    //printf("\n");
                    for ( pmuj=0x18; pmuj<=0x25; pmuj++)
                    {
                        //TempByteA = NLPMUReadRegA(i, 0x0+pmuj);
                        TempByteA = pmu_spd_access(pmuj, TempByteRead , 1);
                        TempWordA = TempWordA + TempByteA;
                        //printf(" %X _ %X,", TempByteA, TempWordA);
                    }
                    
                    // PMU Revision Number
                    //TempByteA = NLPMUReadRegA(i, 75);
                    TempByteA = pmu_spd_access(0x4B, TempByteRead , 1);
                    TempWordA = TempWordA + TempByteA;                  
                    //printf("Checksum is : 0x%X\n", TempWordA);
                    
                    //NLPMUWriteRegA(i, 49, (TempWordA & 0xFF) );
                    pmu_spd_access( (TempWordA & 0xFF), 0x31 , 0);
                    //NLPMUWriteRegA(i, 50, (TempWordA>>8) & 0xFF );
                    pmu_spd_access( ((TempWordA>>8) & 0xFF), 0x32 , 0);
                    
                    //TempByteA = NLPMUReadRegA(i, 49);
                    //TempByteA = pmu_spd_access(0x31, TempByteRead , 1);
                    //TempByteB = NLPMUReadRegA(i, 50);
                    //TempByteB = pmu_spd_access(0x32, TempByteRead , 1);
                    //printf("Read checksum is : 0x%X%X\n", TempByteB, TempByteA);


                    // Write done!!!
                    //NLPMUWriteRegA(i, 255, 'N');
                    pmu_spd_access('N', 0xFF , 0);




    return 0;

}

#endif

// TBD - add to the driver in the form of HW_ACCESS using I2C space.
UINT32 pmu_spd_access(UINT8 spd11, UINT8 spd22, UINT8 spdread)
{
    SINT32 rc;
    UINT64 value = UNINITIALIZED_VALUE; 

    UINT64 value_0x250 = UNINITIALIZED_VALUE;   
    UINT64 value_0x204 = UNINITIALIZED_VALUE;
    UINT32  pmuspdi;


    //printf("\nspd11: 0x%X,   spd22: 0x%X,   spdread: %d\n\n", spd11, spd22, spdread);


    //printf("value_0x250 is: 0x%x\n", value_0x250);

    usleep(1000*1);

    value = 0; 
    rc = aux_hw_access(MAR_AM, &value, 4, FALSE, 4, SPACE_REG, TRUE);
    usleep(1000*10);
    rc = aux_hw_access(MAR_AM, &value, 4, TRUE, 4, SPACE_REG, TRUE);
    //printf("value_0x250 is: 0x%x\n", value);



    // access is write  
    if( spdread != 1 )
    {
        // register
        value = spd22;
        rc = aux_hw_access(PMU_SPD_ADDRESS, &value, 4, FALSE, 4, SPACE_REG, TRUE);

        // data
        usleep(1000*10);
        value = spd11;
        rc = aux_hw_access(PMU_SPD_DATA, &value, 4, FALSE, 4, SPACE_REG, TRUE);
    }
    else
    // access is read
    {
        // register
        value = spd11;
        rc = aux_hw_access(PMU_SPD_ADDRESS, &value, 4, FALSE, 4, SPACE_REG, TRUE);
    }

    


    rc = aux_hw_access(CONTROL_REG, &value, 4, TRUE, 4, SPACE_REG, TRUE);
    if( spdread != 1 )
    {
        value |= (1<<9);
    }
    else
    {
        value &= ~(1<<9);   
    }
    rc = aux_hw_access(CONTROL_REG, &value, 4, FALSE, 4, SPACE_REG, TRUE);
    usleep(1000*5);




    // write 1 to reg 0x200.2
    rc = aux_hw_access(COMMAND_REG, &value, 4, TRUE, 4, SPACE_REG, TRUE);
    if (rc == 0)
    {
        //printf ("Register \t(0x%.3x) = 0x%8X\n", 0x200, (int)(value ));
        value = value | 0x04;
        
        usleep(1000*5);
        rc = aux_hw_access(COMMAND_REG, &value, 4, FALSE, 4, SPACE_REG, TRUE);
    }

    //usleep(1000*10);
    pmuspdi = 0;
    do
    {   
        usleep(1000*1);
        pmuspdi++;
        if (pmuspdi > 1000)
        {
            printf("Access PMU SPD timeout.\n");
            return -1;
        }
    rc = aux_hw_access(COMMAND_REG, &value, 4, TRUE, 4, SPACE_REG, TRUE);

    }   
    while( (value & 0x04) != 0);
    //printf("Wait pmu spd done used  %d x1ms.\n", pmuspdi);    


    // read data from spd
    if( spdread == 1 )
    {

        rc = aux_hw_access(PMU_SPD_DATA, &value, 4, TRUE, 4, SPACE_REG, TRUE);
        spd22 = (UINT8)value;

        //printf("spd22 is: 0x%X\n", spd22);

        usleep(1000*5);
        value = 0;
        rc = aux_hw_access(MAR_AM, &value, 4, FALSE, 4, SPACE_REG, TRUE);

        return  spd22;
        
    }

    usleep(1000*5);
    value = 0x0;
    rc = aux_hw_access(MAR_AM, &value, 4, FALSE, 4, SPACE_REG, TRUE);


    return 0;
}
        


// Next are support routines for testing the character driver operation

typedef uint32_t myint;
#define BUFSIZE (1024*1024)
//#define BUFSIZE (1024*1024*1024)
//static unsigned char write_buffer[BUFSIZE];
//static unsigned char read_buffer[BUFSIZE];
static unsigned char *write_buffer;
static unsigned char *read_buffer;


int nl_write(char *filename, unsigned char *buffer, int offset, ssize_t count)
{
    int fd;
    off_t file_pos = 0;

    fd = open( filename, O_WRONLY | O_NONBLOCK);
    if (fd < 0) perror("open");

    file_pos = lseek(fd, offset, SEEK_SET);

    count = write(fd, buffer, count);

    if (count<0)
    {
        printf("Write Error: count=%ld\n",count);
    }

    close(fd);
    return (count);
}

int nl_read(char *filename, unsigned char *buffer, int offset, ssize_t count)
{
    int fd;
    off_t file_pos = 0;

    fd = open( filename, O_RDONLY | O_NONBLOCK);
    if (fd < 0) perror("open");

    file_pos = lseek(fd, offset, SEEK_SET);

    count = read(fd, buffer, count);

    if (count<0)
    {
        printf("Read Error: count=%ld\n",count);
    }

    close(fd);
    return (count);
}

int nl_verify(ssize_t count, unsigned char *addr1, unsigned char *addr2)
{
    int i;
    int found_mismatch=FALSE;

    i=0;
    while((i<count) && (!found_mismatch))
    {
        if (addr1[i]!=addr2[i])
        {
            printf("Mismatch: i=%d=%x\n",i,i);
            found_mismatch=TRUE;
        }
        else
        {
            i++;
        }
    }

    return(i);
}

void nl_show_buffer(unsigned char *buffer, int offset, int count)
{
    int i;

    printf("\n");

    for (i=offset; i<(offset+count); i++)
    {
        if ((i%16)==0)
        {
            printf("\n %.8X : ",i);
        }
        
        printf("%.2X ",buffer[i]);        
    }
    printf("\n");
}


// Write/Read/Verify - Test character driver READ/WRITE interface using LARGE_IO_SIZE size bytes using 
// a changing DDR source and destination address and pattern

#define LARGE_IO_SIZE (128*1024)
#define DDR_SIZE (1024*1024*1024) // TBD - consolidate all the memory sizes.

int chr_driver_test(char *filename)
{
    int bytes_actual=0;
    int bytes_transferred=LARGE_IO_SIZE; // Starting value
    int i;
    int pattern=1;
    int pass=0; // 0 is success
    unsigned char *src_addr;
    unsigned char *dst_addr;

    write_buffer = (char *)aligned_malloc(BUFSIZE, ALIGN_VALUE);
    read_buffer = (char *)aligned_malloc(BUFSIZE, ALIGN_VALUE);

    src_addr = write_buffer;
    dst_addr = read_buffer;
    unsigned long long src_ddr_addr = 0;
    unsigned long long dst_ddr_addr = 0;
    
    i=bytes_transferred;
    printf("\nTest: Basic READ/WRITE test for character driver\n");
    while((src_addr<(write_buffer+BUFSIZE-LARGE_IO_SIZE)) 
            && (dst_addr<(read_buffer+BUFSIZE-LARGE_IO_SIZE)) 
            && (src_ddr_addr<(DDR_SIZE-LARGE_IO_SIZE)) 
            && (dst_ddr_addr<(DDR_SIZE-LARGE_IO_SIZE))
            && (pass==0))
    {    
        printf("src addr=%p dst addr=%p src ddr addr=%llx dst ddr addr=%llx pattern=%x size=%x ", 
                src_addr, dst_addr, src_ddr_addr, dst_ddr_addr, pattern&0xff, i);

        // Zero out the full buffer - this will take a while.
        memset(src_addr, 0, BUFSIZE);
        memset(dst_addr, 0, BUFSIZE);
        memset(src_addr, pattern, i); // Just fill the amount of pattern wanted

        bytes_actual = nl_write(filename, src_addr, dst_ddr_addr, i);
        if (bytes_actual != i)
        {
            printf("Error: write bytes requested=%x, actual=%x\n", i, bytes_actual);
        }

        bytes_actual = nl_read(filename, dst_addr, src_ddr_addr, i);
        if (bytes_actual != i)
        {
            printf("Error: read bytes requested=%x, actual=%x\n", i, bytes_actual);
        }

        bytes_actual = nl_verify(i, src_addr, dst_addr);
        if (bytes_actual != i)
        {
            printf("Error: data mismatch, verify bytes requested=%x, actual=%x\n", i, bytes_actual);
            nl_show_buffer(read_buffer, bytes_actual-16, 64);
            pass=1;
        }
        else
        {
            printf("PASS\n");
        }

        pattern++;
        // Do not allow a pattern of 0.
        if ((pattern&0xff)==0)
        {
            pattern++;
        }

        // Walk through the DDR using a new pattern each time.
        dst_ddr_addr+=LARGE_IO_SIZE;
        src_ddr_addr+=LARGE_IO_SIZE;
    }

    // Show last results
    //nl_show_buffer(src_addr, 0, 3000);
    //nl_show_buffer(dst_addr, 0, 3000);

    aligned_free(write_buffer);
    aligned_free(read_buffer);
    return(pass);
}

UINT32 EV_char_driver_test(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 retval=0;
    int test_status=0;
    // TBD - make sure that the device is a character device.
    test_status = chr_driver_test(dev_name);

    if (test_status==0)
    {
        retval = TEST_PASS;
        printf("PASS\n");
    }
    else
    {
        retval = TEST_FAIL;
        printf("FAIL\n");
    }

    return(retval);
}


UINT32 EV_nv(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;
    SINT32 rc;
    UINT64 reg_offset = 2;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_REG;
    UINT8 access_size = sizeof(int32_t);  // Use DWORD access only.
    float f, degreesC, degreesF, Vbb, Vab, tBU, Eb, Pb; 
    int last_backup_energy = 0;
    UINT64 error_value = 0;
    int state = NV_STATE_UNDEFINED;
    int is_special_release = FALSE; // FW 00.02.0E
    int is_legacy_release = FALSE;  // Before FW 00.03.00 and not 00.02.0E
    float version_number; // Used to test for greater, equal, or less than a particular version. 

    switch(form)
    {
        case FORM_HUMAN_READABLE:
            result = get_version_settings(&is_special_release, &is_legacy_release);

            if (result != TEST_COULD_NOT_EXECUTE)
            {
                // read NVDIMM register 2
                reg_offset = CONTROL_REG;
                rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);
                if (rc == 0)
                {
                    printf ("Control \t(0x%.3x) = 0x%04X\n", CONTROL_REG, (int)(value & 0xffff));
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if (result != TEST_COULD_NOT_EXECUTE)
            {
                reg_offset = STATUS_REG;
                rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

                if (rc == 0)
                {
                    printf ("Status \t\t(0x%.3x) = 0x%04X\n", STATUS_REG, (int)(value & 0xffff));
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if (result != TEST_COULD_NOT_EXECUTE)
            {
                rc = get_nvdimm_state(&state, FALSE);

                if (rc == 0)
                {
                    printf ("State     \t(0x%.3x) = 0x%04X      %s\n", STATE_REG, (int)(state & 0xffff), nv_state[state]);
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if (result != TEST_COULD_NOT_EXECUTE)
            {
                reg_offset = ERROR_REG;
                rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

                if (rc == 0)
                {
                    error_value = (value & 0xffff);
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if (result != TEST_COULD_NOT_EXECUTE)
            {
                reg_offset = ERROR_CODE_REG;
                rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

                if (rc == 0)
                {
                    error_value |= (value<<32);
                    printf ("Error     \t(0x%.3x) = 0x%012llX\n", ERROR_REG, error_value);
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if (result != TEST_COULD_NOT_EXECUTE)
            {
                if (is_legacy_release)
                {
                    reg_offset = 0x2a8;
                }
                else
                {
                    reg_offset = CAP_VOLTAGE;
                }
                rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

                if (rc == 0)
                {
                    f = (1.0 * (value & 0xffff))/1000.0;
                    printf ("PMU Voltage \t\t= %.3f V\n", f);
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if (result != TEST_COULD_NOT_EXECUTE)
            {
                if (is_legacy_release)
                {
                    reg_offset = 0x900;
                }
                else
                {
                    reg_offset = LAST_CAPACITANCE;
                }

                rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

                if (rc == 0)
                {
                    f = (1.0 * (value & 0xffff))/10.0;
                    printf ("PMU Capacitance \t= %.1f F\n", f);
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if ((result != TEST_COULD_NOT_EXECUTE) && (!is_special_release ) && (!is_legacy_release))
            {
                reg_offset = CURRENT_PMU_TEMP; 
                rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

                if (rc == 0)
                {
                    degreesC = (1.0 * (value & 0xffff));
                    degreesF = ((1.8 * degreesC) + 32.0);
                    printf ("PMU Temperature \t= %3.1f degC (%3.1f degF)\n", degreesC, degreesF);
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if (result != TEST_COULD_NOT_EXECUTE)
            {
                // Special release
                if (is_special_release)
                {
                    reg_offset = 0x414; 
                }
                else
                {
                    if (is_legacy_release)
                    {
                        reg_offset = 0xa00;
                    }
                    else
                    {
                        reg_offset = EV3_CARD_TEMP; 
                    }
                }

                rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

                if (rc == 0)
                {
                    degreesC = (1.0 * (value & 0xffff));
                    degreesF = ((1.8 * degreesC) + 32.0);
                    printf ("Card Temperature \t= %3.1f degC (%3.1f degF)\n", degreesC, degreesF);
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if ((result != TEST_COULD_NOT_EXECUTE) && (!is_legacy_release))
            {
                if (is_special_release)
                {
                    reg_offset = 0x610;
                }
                else
                {
                    reg_offset = EV3_FPGA_TEMP;
                }

                rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

                if (rc == 0)
                {
                    degreesC = (1.0 * (value & 0xffff));
                    degreesF = ((1.8 * degreesC) + 32.0);
                    printf ("FPGA Temperature \t= %3.1f degC (%3.1f degF)\n", degreesC, degreesF);
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if (result != TEST_COULD_NOT_EXECUTE)
            {
                if (is_legacy_release)
                {
                    reg_offset = 0x608;
                }
                else
                {
                    reg_offset = LAST_BACKUP_TIME;
                }

                rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

                if (rc == 0)
                {
                    f = (1.0 * (value & 0xffffff))/1000.0;
                    tBU = f;
                    printf ("Last Backup Time \t= %.3f s\n", f);
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if (result != TEST_COULD_NOT_EXECUTE)
            {
                if (is_legacy_release)
                {
                    reg_offset = 0x618;
                }
                else
                {
                    reg_offset = LAST_RESTORE_TIME;
                }

                rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

                if (rc == 0)
                {
                    f = (1.0 * (value & 0xffffff))/1000.0;
                    printf ("Last Restore Time \t= %.3f s\n", f);
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if (result != TEST_COULD_NOT_EXECUTE)
            {
                if (is_legacy_release)
                {
                    reg_offset = 0x710;
                }
                else
                {
                    reg_offset = LAST_CAP_VOLT_BEFORE_BACKUP;
                }
                rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

                if (rc == 0)
                {
                    f = (1.0 * (value & 0xffff))/1000.0;
                    Vbb = f;
                    printf ("Voltage Before Backup\t= %.3f V\n", f);
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if (result != TEST_COULD_NOT_EXECUTE)
            {
                if (is_legacy_release)
                {
                    reg_offset = 0x91c;
                }
                else
                {
                    reg_offset = CAPACITANCE_LAST_BACKUP_ENERGY;
                }
                rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

                if (rc == 0)
                {
                    last_backup_energy = (int)value;
                    Eb = (1.0 * last_backup_energy);
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if (result != TEST_COULD_NOT_EXECUTE)
            {
                if (is_legacy_release)
                {
                    reg_offset = 0x720;
                }
                else
                {
                    reg_offset = LAST_CAP_VOLT_AFTER_BACKUP;
                }
                rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

                if (rc == 0)
                {
                    f = (1.0 * (value & 0xffff))/1000.0;
                    Vab = f;
                    //Eb = ((Vbb*Vbb) - (Vab*Vab))/2.0;   // Not used
                    Pb = Eb / tBU;
                    printf ("Voltage After Backup\t= %.3f V\n", f);
                    printf ("Last Backup Energy\t= %d J\n", last_backup_energy);
                    printf ("Last Backup Power\t= %.2f W\n", Pb);
                }
                else
                {
                    result = TEST_COULD_NOT_EXECUTE;
                }
            }

            if (is_legacy_release)
            {
                reg_offset = 0x518;
            }
            else
            {
                reg_offset = BACKUP_COUNTET;
            }

            rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);
            if (rc == 0)
            {
                printf ("Backup Cycles       \t= %d\n", (int)(value & 0xffffffff));
            }
            else
            {
                result = TEST_COULD_NOT_EXECUTE;
            }

            printf("\n\n");
            break;
        case FORM_JSON:
            {
                // TBD - all commands could potentially return output formatted for different APIs. 
            }
            break;
        default:
            printf("Error: Unknown output format\n");
            break;
    }
    return(result);
}

#define PIO_BUF_SIZE (1024*1024*1024) 

// Keep - Just in case we need this.
#define barrier() asm volatile("":::"memory") // compiler barrier alone.


// Verify for a specific pattern of 16 bytes at pattern_address
#define vpattern_len 16

UINT32 verify_pattern_special(UINT32 idx, UINT32 num_vec, UINT32 size_in_kB,UINT64 pattern_address, char vpattern[vpattern_len], int valid_bytes)
{
    UINT32 result = TEST_PASS;
    struct timeval start;
    struct timeval end;
    double diff = 0.0;
    struct EvIoReq req;
    const int NUM_vec = num_vec;
    const uint64_t BUF_sz = size_in_kB * 1024;
    const uint64_t TOTAL_buf_sz = BUF_sz * NUM_vec;
    uint64_t mem_sz = get_mem_sz();
    uint64_t sz;
    uint64_t offset;
    struct SgVec *vec = (struct SgVec *) aligned_malloc(sizeof(struct SgVec) * NUM_vec, VECTOR_ALIGN_VALUE);
    int rc = 0;
    ev_buf_t ev_buf;
    ioctl_arg_t *u_arg;
    ioctl_arg_t *u_arg2;
    int isSynchronous = TRUE;
    char *rbuf = NULL;
    int i;
    int pattern_restored=FALSE;
    uint64_t bytes = 0;

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    u_arg2 = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));

    // Fill up verify buffer with one pattern
    char *vbuf = (char *)malloc(TOTAL_buf_sz);

    if (vbuf == NULL)
    {
        perror("verify buffer malloc");
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        for (offset = 0; offset < TOTAL_buf_sz; offset += sz) 
        {
            sz = min(sizeof(patterns[idx]), TOTAL_buf_sz - offset);
            memcpy(vbuf + offset, &patterns[idx], sz);
        }

        printf("Verify memory pattern %lx, started at %llx\n", patterns[idx], start_address_for_testing);

        rbuf = (char *)aligned_malloc(TOTAL_buf_sz, ALIGN_VALUE);
        if( rbuf == NULL)
        {
            perror("read buffer  malloc");
            result = TEST_COULD_NOT_EXECUTE;
        }

        offset = start_address_for_testing;

        // Put the special pattern into the proper buffer offset for now. After verification we will
        // fill those 16 bytes with the proper pattern again.
        for (i=0;i<valid_bytes;i++)
        {
            vbuf[pattern_address+i]=vpattern[i];
        }

        while (offset < mem_sz) 
        {
            int ii;
            uint64_t len;
            uint64_t verify_pos = offset;

            if (bytes > mem_sz/50) 
            {
                bytes = 0;
                printf(".");
                fflush(stdout);
            }

            uint64_t buf_used = 0;
            for (ii = 0; ii < NUM_vec; ii++) 
            {
                len = min((unsigned long)BUF_sz, mem_sz - offset);
                if (len == 0)
                    break;
                vec[ii].ram_base = rbuf + buf_used;
                vec[ii].dev_addr = offset;
                vec[ii].length = len;
                offset += len;
                bytes += len;
                buf_used += len;
            }

            if (buf_used > TOTAL_buf_sz)
            {
                printf("use out of buffer\n");
            }

            if (ii == 0)
            {
                printf("Number of vec can't be zero\n");
            }

            req.vec = vec;
            req.nvec = ii;
            req.status = 1;
            req.cookie = &req;

            ev_buf.type = SGIO;
            ev_buf.buf = (void *)&req;
            ev_buf.sync = isSynchronous;

            u_arg->ioctl_union.ev_buf = ev_buf;
            u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

            gettimeofday(&start, NULL);
            rc = ioctl(s32_evfd, EV_IOC_READ, (void *)u_arg);
            if ((rc < 0) || (u_arg->errnum != 0)) 
            {
                perror("ioctl(EV_IOC_READ)");
                result = TEST_COULD_NOT_EXECUTE;
            }
            else
            {
                while (1) 
                {
                    u_arg2->ioctl_union.ev_io_event_ioc.count = 1;
                    rc = ioctl(s32_evfd, EV_IOC_GET_IOEVENTS, (void *)u_arg2);
                    if ((rc < 0) || (u_arg2->errnum != 0))
                    {
                        perror("ioctl(EV_IOC_GET_IOEVENTS)");
                        result = TEST_FAIL;
                        continue;
                    }

                    if (u_arg2->ioctl_union.ev_io_event_ioc.count == 0) 
                    {
                        result = TEST_FAIL;
                    } 
                    else 
                    {
                        if(!(u_arg2->ioctl_union.ev_io_event_ioc.count == 1))
                        {
                            printf("More than one events received");
                            result = TEST_FAIL;
                            continue;
                        }
                        else
                        {
                            if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count -1].status == 0))
                            {
                                printf("Evram IO failed");
                                result = TEST_FAIL;
                            }
                            else
                            {
                                if(!(u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie == &req))
                                {
                                    printf("\nCookie does not match request cookie = %p  req = %p at index = %d", 
                                        u_arg2->ioctl_union.ev_io_event_ioc.events[u_arg2->ioctl_union.ev_io_event_ioc.count - 1].cookie, &req, 
                                        u_arg2->ioctl_union.ev_io_event_ioc.count - 1);
                                    result = TEST_FAIL;
                                }
                                else
                                {
                                    //printf("\nCookie matches request\n");
                                }
                            }
                        }

                        gettimeofday(&end, NULL);
                        diff += (((end.tv_sec - start.tv_sec) * 1000) + ((end.tv_usec - start.tv_usec)/1000.0));

                        // Verify pattern
                        if (memcmp(vbuf, rbuf, buf_used)) 
                        {
                            uint64_t ii, expected, found;
                            uint64_t error_count=0;
                            for (ii = 0; ii < buf_used; ii += 8, verify_pos += 8) 
                            {
                                expected = *((uint64_t *)(vbuf + ii));
                                found = *((uint64_t *)(rbuf + ii));
                                if (found != expected) 
                                {
                                    printf("Mismatch at offset 0x%lx: expect %lx found %lx\n",
                                             verify_pos, expected, found);
                                    result = TEST_FAIL;
                                    error_count++;
                                    if (error_count>32) 
                                    {
                                        printf("Too many errors to report\n");
                                        break;
                                    }
                                }
                            }
                            break;
                        } 
                        else 
                        {
                            if (!pattern_restored)
                            {
                                vbuf[pattern_address]=0xaa;
                                vbuf[pattern_address+1]=0xaa;
                                vbuf[pattern_address+2]=0xaa;
                                vbuf[pattern_address+3]=0xaa;
                                vbuf[pattern_address+4]=0xaa;
                                vbuf[pattern_address+5]=0xaa;
                                vbuf[pattern_address+6]=0xaa;
                                vbuf[pattern_address+7]=0xaa;
                                vbuf[pattern_address+8]=0xaa;
                                vbuf[pattern_address+9]=0xaa;
                                vbuf[pattern_address+10]=0xaa;
                                vbuf[pattern_address+11]=0xaa;
                                vbuf[pattern_address+12]=0xaa;
                                vbuf[pattern_address+13]=0xaa;
                                vbuf[pattern_address+14]=0xaa;
                                vbuf[pattern_address+15]=0xaa;
                                pattern_restored=TRUE;
                                printf("Pattern Restored\n");
                            }
                            break;
                        }
                    }
                }
            }
        }

        aligned_free(rbuf);
    }

    free(vbuf);
    aligned_free(vec);
    free(u_arg);
    free(u_arg2);

    printf("Done\n");
    printf("%lu bytes verified\n", mem_sz);
    printf("Time taken to Read: %10.5f milli sec\n", diff);
    return result;
}

// To set window index when needed.
static UINT32 set_window(int window)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    SINT32 rc;
    get_set_val_t get_set_val;
    ioctl_arg_t u_arg;

    printf("Setting memory window to 0x%x\n",window);

    memset(&get_set_val,0x00,sizeof(get_set_val_t));

    // WRITE
    get_set_val.is_read = FALSE;
    get_set_val.value = window;

    u_arg.ioctl_union.get_set_val = get_set_val;
    u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = ioctl(s32_evfd, EV_IOC_GET_SET_MEMORY_WINDOW, (void *)&u_arg);
    if ((rc < 0) || (u_arg.errnum != 0))
    {
        perror("ioctl(EV_IOC_GET_SET_MEMORY_WINDOW)");
        result = TEST_FAIL;
    }
    else
    {
        result = TEST_PASS;
    }

    return result;
}

// Some basic PIO integrity testing using naturally aligned direct access.
// We will check aligned PIO for all supported sizes, both write and read accesses are tested and verified
// using the DMA path and a specially designed verify function. 
// We are also using this test to validate and regress the IOCTL hw_access intgerface.
// MMAP is not used in this test.


static UINT32 pio_test_01(unsigned int window_size, unsigned int num_windows, int alignment_offset)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = FALSE;
    int space = SPACE_PIO;
    UINT8 access_size = 1;
    int status = 0;
    UINT64 start_address = 0;
    UINT64 counter1 = 0;
    UINT64 counter2 = 0;
    UINT64 expected_value = UNINITIALIZED_VALUE;
    unsigned char vpattern[vpattern_len];  // Pattern to expect
    int valid_bytes = 0;
    int i;

    printf("Subtest 1: PIO size and alignment test using IOCTL HW_ACCESS interface\n");
    // Check the PIO WRITES first
    // Use DMA to fill and verify the background patterns
    result = fill_pattern(0xa, 32, 64); 
    if (result == TEST_PASS)
    {
        result = verify_pattern(0xa, 32, 64);
        if (result == TEST_PASS)
        {
            value = 0;
            counter1 = 0;
            reg_offset = start_address = 0x400 + alignment_offset; 
            is_read = FALSE;

            // Zero out expected pattern
            for (i=0;i<vpattern_len;i++)
            {
                vpattern[i] = 0;
            }

            valid_bytes = 0;
#ifdef ALL_PIO_ACCESS_SIZES_SUPPORTED
            // Enable and test this once all access sizes are implemented in FPGA logic
            for (access_size=1;access_size<=8;access_size=access_size<<1)
#else
            for (access_size=4;access_size<=4;access_size=access_size<<1)
#endif
            {
                // Write the pattern into the verify pattern buffer
                for (i=0; i<access_size;i++)
                {
                    vpattern[i + valid_bytes] = (unsigned char)((value >> (8 * i))); 
                }
                valid_bytes += access_size; // Track number of bytes to verify

                // Write the unique value to a spacific address.
                // The HW ACCESS IOCTL takes care of the memory window.
                status = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
                if (status != 0)
                {
                    result = TEST_FAIL;
                }
                reg_offset=(start_address + (access_size<<1));
                value = 0;
                for (counter2=0;counter2<(access_size<<1);counter2++)
                {
                    counter1++;
                    value |= (counter1<<(counter2*8));
                }
            }                                        

            // We have an established pattern in the first 16 bytes from "start_address". We will read 
            // those 16 bytes using DMA to ensure proper endianess.    This is done inside 
            // verify_pattern_special.

            // Now verify the pattern and also that no other area in memory got mismatches from 
            // the original background pattern.
            if (result == TEST_PASS)
            {
                result = verify_pattern_special(0xa, 32, 64, start_address-start_address_for_testing, vpattern, valid_bytes);
            }
            
            // Now verify the 16 bytes using all the PIO read sizes.
            if (result == TEST_PASS)
            {
                counter1 = 0;
                reg_offset = start_address; 
                is_read = TRUE;
                expected_value = 0;

#ifdef ALL_PIO_ACCESS_SIZES_SUPPORTED
                // Enable and test this once all access sizes are implemented in FPGA logic
                for (access_size=1;access_size<=8;access_size=access_size<<1)
#else
                for (access_size=4;access_size<=4;access_size=access_size<<1)
#endif
                {
                    value = UNINITIALIZED_VALUE;
                    status = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, FALSE);
                    if (status != 0)
                    {
                        result = TEST_FAIL;
                    }
                    else
                    {
                        if (expected_value == value)
                        {
                            printf("PIO Read is good for size=%d, value=%llx\n", access_size, value);
                        }
                        else
                        {
                            printf("PIO Read failed for size=%d, value=%llx expected value=%llx\n", 
                                    access_size, value, expected_value);
                            result = TEST_FAIL;
                        }
                    }

                    reg_offset=(start_address + (access_size<<1));
                    expected_value = 0;
                    for (counter2=0;counter2<(access_size<<1);counter2++)
                    {
                        counter1++;
                        expected_value |= (counter1<<(counter2*8));
                    }
                }
            }
        }
    }

    return(result);
}

// Writes and verifies any sized PIO using pointer access - for now this test uses aligned accesses only.
// We will use a fixed pattern.
// This is used to check flow control for all access sizes.
#define PAT_02_A (0x44)
#define PAT_02_B (0x4444)
#define PAT_02_C (0x44444444)
#define PAT_02_D (0x4444444444444444ULL)

static UINT32 pio_test_02(unsigned int window_size, unsigned int num_windows, int write_access_size, int read_access_size)
{
    UINT32 retval=TEST_PASS;
    int i;
    int fd;
    size_t mem_sz = PIO_BUF_SIZE;
    volatile void volatile *mem;
    int num_errors=0;
    unsigned char b=0;         // Temp byte.
    unsigned short w=0;     // Temp word.
    unsigned int d=0;         // Temp dword.
    unsigned long long q=0; // Temp qword.
    int window=0;
    struct timeval start; // Measure performance
    struct timeval end;
    double diff = 0.0; // Time in milliseconds


    printf("Subtest 2: PIO flow control test using MMAP and pointers\n");

    mem_sz = window_size;
    // One window for now - TBD - make window capable.
    fd = open(dev_name, O_RDWR);
    mem = mmap(NULL, mem_sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

    printf("mmap address: %p mem size=0x%lx window_size=0x%x num_windows=0x%x write access size=%d read_access_size=%d\n", 
            mem, mem_sz, window_size, num_windows, write_access_size, read_access_size);
   
    window=0;
    do
    {
        printf("Memory Window #%d\n",window);

        if (num_windows>1)
        {
            retval = set_window(window);
        }

        if (retval == TEST_PASS)
        {
            i=0;
            printf("Writing pattern on card, mem_sz=%lx access_size=%d\n", mem_sz, read_access_size);

            gettimeofday(&start, NULL);
            switch(write_access_size)
            {
                case 1:
                    b = ((unsigned char*)mem)[0]; // Do a single read to show access works.
                    do
                    {
                        ((unsigned char*)mem)[i]=(unsigned char)PAT_02_A;
                        i++;
                    } while (i<(mem_sz/write_access_size));
                    break;
                case 2:
                    do
                    {
                        ((unsigned short*)mem)[i]=(unsigned short)PAT_02_B;
                        i++;
                    } while (i<(mem_sz/write_access_size));
                    break;
                case 4:
                    do
                    {
                        ((unsigned int*)mem)[i]=(unsigned int)PAT_02_C;
                        i++;
                    } while (i<(mem_sz/write_access_size));
                    break;
                case 8:
                    do
                    {
                        ((unsigned long long*)mem)[i]=(unsigned long long)PAT_02_D;
                        i++;
                    } while (i<(mem_sz/write_access_size));
                    break;
                default:
                    printf("Error: Invalid write access size %d\n", write_access_size);
                    retval=TEST_FAIL;
                    break;
            }
            gettimeofday(&end, NULL);

            // diff is in milliseconds
            diff = 0.0;
            diff += (((end.tv_sec - start.tv_sec) * 1000) + ((end.tv_usec - start.tv_usec)/1000.0));
            printf("WRITE performance for PIO of access size = %d is %6.3f microseconds per access\n", write_access_size, (1000.0 * diff)/(1.0*i));


            if (retval!=TEST_FAIL)
            {
                retval=TEST_PASS; // Now we can assume pass.
                printf("Verifying pattern from card, mem_sz=%lx access_size=%d\n", mem_sz, read_access_size);
                num_errors=0;
                i=0;

                gettimeofday(&start, NULL);
                switch(read_access_size)
                {
                    case 1:
                        do
                        {
                            b = ((unsigned char*)mem)[i];
                            if (b!=PAT_02_A)
                            {
                                printf("ERROR: Miscompare at card offset = %x, data is %x, should be %x\n",
                                        i, b, PAT_02_A);
                                num_errors++;            
                                retval=TEST_FAIL;
                            }
                            i++;
                        } while ((i<(mem_sz/read_access_size)) && (num_errors<MAX_ERRORS_REPORTED));
                        break;
                    case 2:
                        do
                        {
                            w = ((unsigned short*)mem)[i];
                            if (w!=PAT_02_B)
                            {
                                printf("ERROR: Miscompare at card offset = %x, data is %x, should be %x\n",
                                        i, b, PAT_02_B);
                                num_errors++;            
                                retval=TEST_FAIL;
                            }

                            i++;
                        } while ((i<(mem_sz/read_access_size)) && (num_errors<MAX_ERRORS_REPORTED));
                        break;
                    case 4:
                        do
                        {
                            d = ((unsigned int*)mem)[i];
                            if (d!=PAT_02_C)
                            {
                                printf("ERROR: Miscompare at card offset = %x, data is %x, should be %x\n",
                                        i, b, PAT_02_C);
                                num_errors++;            
                                retval=TEST_FAIL;
                            }
                            i++;
                        } while ((i<(mem_sz/read_access_size)) && (num_errors<MAX_ERRORS_REPORTED));
                        break;
                    case 8:
                        do
                        {
                            q = ((unsigned long long*)mem)[i];
                            if (q!=PAT_02_D)
                            {
                                printf("ERROR: Miscompare at card offset = %x, data is %x, should be %llx\n",
                                        i, b, PAT_02_D);
                                num_errors++;            
                                retval=TEST_FAIL;
                            }

                            i++;
                        } while ((i<(mem_sz/read_access_size)) && (num_errors<MAX_ERRORS_REPORTED));
                        break;
                    default:
                        printf("Error: Invalid read access size %d\n", read_access_size);
                        retval=TEST_FAIL;
                        break;
                }

                gettimeofday(&end, NULL);

                // diff is in milliseconds
                diff = 0.0;
                diff += (((end.tv_sec - start.tv_sec) * 1000) + ((end.tv_usec - start.tv_usec)/1000.0));
                printf("READ performance for PIO of access size = %d is %6.3f microseconds per access\n", read_access_size, (1000.0 * diff)/(1.0*i));


                if ((retval==TEST_FAIL) && (num_errors>=MAX_ERRORS_REPORTED))
                {
                    printf("Too many errors, verification has been stopped\n");
                }
            }
        }

        window++;
    } while ((window<num_windows) && (retval==TEST_PASS));

    close(fd);

    return(retval);
}

// Use this function when filling the card with a fixed byte pattern. We will use the simplest solution for now.
// A more sophisticated solution will use 64-bit writes whenever possible. At least to FPGA 4.34 Build 17 this is
// mandatory due to the card not accepting payloads greater than 12 bytes.
void * ev_memset(void *ptr, int value, size_t count)
{
    int i;
    unsigned char *addr;
    unsigned char val = (unsigned char)(value & 0xff);

    addr = ptr;
    
    for (i=0;i<count;i++)
    {
        *addr = val;              
        addr++;            
    }          

    return ptr;
}

#define PAT_03_A (0x55)
// Let's do some memcpy and check the flow control.
// buffer 1 is source data 1GB written using memcpy.
// buffer 2 is the data read back from the EXPRESSvault using memcpy.
static UINT32 pio_test_03(unsigned int window_size, unsigned int num_windows)
{
    UINT32 retval=TEST_COULD_NOT_EXECUTE;
    int i;
    int fd;
    size_t mem_sz = PIO_BUF_SIZE;
    void *buf1;
    void *buf2;
    volatile void volatile *mem;
    void *ptr=NULL;
    int total_miscompares=0;
    int status = 0;
    int num_errors = 0;
    unsigned int d = 0; 
    int window=0;

    printf("Subtest 3: PIO flow control test using memset and memcpy\n");
    mem_sz = window_size;

    /* Malloc an aligned page. */
    buf1 = malloc(PIO_BUF_SIZE);
    if ((((uintptr_t)buf1) % sizeof(myint)) != 0) 
    {
        printf("Not aligned!\n");
    }

    buf2 = malloc(PIO_BUF_SIZE);
    if ((((uintptr_t)buf2) % sizeof(myint)) != 0) 
    {
        printf("Not aligned!\n");
    }

    fd = open(dev_name, O_RDWR);

    mem = mmap(NULL, mem_sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

    printf("mmap address: %p mem size=0x%lx\n", mem, mem_sz);
   
    // Make sure that the data on the hardware is not left over from a previous test run.
    printf("Writing background patterns\n");
    ptr = memset(buf1, 0x33, PIO_BUF_SIZE);

    // We will scatter some values throughout the buffer to verify that the addressing is correct.
    // The first 256 bytes will have an incrementing pattern.
    printf("Writing incrementing bytes at 0x00..0xFF\n");
    for (i=0;i<0x100;i++)
    {
        ((unsigned char*)buf1)[i] = (unsigned char)(i&0xff);
    }

    printf("Writing incrementing integers powers of two addresses\n");
    // More scattered values at powers of two integer locations.
    for (i=1;i<(PIO_BUF_SIZE/sizeof(int));i=i<<1) 
    {
        ((unsigned int *)buf1)[i] = i;
    }

    printf("Done filling source buffer with pattern\n");
    printf(".");
    ptr = memset(buf2, 0xff, PIO_BUF_SIZE);
    printf(".\n");
    printf("Done filling destination buffer with a background pattern\n");

    // Fill the card using a tried and true method, then switch to memset and repeat.
    printf("\n");

    window=0;
    do
    {
        printf("Memory Window #%d\n",window);

        if (num_windows>1)
        {
            retval = set_window(window);
        }

        i=0;
        do
        {
            ((unsigned int*)mem)[i]=(unsigned int)PAT_02_C;
            i++;
        } while (i<(mem_sz/sizeof(unsigned int)));

        printf("Verifying the card background pattern\n");
        num_errors=0;
        i=0;
        retval=TEST_PASS;
        do
        {
            d = ((unsigned int*)mem)[i];
            if (d!=PAT_02_C)
            {
                printf("ERROR: Miscompare at card offset = %x, data is %x, should be %x\n",
                        i, d, PAT_02_C);
                num_errors++;            
                retval=TEST_FAIL;
            }

            i++;
        } while ((i<(mem_sz/sizeof(unsigned int))) && (num_errors<MAX_ERRORS_REPORTED));

        if ((retval==TEST_FAIL) && (num_errors>=MAX_ERRORS_REPORTED))
        {
            printf("Too many errors, verification has been stopped\n");
        }

        if (retval==TEST_PASS)
        {
            printf("Background pattern on card is verified\n");

            printf("\nFilling card with a new background pattern of %x using memset, mem_sz=%lx\n", PAT_03_A, mem_sz);

            // NOTE: Using memset on the card is not supported. The reason is that on some motherboards, the PCIe
            //       root complex will coalesce CPU IO cycles into larger 128-bit TLP MWr frames. These data payloads 
            //       4 double word (4 DW) in length and the maximum size supported is 3 DW. 3 DW will handle any 
            //       64-bit IO transaction using any alignment.
            ptr = ev_memset((void *)mem, PAT_03_A, mem_sz); // mem_sz could be smaller than the buffers.
            printf("\nVerifying background pattern on card, mem_sz=%lx\n", mem_sz);
            num_errors=0;
            i=0;

            do
            {
                if (((unsigned char*)mem)[i]!=PAT_03_A)
                {
                    printf("ERROR: Miscompare at card offset = %x, data is %x, should be %x\n",
                            i, ((unsigned char*)mem)[i], PAT_03_A);
                    num_errors++;            
                    retval=TEST_FAIL;
                }

                i++;
            } while ((i<mem_sz) && (num_errors<MAX_ERRORS_REPORTED));

            if (retval!=TEST_FAIL)
            {
                printf("Background pattern on card is verified\n");
            }

            printf("Writing pattern to card using memcpy\n");
            ptr = memcpy((void *)mem, buf1, mem_sz);
            printf("Reading pattern from card using memcpy\n");
            ptr = memcpy(buf2, (void *)mem, mem_sz);

            printf("Verifying memcpy'd pattern\n");
            status = memcmp(buf1,buf2, mem_sz);

            if (status != 0)
            {
                printf("ERROR: Miscompare detected - searching buffers for problem\n");
                retval = TEST_FAIL;
                // Lets display at least some of the failures

                i=0;
                num_errors=0;

                do 
                {
                    if ( ((unsigned char*)buf1)[i] != ((unsigned char*)buf2)[i])
                    {
                        printf("ERROR: Miscompare at source buffer byte offset = %x, data is %x, should be %x mem[%x]=%x\n",
                                i, ((unsigned char*)buf2)[i], ((unsigned char*)buf1)[i], i, ((unsigned char*)mem)[i]);
                        num_errors++;
                        if (num_errors>=MAX_ERRORS_REPORTED)
                        {
                            printf("Too many errors found, exiting verify loop\n");
                        }
                    }
                    i++;
                } while ((i<PIO_BUF_SIZE) && (num_errors<MAX_ERRORS_REPORTED));
            }
            else
            {
                retval = TEST_PASS;
            }

            if (total_miscompares != 0)
            {
                printf("Fail, total miscompares=%d\n",total_miscompares);
                retval = TEST_FAIL;
            }
        }

        window++;
    } while ((window<num_windows) && (retval==TEST_PASS));

    free(buf1);
    free(buf2);
    close(fd);

    return(retval);
}

// HW_ACCESS takes care of the window selection. Using this technique, treat it as a flat memory space.
// The starting sector is biased by any skip area that might exist. Use the logical sector number as reported
// by the block device disk partitioning utility.
static UINT32 pio_test_aux(unsigned int window_size, unsigned int num_windows, int test_access_size, UINT64 starting_sector, UINT64 num_sectors)
{
    UINT32 retval=TEST_COULD_NOT_EXECUTE;
    UINT64 i; // Used to track current test address
    int fd;
    size_t mem_sz = PIO_BUF_SIZE;
    char *buf1;
    char *buf2;
    char *mem_ptr = 0;
    volatile void volatile *mem;
    void *ptr=NULL;
    int total_miscompares=0;
    int status = 0;
    int num_errors = 0;
    int window=0;
    UINT64 remaining_sz = 0; // Amount of memory left to be tested. 
    UINT64 reg_offset; // Logical offset into the PIO memory.
    UINT64 reg_offset_tmp; // Used to set the reg_offset during the read portion
    UINT64 window_offset; // Physical offset within the current window
    UINT64 start_address = 0;
    UINT64 last_sector_address = 0;
    UINT64 last_window_address = 0;
    UINT64 buf_size = 0;
    UINT64 buf_index = 0; // Index within the allocated buffer;
    int iteration = 0; // The buffer is smaller than the total memory, multiple iterations are needed to test all of memory.

    remaining_sz = (num_sectors * EV_HARDSECT);

    last_sector_address = start_address + ((num_sectors-1) * EV_HARDSECT); // Starting address of the last sector
    last_window_address = window_size-1;

    printf("Partitioned PIO test - using memset/memcpy (from %llx to %llx) Start sector=%lld Number of sectors=%lld\n", 
            start_address, last_sector_address+EV_HARDSECT-1, starting_sector, num_sectors);

    mem_sz = window_size;

    /* Malloc an aligned page. */

    buf_size = PIO_BUF_SIZE;
    buf1 = malloc(buf_size);
    if ((((uintptr_t)buf1) % sizeof(myint)) != 0) 
    {
        printf("Not aligned!\n");
    }

    buf2 = malloc(buf_size);
    if ((((uintptr_t)buf2) % sizeof(myint)) != 0) 
    {
        printf("Not aligned!\n");
    }

    fd = open(dev_name, O_RDWR);

    mem = mmap(NULL, mem_sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

    printf("mmap address: %p mem size=0x%lx\n", mem, mem_sz);
   
    printf("reg_offset=%llx\n", reg_offset);

    // The total amount of memory to test is larger that the buffer size. 
    // Test as much as possible using the buffer size we have before moving then reuse the local buffers
    // for the next iteration.

    iteration = 0;
    window=0;
    reg_offset = start_address = start_address_for_testing + (starting_sector * EV_HARDSECT); // Start address for testing.

    do
    {
        // Make sure that the data on the hardware is not left over from a previous test run.
        printf("Writing background patterns in local buffers\n");
        ptr = memset(buf1, 0x33, buf_size); // Source buffer
        ptr = memset(buf2, 0xff, buf_size); // Destination buffer

        // WRITE portion
        buf_index = 0;
        reg_offset_tmp = reg_offset; // Ssve the starting value of reg_offset for the READ portion

        do
        {
            window_offset = (reg_offset % window_size);
            // Set the sliding window if there is one. 
            if (num_windows>1)
            {
                window = reg_offset/window_size;
                retval = set_window(window);
            }

            // Calculate size of the memcpy for this window but staying within the specified sectors
            mem_sz = window_size - window_offset;
            if (mem_sz > remaining_sz)
            {
                mem_sz = remaining_sz;
            }

            printf("Memory Window #%d Writing 0x%lx bytes starting at 0x%llx, logical offset is 0x%llx\n",
                    window, mem_sz, window_offset, reg_offset);

            if (retval!=TEST_FAIL)
            {
                mem_ptr = (char *)mem+window_offset;
                printf("Writing pattern to card using memcpy\n");
                ptr = memcpy((void *)mem_ptr, &buf1[buf_index], mem_sz);
            }

            buf_index += mem_sz;
            reg_offset += mem_sz;
            remaining_sz -= mem_sz;
            printf ("buf_index=0x%llx mem_sz=0x%lx buf_size=0x%llx reg_offset=0x%llx\n", buf_index, mem_sz, buf_size, reg_offset);
        } while (((buf_index + mem_sz) < buf_size) && (remaining_sz > 0) && (retval!=TEST_FAIL));


        // Read
        buf_index = 0;
        reg_offset = reg_offset_tmp;
        remaining_sz = (num_sectors * EV_HARDSECT);

        do
        {
            window_offset = (reg_offset % window_size);

            if (num_windows>1)
            {
                window = reg_offset/window_size;
                retval = set_window(window);
            }

            // Calculate size of the memcpy for this window but staying within the specified sectors
            mem_sz = window_size - window_offset;
            if (mem_sz > remaining_sz)
            {
                mem_sz = remaining_sz;
            }

            printf("Memory Window #%d Writing 0x%lx bytes starting at 0x%llx, logical offset is 0x%llx\n",
                    window, mem_sz, window_offset, reg_offset);

            if (retval!=TEST_FAIL)
            {
                mem_ptr = (char *)mem+window_offset;
                printf("Reading pattern from card using memcpy\n");
                ptr = memcpy(&buf2[buf_index], (void *)mem_ptr, mem_sz);
            }

            buf_index += mem_sz;
            reg_offset += mem_sz;
            remaining_sz -= mem_sz;

        } while (((buf_index + mem_sz) < buf_size) && (remaining_sz > 0) && (retval!=TEST_FAIL));

        // Verify
        reg_offset = reg_offset_tmp;

        // The buffers should match
        printf("Verifying memcpy'd pattern\n");
        printf("reg_offset=0x%llx\n", reg_offset);
        printf("buf1=%p buf2=%p buf1_start=%p buf2_start=%p\n", 
                buf1, buf2, (char *)&buf1[reg_offset], (char *)&buf2[reg_offset]);

        status = memcmp((char *)&buf1[reg_offset], (char *)&buf2[reg_offset], remaining_sz);

        if (status != 0)
        {
            printf("ERROR: Miscompare detected - searching buffers for problem\n");
            retval = TEST_FAIL;

            // Lets display at least some of the failures
            i=0;
            num_errors=0;

            do 
            {
                if ( ((unsigned char*)buf1)[i] != ((unsigned char*)buf2)[i])
                {
                    printf("ERROR: Miscompare at source buffer byte offset = %llx, data is %x, should be %x mem[%llx]=%x DDR address = 0x%llx\n",
                            i, ((unsigned char*)buf2)[i], ((unsigned char*)buf1)[i], i, ((unsigned char*)mem)[i], reg_offset_tmp+i);
                    num_errors++;
                    if (num_errors>=MAX_ERRORS_REPORTED)
                    {
                        printf("Too many errors found, exiting verify loop\n");
                    }
                }
                i++;
            } while ((i<buf_size) && (num_errors<MAX_ERRORS_REPORTED));
        }
        else
        {
            retval = TEST_PASS;
        }

        iteration++;
        printf("Remaining size = 0x%llx\n", remaining_sz);
    } while ((remaining_sz > 0) && (num_errors<MAX_ERRORS_REPORTED));

    if (total_miscompares != 0)
    {
        printf("Fail, total miscompares=%d\n",total_miscompares);
        retval = TEST_FAIL;
    }

    free(buf1);
    free(buf2);
    close(fd);

    return(retval);
}

static UINT32 pio_test_04(unsigned long long window_size, unsigned int num_windows, int access_size)
{
    UINT32 result=TEST_COULD_NOT_EXECUTE;
    UINT64 num_sectors=0;

    printf("Subtest 4: Memory Window pattern test\n");

    // Convert to sector numbers. If skip_sectors is used we lose some multiple of sectors due to skip area and have to subtract those sectors.
    num_sectors = ((window_size * num_windows)/EV_HARDSECT) - ev_skip_sectors;
    
    result = pio_test_aux(window_size, num_windows, access_size, 0, num_sectors);

    return(result);
}


// This next test is intended to be used for concurrent DMA/PIO. This requires the user to enter the starting
// sector or block (LBA) and the number of sectors to test. The memset and memcpy will be used within that 
// test range. The source and destination buffers wll be the size of the full EV range.
// The pattern used will be an incrementing quad-word pattern.
// Let's do some memcpy and check the flow control.
// buffer 1 is source data 1GB written using memcpy.
// buffer 2 is the data read back from the EXPRESSvault using memcpy.
static UINT32 pio_test_05(unsigned int window_size, unsigned int num_windows, int access_size, 
                          unsigned int starting_sector, unsigned int num_sectors)
{
    UINT32 result=TEST_COULD_NOT_EXECUTE;
    size_t mem_sz = PIO_BUF_SIZE;
    int first_sector=starting_sector;
    int last_sector=first_sector+num_sectors-1;

    printf("Subtest 5: Partitioned Memory PIO test, first sector=%d sector count=%d last sector=%d\n",
            first_sector, num_sectors, last_sector);
    mem_sz = window_size;

    // The test already takes skip sectors into account.
    result = pio_test_aux(window_size, num_windows, access_size, first_sector, num_sectors);

    return(result);
}

UINT32 EV_pio_test(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_COULD_NOT_EXECUTE;
    char str[MAX_STR];
    char *endptr=str;
    int sub_test = 0; // 0 means run all and is the default.
    unsigned long long size_in_bytes = ev_size_in_bytes; // Get the size from the global value. 
    unsigned long long window_size = EV_SIZE_16GB;  // Default: This will be adjusted later using the driver data.
    unsigned int num_windows = 1;
    int run_all_tests = FALSE;
    int i;
    int start_sector = ev_skip_sectors;
    int end_sector = (ev_size_in_bytes/EV_HARDSECT)-ev_skip_sectors-1;
    int num_sectors = end_sector - start_sector + 1;

    ioctl_arg_t *u_arg;
    SINT32 rc;
    ver_info_t version_info;

    switch(argc)
    {
        case 1:
            // No parameters means to run all tests
            run_all_tests = TRUE;
            result = TEST_FAIL;     // Use FAIL as a marker to proceed to testing.
            sub_test = 0; // Run all
            break;
        case 2:
            // Always hex
            sub_test = strtoul(argv[1], &endptr, 16);
            // Check to make sure the entire string was parsed
            if (strlen(argv[1]) != (endptr-argv[1]))
            {
                printf("Input error: argument not valid <%s>\n", argv[1]);
                 result = TEST_COULD_NOT_EXECUTE;
            }
            else
            {
                // Use FAIL as a marker to proceed to testing.
                result = TEST_FAIL;     
            }
            break;
        case 4:
            // Maybe this is a test that requires additional parameters.
            // Always hex
            sub_test = strtoul(argv[1], &endptr, 16);
            if (sub_test == 5)
            {
            // Override the defaults, which is the entire space.
            // This is in decimal for ease of use
            start_sector = strtoul(argv[2], &endptr, 10);
            num_sectors = strtoul(argv[3], &endptr, 10);
            end_sector = start_sector + num_sectors;
            printf("Start sector = %d End sector = %d Num sectors = %d\n",
                    start_sector, end_sector, num_sectors);
            // We are using FAIL as a marker to proceed to testing.
            result = TEST_FAIL;     
            }
            else
            {
                printf("Input error: argument not valid <%s> sub_test=%d\n", argv[1],sub_test);
                 result = TEST_COULD_NOT_EXECUTE;
            }
            break;
        default:
            printf("Input error: Number of arguments is invalid, argc=%d\n", argc);
             result = TEST_COULD_NOT_EXECUTE;
            break;
    }

    if (result == TEST_FAIL) // In this case TEST_FAIL is used as a marker saying we can go on.
    {
        // First get the card version and determine if a windowing mechanism is needed.

        u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
        
        version_info.driver_rev = 0xFF;
        version_info.driver_ver = 0xFF;
        version_info.fpga_rev = 0xFF;
        version_info.fpga_ver = 0xFF;
        sprintf(version_info.extra_info," ");

        u_arg->ioctl_union.ver_info = version_info;
        u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;
        rc = ioctl(s32_evfd, EV_IOC_GET_VERSION, (void *) u_arg);
        if (rc < 0) 
        {
            perror("ioctl(EV_IOC_GET_MODEL)");
            result = TEST_COULD_NOT_EXECUTE;
        }
        else
        {
            if (u_arg->errnum)
            {
                printf("Error getting Data from Driver : Err No : %d\n", u_arg->errnum);
            }
            else
            {
                version_info = u_arg->ioctl_union.ver_info; // Get the response

                // Currently only one configuration is supported.
                if (version_info.fpga_configuration == 2)
                {
                    num_windows = size_in_bytes/WINDOW_SIZE_CONFIG2;  
                    window_size = WINDOW_SIZE_CONFIG2;
                }
                result = TEST_PASS;
            }
        }    
        
        free(u_arg);

        // Run the test(s). If no parameter is passed, then run all subtests.
        printf("Window size =0x%llx Number of Windows = 0x%x\n", window_size, num_windows);
        switch(sub_test)
        {
            case 0:        // When in doubt run all tests. Fall through to the next case.
            case 1:        // PIO all alignments
                result = TEST_PASS; // 0

                // Check all alignments to 16
#ifdef ALL_PIO_ACCESS_ALIGNMENTS_SUPPORTED
                for (i=0;i<16;i++)
                {
                    result |= pio_test_01(window_size, num_windows, i);
                }
#else
                for (i=0;i<1;i++)
                {
                    result |= pio_test_01(window_size, num_windows, i);
                }
#endif
                if (!run_all_tests)
                break;
            case 2:        // Aligned PIO flow control using pointers
#ifdef ALL_PIO_ACCESS_SIZES_SUPPORTED
                result = pio_test_02(window_size, num_windows,1,1);
                if (result==TEST_PASS)
                {
                    result = pio_test_02(window_size, num_windows,2,2);
                    if (result==TEST_PASS)                       
                    {
                        result = pio_test_02(window_size, num_windows,4,4);
                        if (result==TEST_PASS)
                        {
                            result = pio_test_02(window_size, num_windows,8,8);
                        }
                    }
                }
#else
                result = pio_test_02(window_size, num_windows,4,4);
#endif
                if (!run_all_tests)
                break;
            case 3:     // Flow control - use memcpy
#ifdef MEMSET_MEMCMP_MEMCPY_ACCESS_SUPPORTED
                result = pio_test_03(window_size, num_windows);
#else
                /// Do nothing - drop through to next text if implemented
#endif
                if (!run_all_tests)
                break;
            case 4:     // Memory Window pattern
#ifdef MEMSET_MEMCMP_MEMCPY_ACCESS_SUPPORTED
#ifdef ALL_PIO_ACCESS_SIZES_SUPPORTED
                result = pio_test_04(window_size, num_windows,4);  // Only 32-bit access is supported currently
#else
                result = pio_test_04(window_size, num_windows,1);
#endif
#else
                /// Do nothing - drop through to next text if implemented
#endif
                if (!run_all_tests)
                break;
            case 5:     // Memory Window pattern using sectors - access size is 1 byte as the worst case.
#ifdef MEMSET_MEMCMP_MEMCPY_ACCESS_SUPPORTED
#ifdef ALL_PIO_ACCESS_SIZES_SUPPORTED
                result = pio_test_05(window_size, num_windows,4,start_sector, num_sectors);  // Only 32-bit access is supported currently
#else
                result = pio_test_05(window_size, num_windows,1,start_sector, num_sectors);
#endif
#else
                /// Do nothing - drop through to next text if implemented
#endif
                break; // Don't fall through
            default:
                 result = TEST_COULD_NOT_EXECUTE;
                break;
        }
    }

    return result;
}


/* align_size has to be a power of two !! */
void *aligned_malloc(size_t size, size_t align_size) 
{
    char *ptr,*ptr2,*aligned_ptr;
    int align_mask = align_size - 1;

    ptr=(char *)malloc(size + align_size + sizeof(int));
    if(ptr==NULL) 
        return(NULL);

    ptr2 = ptr + sizeof(int);
    aligned_ptr = ptr2 + (align_size - ((size_t)ptr2 & align_mask));

    ptr2 = aligned_ptr - sizeof(int);
    *((int *)ptr2)=(int)(aligned_ptr - ptr);

    return(aligned_ptr);
}

void aligned_free(void *ptr) 
{
    int *ptr2=(int *)ptr - 1;
    ptr -= *ptr2;
    free(ptr);
}

// This is a failure of some sort
int get_test_result(int errnum)
{
    int result = TEST_FAIL;

    printf("Error: Driver errnum=%d\n", errnum);
    switch(errnum)
    {
        case IOCTL_ERR_SUCCESS:
            result = TEST_PASS; // We do not expect this to be the case
            break;
        case IOCTL_ERR_GET_MODEL:
            break;
        case IOCTL_ERR_GET_MSZKB:
            break;
        case IOCTL_ERR_GET_FLASH_DATA_STATUS:
            break;
        case IOCTL_ERR_GET_BACKUP_STATUS:
            break;
        case IOCTL_ERR_GET_RESTORE_STATUS:
            break;
        case IOCTL_ERR_GET_FLASH_ERASE_STATUS:
            break;
        case IOCTL_ERR_GET_CAP_CHARGE_STATUS:
            break;
        case IOCTL_ERR_SET_ERASE_FLASH_BIT:
            break;
        case IOCTL_ERR_SET_ENABLE_AUTO_SAVE_BIT:
            break;
        case IOCTL_ERR_SET_FORCE_RESTORE_BIT:
            break;
        case IOCTL_ERR_GET_FPGA_STATUS:
            break;
        case IOCTL_ERR_WRITE_FAIL:
            break;
        case IOCTL_ERR_READ_FAIL:
            break;
        case IOCTL_ERR_RESTORE_CORRUPTED:
            printf("Error: Restore data is corrupted or flash is erased\n");
            break;
        case IOCTL_ERR_TIMEOUT:
            printf("Error: Timeout expired\n");
            break;
        case IOCTL_ERR_COMMAND_CANNOT_EXECUTE:
            printf("Error: Command cannot execute due to card state\n");
            result = TEST_COULD_NOT_EXECUTE;
            break;
        case IOCTL_ERR_COMMAND_NOT_SUPPORTED:
            printf("Error: Command does not exist\n");
            result = TEST_COMMAND_DOES_NOT_EXIST;
            break;
        default:
            break;
    }

    return (result);
}


