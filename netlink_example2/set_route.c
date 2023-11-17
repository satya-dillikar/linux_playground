/*
 *
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>

/* Helper structure for ip address data and attributes */
typedef struct {
    char family;
    char bitlen;
    unsigned char data[sizeof(struct in6_addr)];
} _inet_addr;

/* Simple parser of the string IP address
 */
int read_addr(char *addr, _inet_addr *res)
{
    if (strchr(addr, ':')) {
        res->family = AF_INET6;
        res->bitlen = 128;
    } else {
        res->family = AF_INET;
        res->bitlen = 32;
    }

    return inet_pton(res->family, addr, res->data);
}

int get_addr_str(_inet_addr *res, char addr_str[INET6_ADDRSTRLEN]) {
    if (res->family == AF_INET) {
        if (inet_ntop(AF_INET, res->data, addr_str, INET_ADDRSTRLEN) == NULL) {
            return -1;
        }
    } else if (res->family == AF_INET6) {
        if (inet_ntop(AF_INET6, res->data, addr_str, INET6_ADDRSTRLEN) == NULL) {
            return -1;
        }
    } else {
        return -1;
    }

    // printf("Address: %s\n", addr_str);
    return 0;
}

#define NEXT_CMD_ARG() do { argv++; if (--argc <= 0) exit(-1); } while(0)

/* Open netlink socket */
int open_netlink()
{
    struct sockaddr_nl saddr;

    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

    if (sock < 0) {
        perror("Failed to open netlink socket");
        return -1;
    }

    memset(&saddr, 0, sizeof(saddr));

    return sock;
}


/* */

#define BUFFER_SIZE 4096
#define NLMSG_TAIL(nmsg) \
    ((struct rtattr *) (((void *) (nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

/* Add new data to rtattr */
int rtattr_add(struct nlmsghdr *n, int maxlen, int type, const void *data, int alen)
{
    int len = RTA_LENGTH(alen);
    struct rtattr *rta;

    // fprintf(stdout,"Enter rtattr_add type %d maxlen %d \n", type, maxlen);
    if (NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len) > maxlen) {
        fprintf(stderr, "rtattr_add error: message exceeded bound of %d\n", maxlen);
        return -1;
    }

    rta = NLMSG_TAIL(n);
    rta->rta_type = type;
    rta->rta_len = len; 

    if (alen) {
        memcpy(RTA_DATA(rta), data, alen);
    }

    n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);

    return 0;
}

int do_route(int sock, int cmd, int flags, _inet_addr *dst, _inet_addr *gw, int def_gw, int if_idx, uint32_t mpls_label){
    struct {
        struct nlmsghdr n;
        struct rtmsg r;
        char buf[BUFFER_SIZE];
    } nl_request;

    char dst_addr_str[INET6_ADDRSTRLEN];
    char gw_addr_str[INET6_ADDRSTRLEN];

    fprintf(stdout,"Enter do_route\n");
    get_addr_str(dst, dst_addr_str);
    get_addr_str(gw, gw_addr_str);
    fprintf(stdout, "do_route: cmd=%d, flags=%d, dst=%s, gw=%s, def_gw=%d, if_idx=%d, mpls_label=%d\n",
         cmd, flags, dst_addr_str, gw_addr_str, def_gw, if_idx, mpls_label);
    
    /* Initialize request structure */
    nl_request.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    nl_request.n.nlmsg_flags = NLM_F_REQUEST | flags;
    nl_request.n.nlmsg_type = cmd;
    nl_request.r.rtm_family = dst->family;
    nl_request.r.rtm_table = RT_TABLE_MAIN;
    nl_request.r.rtm_scope = RT_SCOPE_NOWHERE;

    /* Set additional flags if NOT deleting route */
    if (cmd != RTM_DELROUTE) {
        nl_request.r.rtm_protocol = RTPROT_BOOT;
        nl_request.r.rtm_type = RTN_UNICAST;
    }

    nl_request.r.rtm_family = dst->family;
    nl_request.r.rtm_dst_len = dst->bitlen;

    /* Select scope, for simplicity we supports here only IPv6 and IPv4 */
    if (nl_request.r.rtm_family == AF_INET6) {
        nl_request.r.rtm_scope = RT_SCOPE_UNIVERSE;
    } else {
        nl_request.r.rtm_scope = RT_SCOPE_LINK;
    }

    /* Set MPLS label */
    if (mpls_label != 0 ){
       nl_request.r.rtm_scope = RT_SCOPE_UNIVERSE;
       nl_request.r.rtm_family = AF_MPLS;
       nl_request.r.rtm_dst_len = 20; // MPLS label size
       nl_request.r.rtm_flags = 0;

    }
    /* Set gateway */
    if (gw->bitlen != 0) {
        if (mpls_label != 0){
          rtattr_add(&nl_request.n, sizeof(nl_request), RTA_GATEWAY, &gw->data, gw->bitlen / 8);
        } else {
          rtattr_add(&nl_request.n, sizeof(nl_request), RTA_VIA, &gw->data, gw->bitlen / 8);
        }
        nl_request.r.rtm_scope = 0;
        nl_request.r.rtm_family = gw->family;
    }

    /* Don't set destination and interface in case of default gateways */
    if (!def_gw) {
          if (mpls_label != 0){
            /* Set mpls label */
            uint32_t data_label = htonl((mpls_label << 12) | (1 << 8)); // Label and TTL set
            rtattr_add(&nl_request.n, sizeof(nl_request), /*RTA_NEWDST*/ RTA_DST, &data_label, 4);          
          } else {
            /* Set destination network */
            rtattr_add(&nl_request.n, sizeof(nl_request), /*RTA_NEWDST*/ RTA_DST, &dst->data, dst->bitlen / 8);
          }
        /* Set interface */
        rtattr_add(&nl_request.n, sizeof(nl_request), RTA_OIF, &if_idx, sizeof(int));
    }

    /* Send message to the netlink */
    // send(sock, &nl_request, sizeof(nl_request), 0);

    struct sockaddr_nl dst_addr;

    bzero(&dst_addr, sizeof(dst_addr));
    dst_addr.nl_family = AF_NETLINK;
    dst_addr.nl_pid = 0;
    dst_addr.nl_groups = 0;

    // Prepare to receive the response
    struct iovec iov = {
        .iov_base = &nl_request,
        .iov_len = nl_request.n.nlmsg_len
    };
    struct msghdr msg = {
        .msg_name = &dst_addr,
        .msg_namelen = sizeof(dst_addr),
        .msg_iov = &iov,
        .msg_iovlen = 1
    };    
    //  send the request
    if (sendmsg(sock, &msg, 0) < 0) {
        perror("sendmsg");
        close(sock);
        return -1;
    }
    if (cmd != RTM_DELROUTE) {
        printf("Waiting for message from kernel\n");    
        // Receive the response
        if (recvmsg(sock, &msg, 0) < 0) {
            perror("recvmsg");
            close(sock);
            return -1;
        }

        struct nlmsghdr *nlh = &nl_request.n;
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nlh);
        // Check if the response is an error
        if (nlh->nlmsg_type == NLMSG_ERROR && err->error != 0) {
            fprintf(stderr, "Error code: %d\n", err->error);
            fprintf(stderr, "Netlink reported error: %s\n", strerror(-err->error));
            close(sock);
            return -1;
        }
    }
    fprintf(stdout, "sucessfully  mpls route\n");
    close(sock);
    return 0;    
}



int main(int argc, char **argv)
{
    int default_gw = 0;
    int if_idx = 0;
    int nl_sock;
    _inet_addr to_addr = { 0 };
    _inet_addr gw_addr = { 0 };

    int nl_cmd;
    int nl_flags;
    uint32_t mpls_label = 0;

    /* Parse command line arguments */
    while (argc > 0) {
        //  fprintf(stdout, "scanning %s\n", *argv);
        if (strcmp(*argv, "add") == 0) {
            nl_cmd = RTM_NEWROUTE;
            nl_flags = NLM_F_CREATE | NLM_F_EXCL| NLM_F_ACK| NLM_F_EXCL;

        } else if (strcmp(*argv, "del") == 0) {
            nl_cmd = RTM_DELROUTE;
            nl_flags = 0;

        } else if (strcmp(*argv, "to") == 0) {
            NEXT_CMD_ARG(); /* skip "to" and jump to the actual destination addr */

            if (read_addr(*argv, &to_addr) != 1) {
                fprintf(stderr, "Failed to parse destination network %s\n", *argv);
                exit(-1);
            }

        } else if (strcmp(*argv, "mpls") == 0) {
            NEXT_CMD_ARG(); /* skip "mpls" and jump to the actual lable value */
            mpls_label = atoi(*argv);
            fprintf(stdout, "parse mpls_label %s\n", *argv);

        } else if (strcmp(*argv, "dev") == 0) {
            NEXT_CMD_ARG(); /* skip "dev" */

            if_idx = if_nametoindex(*argv);

        } else if (strcmp(*argv, "via") == 0) {
            NEXT_CMD_ARG(); /* skip "via"*/

            /* Instead of gw address user can set here keyword "default" */
            /* Try to read this keyword and jump to the actual gateway addr */
            if (strcmp(*argv, "default") == 0) {
                default_gw = 1;
                // NEXT_CMD_ARG();
            } else if (read_addr(*argv, &gw_addr) != 1) {
                fprintf(stderr, "Failed to parse gateway address %s\n", *argv);
                exit(-1);
            }
        }

        argc--; argv++;
    }

    nl_sock = open_netlink();

    if (nl_sock < 0) {
        exit(-1);
    }

    do_route(nl_sock, nl_cmd, nl_flags, &to_addr, &gw_addr, default_gw, if_idx, mpls_label);

    close (nl_sock);

    return 0;
}