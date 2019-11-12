#ifndef ROUTER_H
#define ROUTER_H

#include "event.h"
#include "mem.h"
#include "stb_ds.h"

struct Stat {
    long double_tick_count = 0;
};

struct RouterPortPair {
    Id id;
    int port;
};

struct Connection {
    RouterPortPair src;
    RouterPortPair dst;
    int uniq; // used as hash key
};

void print_conn(const char *name, Connection conn);

// Maps a RouterPortPair to a Connection.  Used for finding a connection with
// either end of it as the key.
struct ConnectionHash {
    RouterPortPair key;
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
    int k; // ring length of torus
    int r; // dimension of torus
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
char *flit_str(const Flit *flit, char *s);

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

char *globalstate_str(GlobalState state, char *s);

// credit_count is omitted in the input unit; it can be found in the output unit
// instead.
struct InputUnit {
    GlobalState global;
    GlobalState next_global;
    int route_port;
    int output_vc;
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

/// A router. It can represent any of a switch node, a source node and a
/// destination node.
struct Router {
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
    bool reschedule_next_tick; // marks whether to self-tick at the next cycle
    Channel **input_channels;  // accessor to the input channels
    Channel **output_channels; // accessor to the output channels
    InputUnit *input_units;    // input units
    OutputUnit *output_units;  // output units
    size_t input_buf_size;     // max size of each input flit queue
    int va_last_grant_input;   // for round-robin arbitration
    int sa_last_grant_input;   // for round-robin arbitration
};

Router router_create(EventQueue *eq, Alloc *fa, Stat *st, TopoDesc td, Id id,
                     int radix, Channel **in_chs, Channel **out_chs);

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

void router_print_state(Router *r);
void router_destroy(Router *r);

#endif
