//boot → wait 5 s → do 10 reads → stop/sleep

#include <stdint.h>
#include "ztimer.h"
#include "sensor.h"


#ifndef TEMP_OSR_BITS
#define TEMP_OSR_BITS      (16)
#endif

#ifndef BOOT_DELAY_SEC
#define BOOT_DELAY_SEC     (5U)
#endif

#ifndef NUM_READS
#define NUM_READS          (10U)
#endif

#ifndef READ_SPACING_MS
#define READ_SPACING_MS    (150U)
#endif

/* Debug prints */
#ifndef DEBUG_PRINTS
#define DEBUG_PRINTS (0)   /* set to 0 for power measurements */
#endif

#if DEBUG_PRINTS
#include <stdio.h>
#define DPRINT(...) printf(__VA_ARGS__)
#define DPUTS(s)    puts(s)
#else
#define DPRINT(...) do {} while (0)
#define DPUTS(s)    do {} while (0)
#endif

static uint8_t _bits_to_osr_mult(void)
{
    switch (TEMP_OSR_BITS) {
        case 16: return 1;
        case 17: return 2;
        case 18: return 4;
        case 19: return 8;
        case 20: return 16;
        default: return 1;
    }
}

int main(void)
{
    DPUTS("boot"); //

    /* Boot delay to settle power analyzer */
    ztimer_sleep(ZTIMER_SEC, BOOT_DELAY_SEC);

    DPRINT("start (TEMP_OSR_BITS=%d)\n", (int)TEMP_OSR_BITS); //

    /* Init sensor (I2C + calibration) */
    if (!Sensor_Init()) {
      DPUTS("Sensor_Init failed"); //
      while (1) { ztimer_sleep(ZTIMER_SEC, 1);; }
    }

    /* Configure oversampling based on build */
    if (!Sensor_SetTempOversampling(_bits_to_osr_mult())) {
      DPUTS("Set oversampling failed");//
      while (1) { ztimer_sleep(ZTIMER_SEC, 1);; }
    }

    /* Do NUM_READS measurements */
    for (uint32_t i = 0; i < NUM_READS; i++) {
      uint32_t t = 0;
      //(void)Sensor_DoTemperatureReading(&t); 

      bool ok = Sensor_DoTemperatureReading(&t);

        /* Print only index + OK/ERR (and optionally value) */
        if (ok) {
            DPRINT("%lu OK %lu\n", (unsigned long)i, (unsigned long)t);
        }
        else {
            DPRINT("%lu ERR\n", (unsigned long)i);
        }

      ztimer_sleep(ZTIMER_MSEC, READ_SPACING_MS);
    }

    DPUTS("done");

    /* Stop/sleep forever */
    while (1) {
        ztimer_sleep(ZTIMER_SEC, 1);
    }
}
