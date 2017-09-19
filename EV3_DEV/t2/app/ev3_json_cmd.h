// Next are JSON capable commands
UINT32 EV_CardCtrl(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_CardInfo(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_CardStatus(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_ErrorGroup(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_WarningGroup(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_ErrorThresholds(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_WarningThresholds(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_FlashStatus(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_PmuConfig(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_PmuInfo(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_PmuStatus(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_LogData(SINT32 argc, SINT8 *argv[], output_format form);
UINT32 EV_All(SINT32 argc, SINT8 *argv[], output_format form);