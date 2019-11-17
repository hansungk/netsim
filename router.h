#ifndef ROUTER_H
#define ROUTER_H

#include "event.h"
#include "mem.h"
#include "stb_ds.h"

#define TERMINAL_PORT 0

typedef struct Stat {
    long double_tick_count;
} Stat;

typedef struct RouterPortPair {
    Id id;
    int port;
} RouterPortPair;

typedef struct Connection {
    RouterPortPair src;
    RouterPortPair dst;
    int uniq; // used as hash key
} Connection;

static const Connection not_connected = {
    .src = (RouterPortPair){.id = {ID_RTR, -1}, .port = -1},
    .dst = (RouterPortPair){.id = {ID_RTR, -1}, .port = -1},
    .uniq = -1,
};

void print_conn(const char *name, Connection conn);

// Maps a RouterPortPair to a Connection.  Used for finding a connection with
// either end of it as the key.
typedef struct ConnectionMap {
    RouterPortPair key;
    Connection value;
} ConnectionMap;

enum TopoType {
    TOP_TORUS,
    TOP_FCLOS,
};

typedef struct TopoDesc {
    enum TopoType type;
    int k; // ring length of torus
    int r; // dimension of torus
} TopoDesc;

// Encodes channel connectivity in a bidirectional map.
// Supports runtime checking for connectivity error.
typedef struct Topology {
    TopoDesc desc;
    ConnectionMap *forward_hash;
    ConnectionMap *reverse_hash;
} Topology;

Connection conn_find_forward(Topology *t, RouterPortPair out_port);
Connection conn_find_reverse(Topology *t, RouterPortPair in_port);

int torus_id_xyz_get(int id, int k, int direction);
int torus_id_xyz_set(int id, int k, int direction, int component);
Topology topology_torus(int k, int r);
void topology_destroy(Topology *top);

int *source_route_compute(TopoDesc td, int src_id, int dst_id);

enum FlitType {
    FLIT_HEAD,
    FLIT_BODY,
    FLIT_TAIL,
};

typedef struct RouteInfo {
    int src;   // source node ID
    int dst;   // destination node ID
    int *path; // series of output ports for this route
    size_t idx;
} RouteInfo;

/// Flit and credit encoding.
/// Follows Fig. 16.13.
typedef struct Flit {
    enum FlitType type;
    RouteInfo route_info;
    long payload;
    long flitnum;
} Flit;

Flit *flit_create(enum FlitType t, int src, int dst, long p, long flitnum);
char *flit_str(const Flit *flit, char *s);

typedef struct Credit {
    // VC is omitted, as we only have one VC per a physical channel.
} Credit;

typedef struct TimedFlit {
    long time;
    Flit *flit;
} TimedFlit;

typedef struct TimedCredit {
    long time;
    Credit credit;
} TimedCredit;

typedef struct Channel {
    Connection conn;
    EventQueue *eventq;
    long delay;
    TimedFlit *buf;
    TimedCredit *buf_credit;
} Channel;

Channel channel_create(EventQueue *eq, long dl, const Connection conn);
void channel_put(Channel *ch, Flit *flit);
void channel_put_credit(Channel *ch, Credit credit);
Flit *channel_get(Channel *ch);
int channel_get_credit(Channel *ch, Credit *c);
void channel_destroy(Channel *ch);

// Pipeline stages.
enum PipelineStage {
    PIPELINE_IDLE,
    PIPELINE_RC,
    PIPELINE_VA,
    PIPELINE_SA,
    PIPELINE_ST,
};

// Global states of each input/output unit.
enum GlobalState {
    STATE_IDLE,
    STATE_ROUTING,
    STATE_VCWAIT,
    STATE_ACTIVE,
    STATE_CREDWAIT,
};

char *globalstate_str(enum GlobalState state, char *s);

// credit_count is omitted in the input unit; it can be found in the output unit
// instead.
typedef struct InputUnit {
    enum GlobalState global;
    enum GlobalState next_global;
    int route_port;
    int output_vc;
    enum PipelineStage stage;
    Flit **buf;
    Flit *st_ready;
} InputUnit;

typedef struct OutputUnit {
    enum GlobalState global;
    enum GlobalState next_global;
    int input_port;
    int input_vc;
    int credit_count;
    Credit *buf_credit;
} OutputUnit;

Event tick_event_from_id(Id id);

/// A router. It can represent any of a switch node, a source node and a
/// destination node.
typedef struct Router {
    Id id;                  // router ID
    int radix;              // radix
    long flit_arrive_count; // # of flits arrived for the destination node
    long flit_gen_count;    // # of flits generated for the destination node
    EventQueue *eventq;     // reference to the simulator-global event queue
    Alloc *flit_allocator;
    Stat *stat;
    TopoDesc top_desc;
    long last_tick; // prevents double-tick in single cycle (initially -1)
    long flit_payload_counter; // for simple payload generation
    long flitnum;              // n-th flit counter of a packet
    long packet_len;           // length of a packet in flits
    long reschedule_next_tick; // marks whether to self-tick at the next cycle
    Channel **input_channels;  // accessor to the input channels
    Channel **output_channels; // accessor to the output channels
    long input_buf_size;       // max size of each input flit queue
    Flit **source_queue;       // source queue
    InputUnit *input_units;    // input units
    OutputUnit *output_units;  // output units
    int va_last_grant_input;   // for round-robin arbitration
    int sa_last_grant_input;   // for round-robin arbitration
} Router;

Router router_create(EventQueue *eq, Id id, int radix, Alloc *fa, Stat *st,
                     TopoDesc td, long packet_len, Channel **in_chs,
                     Channel **out_chs, long input_buf_size);
void router_print_state(Router *r);
void router_destroy(Router *r);

// Events and scheduling.
void router_tick(Router *r);
void router_reschedule(Router *r);

// Pipeline stages.
void source_generate(Router *r);
void destination_consume(Router *r);
void fetch_flit(Router *r);
void fetch_credit(Router *r);
void credit_update(Router *r);
void route_compute(Router *r);
void vc_alloc(Router *r);
void switch_alloc(Router *r);
void switch_traverse(Router *r);
void update_states(Router *r);

// Allocators and arbiters.
int vc_arbit_round_robin(Router *r, int out_port);
int sa_arbit_round_robin(Router *r, int out_port);

#endif
