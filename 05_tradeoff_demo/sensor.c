#include "sensor.h"
#include "shell.h"
#include <stdlib.h>

i2c_t i2cDevice;

static uint8_t i2c_buf[128U];

static uint16_t dig_T1 = 0; // Reference manual chapter 3.11.2
static int16_t  dig_T2 = 0;
static int16_t  dig_T3 = 0;

static int32_t  t_fine; // 

static inline void _print_i2c_read(i2c_t dev, uint16_t *reg, uint8_t *buf, int len)
{
    printf("Success: i2c_%i read %i byte(s) ", dev, len);
    if (reg != NULL) {
        printf("from reg 0x%02x ", *reg);
    }
    printf(": [");
    for (int i = 0; i < len; i++) {
        if (i != 0) {
            printf(", ");
        }
        printf("0x%02x", buf[i]);
    }
    printf("]\n");
}

static int cmd_i2c_write_reg(int argc, char **argv)
{
    int res;
    uint16_t addr;
    uint16_t reg;
    uint8_t flags = 0;
    uint8_t data;
    int dev;

    dev = 0;
    addr = atoi(argv[2]);
    reg = atoi(argv[3]);
    data = atoi(argv[4]);

    printf("Command: i2c_write_reg(%i, 0x%02x, 0x%02x, 0x%02x, [0x%02x", dev, addr, reg, flags, data);
    puts("])");
    res = i2c_write_reg(dev, addr, reg, data, flags);

    if (res == 0) {
        printf("Success: i2c_%i wrote 1 byte\n", dev);
        return 0;
    }
    return res;
}

static int cmd_i2c_read_reg(int argc, char **argv)
{
    int res;
    uint16_t addr;
    uint16_t reg;
    uint8_t flags = 0;
    uint8_t data;
    int dev;

    dev = 0;
    addr = atoi(argv[2]);
    reg = atoi(argv[3]);
    
    printf("Command: i2c_read_reg(%i, 0x%02x, 0x%02x, 0x%02x)\n", dev, addr, reg, flags);
    res = i2c_read_reg(dev, addr, reg, &data, flags);

    if (res == 0) {
        _print_i2c_read(dev, &reg, &data, 1);
        return 0;
    }
    return res;
}

static int cmd_i2c_read_regs(int argc, char **argv)
{
    int res;
    uint16_t addr;
    uint16_t reg;
    uint8_t flags = 0;
    int len;
    int dev;

    dev = 0;
    addr = atoi(argv[2]);
    reg = atoi(argv[3]);
    len = atoi(argv[4]);

    if (len < 1 || len > (int)128U) {
        puts("Error: invalid LENGTH parameter given");
        return 1;
    }
    else {
        printf("Command: i2c_read_regs(%i, 0x%02x, 0x%02x, %i, 0x%02x)\n", dev, addr, reg, len, flags);
        res = i2c_read_regs(dev, addr, reg, i2c_buf, len, flags);
    }

    if (res == 0) {
        _print_i2c_read(dev, &reg, i2c_buf, len);
        return 0;
    }
    return res;
}

bool Sensor_GetChipId(uint8_t *id)
{
    uint8_t flags = 0;

    int res = i2c_read_reg(i2cDevice,
                           TEMP_SENSOR_I2C_ADDR,
                           TEMP_SENSOR_REG_CHIP_ID,
                           id,
                           flags);
    if (res != 0) {
        return false;     // I2C failed
    }

    return (*id == TEMP_SENSOR_CHIP_ID);
}



bool Sensor_Reset(void)
{
  // From the datasheet:
  // "The “reset” register contains the soft reset word reset[7:0]. If the value 0xB6 is written to the register,
  // the device is reset using the complete power-on-reset procedure. Writing other values than 0xB6 has
  // no effect. The readout value is always 0x00"
  //
  // Optional //
  return true;
}

bool Sensor_GetStatus(uint8_t *status)
{
  // The “status” register contains two bits which indicate the status of the device.
  // Optional

  return true;
}

bool Sensor_DoTemperatureReading(uint32_t *reading)
{
    uint8_t data[3];
    uint8_t flags = 0;

    int res = i2c_read_regs(i2cDevice, TEMP_SENSOR_I2C_ADDR, TEMP_SENSOR_REG_TEMP_MSB, data, 3, flags);

    if (res != 0) {
        return false;
    }

    // 20-bit raw ADC temperature: [MSB][LSB][XLSB(7:4)]
    int32_t adc_T = ((int32_t)data[0] << 12) |
                    ((int32_t)data[1] << 4)  |
                    ((int32_t)data[2] >> 4);

    int32_t var1, var2, T;

    var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) *
            ((int32_t)dig_T2)) >> 11;

    var2 = (((((adc_T >> 4) - (int32_t)dig_T1) *
              ((adc_T >> 4) - (int32_t)dig_T1)) >> 12) *
            (int32_t)dig_T3) >> 14;

    t_fine = var1 + var2;

    T = (t_fine * 5 + 128) >> 8;   // T in 0.01 °C

    *reading = (uint32_t)T;        // e.g. 2508 => 25.08°C

    return true;
}


bool Sensor_EnableSampling(void)
{
    uint8_t ctrl_meas = 0x23;  // osrs_t=1, osrs_p=0, mode=normal

    //  0 0 1   0 0 0   1 1  (Temp oversampling x1, Press oversampling, normal mode)

    uint8_t flags = 0;

    int res = i2c_write_reg(i2cDevice, TEMP_SENSOR_I2C_ADDR, TEMP_SENSOR_REG_CTRL_MEAS, ctrl_meas, flags);

    if (res != 0) {
        return false;
    }

    return true;
}


bool Sensor_LoadCalibrationData(void)
{
    uint8_t buf[TEMP_SENSOR_CALIB_LENGTH];
    uint8_t flags = 0;

    int res = i2c_read_regs(i2cDevice, TEMP_SENSOR_I2C_ADDR, TEMP_SENSOR_REG_CALIB_START, buf, TEMP_SENSOR_CALIB_LENGTH, flags);
    
    if (res != 0) {
        printf("Calibration read failed, res=%d\n", res);
        return false;
    }

    // LSB first, then MSB (little endian)
    dig_T1 = (uint16_t)(buf[1] << 8 | buf[0]);
    dig_T2 = (int16_t)(buf[3] << 8 | buf[2]);
    dig_T3 = (int16_t)(buf[5] << 8 | buf[4]);

    printf("Calib T1=%u, T2=%d, T3=%d\n", dig_T1, dig_T2, dig_T3);

    return true;
}

bool Sensor_Init(void)
{
  if ((TEMP_SENSOR_I2C_NUM < 0) || (TEMP_SENSOR_I2C_NUM >= (int)I2C_NUMOF)) {
    printf("I2C device with number \"%d\" not found\n", TEMP_SENSOR_I2C_NUM);
    return false;
  }
  i2cDevice = I2C_DEV(TEMP_SENSOR_I2C_NUM);
  i2c_acquire(i2cDevice);
  bool ret = Sensor_EnableSampling();
  ret |= Sensor_LoadCalibrationData();
  return ret;
}

void Sensor_Deinit(void)
{
  i2c_release(i2cDevice);
}

int Sensor_CmdHandler(int argc, char **argv)
{
  if (argc < 2)
  {
    goto usage;
  }
  if (strncmp(argv[1], "id", 16) == 0)
  {
    uint8_t id = 0xff;
    bool ret = Sensor_GetChipId(&id);
    printf("0x%x (%s) \n", id, (id == TEMP_SENSOR_CHIP_ID) ? "CORRECT" : "INCORRECT");
  }
  else if (strncmp(argv[1], "readreg", 16) == 0)
  {
    return cmd_i2c_read_reg(argc-1, argv);
  }
  else if (strncmp(argv[1], "writereg", 16) == 0)
  {
    return cmd_i2c_write_reg(argc, argv);
  }
  else if (strncmp(argv[1], "readregs", 16) == 0)
  {
    return cmd_i2c_read_regs(argc, argv);
  }
  else if (strncmp(argv[1], "sample", 16) == 0)
  {
    uint32_t reading = 0;
    Sensor_DoTemperatureReading(&reading);
    printf("dig_T1 %d dig_T2 %d dig_T3 %d\n", dig_T1, dig_T2, dig_T3);
    printf("Reading %d (0x%x)\n", reading, reading);
  }
  return 0;

  usage:
  printf("Usage: sensor <id|readreg|readregs|writereg>\n");
  return 1;
}
SHELL_COMMAND(sensor, "Sensor cmd handler", Sensor_CmdHandler);
