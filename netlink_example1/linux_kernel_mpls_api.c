#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "linux/rtnetlink.h"
#include "linux/mpls.h"
#include "linux/kernel.h"
#include <net/if.h>

int set_mpls_platform_labels(int value) {
    int fd;
    char buf[10];

    // Open the sysctl file for writing
    fd = open("/proc/sys/net/mpls/platform_labels", O_WRONLY);
    if (fd < 0) {
        perror("Error opening sysctl file");
        return -1;
    }

    // Write the value to the file
    snprintf(buf, sizeof(buf), "%d", value);
    if (write(fd, buf, strlen(buf)) < 0) {
        perror("Error writing to sysctl file");
        close(fd);
        return -1;
    }

    // Close the file
    close(fd);
    return 0;
}

int set_namespace(const char *namespace) {
    char ns_path[1024];
    int ns_fd;

    snprintf(ns_path, sizeof(ns_path), "/var/run/netns/%s", namespace);
    ns_fd = open(ns_path, O_RDONLY);
    if (ns_fd < 0) {
        perror("Error opening namespace");
        return -1;
    }

    if (setns(ns_fd, CLONE_NEWNET) < 0) {
        perror("Error setting namespace");
        close(ns_fd);
        return -1;
    }

    close(ns_fd);
    return 0;
}

int enable_mpls_on_interface(const char *interface) {
    char path[256];
    int fd;
    char value[] = "1";

    // Construct the sysctl file path
    snprintf(path, sizeof(path), "/proc/sys/net/mpls/conf/%s/input", interface);

    // Open the sysctl file for writing
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("Error opening sysctl file");
        return -1;
    }

    // Write the value to the file
    if (write(fd, value, strlen(value)) < 0) {
        perror("Error writing to sysctl file");
        close(fd);
        return -1;
    }

    // Close the file
    close(fd);


}

#define BUFFER_SIZE 4096

/*
int add_mpls_route(int label, const char *nexthop_ip) {
    struct {
        struct nlmsghdr nlh;
        struct rtmsg rtm;
        char buffer[8192];
    } req;

    struct inet_prefix {
          u_char family;
          u_char prefixlen;
          u_char pad1;
          u_char pad2;
          union
          {
            u_char prefix;
            uint32_t prefix4;
          } u;
    };

    struct sockaddr_nl src_addr;
    struct sockaddr_nl dst_addr;
    int sockfd;

    // Open socket 
    if ((sockfd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE)) < 0) {
        perror("socket creation failed\n");
        return -1;
    }
     bzero(&src_addr, sizeof(src_addr));
     src_addr.nl_family = AF_NETLINK;
     src_addr.nl_pid = getpid();
     src_addr.nl_groups = 0;

     if(bind(sockfd, (struct sockaddr*) &src_addr, sizeof(src_addr)) < 0){
        printf("Bind failed\n");
        return -1;
     }

     bzero(&dst_addr, sizeof(dst_addr));
     dst_addr.nl_family = AF_NETLINK;
     dst_addr.nl_pid = 0;
     dst_addr.nl_groups = 0;

    struct iovec iov = { .iov_base = &req, .iov_len = req.nlh.nlmsg_len };
    struct msghdr msg = { .msg_name = &dst_addr, .msg_namelen = sizeof(dst_addr), .msg_iov = &iov, .msg_iovlen = 1 };
    struct rtattr *rta;
    uint32_t mpls_label = htonl(label << MPLS_LS_LABEL_SHIFT);

    // Initialize the request
    memset(&req, 0, sizeof(req));
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    req.nlh.nlmsg_type = RTM_NEWROUTE;
    req.nlh.nlmsg_pid = getpid();
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE;
    req.nlh.nlmsg_seq = 0;

    req.rtm.rtm_family = AF_MPLS;
    req.rtm.rtm_dst_len = 20; // MPLS label size

    // Add MPLS attributes
    rta = (struct rtattr *) (((char *) &req) + NLMSG_ALIGN(req.nlh.nlmsg_len));
    rta->rta_type = RTA_DST;
    rta->rta_len = RTA_LENGTH(4);
    memcpy(RTA_DATA(rta), &mpls_label, 4);
    req.nlh.nlmsg_len = NLMSG_ALIGN(req.nlh.nlmsg_len) + RTA_LENGTH(4);

    // Add nexthop IP address
    struct in_addr nexthop_addr;
    struct inet_prefix dst;
    dst.family = AF_INET;
    dst.prefixlen = 4; // For IPv4
    if (!inet_pton(AF_INET, nexthop_ip, &dst.u.prefix4)) {
        perror("inet_pton failed for nexthop IP");
        return -1;
    }

    printf("nexthop_addr 0x%x\n", dst.u.prefix4);

    rta = (struct rtattr *) (((char *) &req) + NLMSG_ALIGN(req.nlh.nlmsg_len));
    rta->rta_type = RTA_GATEWAY;
    rta->rta_len = RTA_LENGTH(sizeof(struct in_addr));
    memcpy(RTA_DATA(rta), &dst.u.prefix4, sizeof(struct in_addr));
    req.nlh.nlmsg_len = NLMSG_ALIGN(req.nlh.nlmsg_len) + RTA_LENGTH(sizeof(struct in_addr));


    //  send the request
    if (sendmsg(sockfd, &msg, 0) < 0) {
        perror("sendmsg");
        close(sockfd);
        return -1;
    }

    printf("Waiting for message from kernel\n");

    // Receive the response
    if (recvmsg(sockfd, &msg, 0) < 0) {
        perror("recvmsg");
        close(sockfd);
        return -1;
    }

    struct nlmsghdr *nlh = &req.nlh;
    struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nlh);

    // Check if the response is an error
    if (nlh->nlmsg_type == NLMSG_ERROR && err->error != 0) {
        fprintf(stderr, "Error code: %d\n", err->error);
        fprintf(stderr, "Netlink reported error: %s\n", strerror(-err->error));
        close(sockfd);
        return -1;
    }

    fprintf(stdout, "sucessfully added mpls route\n");
    
    close(sockfd);
    return 0;
}


*/

unsigned int get_if_index(const char *if_name) {
    unsigned int if_index = if_nametoindex(if_name);
    if (if_index == 0) {
        perror("if_nametoindex");
    }
    return if_index;
}


int add_mpls_route(int label, const char *gateway, const char *if_name) {
    int sockfd;
    struct sockaddr_nl sa;
    struct nlmsghdr *nlh;
    struct rtmsg *rtm;
    struct rtattr *rta;
    char buffer[BUFFER_SIZE];
    struct in_addr dst_addr, gw_addr;
    int rta_len = sizeof(struct rtattr);
    int if_index;
    // Create a Netlink socket
    sockfd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    // Initialize the buffer
    memset(buffer, 0, BUFFER_SIZE);
    nlh = (struct nlmsghdr *)buffer;
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    nlh->nlmsg_type = RTM_NEWROUTE;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_ACK| NLM_F_EXCL;
    nlh->nlmsg_seq = 0;
    nlh->nlmsg_pid = getpid();

    rtm = (struct rtmsg *)(NLMSG_DATA(nlh));
    // rtm->rtm_family = AF_INET;
    rtm->rtm_family = AF_MPLS;
    rtm->rtm_table = RT_TABLE_MAIN;
    rtm->rtm_protocol = RTPROT_BOOT;
    rtm->rtm_scope = RT_SCOPE_UNIVERSE;
    rtm->rtm_type = RTN_UNICAST;
    // rtm->rtm_dst_len = 32;
    rtm->   = 20; // MPLS label size
    rtm->rtm_src_len = 0;
    rtm->rtm_tos = 0;
    rtm->rtm_flags = 0;

    // Add MPLS attributes
    rta = (struct rtattr *)(((char *)nlh) + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = RTA_DST;
    uint32_t mpls_label = htonl((label << 12) | (1 << 8)); // Label and TTL set
    rta->rta_len = rta_len + RTA_LENGTH(4);
    memcpy(RTA_DATA(rta), &mpls_label, 4);
    fprintf(stdout," label %d mpls_label 0x%x\n", label, mpls_label);

    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_LENGTH(4);
    fprintf(stdout, "rta_type %d nlmsg_len %d \n", rta->rta_type, nlh->nlmsg_len);

    // Gateway
    inet_pton(AF_INET, gateway, &gw_addr);
    rta = (struct rtattr *)(((char *)nlh) + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = RTA_VIA;
    rta->rta_len = rta_len + RTA_LENGTH(sizeof(gw_addr));
    memcpy(RTA_DATA(rta), &gw_addr, sizeof(gw_addr));
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_LENGTH(sizeof(gw_addr));
    fprintf(stdout, "rta_type %d nlmsg_len %d \n", rta->rta_type, nlh->nlmsg_len);

    if_index = get_if_index(if_name);
    // Network interface index
    fprintf(stderr, "if_name %s if_index %d\n", if_name, if_index);
    rta = (struct rtattr *)(((char *)nlh) + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = RTA_OIF;
    rta->rta_len =  rta_len + RTA_LENGTH(sizeof(if_index));
    memcpy(RTA_DATA(rta), &if_index, sizeof(if_index));
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_LENGTH(sizeof(if_index));
    fprintf(stdout, "rta_type %d nlmsg_len %d \n", rta->rta_type, nlh->nlmsg_len);

    // Send the message
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    if (sendto(sockfd, nlh, nlh->nlmsg_len, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("sendto");
        close(sockfd);
        return -1;
    }

    // Prepare to receive the response
    struct iovec iov = {
        .iov_base = buffer,
        .iov_len = nlh->nlmsg_len
    };
    struct msghdr msg = {
        .msg_name = &sa,
        .msg_namelen = sizeof(sa),
        .msg_iov = &iov,
        .msg_iovlen = 1
    };

    // Receive the response
    if (recvmsg(sockfd, &msg, 0) < 0) {
        perror("recvmsg");
        close(sockfd);
        return -1;
    }

    // Check if the response is an error
    struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nlh);
    if (nlh->nlmsg_type == NLMSG_ERROR && err->error != 0) {
        fprintf(stderr, "Error code: %d\n", err->error);
        fprintf(stderr, "Netlink reported error: %s\n", strerror(-err->error));
        close(sockfd);
        return -1;
    }

    fprintf(stdout, "sucessfully added mpls route\n");
    close(sockfd);
    return 0;
}


int add_unicast_route(const char *destination, const char *gateway, const char *if_name) {
    int sockfd;
    struct sockaddr_nl sa;
    struct nlmsghdr *nlh;
    struct rtmsg *rtm;
    struct rtattr *rta;
    char buffer[BUFFER_SIZE];
    struct in_addr dst_addr, gw_addr;
    int rta_len = sizeof(struct rtattr);
    int if_index;
    // Create a Netlink socket
    sockfd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    // Initialize the buffer
    memset(buffer, 0, BUFFER_SIZE);
    nlh = (struct nlmsghdr *)buffer;
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    nlh->nlmsg_type = RTM_NEWROUTE;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE;
    nlh->nlmsg_seq = 0;
    nlh->nlmsg_pid = getpid();

    rtm = (struct rtmsg *)(NLMSG_DATA(nlh));
    rtm->rtm_family = AF_INET;
    rtm->rtm_table = RT_TABLE_MAIN;
    rtm->rtm_protocol = RTPROT_BOOT;
    rtm->rtm_scope = RT_SCOPE_UNIVERSE;
    rtm->rtm_type = RTN_UNICAST;
    rtm->rtm_dst_len = 32;

    // Destination address
    inet_pton(AF_INET, destination, &dst_addr);
    rta = (struct rtattr *)(((char *)nlh) + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = RTA_DST;
    rta->rta_len = rta_len + sizeof(dst_addr);
    memcpy(RTA_DATA(rta), &dst_addr, sizeof(dst_addr));
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_LENGTH(sizeof(dst_addr));

    // Gateway
    inet_pton(AF_INET, gateway, &gw_addr);
    rta = (struct rtattr *)(((char *)nlh) + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = RTA_GATEWAY;
    rta->rta_len = rta_len + sizeof(gw_addr);
    memcpy(RTA_DATA(rta), &gw_addr, sizeof(gw_addr));
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_LENGTH(sizeof(gw_addr));

    if_index = get_if_index(if_name);
    // Network interface index
    rta = (struct rtattr *)(((char *)nlh) + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = RTA_OIF;
    rta->rta_len = rta_len + sizeof(if_index);
    memcpy(RTA_DATA(rta), &if_index, sizeof(if_index));
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_LENGTH(sizeof(if_index));

    // Send the message
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    if (sendto(sockfd, nlh, nlh->nlmsg_len, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("sendto");
        close(sockfd);
        return -1;
    }

    close(sockfd);
    return 0;
}
