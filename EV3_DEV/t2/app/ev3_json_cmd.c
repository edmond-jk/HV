/* Netlist Copyright 2015, All Rights Reserved */
#include "ev3util.h"
#include "../netlist_ev3_ioctl.h"
#include "ev3_json.h"
#include "ev3_json_cmd.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h> // mmap
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

// External globals
extern SINT32 s32_evfd;
extern SINT8 dev_name[32];

// JSON Support routines and commands
#define MAX_UNIT_STR 64

typedef void(*decfn)(int value, char *output_str);

typedef struct reg_value_info 
{
    char value_name[MAX_UNIT_STR];
    char unit_str[MAX_UNIT_STR];
    int reg_offset; // offset from base
    int bit_msb;    // defines mask
    int bit_lsb;    // defines mask
    decfn decoder_function; // Used to decode and generate special case strings
} reg_value_info;

// Decoder functions next
static void decode_lifetime_estimate_percent(int value, char *output_str)
{
    int percent = 0;

    output_str[0] = '\0'; // Initialize

    if ((value >= 1) && (value <= 10))
    {
        percent = (value - 1) * 10;

        sprintf(output_str,"%d%% - %d%% device life time used", percent, percent + 10);
    }
    else
    {
        if (value == 11)
        {
            // Exceeded maximum lifetime
            sprintf(output_str,"Exceeded its maximum estimated device life time");
        }
        else
        {
            if (value == 0)
            {
                // Undefined
                sprintf(output_str,"Not defined");
            }
            else
            {
                // Reserved
                sprintf(output_str,"Reserved");
            }
        }
    }
}

static void decode_flip_enable_disable(int value, char *output_str)
{
    output_str[0] = '\0'; // Initialize

    if (value == 0)
    {
        sprintf(output_str,"Enabled");
    }
    else
    {
        if (value == 1)
        {
            sprintf(output_str,"Disabled");
        }
    }
}

static void decode_enable_disable(int value, char *output_str)
{
    output_str[0] = '\0'; // Initialize

    if (value == 0)
    {
        sprintf(output_str,"Disabled");
    }
    else
    {
        if (value == 1)
        {
            sprintf(output_str,"Enabled");
        }
    }
}

static void decode_active_not_active(int value, char *output_str)
{
    output_str[0] = '\0'; // Initialize

    if (value == 0)
    {
        sprintf(output_str,"Not Active");
    }
    else
    {
        if (value == 1)
        {
            sprintf(output_str,"Active");
        }
    }
}

static void decode_ready_not_ready(int value, char *output_str)
{
    output_str[0] = '\0'; // Initialize

    if (value == 0)
    {
        sprintf(output_str,"Not Ready");
    }
    else
    {
        if (value == 1)
        {
            sprintf(output_str,"Ready");
        }
    }
}

static void decode_flip_present_not_present(int value, char *output_str)
{
    output_str[0] = '\0'; // Initialize

    if (value == 0)
    {
        sprintf(output_str,"Present");
    }
    else
    {
        if (value == 1)
        {
            sprintf(output_str,"Not Present");
        }
    }
}

static void decode_flash_flags(int value, char *output_str)
{
    output_str[0] = '\0'; // Initialize

    switch (value)
    {
        case 0:
            sprintf(output_str,"Reserved");
            break;
        case 1:
            sprintf(output_str,"Flash Empty");
            break;
        case 2:
            sprintf(output_str,"Backup Failed");
            break;
        case 3:
            sprintf(output_str,"Flash Full");
            break;
        default:
            break;
    }
}

static void decode_controller_state(int value, char *output_str)
{
    output_str[0] = '\0'; // Initialize

    switch(value)
    {
        case 0:
            sprintf(output_str,"INIT");
            break;
        case 4:
            sprintf(output_str,"DISARMED");
            break;
        case 6:
            sprintf(output_str,"RESTORE");
            break;
        case 10:
            sprintf(output_str,"ARMED");
            break;
        case 11:
            sprintf(output_str,"HALT_DMA");
            break;
        case 12:
            sprintf(output_str,"BACKUP");
            break;
        case 13:
            sprintf(output_str,"FATAL");
            break;
        case 15:
            sprintf(output_str,"DONE");
            break;
        default:
            sprintf(output_str,"Invalid State =%d", value);
            break;
    } 
}

static void decode_flash_reserve_level(int value, char *output_str)
{
    output_str[0] = '\0'; // Initialize

    switch(value)
    {
        case 0:
            sprintf(output_str,"Not defined");
            break;
        case 1:
            sprintf(output_str,"Normal");
            break;
        case 2:
            sprintf(output_str,"Warning");
            break;
        case 3:
            sprintf(output_str,"Urgent");
            break;
        default:
            sprintf(output_str,"Invalid value =%d", value);
            break;
    } 
}

static void decode_led_fpga_driver(int value, char *output_str)
{
    output_str[0] = '\0'; // Initialize

    switch(value)
    {
        case 0:
            sprintf(output_str,"EV3 Controller");
            break;
        case 1:
            sprintf(output_str,"Driver");
            break;
        default:
            sprintf(output_str,"Invalid value =%d", value);
            break;
    } 
}




// We will always read DWORDS
// The actual offset used in the processing routine is always the value of reg_offset, aligned on the previous DWORD boundary if not already aligned. 

#define MAX_GROUP_ARRAY 256 // Arbitrary - limit to some pragmatic number

struct reg_value_info CardCtrlInfo[] = 
{
    // Name                                               Units                           Offset                        Bit   Bit
    //                                                                                                                  MSB   LSB
    {"commandReset"                                     , ""                            , COMMAND_REG                   , 0  , 0    , decode_active_not_active}, 
    {"commandWriteConfiguration"                        , ""                            , COMMAND_REG                   , 1  , 1    , decode_active_not_active}, 
    {"commandEraseAndSanitize"                          , ""                            , COMMAND_REG                   , 4  , 4    , decode_active_not_active}, 
    {"commandMeasurePmu"                                , ""                            , COMMAND_REG                   , 5  , 5    , decode_active_not_active}, 
    {"commandLoadFactoryDefaults"                       , ""                            , COMMAND_REG                   , 8  , 8    , decode_active_not_active}, 
    {"controlEnableNvOperation"                         , ""                            , CONTROL_REG                   , 0  , 0    , decode_enable_disable}, 
    {"controlEnableBackup"                              , ""                            , CONTROL_REG                   , 1  , 1    , decode_enable_disable}, 
    {"controlDisablePmuMonitoring"                      , ""                            , CONTROL_REG                   , 4  , 4    , decode_flip_enable_disable}, 
    {"controlEnablePmuAutoTest"                         , ""                            , CONTROL_REG                   , 8  , 8    , decode_enable_disable}, 
    {"ledCtrlLink"                                      , ""                            , LED_CONTROL                   , 0  , 0    , decode_led_fpga_driver}, 
    {"ledCtrlArmed"                                     , ""                            , LED_CONTROL                   , 2  , 2    , decode_led_fpga_driver}, 
    {"ledCtrlBackup"                                    , ""                            , LED_CONTROL                   , 4  , 4    , decode_led_fpga_driver}, 
    {"ledCtrlRestore"                                   , ""                            , LED_CONTROL                   , 6  , 6    , decode_led_fpga_driver}, 
    {"ledCtrlPmu"                                       , ""                            , LED_CONTROL                   , 8  , 8    , decode_led_fpga_driver}, 
    {"ledCtrlError"                                     , ""                            , LED_CONTROL                   , 10 , 10   , decode_led_fpga_driver}, 
    {"ledCtrlDcal"                                      , ""                            , LED_CONTROL                   , 12 , 12   , decode_led_fpga_driver}, 
    //{"errorInjection"                                   , ""                            , CONTROL_REG                   , 8  , 8    , decode_enable_disable}, 
    {"customerNV0"                                      , ""                            , CUST_NV_REG0                  , 31 , 0    , NULL}, 
    {"customerNV1"                                      , ""                            , CUST_NV_REG1                  , 31 , 0    , NULL}, 
    {"customerNV2"                                      , ""                            , CUST_NV_REG2                  , 31 , 0    , NULL}, 
    {"customerNV3"                                      , ""                            , CUST_NV_REG3                  , 31 , 0    , NULL}, 
    {"customerNV4"                                      , ""                            , CUST_NV_REG4                  , 31 , 0    , NULL}, 
    {"customerNV5"                                      , ""                            , CUST_NV_REG5                  , 31 , 0    , NULL}, 
    {"customerNV6"                                      , ""                            , CUST_NV_REG6                  , 31 , 0    , NULL}, 
    {"customerNV7"                                      , ""                            , CUST_NV_REG7                  , 31 , 0    , NULL}, 
    {""                                                 , ""                            , 0                             , 0 , 0     , NULL}, 
};

struct reg_value_info CardStatusInfo[] = 
{
    // Name                                               Units                           Offset                        Bit   Bit
    //                                                                                                                  MSB   LSB
    {"statusBackupInProgress"                           , ""                            , STATUS_REG                    , 0  , 0    , decode_active_not_active}, 
    {"statusRestoreInProgress"                          , ""                            , STATUS_REG                    , 1  , 1    , decode_active_not_active}, 
    {"statusBackpPowerSourcePresent"                    , ""                            , STATUS_REG                    , 3  , 3    , decode_flip_present_not_present}, 
    {"statusBackupPowerSourceReady"                     , ""                            , STATUS_REG                    , 4  , 4    , decode_ready_not_ready}, 
    {"statusConfigurationAllowed"                       , ""                            , STATUS_REG                    , 6  , 6    , decode_active_not_active}, 
    {"statusOperationalError"                           , ""                            , STATUS_REG                    , 7  , 7    , decode_active_not_active}, 
    {"statusCacheDirty"                                 , ""                            , STATUS_REG                    , 8  , 8    , decode_active_not_active}, 
    {"statusDramAvailable"                              , ""                            , STATUS_REG                    , 9  , 9    , decode_active_not_active}, 
    {"statusNvReady"                                    , ""                            , STATUS_REG                    , 10 , 10   , decode_active_not_active}, 
    {"statusRegisterAccessReady"                        , ""                            , STATUS_REG                    , 11 , 11   , decode_active_not_active}, 
    {"statusBackupDone"                                 , ""                            , STATUS_REG                    , 12 , 12   , decode_active_not_active}, 
    {"statusRestoreDone"                                , ""                            , STATUS_REG                    , 13 , 13   , decode_active_not_active}, 
    {"flashFlags"                                       , ""                            , NV_FLAG_STATUS_REG            , 1  , 0    , decode_flash_flags}, 
    {"controllerState"                                  , ""                            , STATE_REG                     , 3  , 0    , decode_controller_state}, 
    {"cardTemperature"                                  , "degC"                        , EV3_CARD_TEMP                 , 31 , 0    , NULL}, 
    {"fpgaTemperature"                                  , "degC"                        , EV3_FPGA_TEMP                 , 31 , 0    , NULL}, 
    {""                                                 , ""                            , 0                             , 0 , 0     , NULL}, 
};


struct reg_value_info ErrorGroupInfo[] = 
{
    // Name                                               Units                           Offset                        Bit   Bit
    //                                                                                                                  MSB   LSB
    {"errorBackupFailure"                               , ""                            , ERROR_REG                     , 0  , 0    , NULL}, 
    {"errorBackupMultiBit"                              , ""                            , ERROR_REG                     , 1  , 1    , NULL}, 
    {"errorRestoreFailure"                              , ""                            , ERROR_REG                     , 2  , 2    , NULL}, 
    {"errorEraseAndSanitizeFailure"                     , ""                            , ERROR_REG                     , 3  , 3    , NULL}, 
    {"errorPmuAcccess"                                  , ""                            , ERROR_REG                     , 4  , 4    , NULL}, 
    {"errorPmuLostCharge"                               , ""                            , ERROR_REG                     , 5  , 5    , NULL}, 
    {"errorPmuVoltageAfterBackupUnderThreshold"         , ""                            , ERROR_REG                     , 6  , 6    , NULL}, 
    {"errorFatal"                                       , ""                            , ERROR_REG                     , 7  , 7    , NULL}, 
    {"errorFlashFreeBlocksUnderThreshold"               , ""                            , ERROR_REG                     , 8  , 8    , NULL}, 
    {"errorPmuSpdChecksum"                              , ""                            , ERROR_REG                     , 9  , 9    , NULL}, 
    {"errorFlashDetect"                                 , ""                            , ERROR_REG                     , 10 , 10   , NULL}, 
    {"errorFlashConfiguration"                          , ""                            , ERROR_REG                     , 11 , 11   , NULL}, 
    {"errorPmuMeasurement"                              , ""                            , ERROR_REG                     , 15 , 15   , NULL}, 
    {"errorPmuCapacitanceBelowThreshold"                , ""                            , ERROR_REG                     , 16 , 16   , NULL}, 
    {"errorPmuIncompatible"                             , ""                            , ERROR_REG                     , 17 , 17   , NULL}, 
    {"errorConfigurationTableFailure"                   , ""                            , ERROR_REG                     , 19 , 19   , NULL}, 
    {"errorIfp"                                         , ""                            , ERROR_REG                     , 21 , 21   , NULL}, 
    {"errorBackupsCompleteOverThreshold"                , ""                            , ERROR_REG                     , 22 , 22   , NULL}, 
    {"errorLifetimeEstimateOverThreshold"               , ""                            , ERROR_REG                     , 23 , 23   , NULL}, 
    {"errorBadData"                                     , ""                            , ERROR_REG                     , 24 , 24   , NULL}, 
    {"errorPmuInvalid"                                  , ""                            , ERROR_REG                     , 25 , 25   , NULL}, 
    {"errorPmuTemperatureOverThreshold"                 , ""                            , ERROR_REG                     , 26 , 26   , NULL}, 
    {"errorConfigTableRead"                             , ""                            , ERROR_REG                     , 27 , 27   , NULL}, 
    {"errorConfigTableInitialization"                   , ""                            , ERROR_REG                     , 28 , 28   , NULL}, 
    {"errorOperationTookTooLong"                        , ""                            , ERROR_REG                     , 29 , 29   , NULL}, 
    {"errorConfigurationViolation"                      , ""                            , ERROR_REG                     , 31 , 31   , NULL}, 
    {"dramSingleBitErrors"                              , ""                            , EV_FPGA_ECC_STATUS            , 15, 8     , NULL}, 
    {"dramMultiBitErrors"                               , ""                            , EV_FPGA_ECC_STATUS            , 23, 16    , NULL}, 
    {"dramLastErrorAddress"                             , ""                            , EV_FPGA_ECC_ERROR_ADDR        , 31, 0     , NULL}, 
    {"backupSingleBitErrors"                            , ""                            , UNCOR_BACK_ECC_ERR_CNT        , 31, 16    , NULL}, 
    {"backupMultiBitErrors"                             , ""                            , UNCOR_BACK_ECC_ERR_CNT        , 15, 0     , NULL}, 
    {"restoreUncorrectableErrors"                       , ""                            , UNCOR_RSTR_ECC_ERR_CNT        , 31, 0     , NULL}, 
    {"backupCumulativeMultiBitErrors"                   , ""                            , CUM_BACK_UNCOR_ECC_ERR_CNT    , 31, 0     , NULL}, 
    {"restoreCumulativeMultiBitErrors"                  , ""                            , CUM_RSTR_UNCOR_ECC_ERR_CNT    , 31, 0     , NULL}, 
    {""                                                 , ""                            , 0                             , 0 , 0     , NULL}, 
};

struct reg_value_info WarningGroupInfo[] = 
{
    // Name                                               Units                           Offset                        Bit   Bit
    //                                                                                                                  MSB   LSB
    {"warningPmuVoltageAfterBackupUnderThreshold"       , ""                            , ERROR_CODE_REG                , 6 , 6     , NULL}, 
    {"warningFlashFreeBlocksUnderThreshold"             , ""                            , ERROR_CODE_REG                , 8 , 8     , NULL}, 
    {"warningPmuCapacitanceBelowThreshold"              , ""                            , ERROR_CODE_REG                , 16, 16    , NULL}, 
    {"warningBackupsCompleteOverThreshold"              , ""                            , ERROR_CODE_REG                , 22, 22    , NULL}, 
    {"warningLifetimeEstimateOverThreshold"             , ""                            , ERROR_CODE_REG                , 23, 23    , NULL}, 
    {"warningPmuTemperatureOverThreshold"               , ""                            , ERROR_CODE_REG                , 26, 26    , NULL}, 
    {"warningOperationTookTooLong"                      , ""                            , ERROR_CODE_REG                , 29, 29    , NULL}, 
    {""                                                 , ""                            , 0                             , 0 , 0     , NULL}, 
};

struct reg_value_info ErrorThresholdsInfo[] = 
{
    // Name                                               Units                           Offset                        Bit   Bit
    //                                                                                                                  MSB   LSB
    {"flashReserveBlockLevelErrorThreshold"             , ""                            , RESERVE_BLOCK_ERROR_THRESH    ,  1 , 0    , decode_flash_reserve_level}, 
    {"flashLifeTimeEstimateErrorThreshold"              , ""                            , LIFE_ESTIMATE_ERR_THRESH      , 31 , 0    , decode_lifetime_estimate_percent}, 
    {"backupsCompletedErrorThreshold"                   , ""                            , BACKUP_COMPLETE_ERROR_CT      , 31 , 0    , NULL}, 
    {"backupTimeErrorThreshold"                         , "ms"                          , MAX_THRSH_BACKUP_TIME         , 31 , 0    , NULL}, 
    {"restoreTimeErrorThreshold"                        , "ms"                          , MAX_THRSH_RESTORE_TIME        , 31 , 0    , NULL}, 
    {"pmuVoltageAfterBackupErrorThreshold"              , "mV"                          , PRG_ERR_CAP_VOLT_THR          , 31 , 0    , NULL}, 
    {"pmuCapacitanceErrorThreshold"                     , "dF"                          , PRG_MIN_CAPACITANCE           , 31 , 0    , NULL}, 
    {"pmuTemperatureErrorThreshold"                     , "degC"                        , PROG_MAX_PMU_TEMP             , 31 , 0    , NULL}, 
    {""                                                 , ""                            , 0                             , 0 , 0     , NULL}, 
};


struct reg_value_info WarningThresholdsInfo[] = 
{
    // Name                                               Units                           Offset                        Bit   Bit
    //                                                                                                                  MSB   LSB
    {"flashReserveBlockLevelWarningThreshold"             , ""                            , RESERVE_BLOCK_WARN_THRESH   ,  1 , 0    , decode_flash_reserve_level}, 
    {"flashLifeTimeEstimateWarningThreshold"              , ""                            , LIFE_ESTIMATE_WARN_THRESH   , 31 , 0    , decode_lifetime_estimate_percent}, 
    {"backupsCompletedWarningThreshold"                   , ""                            , BACKUP_COMPLETE_WARN_CT     , 31 , 0    , NULL}, 
    {"backupTimeWarningThreshold"                         , "ms"                          , MAX_WARN_BACKUP_TIME        , 31 , 0    , NULL}, 
    {"restoreTimeWarningThreshold"                        , "ms"                          , MAX_WARN_RESTORE_TIME       , 31 , 0    , NULL}, 
    {"pmuVoltageAfterBackupWarningThreshold"              , "mV"                          , PRG_WARN_CAP_VOLT_THR       , 31 , 0    , NULL}, 
    {"pmuCapacitanceWarningThreshold"                     , "dF"                          , PRG_WARN_CAPACITANCE        , 31 , 0    , NULL}, 
    {"pmuTemperatureWarningThreshold"                     , "degC"                        , PROG_MAX_WARN_PMU_TEMP      , 31 , 0    , NULL}, 
    {""                                                 , ""                            , 0                             , 0 , 0     , NULL}, 
};


struct reg_value_info FlashStatusInfo[] = 
{
    // Name                                               Units                           Offset    Bit   Bit
    //                                                                                              MSB   LSB
    {"flashReserveBlockLevel"                               , ""                            , RESERVE_BLOCK_LEVEL       , 1 , 0     , decode_flash_reserve_level}, 
    {"flashLifeTimeEstimate"                                , ""                            , LIFE_ESTIMATE             , 7 , 0     , decode_lifetime_estimate_percent}, 
    {"backupsStarted"                                       , ""                            , BACKUP_START_CT           , 31 , 0    , NULL}, 
    {"backupsCompleted"                                     , ""                            , BACKUP_COUNTET            , 31 , 0    , NULL}, 
    {"backupTimeLast"                                       , "ms"                          , LAST_BACKUP_TIME          , 31 , 0    , NULL}, 
    {"backupTimeLongest"                                    , "ms"                          , LONGEST_BACKUP            , 31 , 0    , NULL}, 
    {"restoreTimeLast"                                      , "ms"                          , LAST_RESTORE_TIME         , 31 , 0    , NULL}, 
    {"restoreTimeLongest"                                   , "ms"                          , LONGEST_RESTORE           , 31 , 0    , NULL}, 
    {""                                                 , ""                            , 0                             , 0 , 0     , NULL}, 
};

struct reg_value_info PmuConfigInfo[] = 
{
    // Name                                               Units                           Offset                            Bit   Bit
    //                                                                                                                      MSB   LSB
    {"pmuCapacitanceMeasurementInterval"                    , "s"                           , CAPACITANCE_MEAS_FREQ         , 31 , 0 , NULL}, 
    {"pmuEnergyGuardBand"                                   , "%"                           , CAPACITANCE_ENERGY_GUARD_BAND , 31 , 0 , NULL}, 
    {""                                                     , ""                            , 0      , 0 , 0                         , NULL}, 
};

struct reg_value_info PmuInfoInfo[] = 
{
    // Name                                               Units                           Offset                            Bit   Bit
    //                                                                                                                      MSB   LSB
    {"pmuVoltageMaximum"                                    , "mV"                          , MAX_CHARGE_CAP_VOLTAGE        , 31 , 0 , NULL}, 
    {"pmuVoltageOperatingMaximum"                           , "mV"                          , PMU_MAX_OPERATING_VOLTAGE     , 31 , 0 , NULL}, 
    {"pmuCapacitanceNominal"                                , "dF"                          , PMU_NOMINAL_CAPACITANCE       , 31 , 0 , NULL}, 
    {"pmuTemperatureOperatingMaximum"                       , "degC"                        , PMU_MAX_OPERATING_TEMP        , 31 , 0 , NULL}, 
    {""                                                     , ""                            , 0      , 0 , 0                         , NULL}, 
};

struct reg_value_info PmuStatusInfo[] = 
{
    // Name                                               Units                           Offset                            Bit   Bit
    //                                                                                                                      MSB   LSB
    {"pmuVoltage"                                           , "mV"                          , CAP_VOLTAGE                   , 31 , 0 , NULL}, 
    {"pmuVoltageThreshold"                                  , "mV"                          , PRG_MIN_CAP_VOLT_THR          , 31 , 0 , NULL}, 
    {"pmuLastVoltageBeforeBackup"                           , "mV"                          , LAST_CAP_VOLT_BEFORE_BACKUP   , 31 , 0 , NULL}, 
    {"pmuLastVoltageAfterBackup"                            , "mV"                          , LAST_CAP_VOLT_AFTER_BACKUP    , 31 , 0 , NULL}, 
    {"pmuLowestVoltageAfterBackup"                          , "mV"                          , LOWEST_CAP_VOLT_BACKUP        , 31 , 0 , NULL}, 
    {"pmuChargeTimeLast"                                    , "ms"                          , LAST_CHARGE_TIME              , 31 , 0 , NULL}, 
    {"pmuChargeTimeLongest"                                 , "ms"                          , LONGEST_CHARGE                , 31 , 0 , NULL}, 
    {"pmuCapacitance"                                       , "dF"                          , LAST_CAPACITANCE              , 31 , 0 , NULL}, 
    {"pmuCapacitanceInitial"                                , "dF"                          , FIRST_CAPACITANCE             , 31 , 0 , NULL}, 
    {"pmuCapacitanceLowest"                                 , "dF"                          , LOWEST_CAPACITANCE            , 31 , 0 , NULL}, 
    {"pmuEnergyMinimumRequired"                             , "J"                           , CAPACITANCE_MIN_ENERGY        , 31 , 0 , NULL}, 
    {"pmuEnergy"                                            , "J"                           , CAPACITANCE_ENERGY            , 31 , 0 , NULL}, 
    {"pmuLastBackupEnergy"                                  , "J"                           , CAPACITANCE_LAST_BACKUP_ENERGY, 31 , 0 , NULL}, 
    {"pmuTemperature"                                       , "degC"                        , CURRENT_PMU_TEMP              , 31 , 0 , NULL}, 
    {"pmuTemperatureHighest"                                , "degC"                        , HIGHEST_PMU_TEMP              , 31 , 0 , NULL}, 
    {"pmuOverTemperatureErrorCount"                         , ""                            , NUM_OVER_TEMP_EVENTS          , 31 , 0 , NULL}, 
    {"pmuOverTemperatureWarningCount"                       , ""                            , NUM_OVER_TEMP_WARN_EVENTS     , 31 , 0 , NULL}, 
    {"pmuTemperatureAverage"                                , "degC"                        , PMU_AVERAGE_TEMP              , 31 , 0 , NULL}, 
    {""                                                     , ""                            , 0      , 0 , 0                         , NULL}, 
};


// Returns NULL if index is out of range. Otherwise returns a string which is the value field used for JSON objects
// This will extract bit fields and appends units to the output string.
// The output string must be freed when done.

static char *process_value_reg_info_entry(struct reg_value_info *group, int index)
{
    char *retVal = NULL; 
    int exit_loop = FALSE;
    int max_index = 0;

    UINT32 result = TEST_PASS;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_REG;
    UINT8 access_size = 4;

    UINT32 temp_val;
    int shift_right; 
    UINT32 mask; 
    int i;
    char temp_value_str[MAX_UNIT_STR];

    // First check that the index is not too large. Check size of array.
    while ((max_index < MAX_GROUP_ARRAY) && (!exit_loop))
    {
        if (strcmp (group[max_index].value_name, "") == 0)
        {
            exit_loop = TRUE;
        }
        else
        {
            max_index++;
        }
    }

    // The last entry cannot be used
    if (index < max_index)
    {
        // Let's process the value
        retVal =(char *)malloc(MAX_UNIT_STR);

        // Align on 4 byte boundary - everything is defined in terms of 32-bit registers
        reg_offset = group[index].reg_offset & ~3;  
        
        result = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);
        if (result == 0)
        {
            // We have the register value in 'value', now extract the bit range specified.\
            // index points to the proper entry
            temp_val = (UINT32) value & 0xFFFFFFFF;
            
            shift_right = group[index].bit_lsb;
            temp_val = (temp_val >> shift_right);

            mask = 0; // At least one bit must be needed            
            for (i=0;i<((group[index].bit_msb+1) - group[index].bit_lsb);i++)
            {
                mask |= (1<<i);
            }

            //printf("index=%d  reg offset=0x%x value=0x%llx mask=0x%x temp_val=0x%x\n", index, reg_offset, value, mask, temp_val);

            temp_val &= mask;

            if (group[index].decoder_function == NULL)
            {
                if (strlen(group[index].unit_str) > 0)
                {
                    sprintf(retVal,"%d %s", temp_val, group[index].unit_str);
                }
                else
                {
                    sprintf(retVal,"%d", temp_val);
                }
            }
            else
            {
                group[index].decoder_function(temp_val, temp_value_str);
                if (strlen(group[index].unit_str) > 0)
                {
                    sprintf(retVal,"%s %s", temp_value_str, group[index].unit_str);
                }
                else
                {
                    sprintf(retVal,"%s", temp_value_str);
                }
            }
        }
        else
        {
            printf("ERROR: Unable to read register for entry at index = %d\n", index);
            free (retVal);
            retVal = NULL;
        }
    }

    return retVal;
}

// Helper routines
static struct json_object *json_create_group_object(struct reg_value_info *group, int num_entries)
{
    char *value_field;
    int i;
    struct json_object *jo=NULL; 

    jo = ev_json_create_object("");

    for (i=0;i<num_entries;i++)
    {
        value_field = process_value_reg_info_entry(group, i);

        if (value_field != NULL)
        {
            //printf("Params: key=%s value=%s\n",ErrorGroupInfo[i].value_name, value_field); 
            ev_json_add_key_pair_entry (jo, group[i].value_name, value_field, JSON_ENTRY_APPEND);
            free(value_field);
        }
    }

    return jo;
}

static struct json_object *json_append_group_object(struct json_object *jo, struct reg_value_info *group, int num_entries)
{
    char *value_field;
    int i;

    for (i=0;i<num_entries;i++)
    {
        value_field = process_value_reg_info_entry(group, i);

        if (value_field != NULL)
        {
            ev_json_add_key_pair_entry (jo, group[i].value_name, value_field, JSON_ENTRY_APPEND);
            free(value_field);
        }
    }

    return jo;
}

// EV3UTIL JSON capable commands
static struct json_object *json_create_card_info_group_object()
{
    UINT32 result = TEST_PASS;
    ioctl_arg_t *u_arg;
    SINT32 rc;
    dev_info_t device_info;
    ver_info_t version_info;
    int is_special_release = FALSE;
    int is_legacy_release = FALSE;
    UINT64 reg_offset;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_REG;
    UINT8 access_size = sizeof(int32_t);  // Use DWORD access only.
    unsigned char output_str[25]; // Largest string plus NULL terminator.
    int str_index = 0;
    int i;
    int j;
    char CardPN[JSON_MAX_KEY_VALUE_LENGTH];
    char CardSN[JSON_MAX_KEY_VALUE_LENGTH];
    char PmuPN[JSON_MAX_KEY_VALUE_LENGTH];
    char PmuSN[JSON_MAX_KEY_VALUE_LENGTH];
    struct json_object *jo = NULL; 
    char value_field[JSON_MAX_KEY_VALUE_LENGTH];

    u_arg = (ioctl_arg_t *)malloc(sizeof(ioctl_arg_t));
    
    device_info.dev_id = 0x1234;
    device_info.ven_id = 0x5678;
    u_arg->ioctl_union.dev_info = device_info;
    u_arg->errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

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
            device_info = u_arg->ioctl_union.dev_info; // All is good - Get the response
        }
    }

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
            version_info = u_arg->ioctl_union.ver_info; // All is good - get the response

            // Get Part and Serial numbers
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

            sprintf(CardPN, "%s", output_str);

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
            j=0; // Index into CardSN string
            for (i=0;i<str_index;i++)
            {
                if ((i==0) || (i==3) || (i==4))
                {
                    // These are encoded differently
                    if (output_str[i] != 0)
                    {
                        // Some protection - Printing out non-character will cause ev3load script to fail.
                        sprintf(&(CardSN[j]),"%c", (unsigned char)output_str[i]);
                        j++;
                    }
                }
                else
                {
                    if ((i==2) | (i==7))
                    {
                        sprintf(&(CardSN[j]),"%1X", (unsigned char)output_str[i]);
                        j++;
                    }
                    else
                    {
                        sprintf(&(CardSN[j]),"%02X", (unsigned char)output_str[i]);
                        j+=2;
                    }
                }
            }

            CardSN[j] = 0; // Terminate

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
            sprintf(PmuPN, "%s", output_str);

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
            j=0; // Index into Pmu S/N string
            for (i=0;i<str_index;i++)
            {
                if ((i==0) || (i==3) || (i==4))
                {
                    // These are encoded differently
                    if (output_str[i] != 0)
                    {
                        // Some protection - Printing out non-character will cause ev3load script to fail.
                        sprintf(&(PmuSN[j]),"%c", (unsigned char)output_str[i]);
                        j++;
                    }
                }
                else
                {
                    if ((i==2) || (i==7))
                    {
                        sprintf(&(PmuSN[j]),"%1X", (unsigned char)output_str[i]);
                        j++;
                    }
                    else
                    {
                        sprintf(&(PmuSN[j]),"%02X", (unsigned char)output_str[i]);
                        j+=2;;
                    }
                }
            }

            PmuSN[j] = 0; // Terminate

            //jo = json_create_card_info_group_object(device_info, version_info, CardPN, CardSN, PmuPN, PmuSN);
            jo = ev_json_create_object("");

            if (MFG_NETLIST == device_info.mfg_info)
            {
                sprintf(value_field,"NETLIST");
            }
            else
            {
                sprintf(value_field,"Unknown");
            }

            ev_json_add_key_pair_entry (jo, "manufacturer", value_field, JSON_ENTRY_APPEND);

            sprintf(value_field,"%.4X", device_info.ven_id);
            ev_json_add_key_pair_entry (jo, "vendorId", value_field, JSON_ENTRY_APPEND);

            sprintf(value_field,"%.4X", device_info.dev_id);
            ev_json_add_key_pair_entry (jo, "deviceId", value_field, JSON_ENTRY_APPEND);

            sprintf(value_field,"%d", device_info.total_cards_detected);
            ev_json_add_key_pair_entry (jo, "cardsDetected", value_field, JSON_ENTRY_APPEND);
            ev_json_add_key_pair_entry (jo, "cardPartNumber", CardPN, JSON_ENTRY_APPEND);
            ev_json_add_key_pair_entry (jo, "cardSerialNumber", CardSN, JSON_ENTRY_APPEND);
            ev_json_add_key_pair_entry (jo, "pmuPartNumber", PmuPN, JSON_ENTRY_APPEND);
            ev_json_add_key_pair_entry (jo, "pmuSerialNumber", PmuSN, JSON_ENTRY_APPEND);


            if (version_info.fpga_build == 0)
            {
                sprintf(value_field, "%.2X.%.2X",version_info.fpga_ver, version_info.fpga_rev);
            }
            else
            {
                sprintf(value_field, "%.2X.%.2X   Build %.2X", version_info.fpga_ver, version_info.fpga_rev, version_info.fpga_build);
            }

            ev_json_add_key_pair_entry (jo, "fpgaDmaVersion", value_field, JSON_ENTRY_APPEND);

            sprintf(value_field, "%s", current_fpga_image[version_info.current_fpga_image]);
            ev_json_add_key_pair_entry (jo, "fpgaNvFwImage", value_field, JSON_ENTRY_APPEND);

            sprintf(value_field, "%.2X.%.2X.%.2X", version_info.rtl_version, version_info.rtl_sub_version, version_info.rtl_sub_sub_version);
            ev_json_add_key_pair_entry (jo, "fpgaNvRtlVersion", value_field, JSON_ENTRY_APPEND);

            sprintf(value_field, "%.2X.%.2X.%.2X", version_info.fw_version, version_info.fw_sub_version, version_info.fw_sub_sub_version);
            ev_json_add_key_pair_entry (jo, "fpgaNvFwVersion", value_field, JSON_ENTRY_APPEND);

            sprintf(value_field, "%d",version_info.fpga_configuration);
            ev_json_add_key_pair_entry (jo, "fpgaConfigurationNumber", value_field, JSON_ENTRY_APPEND);

            sprintf(value_field, "%d",version_info.fpga_board_code);
            ev_json_add_key_pair_entry (jo, "fpgaBoardCode", value_field, JSON_ENTRY_APPEND);

            sprintf(value_field, "%d.%d %s", version_info.driver_rev, version_info.driver_ver, version_info.extra_info);
            ev_json_add_key_pair_entry (jo, "driverVersion", value_field, JSON_ENTRY_APPEND);

            sprintf(value_field, "%d.%d %s", APP_REV,APP_VER, APP_DATE);
            ev_json_add_key_pair_entry (jo, "applicationUtilityVersion", value_field, JSON_ENTRY_APPEND);
        }
    }    

    return jo;
}

static struct json_object *json_create_card_status_group_object()
{
    struct json_object *jo=NULL; 
    UINT32 result = TEST_PASS;
    SINT32 rc;
    UINT64 reg_offset = 2;
    UINT64 value = UNINITIALIZED_VALUE;
    UINT64 length = 1;
    int is_read = TRUE;
    int space = SPACE_REG;
    UINT8 access_size = sizeof(int32_t);  // Use DWORD access only.
    UINT64 error_value = 0;
    UINT32 warning_value = 0;
    UINT32 status_value = 0;
    UINT32 flag_value = 0;
    char StateOfHealth[JSON_MAX_KEY_VALUE_LENGTH];

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
        }
        else
        {
            result = TEST_COULD_NOT_EXECUTE;
        }
    }

    if (result != TEST_COULD_NOT_EXECUTE)
    {
        reg_offset = WARNING_REG;
        rc = aux_hw_access(reg_offset, &value, length, is_read, access_size, space, TRUE);

        if (rc == 0)
        {
            warning_value = (value & 0xffff);
        }
        else
        {
            result = TEST_COULD_NOT_EXECUTE;
        }
    }

    // Set StateOfHealth
    sprintf(StateOfHealth, "Green");  // If no bits are set

    if (warning_value != 0)
    {
        sprintf(StateOfHealth, "Yellow");  // Warning bits are asserted
    }

    if (error_value != 0)
    {
        sprintf(StateOfHealth, "Red");  // Error bits are asserted
    }

    jo = ev_json_create_object("");

    ev_json_add_key_pair_entry (jo, "stateOfHealth", StateOfHealth, JSON_ENTRY_APPEND);
    jo = json_append_group_object(jo, CardStatusInfo, sizeof(CardStatusInfo)/sizeof(struct reg_value_info));

    return jo;
}

static struct json_object *json_create_data_log_group_object()
{
    UINT32 result = TEST_PASS;
    ioctl_arg_t u_arg;
    SINT32 rc;
    data_logger_stats_t dl;
    struct json_object *jo = NULL;
    struct json_object *jo_tmp = NULL;
    struct json_object *jo_tmp2 = NULL;
    int i;
    uint64_t entry_type = 0;
    uint64_t entry_value = 0;
    char value_field[JSON_MAX_KEY_VALUE_LENGTH];

    memset(&dl,0x00,sizeof(data_logger_stats_t));

    u_arg.ioctl_union.data_logger_stats = dl;
    u_arg.errnum = IOCTL_ERR_ERRNUM_UNINITIALIZED;

    rc = ioctl(s32_evfd, EV_IOC_GET_DATA_LOGGER_HISTORY, (void *)&u_arg);
    if ((rc < 0) || (u_arg.errnum != 0))
    {
        perror("ioctl(EV_IOC_GET_DATA_LOGGER_HISTORY)");
        result = TEST_COULD_NOT_EXECUTE;
    }
    else
    {
        dl = u_arg.ioctl_union.data_logger_stats; // Get the response

        //printf("data log head=%d tail=%d sample time = %d sample count = %d wraparound = %d \n", dl.head, dl.tail, dl.sample_time, dl.sample_count, dl.wraparound_index);

        i = dl.tail;
        if (i != dl.head)
        {
            jo = ev_json_create_object("");

            //sprintf(jo->key, "",entry_value);
            jo->category = JSON_ARRAY;

            while (i != dl.head)
            {
                // One JSON object has 3 entries

                entry_type = dl.data_log_buffer[i] & DATA_LOGGER_TYPE_MASK;
                entry_value = dl.data_log_buffer[i] & DATA_LOGGER_DATA_MASK;
                switch(entry_type)
                {
                    case DATA_LOGGER_CARD_TEMPERATURE:
                        // Create a new key/value object for every iteration
                        jo_tmp = ev_json_create_object("");
                        sprintf(value_field, "%ld degC",entry_value);
                        ev_json_add_key_pair_entry (jo_tmp, "cardTemperature", value_field, JSON_ENTRY_APPEND);
                        break;
                    case DATA_LOGGER_FPGA_TEMPERATURE:
                        sprintf(value_field, "%ld degC",entry_value);
                        ev_json_add_key_pair_entry (jo_tmp, "fpgaTemperature", value_field, JSON_ENTRY_APPEND);
                        break;
                    case DATA_LOGGER_PMU_TEMPERATURE:
                        sprintf(value_field, "%ld degC",entry_value);
                        ev_json_add_key_pair_entry (jo_tmp, "pmuTemperature", value_field, JSON_ENTRY_APPEND);
    
                        // After the last entry has been added, nest this object into an array
                        // An entry into the array has no key
                        jo_tmp = ev_json_add_object_nested_entry (jo,"", jo_tmp, JSON_ENTRY_APPEND);
                        
                        break;
                    default:
                        break;
                }

                i++;
                if (i >= dl.wraparound_index)
                {
                    i = 0;
                }
            } 
        }
        else
        {
           // Empry - nothing to report
           // Special case: no data - put in am empty key.
            jo=NULL;
        }
    }

    return jo;
}


// EV3UTIL command functions are next
UINT32 EV_CardCtrl(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;

    switch(form)
    {
        case FORM_HUMAN_READABLE:
            printf("JSON only: Supported\n");
            break;
        case FORM_JSON:
            {
                struct json_object *jo=NULL; 

                jo = json_create_group_object(CardCtrlInfo, sizeof(CardCtrlInfo)/sizeof(struct reg_value_info));
                ev_json_show_object(jo);
                ev_json_destroy_object(jo);
            }
            break;
        default:
            printf("Error: Unknown output format\n");
            break;
    }

    return result;    
}

UINT32 EV_CardInfo(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;

    switch(form)
    {
        case FORM_HUMAN_READABLE:
            printf("JSON only: Supported\n");
            break;
        case FORM_JSON:
            {
                struct json_object *jo=NULL; 

                jo = json_create_card_info_group_object();
                    
                ev_json_show_object(jo);
                ev_json_destroy_object(jo);
            }
            break;
        default:
            printf("Error: Unknown output format\n");
            break;
    }

    return result;    
}

UINT32 EV_CardStatus(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;

    switch(form)
    {
        case FORM_HUMAN_READABLE:
            printf("JSON only: Supported\n");
            break;
        case FORM_JSON:
            {
                struct json_object *jo=NULL; 

                jo = json_create_card_status_group_object();
                ev_json_show_object(jo);
                ev_json_destroy_object(jo);
            }
            break;
        default:
            printf("Error: Unknown output format\n");
            break;
    }

    return result;    
}

UINT32 EV_ErrorGroup(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;

    switch(form)
    {
        case FORM_HUMAN_READABLE:
            printf("JSON only: Supported\n");
            break;
        case FORM_JSON:
            {
                struct json_object *jo=NULL; 

                jo = json_create_group_object(ErrorGroupInfo, sizeof(ErrorGroupInfo)/sizeof(struct reg_value_info));
                ev_json_show_object(jo);
                ev_json_destroy_object(jo);
            }
            break;
        default:
            printf("Error: Unknown output format\n");
            break;
    }

    return result;    
}

UINT32 EV_WarningGroup(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;

    switch(form)
    {
        case FORM_HUMAN_READABLE:
            printf("JSON only: Supported\n");
            break;
        case FORM_JSON:
            {
                struct json_object *jo=NULL; 

                jo = json_create_group_object(WarningGroupInfo, sizeof(WarningGroupInfo)/sizeof(struct reg_value_info));
                ev_json_show_object(jo);
                ev_json_destroy_object(jo);
            }
            break;
        default:
            printf("Error: Unknown output format\n");
            break;
    }

    return result;    
}

UINT32 EV_ErrorThresholds(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;

    switch(form)
    {
        case FORM_HUMAN_READABLE:
            printf("JSON only: Supported\n");
            break;
        case FORM_JSON:
            {
                struct json_object *jo=NULL; 

                jo = json_create_group_object(ErrorThresholdsInfo, sizeof(ErrorThresholdsInfo)/sizeof(struct reg_value_info));
                ev_json_show_object(jo);
                ev_json_destroy_object(jo);
            }
            break;
        default:
            printf("Error: Unknown output format\n");
            break;
    }

    return result;    
}

UINT32 EV_WarningThresholds(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;

    switch(form)
    {
        case FORM_HUMAN_READABLE:
            printf("JSON only: Supported\n");
            break;
        case FORM_JSON:
            {
                struct json_object *jo=NULL; 

                jo = json_create_group_object(WarningThresholdsInfo, sizeof(WarningThresholdsInfo)/sizeof(struct reg_value_info));
                ev_json_show_object(jo);
                ev_json_destroy_object(jo);
            }
            break;
        default:
            printf("Error: Unknown output format\n");
            break;
    }

    return result;    
}

UINT32 EV_FlashStatus(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;

    switch(form)
    {
        case FORM_HUMAN_READABLE:
            printf("JSON only: Supported\n");
            break;
        case FORM_JSON:
            {
                struct json_object *jo=NULL; 

                jo = json_create_group_object(FlashStatusInfo, sizeof(FlashStatusInfo)/sizeof(struct reg_value_info));
                ev_json_show_object(jo);
                ev_json_destroy_object(jo);
            }
            break;
        default:
            printf("Error: Unknown output format\n");
            break;
    }

    return result;    
}

UINT32 EV_PmuConfig(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;

    switch(form)
    {
        case FORM_HUMAN_READABLE:
            printf("JSON only: Supported\n");
            break;
        case FORM_JSON:
            {
                struct json_object *jo=NULL; 

                jo = json_create_group_object(PmuConfigInfo, sizeof(PmuConfigInfo)/sizeof(struct reg_value_info));
                ev_json_show_object(jo);
                ev_json_destroy_object(jo);
            }
            break;
        default:
            printf("Error: Unknown output format\n");
            break;
    }

    return result;    
}

UINT32 EV_PmuInfo(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;

    switch(form)
    {
        case FORM_HUMAN_READABLE:
            printf("JSON only: Supported\n");
            break;
        case FORM_JSON:
            {
                struct json_object *jo=NULL; 

                jo = json_create_group_object(PmuInfoInfo, sizeof(PmuInfoInfo)/sizeof(struct reg_value_info));
                ev_json_show_object(jo);
                ev_json_destroy_object(jo);
            }
            break;
        default:
            printf("Error: Unknown output format\n");
            break;
    }

    return result;    
}

UINT32 EV_PmuStatus(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;

    switch(form)
    {
        case FORM_HUMAN_READABLE:
            printf("JSON only: Supported\n");
            break;
        case FORM_JSON:
            {
                struct json_object *jo=NULL; 

                jo = json_create_group_object(PmuStatusInfo, sizeof(PmuStatusInfo)/sizeof(struct reg_value_info));
                ev_json_show_object(jo);
                ev_json_destroy_object(jo);
            }
            break;
        default:
            printf("Error: Unknown output format\n");
            break;
    }

    return result;    
}

UINT32 EV_LogData(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;

    switch(form)
    {
        case FORM_HUMAN_READABLE:
            printf("JSON only: Supported\n");
            break;
        case FORM_JSON:
            {
                struct json_object *jo=NULL; 

                jo = json_create_data_log_group_object();
                ev_json_show_object(jo);
                ev_json_destroy_object(jo);
            }
            break;
        default:
            printf("Error: Unknown output format\n");
            break;
    }

    return result;    
}

// Get all health data

UINT32 EV_All(SINT32 argc, SINT8 *argv[], output_format form)
{
    UINT32 result = TEST_PASS;

    switch(form)
    {
        case FORM_HUMAN_READABLE:
            printf("JSON only: Supported\n");
            break;
        case FORM_JSON:
            {
                struct json_object *jo=NULL; 
                struct json_object *jo_cc=NULL; 
                struct json_object *jo_ci=NULL; 
                struct json_object *jo_cs=NULL; 
                struct json_object *jo_pc=NULL; 
                struct json_object *jo_pi=NULL; 
                struct json_object *jo_ps=NULL; 
                struct json_object *jo2=NULL; 
                struct json_object *jo3=NULL; 
                struct json_object *jo4=NULL; 
                struct json_object *jo5=NULL; 
                struct json_object *jo7=NULL; 
                struct json_object *jo8=NULL; 
                struct json_object *jo9=NULL; 
                struct json_object *jo_log=NULL; 

                struct json_object *jo_top=NULL; 
                struct json_object *jo_tmp=NULL; 
                
                jo_top = ev_json_create_object("");

                jo_cc = json_create_group_object(CardCtrlInfo, sizeof(CardCtrlInfo)/sizeof(struct reg_value_info));
                jo_ci = json_create_card_info_group_object();
                jo_cs = json_create_card_status_group_object();
                jo = json_create_group_object(ErrorGroupInfo, sizeof(ErrorGroupInfo)/sizeof(struct reg_value_info));
                jo2 = json_create_group_object(WarningGroupInfo, sizeof(WarningGroupInfo)/sizeof(struct reg_value_info));
                jo3 = json_create_group_object(ErrorThresholdsInfo, sizeof(ErrorThresholdsInfo)/sizeof(struct reg_value_info));
                jo4 = json_create_group_object(WarningThresholdsInfo, sizeof(WarningThresholdsInfo)/sizeof(struct reg_value_info));
                jo5 = json_create_group_object(FlashStatusInfo, sizeof(FlashStatusInfo)/sizeof(struct reg_value_info));
                jo_pc = json_create_group_object(PmuConfigInfo, sizeof(PmuConfigInfo)/sizeof(struct reg_value_info));
                jo_pi = json_create_group_object(PmuInfoInfo, sizeof(PmuInfoInfo)/sizeof(struct reg_value_info));
                jo_ps = json_create_group_object(PmuStatusInfo, sizeof(PmuStatusInfo)/sizeof(struct reg_value_info));
                jo_log = json_create_data_log_group_object();

                jo_tmp = ev_json_add_object_nested_entry (jo_top,"cardCtrl", jo_cc, JSON_ENTRY_APPEND);
                jo_tmp = ev_json_add_object_nested_entry (jo_top,"cardInfo", jo_ci, JSON_ENTRY_APPEND);
                jo_tmp = ev_json_add_object_nested_entry (jo_top,"cardStatus", jo_cs, JSON_ENTRY_APPEND);
                jo_tmp = ev_json_add_object_nested_entry (jo_top,"error", jo, JSON_ENTRY_APPEND);
                jo_tmp = ev_json_add_object_nested_entry (jo_top,"warning", jo2, JSON_ENTRY_APPEND);
                jo_tmp = ev_json_add_object_nested_entry (jo_top,"errorThreshold", jo3, JSON_ENTRY_APPEND);
                jo_tmp = ev_json_add_object_nested_entry (jo_top,"warningThreshold", jo4, JSON_ENTRY_APPEND);
                jo_tmp = ev_json_add_object_nested_entry (jo_top,"flashStatus", jo5, JSON_ENTRY_APPEND);
                jo_tmp = ev_json_add_object_nested_entry (jo_top,"pmuConfig", jo_pc, JSON_ENTRY_APPEND);
                jo_tmp = ev_json_add_object_nested_entry (jo_top,"pmuInfo", jo_pi, JSON_ENTRY_APPEND);
                jo_tmp = ev_json_add_object_nested_entry (jo_top,"pmuStatus", jo_ps, JSON_ENTRY_APPEND);
                jo_tmp = ev_json_add_object_nested_entry (jo_top,"logData", jo_log, JSON_ENTRY_APPEND);

                ev_json_show_object(jo_top);
                ev_json_destroy_object(jo_top);
                ev_json_destroy_object(jo);
            }
            break;
        default:
            printf("Error: Unknown output format\n");
            break;
    }

    return result;    
}
