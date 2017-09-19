#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>

#define APP_REV 1
#define APP_VER 14
#define APP_DATE "01/02/2017"

typedef unsigned long long UINT64;
typedef unsigned int UINT32;
typedef int SINT32;
typedef unsigned char UINT8;
typedef char SINT8;
typedef void VOID;
typedef void* PVOID;

#if !defined(TRUE)
#define TRUE (1==1)
#define FALSE (!TRUE)
#endif

#define STR_LEN (255)

// Mostly used during scripting use of evutil
// Exit Status
#define TEST_PASS 0
#define TEST_FAIL 1
#define TEST_COULD_NOT_EXECUTE 2
#define TEST_DEVICE_DOES_NOT_EXIST 3
#define TEST_COMMAND_DOES_NOT_EXIST 4

#define UNINITIALIZED_VALUE 0xBB /* This value needs to fit in a byte */

// This is to get the local CPU frequency, used in performance calculations.
// More fields may be added later if needed.
typedef struct CpuInfo 
{	
	float freq;	
} CpuInfo_t;

typedef enum 
{
    FORM_HUMAN_READABLE, 
    FORM_JSON,      
} output_format;

extern char *current_fpga_image[3];

static int get_cpuinfo(struct CpuInfo *info);
UINT32 evshell();
UINT32 evscript(SINT32 argc, SINT8 **argv);
UINT32 evjson(SINT32 argc, SINT8 **argv);
UINT32 invokeCommand(SINT8 *str);
UINT32 processCommand(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 get_skip_sectors(int *skip_sectors, UINT64 *start_address);
UINT32 DisplayHelp(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_set(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_get_model(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 get_size(UINT64 *card_size_in_bytes);
UINT32 EV_get_size(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_get_capacitor_charge_status(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_force_save(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_force_restore(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_arm_nv(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_disarm_nv(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_get_fpga_status(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_fill_pattern(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 fill_pattern(UINT32, UINT32, UINT32);
UINT32 EV_send_sgl_interrupt(SINT32 argc, SINT8 *argv[], output_format form);
VOID exit_program();
UINT64 get_mem_sz(VOID);
UINT32 EV_verify_pattern(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 verify_pattern(UINT32, UINT32, UINT32);
UINT32 EV_fill_pattern_async(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 fill_pattern_async(UINT32, UINT32, UINT32);
UINT32 EV_save_evram(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_load_evram(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 load_evram(const char *fname, UINT32 num_vec, uint64_t buffer_size);
UINT32 save_evram(const char *fname, uint64_t size_in_bytes, UINT32 num_vec, uint64_t buffer_size);
UINT32 EV_verify_user_pattern(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 verify_user_pattern(uint64_t pattern, uint64_t, uint64_t, UINT32, uint64_t);
UINT32 EV_fill_user_pattern(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 fill_user_pattern(uint64_t pattern, uint64_t, uint64_t, UINT32, uint64_t);
UINT32 EV_fill_inc_pattern(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 fill_inc_pattern(uint64_t, uint64_t, UINT32, uint64_t);
UINT32 EV_verify_inc_pattern(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 verify_inc_pattern(uint64_t, uint64_t, UINT32, uint64_t);
UINT32 EV_get_version(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_program_fpga(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 program_fpga(const char *fname, int force_program);
UINT32 EV_rb(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_rw(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_rd(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_rq(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_wb(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_ww(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_wd(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_wq(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_dr(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_crb(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_crw(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_crd(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_cwb(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_cww(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_cwd(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_dc(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_prb(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_prw(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_prd(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_prq(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_pwb(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_pww(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_pwd(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_pwq(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_dp(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_irb(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_iwb(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_di(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 aux_hw_access(UINT64 reg_offset, UINT64 *value, UINT64 length, int is_read, UINT8 access_size, int space, int silent);
UINT32 EV_beacon(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_passcode(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_chip_reset(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_dbg_log_state(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_reset_stats(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_get_perf_stats(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_max_dmas(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_max_descriptors(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_memory_window(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_ecc_reset(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_ecc_status(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_ecc_sbe(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_ecc_mbe(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_mmap_test(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_char_driver_test(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_pio_test(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_program_id(SINT32 argc, SINT8 *argv[], output_format form);
int get_test_result(int errnum);
UINT32 EV_nv(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_enable_stats(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_card_ready(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_pmu_read(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_pmu_write(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_pmu_info(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_pmu_update(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 pmu_spd_access(UINT8 spdreg, UINT8 spdvalue, UINT8 spdread);
UINT32 EV_write_access(SINT32 argc, SINT8 *argv[], output_format form);



