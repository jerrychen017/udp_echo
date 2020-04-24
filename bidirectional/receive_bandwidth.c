#include "receive_bandwidth.h"
#include "feedbackLogger.h"

#define ALPHA 0.1      // closer to 0 is smoother, closer to 1 is quicker reaction (90 packets ~ 1Mbps) 0.2/0.1 for regular
#define THRESHOLD 0.95 // percent drop threshold
#define RECV_TIMEOUT_SEC 5
#define RECV_TIMEOUT_USEC 0
static bool kill_thread = false;

void stop_receiving_thread()
{
    kill_thread = true;
}

void *receive_bandwidth_pthread(void *args)
{
    struct recv_bandwidth_args *recv_args = (struct recv_bandwidth_args *)args;
    receive_bandwidth(recv_args->sk, recv_args->pred_mode, recv_args->expected_addr, recv_args->params);
    return NULL;
}

void receive_bandwidth(int s_bw, int predMode, struct sockaddr_in expected_addr, struct parameters params)
{
    // parameter variables
    int BURST_SIZE = params.burst_size;
    int INTERVAL_SIZE = params.interval_size;
    double INTERVAL_TIME = params.interval_time;
    int INSTANT_BURST = params.instant_burst;
    int BURST_FACTOR = params.burst_factor;
    double MIN_SPEED = params.min_speed;
    double MAX_SPEED = params.max_speed;
    double START_SPEED = params.start_speed;
    int GRACE_PERIOD = params.grace_period;

    kill_thread = false;
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    // Select loop
    fd_set mask;
    fd_set read_mask;
    struct timeval timeout;
    int num;
    int len;

    // state variables
    // Current rate the server expectes to receive at
    double curRate = 0;
    // Rate that ewma predicts
    double ewmaRate = 0;
    // only use measurements after we have received WINDOW_SIZE packets at current rate
    int numAtCurRate = 0;
    // number of packets below threshold
    int numBelowThreshold = 0;

    // next expected seq number
    int seq = 0;
    // Arrival time of last BURST_SIZE packets
    struct timeval arrivals[BURST_SIZE];
    // whether we have received a packet in window
    int received[BURST_SIZE];

    // whether we are in a burst
    bool burst = false;
    // First and last sequence numbers we receive
    int bStart = BURST_SIZE, bEnd = -1;
    struct timeval barrivals[BURST_SIZE];
    int breceived[BURST_SIZE];

    memset(received, 0, sizeof(received));
    memset(breceived, 0, sizeof(breceived));

    // variables to store caclulations in
    double calculated_speed;
    struct timeval tm_diff;

    // packet buffers
    data_packet data_pkt;
    packet_header report_pkt;
    packet_header ack_pkt;

    memset(&ack_pkt, 0, sizeof(packet_header));
    memset(&report_pkt, 0, sizeof(packet_header));
    memset(&data_pkt, 0, sizeof(data_packet));

    // Add file descriptors to fdset
    FD_ZERO(&mask);
    FD_SET(s_bw, &mask);

    for (;;)
    {
        if (kill_thread)
        {
            // send out NETWORK_STOP
            report_pkt.type = NETWORK_STOP;
            report_pkt.rate = 0;
            report_pkt.seq_num = 0;

            sendto_dbg(s_bw, &report_pkt, sizeof(report_pkt), 0,
                       (struct sockaddr *)&from_addr, from_len);
            close(s_bw);
            return;
        }
        read_mask = mask;
        timeout.tv_sec = RECV_TIMEOUT_SEC;
        timeout.tv_usec = RECV_TIMEOUT_USEC;

        num = select(FD_SETSIZE, &read_mask, NULL, NULL, &timeout);

        if (num > 0)
        {
            if (FD_ISSET(s_bw, &read_mask))
            {
                len = recvfrom(s_bw, &data_pkt, sizeof(data_packet), 0,
                               (struct sockaddr *)&from_addr, &from_len);
                if (len < 0)
                {
                    perror("socket error");
                    exit(1);
                }
                // printf("received %d bytes, seq %d at rate %f\n", len, data_pkt.hdr.seq_num, data_pkt.hdr.rate );

                // When we receive a new START message, reset the server
                if (data_pkt.hdr.type == NETWORK_START)
                {
                    if (from_addr.sin_addr.s_addr == expected_addr.sin_addr.s_addr && from_addr.sin_port == expected_addr.sin_port)
                    {
                        ack_pkt.type = NETWORK_START_ACK;
                    }
                    else
                    {
                        ack_pkt.type = NETWORK_BUSY;
                    }
                    ack_pkt.rate = 0;
                    ack_pkt.seq_num = 0;
                    sendto_dbg(s_bw, &ack_pkt, sizeof(packet_header), 0,
                               (struct sockaddr *)&from_addr, from_len);
                    continue;
                }
                else if (data_pkt.hdr.type == NETWORK_STOP)
                {
                    close(s_bw);
                    return;
                }

                double expectedRate = data_pkt.hdr.rate;
                int currSeq = data_pkt.hdr.seq_num;

                // keep track of how many packets we've received at the current rate
                if (expectedRate != curRate)
                {
                    curRate = expectedRate;
                    numAtCurRate = 0;
                }
                numAtCurRate++;

                // out of order packet, drop
                if (currSeq < seq)
                {
                    continue;
                }

                // mark packets up to received seq as not received
                for (; seq < currSeq; seq++)
                {
                    received[seq % BURST_SIZE] = 0;
                }
                // mark current pacekt as received and record arrival time
                int i = currSeq % BURST_SIZE;
                received[i] = 1;
                gettimeofday(&arrivals[i], NULL);

                if (data_pkt.hdr.type == NETWORK_BURST)
                {
                    // first burst packet we have received
                    if (!burst)
                    {
                        memset(breceived, 0, sizeof(breceived));
                        burst = true;
                    }

                    // Calculate index within burst
                    int bursti = data_pkt.hdr.seq_num - data_pkt.hdr.burst_start;

                    if (bursti >= BURST_SIZE)
                    {
                        printf("invalid burst index %d\n", bursti);
                        exit(1);
                    }
                    gettimeofday(&barrivals[bursti], NULL);
                    breceived[bursti] = 1;

                    bStart = bursti < bStart ? bursti : bStart;
                    bEnd = bursti > bEnd ? bursti : bEnd;
                }
                else
                {
                    // burst just finished, send report
                    if (burst)
                    {
                        tm_diff = diffTime(barrivals[bEnd], barrivals[bStart]);
                        if (bEnd != bStart)
                        {
                            calculated_speed = interval_to_speed(tm_diff, bEnd - bStart);
                            printf("Burst calculated speed of %.4f Mbps\n", calculated_speed);
                            report_pkt.type = NETWORK_BURST_REPORT;
                            report_pkt.rate = calculated_speed;
                            report_pkt.seq_num = currSeq;

                            sendto_dbg(s_bw, &report_pkt, sizeof(report_pkt), 0,
                                       (struct sockaddr *)&from_addr, from_len);

                            // Reset burst stuff
                            burst = false;
                            bStart = BURST_SIZE;
                            bEnd = -1;
                        }
                    }

                    // First assume rate is as expected (if we are unable to calculate because we
                    // haven't received enough, we don't want to do anything)
                    double calcRate = expectedRate;

                    // EWMA
                    if (predMode == 0 && numAtCurRate >= 2)
                    {
                        if (!received[(currSeq - 1) % BURST_SIZE])
                            continue;

                        tm_diff = diffTime(arrivals[currSeq % BURST_SIZE], arrivals[(currSeq - 1) % BURST_SIZE]);
                        calculated_speed = interval_to_speed(tm_diff, 1);
                        ewmaRate = (ALPHA * calculated_speed) + (1 - ALPHA) * ewmaRate;
                        // printf("Computed sending rate of %.4f Mbps\n", ewmaRate);
                        calcRate = ewmaRate;
                    }
                    // Running Avg
                    if (predMode == 1 && numAtCurRate >= BURST_SIZE)
                    {
                        if (!received[(currSeq + 1) % BURST_SIZE])
                            continue;

                        // Wrap around average of packets
                        tm_diff = diffTime(arrivals[currSeq % BURST_SIZE], arrivals[(currSeq + 1) % BURST_SIZE]);
                        calculated_speed = interval_to_speed(tm_diff, BURST_SIZE - 1);
                        calcRate = calculated_speed; //Figure out threshold
                        // printf("Computed sending rate of %.4f Mbps\n", calcRate);
                        if (currSeq % INTERVAL_SIZE == INTERVAL_SIZE / 2)
                        {
                            sendFeedbackDownload(data_pkt.hdr.rate);
                        }
                    }

                    // Send report packet if we are under 90 percent of expected rate
                    if (calcRate <= THRESHOLD * expectedRate)
                    {
                        if (numBelowThreshold == GRACE_PERIOD)
                        {
                            report_pkt.type = NETWORK_REPORT;
                            report_pkt.rate = calcRate;
                            report_pkt.seq_num = currSeq;
                            sendto_dbg(s_bw, &report_pkt, sizeof(report_pkt), 0,
                                       (struct sockaddr *)&from_addr, from_len);
                            // printf("Computed rate %.4f below threshold, actual rate %.4f\n", calcRate, expectedRate);
                            numBelowThreshold = 0;
                        }
                        else
                        {
                            numBelowThreshold++;
                        }
                    }
                    else
                    {
                        // reset numBelowThreshold when received non-delayed packet within the grace period
                        if (numBelowThreshold != 0)
                        {
                            // printf("num threshold is %d\n", numBelowThreshold);
                        }
                        numBelowThreshold = 0;
                    }
                }

                // increment seq
                seq = currSeq + 1;
            }
        }
        else
        {
            printf("Stop receiving bandwidth, accepting new connection\n");
            close(s_bw);
            return;
        }
    }
}
