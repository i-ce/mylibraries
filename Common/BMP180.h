#ifndef BMP180_H
#define BMP180_H

#include <stdint.h>
#include <math.h>

/* Registers Deginition */
#define AC1_Reg		0xAA
#define AC2_Reg 	0xAC
#define AC3_Reg 	0xAE
#define AC4_Reg 	0xB0
#define AC5_Reg 	0xB2
#define AC6_Reg		0xB4
#define B1_Reg		0xB6
#define B2_Reg		0xB8
#define MB_Reg		0xBA
#define MC_Reg		0xBC
#define MD_Reg		0xBE

#define out_xlsb	0xF8
#define out_lsb		0xF7
#define out_msb		0xF6
#define ctrl_meas	0xF4
#define id				0xD0
#define soft_reset 0xE0

typedef enum
{
	BMP180_OV_Single = 0,
	BMP180_OversamplingX2,
	BMP180_OversamplingX4,
	BMP180_OversamplingX8,	
}BMP180_OversamplingEnumTypeDef;

typedef struct
{
	/* Data */
	float Temperature;
	int32_t Pressure;
	
	/* Functions */
	uint8_t (*WriteReg)(uint8_t I2C_Adrs, uint8_t Reg, uint8_t Value);
	uint8_t (*ReadReg) (uint8_t I2C_Adrs, uint8_t Reg, uint8_t * buf, uint16_t size);
	void (*delay_func)(uint16_t ms);
	
	/* Settings */
	uint8_t I2C_Adrs;				//I2c address. Default value 0xEE
	BMP180_OversamplingEnumTypeDef P_Oversampling;
	
	/* Internal data */
	short AC1;
	short AC2;
	short AC3;
	unsigned short AC4;
	unsigned short AC5;
	unsigned short AC6;
	short B1;
	short B2;
	short MB;
	short MC;
	short MD;
	long UT;
	long UP;
}BMP180_StructTypeDef;

uint8_t BMP180_Init (BMP180_StructTypeDef * BMP180_Struct);
void BMP180_Get_Result (BMP180_StructTypeDef * BMP180_Struct);
float Altitude (uint32_t Pressure);
uint16_t Pa_To_Hg (uint32_t Pressure_In_Pascals);
uint8_t BMP180_Check_ID (BMP180_StructTypeDef * BMP180_Struct);

#endif
