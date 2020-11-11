/* standard library headers */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <sys/time.h>
#include "utility.h"

/* BG stack headers */
#include "bg_types.h"
#include "gecko_bglib.h"

/* Own header */
#include "app.h"
//#include "dump.h"
#include "support.h"
#include "common.h"

struct timeval now, start, target, delta;

struct packet {
  struct packet *next;
  uint32_t microseconds;
  int8_t rssi;
};

struct data {
  bd_addr address;
  uint32_t count;
} data[1024];
uint16_t data_count = 0;

struct data *lookup_address (bd_addr *address) {
  for (int i = 0; i < data_count; i++) {
    if(!memcmp(address,&data[i].address,6)) {
      return &data[i];
    }
  }
  assert(data_count < 1024);
  memcpy(&data[data_count].address,address,6);
  data[data_count].count = 0;
  return &data[data_count++];
}

void process_address(bd_addr *address) {
  struct data *d = lookup_address(address);
  d->count++;
}

// App booted flag
static bool appBooted = false;
static struct {
  char *name;
  uint32 advertising_interval;
  uint16 connection_interval, mtu; 
  bd_addr remote;
  uint8 advertise, connection;
} config = { .remote = { .addr = {0,0,0,0,0,0}},
	     .connection = 0xff,
	     .advertise = 1,
	     .name = NULL,
	     .advertising_interval = 160, // 100 ms
	     .connection_interval = 80, // 100 ms
	     .mtu = 23,
};
  
const char *getAppOptions(void) {
  return "a<remote-address>n<name>";
}

void appOption(int option, const char *arg) {
  double dv;
  switch(option) {
  case 'a':
    parse_address(arg,&config.remote);
    config.advertise = 0;
    break;
  case 'i':
    sscanf(arg,"%lf",&dv);
    config.advertising_interval = round(dv/0.625);
    config.connection_interval = round(dv/1.25);
    break;
  case 'n':
    config.name = strdup(arg);
    break;
  default:
    fprintf(stderr,"Unhandled option '-%c'\n",option);
    exit(1);
  }
}

void appInit(void) {
  if(config.advertise) return;
  for(int i = 0; i < 6; i++) {
    if(config.remote.addr[i]) return;
  }
  printf("Usage: master [ -a <address> ]\n");
  exit(1);
}

/***********************************************************************************************//**
 *  \brief  Event handler function.
 *  \param[in] evt Event pointer.
 **************************************************************************************************/
void appHandleEvents(struct gecko_cmd_packet *evt)
{
  if (NULL == evt) {
    if(!appBooted) return;
    gettimeofday(&now,NULL);
    timersub(&now,&target,&delta);
    //printf("%d.%d\n",delta.tv_sec,delta.tv_usec);
    if(delta.tv_sec < 0) return;
    timersub(&now,&start,&delta);
    double dt = delta.tv_sec + 1e-6*delta.tv_usec;
    int total = 0;
    for(int i = 0; i < data_count; i++) {
      printf("%s %f\n", hex(6,&data[i].address.addr[0]),data[i].count/dt);
      total += data[i].count;
    }
    printf("Total: %f\n",total/dt);
    delta.tv_sec = 1;
    delta.tv_usec = 0;
    timeradd(&target,&delta,&target);
    return;
  }

  // Do not handle any events until system is booted up properly.
  if ((BGLIB_MSG_ID(evt->header) != gecko_evt_system_boot_id)
      && !appBooted) {
#if defined(DEBUG)
    printf("Event: 0x%04x\n", BGLIB_MSG_ID(evt->header));
#endif
    millisleep(50);
    return;
  }

  /* Handle events */
#ifdef DUMP
  switch (BGLIB_MSG_ID(evt->header)) {
  default:
    dump_event(evt);
  }
#endif
  switch (BGLIB_MSG_ID(evt->header)) {
  case gecko_evt_system_boot_id: /*********************************************************************************** system_boot **/
#define ED evt->data.evt_system_boot
    appBooted = true;
    gettimeofday(&start,NULL);
    delta.tv_sec = 1;
    delta.tv_usec = 0;
    timeradd(&start,&delta,&target);
    gecko_cmd_le_gap_start_discovery(le_gap_phy_1m, le_gap_discover_observation);
    break;
#undef ED

  case gecko_evt_le_gap_scan_response_id: /***************************************************************** le_gap_scan_response **/
#define ED evt->data.evt_le_gap_scan_response
    process_address(&ED.address);
    break;
#undef ED

  case gecko_evt_le_connection_opened_id: /***************************************************************** le_connection_opened **/
#define ED evt->data.evt_le_connection_opened
    config.connection = ED.connection;
    break;
#undef ED

  case gecko_evt_gatt_mtu_exchanged_id: /********************************************************************* gatt_mtu_exchanged **/
#define ED evt->data.evt_gatt_mtu_exchanged
    config.mtu = ED.mtu;
    break;
#undef ED

  case gecko_evt_le_connection_closed_id: /***************************************************************** le_connection_closed **/
#define ED evt->data.evt_le_connection_closed
    exit(1);
    break;
#undef ED

  default:
    break;
  }
}
