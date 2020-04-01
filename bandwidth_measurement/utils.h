#ifndef UTILS_H
#define UTILS_H

struct timeval diffTime(struct timeval left, struct timeval right);
int gtTime(struct timeval left, struct timeval right); 

struct timeval speed_to_interval(double speed);
double interval_to_speed(struct timeval interval, int num_packets);

#endif 