#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>

#include "thread.h"
#include "msg.h"

#include "net/gnrc.h"
#include "net/gnrc/netapi.h"
#include "net/gnrc/netif.h"
#include "net/gnrc/rpl.h"
#include "net/gnrc/rpl/structs.h"
#include "net/utils.h"

#include "ztimer.h"

#include "sensor.h"
#include "wsn_util.h"
#include "main.h"

/* Compile-time role select
   Build with:
 *   Sensor: make ... CFLAGS+=-DWSN_ROLE=ROLE_SENSOR
 *   Root:   make ... CFLAGS+=-DWSN_ROLE=ROLE_ROOT
 */
#define ROLE_SENSOR   (1)
#define ROLE_ROOT     (2)

#ifndef WSN_ROLE
#define WSN_ROLE ROLE_SENSOR
#endif

/* Experiment parameters */
#ifndef SAMPLE_PERIOD_MS
#define SAMPLE_PERIOD_MS   (200U)   /* sensor read rate */
#endif

#ifndef SEND_PERIOD_MS
#define SEND_PERIOD_MS     (1000U)  /* transmit rate */
#endif

#ifndef BOOT_DELAY_MS
#define BOOT_DELAY_MS      (1000U)  
#endif

static const char *rootAddrStr = "2001::1";

#define MSG_QUEUE_SIZE     (8)

static msg_t _main_msg_queue[MSG_QUEUE_SIZE];
static msg_t _thread_msg_queue[MSG_QUEUE_SIZE];

static char threadStack[THREAD_STACKSIZE_DEFAULT];
static kernel_pid_t threadPid = KERNEL_PID_UNDEF;

static volatile bool running = true;
static WSN_Role_e myRole = WSN_UNSET_ROLE;

/* Two timers + two messages */
static ztimer_t sampleTimer;
static ztimer_t sendTimer;

static msg_t sampleMsg = (msg_t){ .type = WSN_IPC_SAMPLE };
static msg_t sendMsg   = (msg_t){ .type = WSN_IPC_SEND };

/* Latest sample state (updated at SAMPLE rate, sent at SEND rate) */
static uint32_t seq = 0;
static uint32_t last_seq = 0;
static uint32_t last_ms = 0;
static uint32_t last_temp = 0;  /* 0.01°C */
static bool last_valid = false;

/* Helpers */
static uint32_t now_ms(void)
{
    return (uint32_t)ztimer_now(ZTIMER_MSEC);
}

/* -------------------- ROOT: packet handler -------------------- */
static void PacketReceptionHandler(gnrc_pktsnip_t *pkt)
{
  for (gnrc_pktsnip_t *snip = pkt; snip != NULL; snip = snip->next) {
    if (snip->type == GNRC_NETTYPE_UNDEF) {
      /* Payload */
      char buf[96];
      size_t n = snip->size;
      if (n >= sizeof(buf)) { n = sizeof(buf) - 1; }
      memcpy(buf, snip->data, n);
      buf[n] = '\0';

      /* Root output: prefix with R, so Python can parse easily */
      printf("R,%s\n", buf);
      return;
    }
  }
}

/* -------------------- SENSOR: sampling task -------------------- */
static void DoSample(void)
{
  uint32_t t = 0;
  uint32_t tms = now_ms();
  uint32_t s = seq++;

  if (!Sensor_DoTemperatureReading(&t)) {
    /* Print error sample so you still have a timeline */
    printf("S,%lu,%lu,ERR\n", (unsigned long)s, (unsigned long)tms);
    return;
  }

  last_seq = s;
  last_ms = tms;
  last_temp = t;
  last_valid = true;

  /* Print ALL local samples to sensor serial */
  printf("S,%lu,%lu,%" PRIu32 "\n", (unsigned long)s, (unsigned long)tms, t);
}

/* -------------------- SENSOR: send task -------------------- */
static void DoSend(void)
{
    if (!last_valid) {
      return;
    }

    /* Send latest sample at SEND rate */
    char payload[96];
    int len = snprintf(payload, sizeof(payload),"%lu,%lu,%" PRIu32,(unsigned long)last_seq, (unsigned long)last_ms, last_temp);
    if (len > 0) {
        WSNUtil_Send(rootAddrStr, payload, (size_t)len);
    }
}

/* -------------------- Common thread (RX + timers) -------------------- */
static void *WSN_NodeThread(void *arg)
{
  (void)arg;
  msg_t msg, reply;
  msg_init_queue(_thread_msg_queue, MSG_QUEUE_SIZE);

  while (running) {
    msg_receive(&msg);

    switch (msg.type) {
      case GNRC_NETAPI_MSG_TYPE_RCV:
        PacketReceptionHandler((gnrc_pktsnip_t *)msg.content.ptr);
        gnrc_pktbuf_release((gnrc_pktsnip_t *)msg.content.ptr);
        break;

      case GNRC_NETAPI_MSG_TYPE_GET:
      case GNRC_NETAPI_MSG_TYPE_SET:
        msg_reply(&msg, &reply);
        break;

      case WSN_IPC_SAMPLE:
        if (myRole == WSN_SENSOR_ROLE) {DoSample(); ztimer_set_msg(ZTIMER_MSEC, &sampleTimer,SAMPLE_PERIOD_MS, &sampleMsg, threadPid);
        }
        break;

      case WSN_IPC_SEND:
        if (myRole == WSN_SENSOR_ROLE) {DoSend(); ztimer_set_msg(ZTIMER_MSEC, &sendTimer, SEND_PERIOD_MS, &sendMsg, threadPid);
        }
        break;

      default:
        break;
    }
  }
  return NULL;
}

/* -------------------- Init root networking -------------------- */
static void Root_NetInit(void)
{
  netif_t *mainIface = netif_iter(NULL);
  int16_t mainIfaceId = netif_get_id(mainIface);

  /* Set global ipv6 addr */
  ipv6_addr_t addr;
  uint16_t flags = GNRC_NETIF_IPV6_ADDRS_FLAGS_STATE_VALID;
  uint8_t prefix_len = ipv6_addr_split_int(rootAddrStr, '/', 64U);
  prefix_len = (prefix_len < 1) ? 64U : prefix_len;
  ipv6_addr_from_str(&addr, rootAddrStr);
  flags |= (prefix_len << 8U);

  if (netif_set_opt(mainIface, NETOPT_IPV6_ADDR, flags, &addr, sizeof(addr)) < 0) {
    puts("Error: unable to add IPv6 addr");
    return;
  }

  if (gnrc_rpl_init(mainIfaceId) < 0) {
    puts("Error: unable to init rpl");
    return;
  }

  if (gnrc_rpl_root_init(mainIfaceId, &addr, false, false) == NULL) {
    puts("Error: unable to init rpl root");
    return;
  }

  WSNUtil_StartServer(threadPid);
}

/* -------------------- WSN init -------------------- */
static void WSN_Init(WSN_Role_e role)
{
    myRole = role;

    threadPid = thread_create(threadStack, sizeof(threadStack), THREAD_PRIORITY_MAIN - 1, 0, WSN_NodeThread, NULL, (role == WSN_SENSOR_ROLE) ? "wsn_sensor" : "wsn_root");

    if (myRole == WSN_ROOT_ROLE) {
        Root_NetInit();
    }
}

/* -------------------- main -------------------- */
int main(void)
{
  msg_init_queue(_main_msg_queue, MSG_QUEUE_SIZE);

  ztimer_sleep(ZTIMER_MSEC, BOOT_DELAY_MS);

  #if (WSN_ROLE == ROLE_ROOT)
    puts("ROLE_ROOT");
    WSN_Init(WSN_ROOT_ROLE);

  #elif (WSN_ROLE == ROLE_SENSOR)
    puts("ROLE_SENSOR");

  if (!Sensor_Init()) {
      puts("Sensor init failed");
  }

  WSN_Init(WSN_SENSOR_ROLE);

  /* start both periodic events immediately */
  ztimer_set_msg(ZTIMER_MSEC, &sampleTimer, 0, &sampleMsg, threadPid);
  ztimer_set_msg(ZTIMER_MSEC, &sendTimer,   0, &sendMsg,   threadPid);

  #else
  #error "Invalid WSN_ROLE (use ROLE_SENSOR or ROLE_ROOT)"
  #endif

  while (1) {
    ztimer_sleep(ZTIMER_MSEC, 1000);
  }
}

