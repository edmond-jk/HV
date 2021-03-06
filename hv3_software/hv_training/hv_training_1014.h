/* Build switches */
#define BOTH_FPGA				1
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
#define	CMD_CK_ADDR				102
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

int i2cCMDtoCK[2] = { 0x5066, 0x7066 };
int i2cRdWrLat[2] = { 0x5007, 0x7007 };
int i2cOEDelay[2] = { 0x5070, 0x7070 };
int i2cMuxCmd[9] = { 0x5100, 0x5101, 0x5102, 0x5103, 0x7100, 0x7101, 0x7102, 0x7103, 0x5104 };
int i2cMuxSet[9] = { 0x5008, 0x5009, 0x500a, 0x500b, 0x7008, 0x7009, 0x700a, 0x700b, 0x5005 };

int i2cDQRESULT[9] = { 0x5069, 0x506a, 0x506b, 0x506c, 0x7069, 0x706a, 0x706b, 0x706c, 0x506d };
int i2cMMioData[9] = { 0x5100, 0x5101, 0x5102, 0x5103, 0x7100, 0x7101, 0x7102, 0x7103, 0x5104 };

int i2cDQSRESULT[3] = { 0x506e, 0x706e, 0x506f };

int i2cIoCycDly[18] = {	// 10/14/2016
					0x5176, 0x5177, 0x5178, 0x5179, 0x517a, 0x517b, 0x517c, 0x517d,
					0x7176, 0x7177, 0x7178, 0x7179, 0x717a, 0x717b, 0x717c, 0x717d,
 					0x517e, 0x517f,
				};

int i2cvRefDQS[18] = {	// VREF DQS training.  8/26/2016
					0x5146, 0x5147, 0x5148, 0x5149, 0x514a, 0x514b, 0x514c, 0x514d,
					0x7146, 0x7147, 0x7148, 0x7149, 0x714a, 0x714b, 0x714c, 0x714d,
					0x514e, 0x514f
				};

int i2cWrDQS[18] = {	// WR DQS training.  8/26/2016
					0x5156, 0x5157, 0x5158, 0x5159, 0x515a, 0x515b, 0x515c, 0x515d,
					0x7156, 0x7157, 0x7158, 0x7159, 0x715a, 0x715b, 0x715c, 0x715d,
					0x515e, 0x515f
				};

int i2cRdDQS[18] = {	// RD DQS training.  8/26/2016
					0x5166, 0x5167, 0x5168, 0x5169, 0x516a, 0x516b, 0x516c, 0x516d,
					0x7166, 0x7167, 0x7168, 0x7169, 0x716a, 0x716b, 0x716c, 0x716d,
					0x516e, 0x516f
				};
#if 0
int i2cTXDQS1[18] = {
					0x5010, 0x5011, 0x5012, 0x5013, 0x5014, 0x5015, 0x5016, 0x5017,
					0x7010, 0x7011, 0x7012, 0x7013, 0x7014, 0x7015, 0x7016, 0x7017,
					0x5018, 0x5019
				};

int i2cRXDQS1[18] = {
					0x5022, 0x5023, 0x5024, 0x5025, 0x5026, 0x5027, 0x5028, 0x5029,
					0x7022, 0x7023, 0x7024, 0x7025, 0x7026, 0x7027, 0x7028, 0x7029,
					0x502a, 0x502b
				};
#endif
#if 0
int i2cTXDQS2[18] = {
					0x5034, 0x5035, 0x5036, 0x5037, 0x5038, 0x5039, 0x503a, 0x503b,
					0x7034, 0x7035, 0x7036, 0x7037, 0x7038, 0x7039, 0x703a, 0x703b,
					0x503c, 0x503d
				};
#else
int i2cTXDQS2[18] = {
					0x5106, 0x5107, 0x5108, 0x5109, 0x510a, 0x510b, 0x510c, 0x510d,
					0x7106, 0x7107, 0x7108, 0x7109, 0x710a, 0x710b, 0x710c, 0x710d,
 					0x510e, 0x510f,
				};
#endif
int i2cTXDQS1p[72] = { 
					0x5010, 0x501A, 0x5024, 0x502E, 0x5011, 0x501B, 0x5025, 0x502F,
 					0x5012, 0x501C, 0x5026, 0x5030, 0x5013, 0x501D, 0x5027, 0x5031,
 					0x5014, 0x501E, 0x5028, 0x5032, 0x5015, 0x501F, 0x5029, 0x5033,
 					0x5016, 0x5020, 0x502A, 0x5034, 0x5017, 0x5021, 0x502B, 0x5035,
  					0x7010, 0x701A, 0x7024, 0x702E, 0x7011, 0x701B, 0x7025, 0x702F,
 					0x7012, 0x701C, 0x7026, 0x7030, 0x7013, 0x701D, 0x7027, 0x7031,
 					0x7014, 0x701E, 0x7028, 0x7032, 0x7015, 0x701F, 0x7029, 0x7033,
 					0x7016, 0x7020, 0x702A, 0x7034, 0x7017, 0x7021, 0x702B, 0x7035,
					0x5018, 0x5022, 0x502C, 0x5036, 0x5019, 0x5023, 0x502D, 0x5037	
				};

int i2cRXDQS1p[72] = { 
					0x5038, 0x5042, 0x504c, 0x5056,	0x5039, 0x5043, 0x504d, 0x5057,	
 					0x503a, 0x5044, 0x504e, 0x5058,	0x503b, 0x5045, 0x504f, 0x5059,	
 					0x503c, 0x5046, 0x5050, 0x505a,	0x503d, 0x5047, 0x5051, 0x505b,	
 					0x503e, 0x5048, 0x5052, 0x505c,	0x503f, 0x5049, 0x5053, 0x505d,	
 					0x7038, 0x7042, 0x704c, 0x7056,	0x7039, 0x7043, 0x704d, 0x7057,	
 					0x703a, 0x7044, 0x704e, 0x7058,	0x703b, 0x7045, 0x704f, 0x7059,	
 					0x703c, 0x7046, 0x7050, 0x705a, 0x703d, 0x7047, 0x7051, 0x705b,
 					0x703e, 0x7048, 0x7052, 0x705c, 0x703f, 0x7049, 0x7053, 0x705d,
					0x5040, 0x504a, 0x5054, 0x505e, 0x5041, 0x504b, 0x5055, 0x505f	
				};

int i2cTXDQS2p[72] = { 
					0x5106, 0x5116, 0x5126, 0x5136, 0x5107, 0x5117, 0x5127, 0x5137,
 					0x5108, 0x5118, 0x5128, 0x5138, 0x5109, 0x5119, 0x5129, 0x5139,
 					0x510a, 0x511a, 0x512a, 0x513a, 0x510b, 0x511b, 0x512b, 0x513b,
 					0x510c, 0x511c, 0x512c, 0x513c, 0x510d, 0x511d, 0x512d, 0x513d,
					0x7106, 0x7116, 0x7126, 0x7136, 0x7107, 0x7117, 0x7127, 0x7137,
 					0x7108, 0x7118, 0x7128, 0x7138, 0x7109, 0x7119, 0x7129, 0x7139,
 					0x710a, 0x711a, 0x712a, 0x713a, 0x710b, 0x711b, 0x712b, 0x713b,
 					0x710c, 0x711c, 0x712c, 0x713c, 0x710d, 0x711d, 0x712d, 0x713d,
 					0x510e, 0x511e, 0x512e, 0x513e, 0x510f, 0x511f, 0x512f, 0x513f
				};

int spdTXDQS1[18] = {
					0xa184, 0xa185, 0xa186, 0xa187, 0xa188, 0xa189, 0xa18a, 0xa18b,
					0xa18e, 0xa18f, 0xa190, 0xa191, 0xa192, 0xa193, 0xa194, 0xa195,
 					0xa18c, 0xa18d,
				};

int spdRXDQS1[18] = {
					0xa196, 0xa197, 0xa198, 0xa199, 0xa19a, 0xa19b, 0xa19c, 0xa19d,
					0xa1a0, 0xa1a1, 0xa1a2, 0xa1a3, 0xa1a4, 0xa1a5, 0xa1a6, 0xa1a7,
 					0xa19e, 0xa19f,
				};

int spdTXDQS2[18] = {
					0xa1a8, 0xa1a9, 0xa1aa, 0xa1ab, 0xa1ac, 0xa1ad, 0xa1ae, 0xa1af,
					0xa1b2, 0xa1b3, 0xa1b4, 0xa1b5, 0xa1b6, 0xa1b7, 0xa1b8, 0xa1b9,
 					0xa1b0, 0xa1b1,
				};

int spdHWRev[4]   = { 0xa180, 0xa181, 0x182, 0x183 };
int spdCMDtoCK[2] = { 0xa1da, 0xa1db };
int spdRdWrLat[2] = { 0xa1dc, 0xa1dd };
int spdvRefDQS[2] = { 0xa1de, 0xa1df };
int spdOEDelay[2] = { 0xa1e0, 0xa1e1 };
int spdCmdDQMux[9] = { 0xa1e5, 0xa1e6, 0xa1e7, 0xa1e8, 0xa1ea, 0xa1eb, 0xa1ec, 0xa1ed, 0xa1a9 };
int spdIoCycDly[18] = {	
					0xa1ee, 0xa1ef, 0xa1f0, 0xa1f1, 0xa1f2, 0xa1f3, 0xa1f4, 0xa1f5,
					0xa1f8, 0xa1f9, 0xa1fa, 0xa1fb, 0xa1fc, 0xa1fd, 0xa1fe, 0xa1ff,
 					0xa1f6, 0xa1f7,
				};


unsigned long DS_MMIOCmdWindow[8] = { // mmio_cmd_buffer
					0xc650c650d40fd40f, 0x7e957e95f45ff45f,
					0x55a955a9bd76bd76, 0x23a123a141f141f1,
					0xaaedaaedcceeccee, 0x360e360e40894089,
					0xb0aeb0ae0eaf0eaf, 0x6593659340634063
				};
unsigned long DS_MMIOCmdWindow_ECC = 0x33efe9de11965f0f;
unsigned long DS_TrainingWindow[8] = { // hvdimm training window BG0, BA0, X:0E000, Y:0, tohm - 1GB
					0xd844d8448d848d84, 0xe5c8e5c86e5c6e5c,
					0x73a673a6f73af73a, 0x867f867fb867b867,
					0xed9bed9b6ed96ed9, 0xacc6acc62acc2acc,
					0x1732173201730173, 0xeeb0eeb01eeb1eeb
				};
unsigned long DS_TrainingWindow_ECC = 0x0b236cb9f76a8c44;
