/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/bpf.h>

#include <bpf/bpf_helpers.h>

struct
{
    __uint(type, BPF_MAP_TYPE_XSKMAP);

    // Representing a queue ID of a network interface.
    __type(key, __u32);

    // Which is the file descriptor of the associated AF_XDP socket.
    __type(value, __u32);

    // The maximum number of queue-to-socket associations the map can hold.
    // Defined at map creation time.
    __uint(max_entries, 64);
} xsks_map SEC(".maps");

struct
{
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __type(key, __u32);
    __type(value, __u32);
    __uint(max_entries, 64);
} xdp_stats_map SEC(".maps");

SEC("xdp")
int xdp_sock_prog(struct xdp_md *ctx)
{
    int index = ctx->rx_queue_index;
    __u32 *pkt_count;

    pkt_count = bpf_map_lookup_elem(&xdp_stats_map, &index);
    if (pkt_count)
    {

        /* We pass every other packet */
        if ((*pkt_count)++ & 1)
            return XDP_PASS;
    }

    if (pkt_count)
        bpf_printk("ANHMA --- kernel space %d\n", (*pkt_count));

    /* A set entry here means that the correspnding queue_id
     * has an active AF_XDP socket bound to it. */
    if (bpf_map_lookup_elem(&xsks_map, &index))
        return bpf_redirect_map(&xsks_map, index, XDP_PASS);

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
