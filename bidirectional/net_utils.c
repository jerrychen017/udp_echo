#include "net_utils.h"

/**
 * Gets the data generator local addr based on if running on Android
 */
struct sockaddr_un get_datagen_addr(bool android, socklen_t *len)
{
    struct sockaddr_un addr;

    if (android) {
        const char name[] = ANDROID_SOCK_DATAGEN; // fix android socket error
        memset(&addr, 0, sizeof(addr)); // fix android socket error
        addr.sun_family = AF_UNIX;
        memcpy(addr.sun_path, name, sizeof(name) - 1); // fix android socket error
        *len = strlen(addr.sun_path) + sizeof(name); // fix android socket error
        addr.sun_path[0] = 0; // fix android socket error
    } else {
        addr.sun_family = AF_UNIX;
        strcpy(addr.sun_path, SOCK_DATAGEN);
        *len = strlen(addr.sun_path) + sizeof(addr.sun_family);
    }

    return addr;
}

/**
 * Gets the controller local addr based on if running on Android
 */
struct sockaddr_un get_controller_addr(bool android, socklen_t *len)
{
    struct sockaddr_un addr;

    if (android) {
        const char name[] = ANDROID_SOCK_CONTROLLER; // fix android socket error
        memset(&addr, 0, sizeof(addr)); // fix android socket error
        addr.sun_family = AF_UNIX;
        memcpy(addr.sun_path, name, sizeof(name) - 1); // fix android socket error
        *len = strlen(addr.sun_path) + sizeof(name); // fix android socket error
        addr.sun_path[0] = 0; // fix android socket error
    } else {
        addr.sun_family = AF_UNIX;
        strcpy(addr.sun_path, SOCK_CONTROLLER);
        *len = strlen(addr.sun_path) + sizeof(addr.sun_family);
    }

    return addr;
}

/**
 * Setup unix socket, binds and connects to addr
 */
int setup_unix_socket(struct sockaddr_un addr, socklen_t len)
{
    int s;

    if ((s = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
        perror("socket error\n");
        exit(1);
    }

    unlink(addr.sun_path);
    if (bind(s, (struct sockaddr *) &addr, len) == -1) {
        perror("bind");
        exit(1);
    }

    return s;
}

/**
 * given host name and port, get address
 */
struct sockaddr_in addrbyname(const char *hostname, int port)
{
    int host_num;
    struct hostent h_ent, *p_h_ent;

    struct sockaddr_in addr;

    p_h_ent = gethostbyname(hostname);
    if (p_h_ent == NULL) {
        printf("gethostbyname error.\n");
        exit(1);
    }

    memcpy( &h_ent, p_h_ent, sizeof(h_ent));
    memcpy( &host_num, h_ent.h_addr_list[0], sizeof(host_num) );

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = host_num;
    addr.sin_port = htons(port);

    return addr;
}

/**
 * Setup UDP socket and bind
 */
int setup_bound_socket(int port)
{
    struct sockaddr_in name;

    int s_recv = socket(AF_INET, SOCK_DGRAM, 0);  /* socket for receiving (udp) */
    if (s_recv < 0) {
        perror("socket recv error\n");
        exit(1);
    }

    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;
    name.sin_port = htons(port);

    if (bind( s_recv, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("bind error\n");
        exit(1);
    }

    return s_recv;
}

/**
 * Setup TCP socket and connect to the host
 */
int setup_tcp_socket_send(const char *hostname, int port)
{
    struct sockaddr_in host;

    int s_recv = socket(AF_INET, SOCK_STREAM, 0);  /* socket for receiving (udp) */
    if (s_recv < 0) {
        perror("tcp socket recv error\n");
        exit(1);
    }

    host.sin_family = AF_INET;
    host.sin_port = htons(port);

    struct hostent h_ent, *p_h_ent;

    p_h_ent = gethostbyname(hostname);
    if (p_h_ent == NULL) {
        printf("gethostbyname error.\n");
        exit(1);
    }

    int host_num;
    memcpy( &h_ent, p_h_ent, sizeof(h_ent));
    memcpy( &host_num, h_ent.h_addr_list[0], sizeof(host_num) );
    host.sin_addr.s_addr = host_num;

    while(connect(s_recv, (struct sockaddr *)&host, sizeof(host)) < 0) /* Connect! */
    {
        printf( "T_ncp: could not connect to server\n");
    }
    return s_recv;
}

/**
 * Setup TCP socket, bind and listen
 */
int setup_tcp_socket_recv(int port) {
    int s;
    struct sockaddr_in name;
    long on = 1;
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s<0) {
        perror("T_rcv: socket");
        exit(1);
    }

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) < 0)
    {
        perror("T_rcv: setsockopt error \n");
        exit(1);
    }

    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;
    name.sin_port = htons(port);

    if ( bind( s, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("T_rcv: bind");
        exit(1);
    }

    if (listen(s, 4) < 0) {
        perror("T_rcv: listen");
        exit(1);
    }

    return s;
}

