int set_mpls_platform_labels(int value);
int enable_mpls_on_interface(const char *interface);
// int add_mpls_route(int label, const char *nexthop_ip);
int add_mpls_route(int label, const char *gateway, const char *if_name);
int set_namespace(const char *namespace);
int add_unicast_route(const char *destination, const char *gateway, const char *if_name);