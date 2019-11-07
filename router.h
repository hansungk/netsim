#ifndef ROUTER_H
#define ROUTER_H

#include "event.h"
#include "mem.h"
#include "stb_ds.h"

struct Stat {
    long double_tick_count{0};
};

struct RouterPortPair {
    Id id;
    int port;
};

#define RPHASH(ptr) (stbds_hash_bytes(ptr, sizeof(RouterPortPair), 0))

struct Connection {
    RouterPortPair src;
    RouterPortPair dst;
    int uniq; // Used as hash key.
};

void print_conn(const char *name, Connection conn);

struct ConnectionHash {
    size_t key;
    Connection value;
};

static const Connection not_connected = (Connection){
    .src =
        (RouterPortPair){
            .id = (Id){.type = ID_RTR, .value = -1},
            .port = -1,
        },
    .dst =
        (RouterPortPair){
            .id = (Id){.type = ID_RTR, .value = -1},
            .port = -1,
        },
};

long id_hash(Id id);

// Encodes channel connectivity in a bidirectional map.
// Supports runtime checking for connectivity error.
struct Topology {
    ConnectionHash *forward_hash;
    ConnectionHash *reverse_hash;
};

Topology topology_ring(int n);
void topology_destroy(Topology *top);

Connection conn_find_forward(Topology *t, RouterPortPair out_port);
Connection conn_find_reverse(Topology *t, RouterPortPair in_port);

enum TopoType {
    TOP_TORUS,
    TOP_FCLOS,
};

struct TopoDesc {
    TopoType type;
    int k; // side length for tori
    int r; // dimension for tori
};

/// Source-side all-in-one route computation.
int *source_route_compute(TopoDesc td, int src_id, int dst_id);

enum FlitType {
    FLIT_HEAD,
    FLIT_BODY,
    FLIT_TAIL,
};

struct RouteInfo {
    int src;   // source node ID
    int dst;   // destination node ID
    int *path; // series of output ports for this route
    size_t idx;
};

/// Flit and credit encoding.
/// Follows Fig. 16.13.
struct Flit {
    FlitType type;
    RouteInfo route_info;
    long payload;
};

Flit *flit_create(FlitType t, int src, int dst, long p);
void print_flit(const Flit *flit);

struct Credit {
    // VC is omitted, as we only have one VC per a physical channel.
};

struct TimedFlit {
    long time;
    Flit *flit;
};

struct TimedCredit {
    long time;
    Credit credit;
};

struct Channel {
    Channel(EventQueue *eq, long dl, const Connection conn);

    void put(Flit *flit);
    void put_credit(const Credit &credit);
    Flit *get();
    bool get_credit(Credit *c);

    Connection conn;
    EventQueue *eventq;
    long delay;
    TimedFlit *buf = NULL;
    TimedCredit *buf_credit = NULL;
};

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

struct InputUnit {
    GlobalState global;
    GlobalState next_global;
    int route_port;
    int output_vc;
    // credit count is omitted; it can be found in the output
    // units instead.
    PipelineStage stage;
    Flit **buf;
    Flit *st_ready;
};

struct OutputUnit {
    GlobalState global;
    GlobalState next_global;
    int input_port;
    int input_vc;
    int credit_count;
    Credit *buf_credit;
};

Event tick_event_from_id(Id id);

/// A node. Despite its name, it can represent any of a router node, a source
/// node and a destination node.
struct Router {
    Router(EventQueue *eq, Alloc *fa, Stat *st, TopoDesc td, Id id, int radix,
           Channel **in_chs, Channel **out_chs);

    // Tick event
    void tick();

    void source_generate();
    void destination_consume();
    void fetch_flit();
    void fetch_credit();
    void credit_update();
    void route_compute();
    void vc_alloc();
    void switch_alloc();
    void switch_traverse();
    void update_states();

    // Allocators and arbiters
    int vc_arbit_round_robin(int out_port);
    int sa_arbit_round_robin(int out_port);

    // Misc
    /* int get_radix() const { return input_units.size(); } */
    int get_radix() const { return arrlen(input_units); }

    // Mark self-reschedule on the next tick
    void mark_reschedule() { reschedule_next_tick = true; }
    void do_reschedule();

    Id id;                     // router ID
    long flit_arrive_count{0}; // # of flits arrived for the destination node
    long flit_gen_count{0};    // # of flits generated for the destination node

    EventQueue *eventq; // reference to the simulator-global event queue
    Alloc *flit_allocator;
    Stat *stat;
    TopoDesc top_desc;
    size_t input_buf_size{100};
    long last_tick{-1}; // record the last tick time to prevent double-tick in
                        // single cycle
    long last_reschedule_tick{-1}; // XXX: hacky?
    long flit_payload_counter{0};  // for simple payload generation
    bool reschedule_next_tick{
        false}; // marks whether to self-tick at the next cycle

    // Pointers to the input/output channels for each port.
    Channel **input_channels;
    Channel **output_channels;
    // Array of input/output units. Their lengths are the same as the radix of
    // this router.
    InputUnit *input_units;
    OutputUnit *output_units;

    // Allocator variables.
    int va_last_grant_input;
    int sa_last_grant_input;
};

void router_destroy(Router *r);

#endif
