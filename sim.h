#ifndef SIM_H
#define SIM_H

#include "event.h"
#include "router.h"
#include <vector>
#include <memory>

void fatal(const char *fmt, ...);

typedef struct ChannelMap {
    int key;
    Channel *value;
} ChannelMap;

typedef struct Sim {
    Sim(int debug_mode, Topology top, int terminal_count,
        int router_count, int radix, long input_buf_size);

    EventQueue eventq; // global event queue
    Stat stat;
    int debug_mode;
    Topology topology;
    TrafficDesc traffic_desc;
    RandomGenerator rand_gen;
    long input_buf_size; // router input buffer size
    long channel_delay;
    long packet_len;    // length of a packet in flits
    ChannelMap *channel_map;
    Channel *channels;
    std::vector<std::unique_ptr<Router>> routers;
    std::vector<std::unique_ptr<Router>> src_nodes;
    std::vector<std::unique_ptr<Router>> dst_nodes;
} Sim;

void sim_run(Sim *sim, long until);
void sim_process(Sim *sim, Event e);
void sim_report(Sim *sim);
void sim_destroy(Sim *sim);

#endif
