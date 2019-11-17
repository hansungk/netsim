#include "router.h"
#include <assert.h>

// Get the component of id along 'direction' axis.
int torus_id_xyz_get(int id, int k, int direction)
{
    int val = id;
    for (int i = 0; i < direction; i++)
        val /= k;
    return val % k;
}

// Set the component of id along 'direction' axis to 'component'.
int torus_id_xyz_set(int id, int k, int direction, int component)
{
    int weight = 1;
    for (int i = 0; i < direction; i++)
        weight *= k;
    int delta = component - torus_id_xyz_get(id, k, direction);
    return id + delta * weight;
}

int torus_set_id_xyz(int id, int k, int direction)
{
    int val = id;
    for (int i = 0; i < direction; i++)
        val /= k;
    return val % k;
}

Topology topology_create(void)
{
    return (Topology){0};
}

void topology_destroy(Topology *top)
{
    hmfree(top->forward_hash);
    hmfree(top->reverse_hash);
}

int topology_connect(Topology *t, RouterPortPair input, RouterPortPair output)
{
    int old_output_i = hmgeti(t->forward_hash, input);
    int old_input_i = hmgeti(t->reverse_hash, output);
    if (old_output_i >= 0 || old_input_i >= 0) {
        // FIXME: When only one of _i's are >= 0, this results in buffer
        // overflow.
        RouterPortPair old_output = t->forward_hash[old_output_i].value.dst;
        RouterPortPair old_input = t->reverse_hash[old_input_i].value.src;
        if (input.id.type == old_input.id.type &&
            input.id.value == old_input.id.value &&
            input.port == old_input.port &&
            output.id.type == old_output.id.type &&
            output.id.value == old_output.id.value &&
            output.port == old_output.port) {
            // Already connected.
            return 1;
        } else {
            // Bad connectivity: source or destination port is already connected
            return 0;
        }
    }
    int uniq = hmlen(t->forward_hash);
    Connection conn = (Connection){.src = input, .dst = output, .uniq = uniq};
    hmput(t->forward_hash, input, conn);
    hmput(t->reverse_hash, output, conn);
    assert(hmgeti(t->forward_hash, input) >= 0);
    return 1;
}

int topology_connect_terminals(Topology *t, const int *ids)
{
    int res = 1;
    for (int i = 0; i < arrlen(ids); i++) {
        RouterPortPair src_port = {src_id(ids[i]), 0};
        RouterPortPair dst_port = {dst_id(ids[i]), 0};
        RouterPortPair rtr_port = {rtr_id(ids[i]), 0};

        // Bidirectional channel
        res &= topology_connect(t, src_port, rtr_port);
        res &= topology_connect(t, rtr_port, dst_port);
        if (!res)
            return 0;
    }
    return 1;
}

// direction: dimension that the path is in. XYZ = 012.
// to_larger: whether the output port points to a Router with higher ID or not.
int get_output_port(int direction, int to_larger)
{
    return direction * 2 + (to_larger ? 2 : 1);
}

// Port usage: 0:terminal, 1:counter-clockwise, 2:clockwise
static int topology_connect_ring(Topology *t, long size, const int *ids, int direction)
{
    printf("%s: ids={", __func__);
    for (long i = 0; i < size; i++)
        printf("%d,", ids[i]);
    printf("}\n");

    int port_cw = get_output_port(direction, 1);
    int port_ccw = get_output_port(direction, 0);
    int res = 1;
    for (long i = 0; i < size; i++) {
        int l = ids[i];
        int r = ids[(i + 1) % size];
        RouterPortPair lport = {rtr_id(l), port_cw};
        RouterPortPair rport = {rtr_id(r), port_ccw};

        // Bidirectional channel
        res &= topology_connect(t, lport, rport);
        res &= topology_connect(t, rport, lport);
        if (!res)
            return 0;
    }
    return 1;
}

// Connects part of the torus that corresponds to the the given parameters.
// Calls itself recursively to form the desired connection.
//
// dimension: size of the 'normal' array.
// offset: offset of the lowest index.
int topology_connect_torus_dimension(Topology *t, int k, int r, int dimension,
                                     int *normal, int offset)
{
    int res = 1;
    int zeros = 0;
    for (int i = 0; i < dimension; i++)
        if (normal[i] == 0)
            zeros++;

    if (zeros == 1) {
        // Ring
        int stride = 1;
        for (int i = 0; i < dimension; i++, stride *= k) {
            if (normal[i] == 0) {
                int ids[NORMALLEN];
                for (int j = 0; j < k; j++)
                    ids[j] = offset + j * stride;
                res &= topology_connect_ring(t, k, ids, i);
                break; // only one 0 in normal
            }
        }
    } else {
        int stride = 1;
        for (int i = 0; i < dimension; i++, stride *= k) {
            if (normal[i] == 0) {
                int subnormal[NORMALLEN];
                memcpy(subnormal, normal, dimension * sizeof(int));
                subnormal[i] = 1; // lock this dimension
                for (int j = 0; j < k; j++) {
                    int suboffset = offset + j * stride;
                    res &= topology_connect_torus_dimension(
                        t, k, r, dimension, subnormal, suboffset);
                }
            }
        }
    }

    return res;
}

Topology topology_torus(int k, int r)
{
    Topology top = topology_create();
    top.desc = (TopoDesc){TOP_TORUS, k, r};

    int normal[NORMALLEN] = {0};
    int res = 1;

    // Inter-switch channels
    res &= topology_connect_torus_dimension(&top, k, r, r, normal, 0);

    // Terminal channels
    int total_nodes = 1;
    int *ids = NULL;
    for (int i = 0; i < r; i++) total_nodes *= k; // k ^ r
    for (int id = 0; id < total_nodes; id++) {
        arrput(ids, id);
    }
    res &= topology_connect_terminals(&top, ids);
    assert(res);

    arrfree(ids);
    return top;
}

// Compute the ID of the router which is the result of moving 'src_id' along
// the 'move_direction' axis to be aligned with 'dst__id'.  That is, compute the
// ID that has the same component along the 'direction' axis as 'dst_id', and
// along all the other axes as 'src_id'.
//
//        move_direction
// src_id -------------> (return)
//                          |
//                        dst_id
//
// This function is mainly used for computing IDs of the intermediate nodes in
// the dimension order routing.
int torus_align_id(int k, int src_id, int dst_id, int move_direction)
{
    int component = torus_id_xyz_get(dst_id, k, move_direction);
    return torus_id_xyz_set(src_id, k, move_direction, component);
}
