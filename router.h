#ifndef ROUTER_H
#define ROUTER_H

#include "event.h"
#include "stb_ds.h"
#include <vector>

// Port that is always connected to a terminal.
#define TERMINAL_PORT 0
// Maximum supported torus dimension.
#define NORMALLEN 10
// Excess storage in channel to prevent overrun.
#define CHANNEL_SLACK 4
// Packet size in number of flits.
#define PACKET_SIZE 4

typedef struct PacketTimestamp {
    long gen; // cycle # that the packet was generated
    long arr; // cycle # that the whole packet arrived
} PacketTimestamp;

// Used to track the network latency of a packet.
typedef struct PacketTimestampMap {
    long key; // packet ID
    PacketTimestamp value;
} PacketTimestampMap;

typedef struct Stat {
    long double_tick_count = 0;
    PacketTimestampMap *packet_timestamp_map = NULL;
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

int get_output_port(int direction, int to_larger);
int torus_id_xyz_get(int id, int k, int direction);
int torus_id_xyz_set(int id, int k, int direction, int component);
int torus_align_id(int k, int src_id, int dst_id, int move_direction);
Topology topology_torus(int k, int r);
void topology_destroy(Topology *top);

Connection conn_find_forward(Topology *t, RouterPortPair out_port);
Connection conn_find_reverse(Topology *t, RouterPortPair in_port);

enum TrafficType {
    TRF_UNIFORM_RANDOM,
    TRF_DESIGNATED,
};

struct TrafficDesc {
    TrafficDesc() {}
    TrafficDesc(TrafficType t, std::vector<int> ds) : type(t), dests(ds) {}

    TrafficType type; // traffic type
    std::vector<int> dests{0};       // destination table
};

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
struct InputUnit {
    enum GlobalState global;
    enum GlobalState next_global;
    int route_port;
    int output_vc;
    enum PipelineStage stage;
    Flit **buf;
    Flit *st_ready;
};

struct OutputUnit {
    enum GlobalState global;
    enum GlobalState next_global;
    int input_port;
    int input_vc;
    int credit_count;
    Credit *buf_credit;
};

Event tick_event_from_id(Id id);

/// A router. It can represent any of a switch node, a source node and a
/// destination node.
struct Router {
    Router(EventQueue *eq, Id id, int radix, Stat *st, TopoDesc td,
           TrafficDesc trd, long packet_len, Channel **in_chs,
           Channel **out_chs, long input_buf_size);
    ~Router();

    Id id;                      // router ID
    int radix;                  // radix
    long flit_arrive_count = 0; // # of flits arrived for the destination node
    long flit_depart_count = 0; // # of flits departed for the destination node
    EventQueue *eventq;         // reference to the simulator-global event queue
    Stat *stat;
    TopoDesc top_desc;
    TrafficDesc traffic_desc;
    long last_tick = -1;            // prevents double-tick in a cycle
    long packet_len;           // length of a packet in flits
    bool reschedule_next_tick =
        false; // marks whether to self-tick at the next cycle

    struct SourceGenInfo {
        bool packet_finished = true;
        long next_packet_start = 0;
        long flit_payload_counter = 0; // for simple payload generation
        long flitnum = 0;              // n-th flit counter of a packet
    } sg_info;

    Channel **input_channels;  // accessor to the input channels
    Channel **output_channels; // accessor to the output channels
    long input_buf_size;       // max size of each input flit queue
    Flit **source_queue;       // source queue
    InputUnit *input_units;    // input units
    OutputUnit *output_units;  // output units
    int va_last_grant_input = 0;   // for round-robin arbitration
    int sa_last_grant_input = 0;   // for round-robin arbitration
};

void router_print_state(Router *r);

// Events and scheduling.
void router_tick(Router *r);
void router_reschedule(Router *r);

// Routing.
int *source_route_compute(TopoDesc td, int src_id, int dst_id);

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
