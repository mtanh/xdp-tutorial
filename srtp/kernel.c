/* SPDX-License-Identifier: GPL-2.0 */

#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/bpf.h>
#include <linux/in.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include "parsing_helpers.h"
#include "common_kern_user.h"

char _license[] SEC("license") = "GPL";

struct
{
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __type(key, __u32);   // UMEM queue ID.
    __type(value, __u32); // AF_XDP socket descriptor.
    __uint(max_entries, 64);
} xsks_map SEC(".maps");

struct
{
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 64);
    __type(key, struct srtp_key_ipv6);
    __type(value, struct srtp_state);
} flow_map SEC(".maps");

SEC("xdp")
int xdp_drop(struct xdp_md *ctx)
{
    return XDP_DROP;
}

SEC("xdp")
int xdp_pass(struct xdp_md *ctx)
{
    return XDP_PASS;
}

SEC("xdp")
int srtp_ipv6(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct hdr_cursor nh = {.pos = data};
    struct srtp_state *state;
    struct srtp_key_ipv6 key = {};
    struct ethhdr *eth;
    struct ipv6hdr *ipv6hdr;
    struct udphdr *udphdr;

    int eth_type;
    int ip_type;

    eth_type = parse_ethhdr(&nh, data_end, &eth);
    if (eth_type != bpf_htons(ETH_P_IPV6))
        return XDP_PASS;

    ip_type = parse_ip6hdr(&nh, data_end, &ipv6hdr);
    if (ip_type != IPPROTO_TCP)
        return XDP_PASS;

    if (parse_udphdr(&nh, data_end, &udphdr) < 0)
        return XDP_PASS;

    // Set key follows userspace.
    key.src_ip = ipv6hdr->saddr;
    key.dst_ip = ipv6hdr->daddr;
    key.src_port = udphdr->source;
    key.dst_port = udphdr->dest;

    // Find the SRTP settings.
    state = bpf_map_lookup_elem(&flow_map, &key);
    if (!state)
        return XDP_PASS;

    // Extract the metadata from the settings.
    int active = state->is_active;
    if (active != 1)
        return XDP_DROP;

    int srtp = state->is_srtp;
    if (!srtp)
        return XDP_PASS;

    int index = ctx->rx_queue_index;
    if (bpf_map_lookup_elem(&xsks_map, &index))
        return bpf_redirect_map(&xsks_map, index, 0);

    return XDP_PASS;
}
