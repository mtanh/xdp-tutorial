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

SEC("xdp")
int udp_filter(struct xdp_md *ctx)
{
    int eth_type;
    int ip_type;
    int udp_len;
    struct ethhdr *eth;
    struct iphdr *iphdr;
    struct udphdr *udphdr;

    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    struct hdr_cursor nh = {.pos = data};

    eth_type = parse_ethhdr(&nh, data_end, &eth);
    if (eth_type != bpf_htons(ETH_P_IP))
        return XDP_PASS;

    ip_type = parse_iphdr(&nh, data_end, &iphdr);
    if (ip_type != IPPROTO_UDP)
        return XDP_PASS;

    udp_len = parse_udphdr(&nh, data_end, &udphdr);
    if (udp_len < 0)
        return XDP_PASS;

    bpf_printk("UDP packet: src_port=%d, dest_port=%d, length=%d\n",
               bpf_ntohs(udphdr->source), bpf_ntohs(udphdr->dest), udp_len);

    return XDP_PASS;
}

SEC("xdp")
int xdp_icmp_echo_func(struct xdp_md *ctx)
{
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    struct hdr_cursor nh;
    struct ethhdr *eth;
    int eth_type;
    int ip_type;
    // int icmp_type;
    struct iphdr *iphdr;
    struct ipv6hdr *ipv6hdr;
    // __u16 echo_reply;
    // struct icmphdr_common *icmphdr;
    // __u32 action = XDP_PASS;

    bpf_printk("xdp_icmp_echo_func --- %d", __LINE__);

    /* These keep track of the next header type and iterator pointer */
    nh.pos = data;

    bpf_printk("xdp_icmp_echo_func --- %d", __LINE__);

    /* Parse Ethernet and IP/IPv6 headers */
    eth_type = parse_ethhdr(&nh, data_end, &eth);
    if (eth_type == bpf_htons(ETH_P_IP))
    {
        bpf_printk("xdp_icmp_echo_func --- %d", __LINE__);

        ip_type = parse_iphdr(&nh, data_end, &iphdr);
        if (ip_type != IPPROTO_ICMP)
        {
            goto out;
        }

        bpf_printk("IPv4:");
        __be32 src_ip = iphdr->saddr;
        __be32 dest_ip = iphdr->daddr;
        bpf_printk("source : ");
        bpf_printk("%d ", src_ip & 0xFF);
        bpf_printk("%d ", (src_ip >> 8) & 0xFF);
        bpf_printk("%d ", (src_ip >> 16) & 0xFF);
        bpf_printk("%d ", (src_ip >> 24) & 0xFF);

        bpf_printk("dest  : ");
        bpf_printk("%d ", dest_ip & 0xFF);
        bpf_printk("%d ", (dest_ip >> 8) & 0xFF);
        bpf_printk("%d ", (dest_ip >> 16) & 0xFF);
        bpf_printk("%d ", (dest_ip >> 24) & 0xFF);
    }
    else if (eth_type == bpf_htons(ETH_P_IPV6))
    {
        ip_type = parse_ip6hdr(&nh, data_end, &ipv6hdr);
        if (ip_type != IPPROTO_ICMPV6)
        {
            goto out;
        }
    }
    else
    {
        goto out;
    }

out:
    return XDP_PASS;
}

SEC("xdp")
int handle_outside(struct xdp_md *ctx)
{
    int eth_type, ip_type;
    struct ethhdr *eth;
    struct iphdr *iphdr;
    // struct ipv6hdr *ipv6hdr;
    struct udphdr *udphdr;
    // struct tcphdr *tcphdr;
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    struct hdr_cursor nh = {.pos = data};

    eth_type = parse_ethhdr(&nh, data_end, &eth);
    if (eth_type < 0)
    {
        bpf_printk("ANHMA --- %d", __LINE__);
        return XDP_ABORTED;
    }

    if (eth_type == bpf_htons(ETH_P_IP))
    {
        ip_type = parse_iphdr(&nh, data_end, &iphdr);
    }
    else if (eth_type == bpf_htons(ETH_P_IPV6))
    {
        bpf_printk("ANHMA --- %d", __LINE__);
        return XDP_PASS;
    }
    else
    {
        return XDP_PASS;
    }

    if (ip_type == IPPROTO_UDP)
    {
        if (parse_udphdr(&nh, data_end, &udphdr) < 0)
        {
            return XDP_ABORTED;
        }
    }
    else if (ip_type == IPPROTO_ICMP)
    {
        bpf_printk("ANHMA --- %d", __LINE__);
        return XDP_PASS;
    }
    else if (ip_type == IPPROTO_ICMPV6)
    {
        bpf_printk("ANHMA --- %d", __LINE__);
        return XDP_PASS;
    }
    else
    {
        return XDP_PASS;
    }

    // Dump MAC address.
    if (eth && eth->h_source[0] != '\0' && eth->h_dest[0] != '\0')
    {
        bpf_printk("MAC:");
        bpf_printk("source : ");
        bpf_printk("%02x ", eth->h_source[0]);
        bpf_printk("%02x ", eth->h_source[1]);
        bpf_printk("%02x ", eth->h_source[2]);
        bpf_printk("%02x ", eth->h_source[3]);
        bpf_printk("%02x ", eth->h_source[4]);
        bpf_printk("%02x ", eth->h_source[5]);

        bpf_printk("dest   : ");
        bpf_printk("%02x ", eth->h_dest[0]);
        bpf_printk("%02x ", eth->h_dest[1]);
        bpf_printk("%02x ", eth->h_dest[2]);
        bpf_printk("%02x ", eth->h_dest[3]);
        bpf_printk("%02x ", eth->h_dest[4]);
        bpf_printk("%02x ", eth->h_dest[5]);
    }

    // Dump IPv4 address.
    if (iphdr)
    {
        bpf_printk("IPv4:");
        __be32 src_ip = iphdr->saddr;
        __be32 dest_ip = iphdr->daddr;
        bpf_printk("source : ");
        bpf_printk("%d ", src_ip & 0xFF);
        bpf_printk("%d ", (src_ip >> 8) & 0xFF);
        bpf_printk("%d ", (src_ip >> 16) & 0xFF);
        bpf_printk("%d ", (src_ip >> 24) & 0xFF);

        bpf_printk("dest  : ");
        bpf_printk("%d ", dest_ip & 0xFF);
        bpf_printk("%d ", (dest_ip >> 8) & 0xFF);
        bpf_printk("%d ", (dest_ip >> 16) & 0xFF);
        bpf_printk("%d ", (dest_ip >> 24) & 0xFF);
    }

    // Dump UDP ports.
    if (udphdr)
    {
        bpf_printk("UDP:");
        bpf_printk("  source : %d", bpf_ntohs(udphdr->source));
        bpf_printk("  dest   : %d", bpf_ntohs(udphdr->dest));
    }

    return XDP_PASS;
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
    bpf_printk("src_mac[0]: %02x\n", src_mac[0]);
    bpf_printk("src_mac[1]: %02x\n", src_mac[1]);
    bpf_printk("src_mac[2]: %02x\n", src_mac[2]);
    bpf_printk("src_mac[3]: %02x\n", src_mac[3]);
    bpf_printk("src_mac[4]: %02x\n", src_mac[4]);
    bpf_printk("src_mac[5]: %02x\n", src_mac[5]);

    // unsigned char *dest_mac = eth->h_dest;
    // bpf_printk("dest_mac[0]: %02x\n", dest_mac[0]);
    // bpf_printk("dest_mac[1]: %02x\n", dest_mac[1]);
    // bpf_printk("dest_mac[2]: %02x\n", dest_mac[2]);
    // bpf_printk("dest_mac[3]: %02x\n", dest_mac[3]);
    // bpf_printk("dest_mac[4]: %02x\n", dest_mac[4]);
    // bpf_printk("dest_mac[5]: %02x\n", dest_mac[5]);

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
