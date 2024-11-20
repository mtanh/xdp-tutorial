/* SPDX-License-Identifier: GPL-2.0 */

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/resource.h>

#include <bpf/bpf.h>
#include <xdp/xsk.h>
#include <xdp/libxdp.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <linux/if_ether.h>
#include <linux/ipv6.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/icmpv6.h>

#include "../common/common_params.h"
#include "../common/common_user_bpf_xdp.h"
#include "../common/common_libbpf.h"

#define NUM_FRAMES 4096
#define FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE
#define RX_BATCH_SIZE 64
#define INVALID_UMEM_FRAME UINT64_MAX

static struct xdp_program *prog;
int xsk_map_fd;
bool custom_xsk = false;
struct config cfg = {
    .ifindex = -1,
};

struct xsk_umem_info
{
    struct xsk_ring_prod fq;
    struct xsk_ring_cons cq;
    struct xsk_umem *umem;
    void *buffer;
};

struct stats_record
{
    uint64_t timestamp;
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t tx_packets;
    uint64_t tx_bytes;
};

struct xsk_socket_info
{
    struct xsk_ring_cons rx;
    struct xsk_ring_prod tx;
    struct xsk_umem_info *umem;
    struct xsk_socket *xsk;

    uint64_t umem_frame_addr[NUM_FRAMES];
    uint32_t umem_frame_free;

    uint32_t outstanding_tx;

    struct stats_record stats;
    struct stats_record prev_stats;
};

static inline __u32 xsk_ring_prod__free(struct xsk_ring_prod *r)
{
    r->cached_cons = *r->consumer + r->size;
    return r->cached_cons - r->cached_prod;
}

static const char *__doc__ = "AF_XDP kernel bypass SRTP (NIC 1)\n";

static bool global_exit;

static struct xsk_umem_info *configure_xsk_umem(void *buffer, uint64_t size)
{
    struct xsk_umem_info *umem;
    int ret;

    umem = calloc(1, sizeof(*umem));
    if (!umem)
    {
        return NULL;
    }

    ret = xsk_umem__create(&umem->umem, buffer, size, &umem->fq, &umem->cq, NULL);
    if (ret)
    {
        errno = -ret;
        return NULL;
    }

    umem->buffer = buffer;
    return umem;
}

static uint64_t xsk_alloc_umem_frame(struct xsk_socket_info *xsk)
{
    uint64_t frame;
    if (xsk->umem_frame_free == 0)
    {
        return INVALID_UMEM_FRAME;
    }

    frame = xsk->umem_frame_addr[--xsk->umem_frame_free];
    xsk->umem_frame_addr[xsk->umem_frame_free] = INVALID_UMEM_FRAME;
    return frame;
}

static void xsk_free_umem_frame(struct xsk_socket_info *xsk, uint64_t frame)
{
    assert(xsk->umem_frame_free < NUM_FRAMES);
    xsk->umem_frame_addr[xsk->umem_frame_free++] = frame;
}

static uint64_t xsk_umem_free_frames(struct xsk_socket_info *xsk)
{
    return xsk->umem_frame_free;
}

static struct xsk_socket_info *xsk_configure_socket(struct config *cfg, struct xsk_umem_info *umem)
{
    struct xsk_socket_config xsk_cfg;
    struct xsk_socket_info *xsk_info;
    uint32_t idx;
    int i;
    int ret;
    uint32_t prog_id;

    xsk_info = calloc(1, sizeof(*xsk_info));
    if (!xsk_info)
    {
        return NULL;
    }

    xsk_info->umem = umem;
    xsk_cfg.rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
    xsk_cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
    xsk_cfg.xdp_flags = cfg->xdp_flags;
    xsk_cfg.bind_flags = cfg->xsk_bind_flags;

    xsk_cfg.libbpf_flags = (custom_xsk) ? XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD : 0;
    ret = xsk_socket__create(&xsk_info->xsk, cfg->ifname,
                             cfg->xsk_if_queue, umem->umem, &xsk_info->rx,
                             &xsk_info->tx, &xsk_cfg);
    if (ret)
    {
        goto error_exit;
    }

    // Enter the AF_XDP socket into the custom program map.
    if (custom_xsk)
    {
        ret = xsk_socket__update_xskmap(xsk_info->xsk, xsk_map_fd);
        if (ret)
        {
            goto error_exit;
        }
    }
    else
    {
        // Getting the program ID must be after the xdp_socket__create() call.
        if (bpf_xdp_query_id(cfg->ifindex, cfg->xdp_flags, &prog_id))
        {
            goto error_exit;
        }
    }

    // Initialize umem frame allocation.
    for (i = 0; i < NUM_FRAMES; i++)
    {
        xsk_info->umem_frame_addr[i] = i * FRAME_SIZE;
    }

    xsk_info->umem_frame_free = NUM_FRAMES;

    // Stuff the receive path with buffers, we assume we have enough.
    ret = xsk_ring_prod__reserve(&xsk_info->umem->fq,
                                 XSK_RING_PROD__DEFAULT_NUM_DESCS,
                                 &idx);

    if (ret != XSK_RING_PROD__DEFAULT_NUM_DESCS)
    {
        goto error_exit;
    }

    for (i = 0; i < XSK_RING_PROD__DEFAULT_NUM_DESCS; i++)
    {
        *xsk_ring_prod__fill_addr(&xsk_info->umem->fq, idx++) = xsk_alloc_umem_frame(xsk_info);
    }

    xsk_ring_prod__submit(&xsk_info->umem->fq, XSK_RING_PROD__DEFAULT_NUM_DESCS);

    return xsk_info;

error_exit:
    errno = -ret;
    return NULL;
}

static void complete_tx(struct xsk_socket_info *xsk)
{
    unsigned int completed;
    uint32_t idx_cq;

    if (!xsk->outstanding_tx)
    {
        return;
    }

    sendto(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);

    // Collect/free completed TX buffers.
    completed = xsk_ring_cons__peek(&xsk->umem->cq,
                                    XSK_RING_CONS__DEFAULT_NUM_DESCS,
                                    &idx_cq);

    if (completed > 0)
    {
        for (int i = 0; i < completed; i++)
        {
            xsk_free_umem_frame(xsk,
                                *xsk_ring_cons__comp_addr(&xsk->umem->cq,
                                                          idx_cq++));
        }

        xsk_ring_cons__release(&xsk->umem->cq, completed);
        xsk->outstanding_tx -= completed < xsk->outstanding_tx ? completed : xsk->outstanding_tx;
    }
}

static inline __sum16 csum16_add(__sum16 csum, __be16 addend)
{
    uint16_t res = (uint16_t)csum;

    res += (__u16)addend;
    return (__sum16)(res + (res < (__u16)addend));
}

static inline __sum16 csum16_sub(__sum16 csum, __be16 addend)
{
    return csum16_add(csum, ~addend);
}

static inline void csum_replace2(__sum16 *sum, __be16 old, __be16 new)
{
    *sum = ~csum16_add(csum16_sub(~(*sum), old), new);
}

static bool process_packet(struct xsk_socket_info *xsk, uint64_t addr, uint32_t len)
{
    uint8_t *pkt = xsk_umem__get_data(xsk->umem->buffer, addr);

    // fprintf(stdout, "ANHMA --- process_packet.\n");

    int ret;
    uint32_t tx_idx = 0;
    uint8_t tmp_mac[ETH_ALEN];
    struct in6_addr tmp_ip;
    struct ethhdr *eth = (struct ethhdr *)pkt;
    uint16_t src_port;
    uint16_t dst_port;

    // Check if it is IPv4 UDP packet.
    if (ntohs(eth->h_proto) == ETH_P_IP)
    {
        struct iphdr *ip = (struct iphdr *)(pkt + sizeof(struct ethhdr));

        // Check if it's a UDP packet
        if (ip->protocol == IPPROTO_UDP)
        {
            struct udphdr *udp = (struct udphdr *)(pkt + sizeof(struct ethhdr) + ip->ihl * 4);

            // Extract UDP source and destination ports
            src_port = ntohs(udp->source);
            dst_port = ntohs(udp->dest);

            printf("ANHMA --- UDP Packet: Source Port: %d, Destination Port: %d\n", src_port, dst_port);
        }
    }
    else if (ntohs(eth->h_proto) == ETH_P_IPV6)
    {
        printf("IPv6 packets not implemented in this example.\n");
        return false;
    }
    else
    {
        // Not a IP packet.
        return false;
    }

    // If it does not come from the given NIC interface:
    // 1/ Perhaps it is from outside. The packet will be treat as
    //    SRTP packet and we try to decode it.
    // 2/ Calculate checksum if decode successfully.
    // 3/ Swap the source and dest IP and MAC address so that
    //    kernel space redirect the packet to the given NIC.

    // If it come from the given NIC interface:
    // 1/ Sannity check, drop if invalid.
    // 2/ Tranmit out the packet.

    memcpy(tmp_mac, eth->h_dest, ETH_ALEN);
    memcpy(eth->h_dest, eth->h_source, ETH_ALEN);
    memcpy(eth->h_source, tmp_mac, ETH_ALEN);
    // csum_replace2()

    // Process one by one packet.
    ret = xsk_ring_prod__reserve(&xsk->tx, 1, &tx_idx);
    if (ret != 1)
    {
        // No more transmit slots, drop the packet.
        return false;
    }

    xsk_ring_prod__tx_desc(&xsk->tx, tx_idx)->addr = addr;
    xsk_ring_prod__tx_desc(&xsk->tx, tx_idx)->len = len;
    xsk_ring_prod__submit(&xsk->tx, 1);
    xsk->outstanding_tx++;

    xsk->stats.tx_bytes += len;
    xsk->stats.tx_packets++;
    return true;
}

static void handle_receive_packets(struct xsk_socket_info *xsk)
{
    unsigned int rcvd, stock_frames, i;
    uint32_t idx_rx = 0, idx_fq = 0;
    int ret;

    rcvd = xsk_ring_cons__peek(&xsk->rx, RX_BATCH_SIZE, &idx_rx);
    if (!rcvd)
    {
        return;
    }

    // Stuff the ring with as much frames as possible.
    stock_frames = xsk_prod_nb_free(&xsk->umem->fq, xsk_umem_free_frames(xsk));

    if (stock_frames > 0)
    {
        ret = xsk_ring_prod__reserve(&xsk->umem->fq, stock_frames,
                                     &idx_fq);

        while (ret != stock_frames)
        {
            ret = xsk_ring_prod__reserve(&xsk->umem->fq, rcvd,
                                         &idx_fq);
        }

        for (i = 0; i < stock_frames; i++)
        {
            *xsk_ring_prod__fill_addr(&xsk->umem->fq, idx_fq++) = xsk_alloc_umem_frame(xsk);
        }

        xsk_ring_prod__submit(&xsk->umem->fq, stock_frames);
    }

    // Process received packets.
    for (i = 0; i < rcvd; i++)
    {
        uint64_t addr = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx)->addr;
        uint32_t len = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx++)->len;

        if (!process_packet(xsk, addr, len))
        {
            xsk_free_umem_frame(xsk, addr);
        }

        xsk->stats.rx_bytes += len;
    }

    xsk_ring_cons__release(&xsk->rx, rcvd);
    xsk->stats.rx_packets += rcvd;

    complete_tx(xsk);
}

static void rx_and_process(struct config *cfg,
                           struct xsk_socket_info *xsk_socket)
{
    struct pollfd fds[2];
    int ret, nfds = 1;

    memset(fds, 0, sizeof(fds));
    fds[0].fd = xsk_socket__fd(xsk_socket->xsk);
    fds[0].events = POLLIN;

    while (!global_exit)
    {
        if (cfg->xsk_poll_mode)
        {
            ret = poll(fds, nfds, -1);
            if (ret <= 0 || ret > 1)
            {
                continue;
            }
        }

        handle_receive_packets(xsk_socket);
    }
}

int main(int argc, char **argv)
{
    int ret;
    void *packet_buffer;
    uint64_t packet_buffer_size;
    DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts);
    DECLARE_LIBXDP_OPTS(xdp_program_opts, xdp_opts, 0);
    struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
    struct xsk_umem_info *umem;
    struct xsk_socket_info *xsk_socket;
    int err;
    char errmsg[1024];

    // Virtual interface test1.
    cfg.ifname = (char *)"test1";
    strncpy(cfg.ifname, optarg, IF_NAMESIZE);
    cfg.ifindex = if_nametoindex(cfg.ifname);
    if (cfg.ifindex == -1)
    {
        fprintf(stderr, "ERROR: Required option --dev missing\n\n");
        return EXIT_FAIL_OPTION;
    }

    // Kernel file name and program name.
    strncpy(cfg.filename, "srtp_kern_1.o", sizeof(cfg.filename));
    strncpy(cfg.progname, "xdp_sock_prog", sizeof(cfg.progname));

    // Load kernel custom XDP program.
    struct bpf_map *map;
    custom_xsk = true;

    prog = xdp_program__create(&xdp_opts);
    err = libxdp_get_error(prog);
    if (err)
    {
        libxdp_strerror(err, errmsg, sizeof(errmsg));
        fprintf(stderr, "ERR: loading program: %s\n", errmsg);
        return err;
    }

    err = xdp_program__attach(prog, cfg.ifindex, XDP_MODE_SKB, 0);
    if (err)
    {
        libxdp_strerror(err, errmsg, sizeof(errmsg));
        fprintf(stderr, "Couldn't attach XDP program on iface '%s' : %s (%d)\n", cfg.ifname, errmsg, err);
        return err;
    }

    // Load the xsks_map.
    map = bpf_object__find_map_by_name(xdp_program__bpf_obj(prog), "xsks_map");
    xsk_map_fd = bpf_map__fd(map);
    if (xsk_map_fd < 0)
    {
        fprintf(stderr, "ERROR: no xsks map found: %s\n", strerror(xsk_map_fd));
        exit(EXIT_FAILURE);
    }

    // Lock the memory area.
    if (setrlimit(RLIMIT_MEMLOCK, &rlim))
    {
        fprintf(stderr, "ERROR: setrlimit(RLIMIT_MEMLOCK) \"%s\"\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Allocate buffer.
    packet_buffer_size = NUM_FRAMES * FRAME_SIZE;
    if (posix_memalign(&packet_buffer,
                       getpagesize(), /* PAGE_SIZE aligned */
                       packet_buffer_size))
    {
        fprintf(stderr, "ERROR: Can't allocate buffer memory \"%s\"\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Initialize shared packet_buffer for umem usage */
    umem = configure_xsk_umem(packet_buffer, packet_buffer_size);
    if (umem == NULL)
    {
        fprintf(stderr, "ERROR: Can't create umem \"%s\"\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Configure the AF_XDP socket.
    xsk_socket = xsk_configure_socket(&cfg, umem);
    if (xsk_socket == NULL)
    {
        fprintf(stderr, "ERROR: Can't setup AF_XDP socket \"%s\"\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Receive and process packets.
    rx_and_process(&cfg, xsk_socket);

    // Cleanup.
    xsk_socket__delete(xsk_socket->xsk);
    xsk_umem__delete(umem->umem);

    return EXIT_OK;
}