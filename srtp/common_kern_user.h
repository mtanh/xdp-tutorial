#ifndef __COMMON_KERN_USER_H
#define __COMMON_KERN_USER_H

#include <linux/if_ether.h>
#include <linux/ipv6.h>
#include <linux/ip.h>

struct srtp_key_ipv4
{
    struct in_addr src_ip;
    struct in_addr dst_ip;
    __be16 src_port;
    __be16 dst_port;
};

struct srtp_key_ipv6
{
    struct in6_addr src_ip;
    struct in6_addr dst_ip;
    __be16 src_port;
    __be16 dst_port;
};

struct srtp_state
{
    int is_srtp;
    int is_active;
    int ifindex_out;
};

#endif /* __COMMON_KERN_USER_H */
