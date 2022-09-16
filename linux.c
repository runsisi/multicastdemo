#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#include <arpa/inet.h>

int get_ifindex(char* optarg) {
    struct in_addr in_addr;
    if (inet_aton(optarg, &in_addr) != 0) {
        // get interface index by addr
        struct ifaddrs *ifaddrs;
        int err = getifaddrs(&ifaddrs);
        if (err < 0) {
            err = errno;
            fprintf(stderr, "failed to get interface address: %s\n", strerror(err));
            exit(1);
        }

        char *ifa_name = NULL;
        for (struct ifaddrs *ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL) {
                continue;
            }
            if (ifa->ifa_addr->sa_family != AF_INET) {
                continue;
            }
            struct sockaddr_in *sock_addr = (struct sockaddr_in *)ifa->ifa_addr;
            if (sock_addr->sin_addr.s_addr != in_addr.s_addr) {
                continue;
            }

            ifa_name = ifa->ifa_name;
            break;
        }

        if (ifa_name == NULL) {
            fprintf(stderr, "interface address not found\n");
            exit(1);
        }

        unsigned idx = if_nametoindex(ifa_name);
        if (idx == 0) {
            err = errno;
            fprintf(stderr, "failed to get interface index: %s\n", strerror(err));
            exit(1);
        }

        freeifaddrs(ifaddrs);

        args->ifindex = idx;
    } else {
        unsigned idx;
        if ((idx = if_nametoindex(optarg)) == 0) {
            int err = errno;
            fprintf(stderr, "interface not found: %s\n", strerror(err));
            exit(1);
        }

        args->ifindex = idx;
    }
}