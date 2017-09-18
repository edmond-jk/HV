/* Build switches */
#define MMAP_DEBUG				0
#define BOTH_FPGA				0
#define ECC_ON					0

/* Training modes */
#define	NORMAL_MODE				0
#define	TX_DQDQS_1_MODE			1
#define	RX_DQDQS_1_MODE			2
#define	TX_DQDQS_2_MODE			3
#define	CMD_CK_MODE				9

/* Register start address */
#define	TRN_MODE_ADDR			0
#define	FPGA_PLL_RESET_ADDR		14
#define	TX_DQDQS1_START_ADDR	16
#define	RX_DQDQS1_START_ADDR	34
#define	TX_DQDQS2_START_ADDR	52
#define	CMD_CK_ADDR				88
#define	TRN_STATUS				103
#define	TRN_ERR_CODE_ADDR		104
#define	TRN_RESULTS_DQ_START	105
#define	TRN_RESULTS_ECC			109
#define	TRN_RESULTS_CMD			114
#define TX_DQDQS2_FPGA_DATA1	115
#define TX_DQDQS2_FPGA_DATA2	119

/* Register size */
#define	TRN_MODE_SZ				1
#define	FPGA_PLL_RESET_SZ		1
#define	TX_DQDQS1_SZ			18
#define	RX_DQDQS1_SZ			TX_DQDQS1_SZ
#define	TX_DQDQS2_SZ_FPGA0		10
#define	TX_DQDQS2_SZ_FPGA1		8
#define	CMD_CK_SZ				1
#define	TRN_STATUS_SZ			1
#define	TRN_ERR_CODE_SZ			1
#define	TRN_RESULTS_DQ_SZ		4
#define TX_DQDQS2_FPGA_DATA_SZ	4

/* Training status bit masks */
#define	TRN_MODE				0x01
#define	RDY_EXE					0x02
#define	CAPTURE_DN				0x04
#define	TRN_ERR					0x80

/* Training loop numbers */
#define CMD_CK_LP				256
#define TX_DQDQS_LP				256

/* ECC offset */
#define	ECC_OFFSET				16
