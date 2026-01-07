#include "sensor.h"
#include <stdint.h>
#include "periph/i2c.h"
#include <stdbool.h>

#include "ztimer.h"

static i2c_t i2cDevice;

/* Calibration coefficients (BMP280 datasheet: 3.11.2) */
static uint16_t dig_T1 = 0;
static int16_t  dig_T2 = 0;
static int16_t  dig_T3 = 0;
static int32_t  t_fine = 0;

/* Cache last configured ctrl_meas (sleep mode) */
static uint8_t _current_ctrl_meas = 0x00;

/* -------------------------------------------------------------------------- */
/* Small I2C helpers (acquire/release per transaction)*/


static inline int _i2c_read_reg(uint16_t reg, uint8_t *data)
{
    uint8_t flags = 0;
    i2c_acquire(i2cDevice);
    int res = i2c_read_reg(i2cDevice, TEMP_SENSOR_I2C_ADDR, reg, data, flags);
    i2c_release(i2cDevice);
    return res;
}

static inline int _i2c_read_regs(uint16_t reg, uint8_t *buf, int len)
{
    uint8_t flags = 0;
    i2c_acquire(i2cDevice);
    int res = i2c_read_regs(i2cDevice, TEMP_SENSOR_I2C_ADDR, reg, buf, len, flags);
    i2c_release(i2cDevice);
    return res;
}

static inline int _i2c_write_reg(uint16_t reg, uint8_t data)
{
    uint8_t flags = 0;
    i2c_acquire(i2cDevice);
    int res = i2c_write_reg(i2cDevice, TEMP_SENSOR_I2C_ADDR, reg, data, flags);
    i2c_release(i2cDevice);
    return res;
}

/* -------------------------------------------------------------------------- */
/* BMP280 config helpers */


/* Convert oversampling multiplier -> osrs_t bits (ctrl_meas[7:5]) Section 3.3.2 */
static uint8_t _osr_mult_to_osrs_t_bits(uint8_t osr_mult)
{
    switch (osr_mult) {
        case 1:  return 1; 
        case 2:  return 2; 
        case 4:  return 3; 
        case 8:  return 4; 
        case 16: return 5; 
        default: return 1;
    }
}

/* Store ctrl_meas (sleep mode) to trigger forced measurements later */
static bool _write_ctrl_meas_sleep(uint8_t osrs_t_bits)
{
    /* ctrl_meas:
     * [7:5] osrs_t
     * [4:2] osrs_p = 0 (skip pressure)
     * [1:0] mode = 00 (sleep)
     */
    uint8_t ctrl_meas = (osrs_t_bits << 5) | (0 << 2) | 0x00;

    if (_i2c_write_reg(TEMP_SENSOR_REG_CTRL_MEAS, ctrl_meas) != 0) {
        return false;
    }

    _current_ctrl_meas = ctrl_meas;
    return true;
}

static bool _trigger_forced_measurement(void)
{
    /* mode = 01 (forced), keep oversampling bits */
    uint8_t forced = (_current_ctrl_meas & 0xFC) | 0x01;
    return (_i2c_write_reg(TEMP_SENSOR_REG_CTRL_MEAS, forced) == 0);
}

/* Wait until measuring bit clears (status[3] = 0) */
static bool _wait_measurement_done(void)
{
    while (1) {
        uint8_t status = 0;

        if (_i2c_read_reg(TEMP_SENSOR_REG_STATUS, &status) != 0) {
            return false;
        }

        if ((status & (1 << 3)) == 0) {
            return true;  // measurement finished
        }

        /* Sensor still measuring → sleep a little */
        ztimer_sleep(ZTIMER_MSEC, 1);
    }
}

/* -------------------------------------------------------------------------- */
/* Sensor functions */


bool Sensor_GetChipId(uint8_t *id)
{
    if (_i2c_read_reg(TEMP_SENSOR_REG_CHIP_ID, id) != 0) {
        return false;
    }
    return (*id == TEMP_SENSOR_CHIP_ID);
}

bool Sensor_Reset(void)
{
    /* Optional: Soft reset (BMP280 reset register 0xE0, write 0xB6) */
    if (_i2c_write_reg(TEMP_SENSOR_REG_RESET, 0xB6) != 0) {
        return false;
    }
    return true;
}

bool Sensor_GetStatus(uint8_t *status)
{
    return (_i2c_read_reg(TEMP_SENSOR_REG_STATUS, status) == 0);
}

bool Sensor_LoadCalibrationData(void)
{
    uint8_t buf[TEMP_SENSOR_CALIB_LENGTH];

    if (_i2c_read_regs(TEMP_SENSOR_REG_CALIB_START, buf, TEMP_SENSOR_CALIB_LENGTH) != 0) {
        return false;
    }

    /* little-endian in registers */
    dig_T1 = (uint16_t)((buf[1] << 8) | buf[0]);
    dig_T2 = (int16_t)((buf[3] << 8) | buf[2]);
    dig_T3 = (int16_t)((buf[5] << 8) | buf[4]);

    //DBG_PRINTF("Calib: T1=%u T2=%d T3=%d\n", dig_T1, dig_T2, dig_T3);
    return true;
}

bool Sensor_EnableSampling(void)
{
    return _write_ctrl_meas_sleep(_osr_mult_to_osrs_t_bits(1));
}

bool Sensor_SetTempOversampling(uint8_t osr_mult)
{
    uint8_t osrs_t_bits = _osr_mult_to_osrs_t_bits(osr_mult);
    return _write_ctrl_meas_sleep(osrs_t_bits);
}

bool Sensor_DoTemperatureReading(uint32_t *reading)
{
    /* 1) Trigger exactly one measurement */
    if (!_trigger_forced_measurement()) {
        return false;
    }

    /* 2) Wait for measurement completion */
    if (!_wait_measurement_done()) {
        return false;
    }

    /* 3) Read raw temperature (20-bit) */
    uint8_t data[3];
    if (_i2c_read_regs(TEMP_SENSOR_REG_TEMP_MSB, data, 3) != 0) {
        return false;
    }

    int32_t adc_T = ((int32_t)data[0] << 12) |
                    ((int32_t)data[1] << 4)  |
                    ((int32_t)data[2] >> 4);

    /* 4) Compensation (BMP280 datasheet) -> T in 0.01°C */
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) *
                    ((int32_t)dig_T2)) >> 11;

    int32_t var2 = (((((adc_T >> 4) - (int32_t)dig_T1) *
                      ((adc_T >> 4) - (int32_t)dig_T1)) >> 12) *
                    (int32_t)dig_T3) >> 14;

    t_fine = var1 + var2;

    int32_t T = (t_fine * 5 + 128) >> 8;  /* 0.01°C */

    *reading = (uint32_t)T;
    return true;
}

bool Sensor_Init(void)
{
    if ((TEMP_SENSOR_I2C_NUM < 0) || (TEMP_SENSOR_I2C_NUM >= (int)I2C_NUMOF)) {
        return false;
    }

    i2cDevice = I2C_DEV(TEMP_SENSOR_I2C_NUM);

    /* Optional: verify chip id */
    uint8_t id = 0;
    if (!Sensor_GetChipId(&id)) {
        /* still return false: wiring/address issue */
        return false;
    }

    /* Load calibration first (needed for correct conversion) */
    if (!Sensor_LoadCalibrationData()) {
        return false;
    }

    /* Default config: oversampling x1, sleep mode (forced is triggered per read) */
    if (!Sensor_EnableSampling()) {
        return false;
    }

    return true;
}

void Sensor_Deinit(void)
{
    
}
