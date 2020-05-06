#ifndef RECEIVE_BANDWIDTH
#define RECEIVE_BANDWIDTH

#include "net_include.h"
#include "bandwidth_utils.h"
#include "sendto_dbg.h"

struct recv_bandwidth_args
{
    int sk;
    int pred_mode;
    struct sockaddr_in expected_addr;
    struct parameters params;
    bool android;
};

void stop_receiving_thread();
void stop_tcp_recv_thread();
void receive_bandwidth(int sk, struct sockaddr_in expected_addr, struct parameters params, bool android);
void server_receive_bandwidth_tcp(int sk);
void client_receive_bandwidth_tcp(int sk);
void *receive_bandwidth_pthread(void *);

#endif
