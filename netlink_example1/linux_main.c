#define _GNU_SOURCE

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include "linux_kernel_mpls_api.h"

int main1() {
    int result;

    // Set the MPLS platform labels to 65535
    result = set_mpls_platform_labels(65535);
    if (result == 0) {
        printf("Successfully set MPLS platform labels to 65535.\n");
    } else {
        printf("Failed to set MPLS platform labels.\n");
    }

    return result;
}

int main2() {
    int result;


    // Enable MPLS on Ethernet1
    result = enable_mpls_on_interface("Ethernet1");
    if (result == 0) {
        printf("Successfully enabled MPLS on Ethernet1.\n");
    } else {
        printf("Failed to enable MPLS on Ethernet1.\n");
    }

    // Enable MPLS on Ethernet2
    result = enable_mpls_on_interface("Ethernet2");
    if (result == 0) {
        printf("Successfully enabled MPLS on Ethernet2.\n");
    } else {
        printf("Failed to enable MPLS on Ethernet2.\n");
    }

    return result;
}

int main3() {
    const char *if_name ="veth0"; // Interface name
    if (add_mpls_route(111, "10.3.3.2", if_name) == 0) {
        printf("MPLS route added successfully.\n");
    } else {
        printf("Failed to add MPLS route.\n");
    }
    return 0;
}

int main4() {
    const char *namespace = "zebosfib0";
    int ns_original;



    // Save the current network namespace
    ns_original = open("/proc/self/ns/net", O_RDONLY);
    if (ns_original < 0) {
        perror("Error saving original namespace");
        return -1;
    }

    // Set the target network namespace
    if (set_namespace(namespace) < 0) {
        setns(ns_original, CLONE_NEWNET);
        close(ns_original);
        return -1;
    }

    // main1();
    // main2();
    main3();

    // Restore the original network namespace
    if (setns(ns_original, CLONE_NEWNET) < 0) {
        perror("Error restoring original namespace");
        close(ns_original);
        return -1;
    }

    close(ns_original);
    return 0;    
}

int main5() {
    const char *dest = "192.168.1.100";  // Destination IP
    const char *gateway = "10.3.3.2"; // Gateway IP
    const char *if_name ="veth0"; // Interface name

    if (add_unicast_route(dest, gateway, if_name) == 0) {
        printf("Route added successfully.\n");
    } else {
        printf("Failed to add route.\n");
    }

    return 0;
}
int main() {
    // mpls add
    main3();
    // unicast add
    // main5();

}