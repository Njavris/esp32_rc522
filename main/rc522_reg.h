#ifndef __RC522_REG_H__
#define __RC522_REG_H__


typedef enum {
	// Page 0: Command and status
	CommandReg		= 0x01,
	CommIEnReg		= 0x02,
	DivIEnReg		= 0x03,
	CommIrqReg		= 0x04,
	DivIrqReg		= 0x05,
	ErrorReg		= 0x06,
	Status1Reg		= 0x07,
	Status2Reg		= 0x08,
	FIFODataReg		= 0x09,
	FIFOLevelReg		= 0x0A,
	WaterLevelReg		= 0x0B,
	ControlReg		= 0x0C,
	BitFramingReg		= 0x0D,
	CollReg			= 0x0E,
	// Page 1: Command
	ModeReg			= 0x11,
	TxModeReg		= 0x12,
	RxModeReg		= 0x13,
	TxControlReg		= 0x14,
	TxASKReg		= 0x15,
	TxSelReg		= 0x16,
	RxSelReg		= 0x17,
	RxThresholdReg		= 0x18,
	DemodReg		= 0x19,
	MfTxReg			= 0x1C,
	MfRxReg			= 0x1D,
	SerialSpeedReg		= 0x1F,
	// Page 2: Configuration
	CRCResultRegH		= 0x21,
	CRCResultRegL		= 0x22,
	ModWidthReg		= 0x24,
	RFCfgReg		= 0x26,
	GsNReg			= 0x27,
	CWGsPReg		= 0x28,
	ModGsPReg		= 0x29,
	TModeReg		= 0x2A,
	TPrescalerReg		= 0x2B,
	TReloadRegH		= 0x2C,
	TReloadRegL		= 0x2D,
	TCounterValueRegH 	= 0x2E,
	TCounterValueRegL 	= 0x2F,
	// Page 3: Test registers
	TestSel1Reg		= 0x31,
	TestSel2Reg		= 0x32,
	TestPinEnReg		= 0x33,
	TestPinValueReg		= 0x34,
	TestBusReg		= 0x35,
	AutoTestReg		= 0x36,
	VersionReg		= 0x37,
	AnalogTestReg		= 0x38,
	TestDAC1Reg		= 0x39,
	TestDAC2Reg		= 0x3A,
	TestADCReg		= 0x3B
} RC522_Register;

#define CMD_IDLE		0x0
#define CMD_MEM		0x1
#define CMD_GEN_RND_ID	0x2
#define CMD_CALC_CRC	0x3
#define CMD_TX		0x4
#define CMD_NO_CHANGE	0x7
#define CMD_RX		0x8
#define CMD_TRX		0xc
#define CMD_MFAUTH	0xe
#define CMD_RST		0xf

#endif // __RC522_REG_H__
