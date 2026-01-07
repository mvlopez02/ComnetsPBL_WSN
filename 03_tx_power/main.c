// boot → wait 5 s → send packets → stop/sleep
#include <stdbool.h>

#include <stdlib.h>
#include <inttypes.h>
#include "random.h"

#include "thread.h"
//#include "shell.h"
#include "msg.h"

#include "net/gnrc.h"
#include "net/gnrc/netapi.h"
#include "net/gnrc/udp.h"
#include "net/utils.h"
#include "net/gnrc/netif.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "ztimer.h"
#include "wsn_util.h"

/* ===== Experiment parameters ===== */
#ifndef PACKET_SIZE
#define PACKET_SIZE      (8U)     /* set via: make PACKET_SIZE=8/16/32 */
#endif

#ifndef NUM_PACKETS
#define NUM_PACKETS      (100U)
#endif

#ifndef BOOT_DELAY_SEC
#define BOOT_DELAY_SEC   (5U)
#endif

/* Destination is "unimportant" */
#ifndef DEST_ADDR
#define DEST_ADDR "2001::1"
#endif

/* Helper macros to stringify numbers in macros */
#define STR_HELPER(x) #x
#define XSTR(x) STR_HELPER(x)

/* Embed a build marker so you can verify the .elf/.uf2 with `strings` */
static const char build_marker[] = "[TX_POWER] size=" XSTR(PACKET_SIZE) " count=" XSTR(NUM_PACKETS);

int main(void)
{
    puts(build_marker);

    /* Boot delay to settle power analyzer*/
    ztimer_sleep(ZTIMER_USEC, (uint32_t)BOOT_DELAY_SEC * 1000000U);

    /* Payload (alphabet content) */
    static char payload[PACKET_SIZE];
    for (unsigned i = 0; i < PACKET_SIZE; i++) {
        payload[i] = (char)('A' + (i % 26));
    }

    /* Send burst */
    for (unsigned i = 0; i < NUM_PACKETS; i++) {
        WSNUtil_Send(DEST_ADDR, payload, sizeof(payload));

        /* 10 ms gap in between packets */
        ztimer_sleep(ZTIMER_USEC, 10U * 1000U);
    }

    /* Go idle forever (so analyzer sees post-burst baseline) */
    while (1) {
        ztimer_sleep(ZTIMER_USEC, 60U * 1000000U);
    }

    return 0;
}
