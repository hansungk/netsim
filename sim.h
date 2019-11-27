#ifndef SIM_H
#define SIM_H

#include "event.h"
#include "router.h"
#include "mem.h"

void fatal(const char *fmt, ...);

typedef struct ChannelMap {
    int key;
    Channel *value;
} ChannelMap;

typedef struct Sim {
    EventQueue eventq; // global event queue
    Alloc *flit_allocator;
    Stat stat;
    int debug_mode;
    Topology topology;
    TrafficDesc traffic_desc;
    long input_buf_size; // router input buffer size
    long channel_delay;
    long packet_len;    // length of a packet in flits
    ChannelMap *channel_map;
    Channel *channels;
    Router *routers;
    Router *src_nodes;
    Router *dst_nodes;
} Sim;

void sim_init(Sim *sim, int debug_mode, Topology top, int terminal_count,
              int router_count, int radix, long input_buf_size);
void sim_run(Sim *sim, long until);
void sim_process(Sim *sim, Event e);
void sim_report(Sim *sim);
void sim_destroy(Sim *sim);

#endif
