/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/bpf.h>
#include <linux/in.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include "../common/parsing_helpers.h"

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

/* to u64 in host order */
// static inline __u64 ether_addr_to_u64(const __u8 *addr)
// {
//     __u64 u = 0;
//     int i;

//     for (i = ETH_ALEN - 1; i >= 0; i--)
//         u = u << 8 | addr[i];
//     return u;
// }

#define bpf_debug(fmt, ...)                    \
    {                                          \
        char __fmt[] = fmt;                    \
        bpf_trace_printk(__fmt, sizeof(__fmt), \
                         ##__VA_ARGS__);       \
    }

SEC("xdp")
int xdp_sock_prog(struct xdp_md *ctx)
{
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    struct hdr_cursor nh;
    struct ethhdr *eth;
    int eth_type;
    // int action = XDP_PASS;
    // unsigned char *dst;

    /* These keep track of the next header type and iterator pointer */
    nh.pos = data;

    /* Parse Ethernet and IP/IPv6 headers */
    eth_type = parse_ethhdr(&nh, data_end, &eth);
    if (!eth)
    {
        return XDP_DROP;
    }

    if (eth_type == -1)
    {
        return XDP_PASS;
    }

    // Extract the source MAC address from the Ethernet header
    unsigned char *src_mac = eth->h_source;
    bpf_debug("src_mac[0]: %02x\n", src_mac[0]);
    bpf_debug("src_mac[1]: %02x\n", src_mac[1]);
    bpf_debug("src_mac[2]: %02x\n", src_mac[2]);
    bpf_debug("src_mac[3]: %02x\n", src_mac[3]);
    bpf_debug("src_mac[4]: %02x\n", src_mac[4]);
    bpf_debug("src_mac[5]: %02x\n", src_mac[5]);

    // unsigned char *dest_mac = eth->h_dest;
    // bpf_debug("dest_mac[0]: %02x\n", dest_mac[0]);
    // bpf_debug("dest_mac[1]: %02x\n", dest_mac[1]);
    // bpf_debug("dest_mac[2]: %02x\n", dest_mac[2]);
    // bpf_debug("dest_mac[3]: %02x\n", dest_mac[3]);
    // bpf_debug("dest_mac[4]: %02x\n", dest_mac[4]);
    // bpf_debug("dest_mac[5]: %02x\n", dest_mac[5]);

    // bpf_printk("src: %llu, dst: %llu, proto: %u\n",
    //            ether_addr_to_u64(eth->h_source),
    //            ether_addr_to_u64(eth->h_dest),
    //            bpf_ntohs(eth->h_proto));

    int index = ctx->rx_queue_index;
    // __u32 *pkt_count;

    // pkt_count = bpf_map_lookup_elem(&xdp_stats_map, &index);
    // if (pkt_count)
    // {

    //     /* We pass every other packet */
    //     if ((*pkt_count)++ & 1)
    //         return XDP_PASS;
    // }

    // if (pkt_count)
    //     bpf_printk("ANHMA --- kernel space %d\n", (*pkt_count));

    /* A set entry here means that the correspnding queue_id
     * has an active AF_XDP socket bound to it. */
    if (bpf_map_lookup_elem(&xsks_map, &index))
        return bpf_redirect_map(&xsks_map, index, XDP_PASS);

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
