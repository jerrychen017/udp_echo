#include "send_bandwidth.h"
#include "controller.h"
#include "data_generator.h"
#include "feedbackLogger.h"
#include "net_utils.h"

/**
 * static variable used for Android client to terminate threads
 */
static bool kill_thread = false;

/**
 * Calls send_bandwidth() with given arguments. Used to match signatures for pthread_create.
 */
void *send_bandwidth_pthread(void *args)
{
    struct send_bandwidth_args *recv_args = (struct send_bandwidth_args *)args;
    send_bandwidth(recv_args->addr, recv_args->sk, recv_args->android, recv_args->params);

    return NULL;
}

/**
 * Starts data generator on a new thread and runs controller.
 */
void send_bandwidth(struct sockaddr_in addr, int sk, bool android, struct parameters params)
{
    struct data_generator_args send_args;
    pthread_t tid; // thread id for data generator

    send_args.android = android;
    pthread_create(&tid, NULL, &start_generator_pthread, (void *)&send_args);

    start_controller(android, addr, sk, params);
    pthread_join(tid, NULL);
    return;
}

/**
 * Sends data stream to the receiver over TCP on the client side
 */
void client_send_bandwidth_tcp(int s_bw){
    kill_thread = false;

    data_packet data_pkt;
    data_packet recv_pkt;
    data_pkt.hdr.type = NETWORK_DATA;

    // Select loop
    fd_set mask;
    fd_set read_mask;
    struct timeval timeout;

    int num;
    int len;

    // Add file descriptors to fdset
    FD_ZERO(&mask);
    FD_SET(s_bw, &mask);

    for (;;)
    {
        if (kill_thread)
        {
            close(s_bw);
            return;
        }
        read_mask = mask;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;

        send(s_bw, &data_pkt, sizeof(data_pkt), 0);
        num = select(FD_SETSIZE, &read_mask, NULL, NULL, &timeout);

        if (num > 0)
        {
            if (FD_ISSET(s_bw, &read_mask)) {
                len = recv(s_bw, &recv_pkt, sizeof(recv_pkt), 0);
                if (len > 0) {
                    sendFeedbackUpload(recv_pkt.hdr.rate);
                }
            }
        }
    }
}

/**
 * Sends data stream to the receiver over TCP on the server side
 */
void server_send_bandwidth_tcp(int s_bw) {
    // Select loop
    fd_set mask;
    fd_set read_mask;
    struct timeval timeout;
    int num;
    int recv_s = -1;
    struct timeval tm_last;
    gettimeofday(&tm_last, NULL);
    int ret;

    // packet buffers
    data_packet data_pkt;
    memset(&data_pkt, 0, sizeof(data_packet));

    // Add file descriptors to fdset
    FD_ZERO(&mask);
    FD_SET(s_bw, &mask);

    for (;;)
    {
        read_mask = mask;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        num = select(FD_SETSIZE, &read_mask, NULL, NULL, &timeout);

        if (num > 0)
        {
            if (FD_ISSET(s_bw, &read_mask)) {
                printf("Establish connection with android receiver\n");
                recv_s = accept(s_bw, 0, 0);
                for(;;) {
                    ret = send(recv_s, &data_pkt, sizeof(data_pkt), 0);
                    if (ret < 0) {
                        close(recv_s);
                        close(s_bw);
                        return;
                    }

                }
            }
        }
        else
        {
            printf("Download: Stop receiving bandwidth, accepting new connection\n");
            if (recv_s != -1) {
                close(recv_s);
            }
            close(s_bw);
            return;
        }
    }
}

/**
 * Calls server_send_bandwidth_tcp() with given arguments. Used to match signatures for pthread_create.
 */
void * server_send_bandwidth_tcp_pthread(void * args) {
    struct send_bandwidth_args * recv_args = (struct send_bandwidth_args *) args;
    server_send_bandwidth_tcp(recv_args->sk);
    return NULL;
}

/**
 * Breaks out client_send_bandwidth_tcp() select for loop and stops the thread
 */
void stop_tcp_send_thread() {
    kill_thread = true;
}

