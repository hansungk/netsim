#ifndef SIM_H
#define SIM_H

#include "event.h"
#include "router.h"
#include "mem.h"

void fatal(const char *fmt, ...);

struct ChannelMap {
  int key;
  Channel *value;
};

struct Sim
{
  Sim(int terminal_count, int router_count, int radix, Topology &top);

  // Run the simulator.
  void run(long until = -1);
  // Process an event.
  void process(const Event &e);
  void report() const;

  void handler();

  EventQueue eventq{}; // global event queue
  Alloc *flit_allocator;
  Stat stat;
  Topology topology;
  long channel_delay{1};
  // std::map<Connection, Channel &> channel_map{};
  ChannelMap *channel_map;
  Channel *channels;
  std::vector<Router> routers{};
  std::vector<Router> src_nodes{};
  std::vector<Router> dst_nodes{};
};

void sim_destroy(Sim *sim);

#endif
