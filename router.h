#ifndef ROUTER_H
#define ROUTER_H

#include "event.h"
#include "stb_ds.h"
#include <vector>
#include <map>
#include <random>
#include <deque>

// Port that is always connected to a terminal.
#define TERMINAL_PORT 0
// Only 1 VC is used for the input channel for destination nodes.
#define DESTINATION_VC 0
// Single VC used in the terminal-router channel.
#define TERMINAL_VC 0
// Maximum supported torus dimension.
#define NORMALLEN 10
// Excess storage in channel to prevent overrun.
#define CHANNEL_SLACK 4

// ID of the source node is encoded into PacketId.
struct PacketId {
    long src;
    long id;
};

inline bool operator<(const PacketId &a, const PacketId &b)
{
    return (a.src < b.src) || (a.src == b.src && a.id < b.id);
}

struct PacketTimestamp {
    long gen; // cycle # that the packet was generated
    long arr; // cycle # that the whole packet arrived
};

struct Stat {
    long double_tick_count = 0;
    std::map<PacketId, PacketTimestamp> packet_ledger;
    long latency_sum = 0;
    long packet_num = 0;
};

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
    // Default: Uniform Random Traffic
    TrafficDesc(int terminal_count);
    TrafficDesc(TrafficType t, std::vector<int> ds) : type(t), dests(ds) {}

    TrafficType type; // traffic type
    std::vector<int> dests;       // destination table
};

enum FlitType {
    FLIT_HEAD,
    FLIT_BODY,
    FLIT_TAIL,
};

typedef struct RouteInfo {
    int src;   // source node ID
    int dst;   // destination node ID
    std::vector<int> path; // series of output ports for this route
    size_t idx = 0;
} RouteInfo;

/// Flit and credit encoding.
/// Follows Fig. 16.13.
struct Flit {
    Flit(enum FlitType t, int vc, int src, int dst, PacketId pid, long flitnum);

    enum FlitType type;
    long vc_num;
    RouteInfo route_info;
    PacketId packet_id;
    long flitnum;
};

char *flit_str(const Flit *flit, char *s);

struct Credit {
    Credit() {}
    Credit(const std::vector<long> &vc_nums) : vc_nums(vc_nums) {}
    // There are cases where each of multiple input VCs of a the downstream
    // buffer send a credit over the same physical channel.  For these cases,
    // encode a list of VCs that sent the credit.
    std::vector<long> vc_nums;
    Id id; // for debugging purposes
};

typedef struct TimedFlit {
    long time;
    Flit *flit;
} TimedFlit;

typedef struct TimedCredit {
    long time;
    Credit *credit;
} TimedCredit;

typedef struct Channel {
    Channel(EventQueue *eq, long dl, const Connection conn);
    ~Channel();

    Connection conn;
    EventQueue *eventq;
    long delay;
    TimedFlit *buf = NULL;
    std::deque<TimedCredit> buf_credit;
} Channel;

Channel channel_create(EventQueue *eq, long dl, const Connection conn);
void channel_put(Channel *ch, Flit *flit);
void channel_put_credit(Channel *ch, Credit *credit);
Flit *channel_get(Channel *ch);
Credit *channel_get_credit(Channel *ch);
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
    InputUnit(int vc_count, int bufsize);

    struct VC {
        VC(int bufsize);
        ~VC();

        enum GlobalState global = STATE_IDLE;
        enum GlobalState next_global = STATE_IDLE;
        int route_port = -1;
        int output_vc = -1;
        enum PipelineStage stage = PIPELINE_IDLE;
        Flit **buf = NULL;
        Flit *st_ready = NULL;
    };
    std::vector<VC> vcs;
};

struct OutputUnit {
    OutputUnit(int vc_count, int bufsize);

    struct VC {
        VC(int bufsize);
        ~VC();

        enum GlobalState global = STATE_IDLE;
        enum GlobalState next_global = STATE_IDLE;
        int input_port = -1;
        int input_vc = -1;
        int credit_count;
        Credit **buf_credit = NULL;
    };
    std::vector<VC> vcs;
};

Event tick_event_from_id(Id id);

struct RandomGenerator {
    RandomGenerator(int terminal_count);

    std::default_random_engine def;
    std::random_device rd;
    std::uniform_int_distribution<int> uni_dist;
};

/// A router. It can represent any of a switch node, a source node and a
/// destination node.
struct Sim;
struct Router {
    Router(Sim &sim, EventQueue *eq, Stat *st, Id id, int radix, int vc_count,
           TopoDesc td, TrafficDesc trd, RandomGenerator &rg, long packet_len,
           Channel **in_chs, Channel **out_chs, long input_buf_size);
    ~Router();

    Sim &sim;           // FIXME: not pretty
    EventQueue *eventq; // reference to the simulator-global event queue
    Stat *stat;
    Id id;                      // router ID
    int radix;                  // radix
    int vc_count;               // number of VCs per channel
    long flit_arrive_count = 0; // # of flits arrived for the destination node
    long flit_depart_count = 0; // # of flits departed for the destination node
    TopoDesc top_desc;
    TrafficDesc traffic_desc;
    RandomGenerator &rand_gen;
    long last_tick = -1; // prevents double-tick in a cycle
    long packet_len;     // length of a packet in flits
    bool reschedule_next_tick =
        false; // marks whether to self-tick at the next cycle
    struct SourceGenInfo {
        bool packet_finished = true;
        long next_packet_start = 0;
        long packet_counter = 0;
        long flitnum = 0; // n-th flit counter of a packet
    } sg;
    Channel **input_channels;             // accessor to the input channels
    Channel **output_channels;            // accessor to the output channels
    long input_buf_size;                  // max size of each input flit queue
    Flit **source_queue;                  // source queue
    std::vector<InputUnit> input_units;   // input units
    std::vector<OutputUnit> output_units; // output units
    struct Allocator {
    } alloc;
    int src_last_grant_output; // for round-robin arbitration
    int dst_last_grant_input; // for round-robin arbitration
    std::vector<int>
        va_last_grant_input; // for round-robin arbitration, for each input VC
    std::vector<int>
        va_last_grant_output; // for round-robin arbitration, for each output VC
    std::vector<int>
        sa_last_grant_input; // for round-robin arbitration, for each input VC
    std::vector<int>
        sa_last_grant_output; // for round-robin arbitration, for each output VC
};

void router_print_state(Router *r);

// Events and scheduling.
void router_tick(Router *r);
void router_reschedule(Router *r);

// Routing.
std::vector<int> source_route_compute(TopoDesc td, int src_id, int dst_id);

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
