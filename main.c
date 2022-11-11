#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>

#define MULTICAST_PORT 8101
#define RCV_BUF_SIZE (64 << 10)

struct args {
    struct in_addr addr;    // multicast group
    int port;               // multicast port
    unsigned ifindex;       // interface for multicast
    int server;
    int client;
};

static void usage() {
    const char *usage_text =
            "UDP multicast demo.\n\n"
            "%s options address\n\n"
            "  -h, --help    Print this help\n"
            "  -p, --port    UDP port (default 8101)\n"
            "  --iface       Interface (either name or IP address)\n"
            "  -s, --server  Running in server mode\n"
            "  -c, --client  Running in client mode\n\n";
    printf(usage_text, "multicast");
}

static void parse_args(int argc, char **argv, struct args *args) {
    enum {
        OPT_IFACE = 'z' + 1,
    };

    char *short_opts = "hp:sc";
    struct option long_opts[] = {
        {"help",   no_argument,       NULL, 'h'},
        {"port",   required_argument, NULL, 'p'},
        {"iface",  required_argument, NULL, OPT_IFACE},
        {"server", no_argument,       NULL, 's'},
        {"client", no_argument,       NULL, 'c'},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
        switch (opt) {
        case 'p': {
            int port = atoi(optarg);
            if (port < 1 || port > 65535) {
                fprintf(stderr, "invalid port: %s\n", optarg);
                exit(1);
            }
            args->port = htons(port);
            break;
        }
        case OPT_IFACE: {
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

                struct ifaddrs *ifa;
                for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
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

#ifdef _WIN32
                    struct ifaddrs_hwdata *ifa_data = (struct ifaddrs_hwdata *)ifa->ifa_data;
                    args->ifindex = ifa_data->ifa_ifindex;
#else
                    int ifa_index = if_nametoindex(ifa->ifa_name);
                    if (ifa_index == 0) {
                        err = errno;
                        fprintf(stderr, "failed to get interface index: %s\n", strerror(err));
                        exit(1);
                    }

                    args->ifindex = ifa_index;
#endif
                    break;
                }

                freeifaddrs(ifaddrs);

                if (ifa == NULL) {
                    fprintf(stderr, "address not found\n");
                    exit(1);
                }
            }
#ifndef _WIN32
            // interface name is useless under windows, do not use it!
            // cygwin also has inconsistency here
            // https://stackoverflow.com/questions/8978670/what-do-windows-interface-names-look-like
            else {
                unsigned idx;
                if ((idx = if_nametoindex(optarg)) == 0) {
                    int err = errno;
                    fprintf(stderr, "interface not found: %s\n", strerror(err));
                    exit(1);
                }

                args->ifindex = idx;
            }
#endif
            break;
        }
        case 's': {
            args->server = 1;
            break;
        }
        case 'c': {
            args->client = 1;
            break;
        }
        case 'h': {
            usage();
            exit(0);
        }
        default: {
            usage();
            exit(1);
        }
        }
    }

    // validate options
    if (!(args->server ^ args->client)) {
        fprintf(stderr, "mode not specified or invalid (either server or client)\n");
        usage();
        exit(1);
    }

    if (args->ifindex == 0) {
        fprintf(stderr, "either interface name or address must be specified\n");
        usage();
        exit(1);
    }

    if (argc != optind + 1) {
        fprintf(stderr, "missing positional argument for multicast group address\n");
        usage();
        exit(1);
    }

    // handle positional args
    struct in_addr addr;    // multicast group address
    if (inet_aton(argv[optind++], &addr) == 0) {
        fprintf(stderr, "malformed multicast address: %s\n", optarg);
        exit(1);
    }
    args->addr.s_addr = addr.s_addr;

    // handle default args
    if (args->port == 0) {
        args->port = htons(MULTICAST_PORT);
    }
}

// https://stackoverflow.com/questions/29242/off-the-shelf-c-hex-dump-code/29865#29865
static void hex_dump(char *buf, int buflen) {
    for (int i = 0; i < buflen; i += 16) {
        printf("%06x: ", i);
        for (int j = 0; j < 16; j++)
            if (i + j < buflen)
                printf("%02x ", (unsigned char)buf[i + j]);
            else
                printf("   ");
        printf(" ");
        for (int j = 0; j < 16; j++)
            if (i + j < buflen)
                printf("%c", isprint(buf[i + j]) ? buf[i + j] : '.');
        printf("\n");
    }
}

int g_running = 1;

static void term_handler(int sig) {
    g_running = 0;
    fprintf(stdout, "exiting!\n");
}

int main(int argc, char **argv) {
    int err;
    struct sigaction act;
    act.sa_handler = term_handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    err = sigaction(SIGINT, &act, NULL);
    if (err < 0) {
        err = errno;
        fprintf(stderr, "register SIGINT handler failed: %s\n", strerror(err));
        return 1;
    }
    err = sigaction(SIGTERM, &act, NULL);
    if (err < 0) {
        err = errno;
        fprintf(stderr, "register SIGTERM handler failed: %s\n", strerror(err));
        return 1;
    }

    struct args args;
    memset(&args, 0, sizeof(args));
    parse_args(argc, argv, &args);

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd < 0) {
        err = errno;
        fprintf(stderr, "create socket failed: %s\n", strerror(err));
        return 1;
    }

    int opt_reuseaddr = 1;
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt_reuseaddr, sizeof(opt_reuseaddr));
    if (err < 0) {
        err = errno;
        fprintf(stderr, "set socket option \"SO_REUSEADDR\" failed: %s\n", strerror(err));
        close(fd);
        return 1;
    }

    // enable multicast for designated interface
#ifdef _WIN32
    struct ip_mreq iface = {
        .imr_multiaddr.s_addr = args.addr.s_addr,
        .imr_interface = htonl(args.ifindex),
    };
#else
    struct ip_mreqn iface = {
        .imr_multiaddr.s_addr = args.addr.s_addr,   // multicast group address
        // .imr_ifindex has high priority than .imr_address which is the local IP address of interface
        .imr_ifindex = args.ifindex,
    };
#endif
    err = setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &iface, sizeof(iface));
    if (err < 0) {
        err = errno;
        fprintf(stderr, "set socket option \"IP_ADD_MEMBERSHIP\" failed: %s\n", strerror(err));
        close(fd);
        return 1;
    }

    struct sockaddr_in mc_addr = {
        .sin_family = AF_INET,
#ifdef _WIN32
        // windows does not support bind to multicast address
        .sin_addr = INADDR_ANY,
#else
        .sin_addr = args.addr,
#endif
        .sin_port = args.port,
    };

    if (args.server) {
        err = bind(fd, (struct sockaddr *)&mc_addr, sizeof(mc_addr));
        if (err < 0) {
            err = errno;
            fprintf(stderr, "bind socket failed: %s", strerror(err));
            close(fd);
            return 1;
        }

        while (g_running) {
            char buf[RCV_BUF_SIZE];
            err = recvfrom(fd, buf, RCV_BUF_SIZE, 0, NULL, 0);
            if (err < 0) {
                err = errno;
                if (err != EINTR) {
                    fprintf(stderr, "socket recv failed: %s\n", strerror(err));
                    close(fd);
                    return 1;
                }
                continue;
            } else if (err == 0) {
                continue;
            }

            hex_dump(buf, err);
        }
    }

    if (args.client) {
        // bind interface for sending
#ifdef _WIN32
        err = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF,
                         &iface.imr_interface.s_addr, sizeof(iface.imr_interface.s_addr));
#else
        err = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface));
#endif
        if (err < 0) {
            err = errno;
            fprintf(stderr, "set socket option \"IP_MULTICAST_IF\" failed: %s\n", strerror(err));
            close(fd);
            return 1;
        }

        err = sendto(fd, "Hello, world!", strlen("Hello, world!"), 0, (struct sockaddr *)&mc_addr, sizeof(mc_addr));
        if (err < 0) {
            err = errno;
            fprintf(stderr, "socket recv failed: %s\n", strerror(err));
        }
    }

    close(fd);
    return 0;
}
