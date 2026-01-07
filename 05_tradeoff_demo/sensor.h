#include <stdbool.h>
#include "periph/i2c.h"

#define TEMP_SENSOR_CHIP_ID  (0x58) // The expected value of the chip id
#define TEMP_SENSOR_I2C_ADDR (0x76) // dec 118
#define TEMP_SENSOR_I2C_NUM  (0) 

/*
 * Registers
*/

#define TEMP_SENSOR_REG_CHIP_ID    (0xD0)

//PAGE 24
#define TEMP_SENSOR_REG_TEMP_XLSB  (0xFC) //0XFC
#define TEMP_SENSOR_REG_TEMP_LSB   (0xFB)
#define TEMP_SENSOR_REG_TEMP_MSB   (0xFA)
#define TEMP_SENSOR_REG_PRESS_XLSB (0xF9)
#define TEMP_SENSOR_REG_PRESS_LSB  (0xF8)
#define TEMP_SENSOR_REG_PRESS_MSB  (0xF7)
#define TEMP_SENSOR_REG_CONFIG     (0xF5)
#define TEMP_SENSOR_REG_CTRL_MEAS  (0xF4) //0xF4
#define TEMP_SENSOR_REG_STATUS     (0xF3)
#define TEMP_SENSOR_REG_RESET      (0xE0)

// Calibration values
#define TEMP_SENSOR_REG_CAL_T1     () //0x88, 0x89
#define TEMP_SENSOR_REG_CAL_T2     () //0x8A, 0x8B
#define TEMP_SENSOR_REG_CAL_T3     () //0x8C, 0x8D

#define TEMP_SENSOR_REG_CALIB_START  (0x88)  // T1 LSB
#define TEMP_SENSOR_CALIB_LENGTH     (6) 

bool Sensor_Init(void);
void Sensor_Deinit(void);

bool Sensor_GetChipId(uint8_t *id);
bool Sensor_Reset(void);
bool Sensor_GetStatus(uint8_t *status);

bool Sensor_EnableSampling(void);
bool Sensor_LoadCalibrationData(void);
bool Sensor_DoTemperatureReading(uint32_t *reading);

int Sensor_CmdHandler(int argc, char **argv);
