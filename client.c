#include <arpa/inet.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include "utils.h"

int main(int argc, char *argv[]) {
    int listen_sockfd, send_sockfd, select_fd;
    struct sockaddr_in client_addr, server_addr_to, server_addr_from;
    socklen_t addr_size = sizeof(server_addr_to);
    struct packet pkt;
    struct packet ack_pkt;
    char buffer[PAYLOAD_SIZE];
    unsigned int ack_num = 0;
    int last = -1;
    char ack = 0;

    // read filename from command line argument
    if (argc != 2) {
        printf("Usage: ./client <filename>\n");
        return 1;
    }
    char *filename = argv[1];

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Configure the server address structure to which we will send data
    memset(&server_addr_to, 0, sizeof(server_addr_to));
    server_addr_to.sin_family = AF_INET;
    server_addr_to.sin_port = htons(SERVER_PORT_TO);
    server_addr_to.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Configure the client address structure
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(CLIENT_PORT);
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the client address
    if (bind(listen_sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Open file for reading
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("Error opening file");
        close(listen_sockfd);
        close(send_sockfd);
        return 1;
    }
    
    ssize_t bytes_read;
    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = TIMEOUT_USEC;
    
    // TODO: Read from file, and initiate reliable data transfer to the server
    struct queue *window_start;
    struct queue *cur_window_seg;
    struct queue nodes[MAX_SEQUENCE];
    for (size_t i = 0; i < MAX_SEQUENCE-1; ++i) {
        nodes[i].next = &nodes[i+1];
        nodes[i].used = 0;
	nodes[i].seqnum = 0;
    }
    nodes[MAX_SEQUENCE-1].next = &nodes[0];
    nodes[MAX_SEQUENCE-1].used = 0;
    nodes[MAX_SEQUENCE-1].seqnum = 0;
    window_start = &(nodes[0]);

    unsigned long timestamps[MAX_SEQUENCE] = {0};

    float cwnd = 1;
    int ssthresh = 6;
    int dup_ack = 1;
    int prev_ack = MAX_SEQUENCE;
    float cwnd_change = 0;
    int high_seq_num = 0;
    int start_seq_num = 0;
    int cur_seq_num = 0;
    char in_timeout = 0;
    char fr_running = 0;
    struct timeval instant_time;
    int ca_freq = 0;
    fd_set read_fds;
    char done_timeout = 0;
    unsigned int timeout_start_time;
    int last_transmitted = MAX_SEQUENCE;
    ssize_t bytes_recv;
    select_fd = listen_sockfd;
    int transmitting_timeout = 0;
    while(1){
	if ((in_timeout && transmitting_timeout) || (!(dup_ack % 3) && fr_running)) /* && last_transmitted != (int)window_start->seqnum*/ {
	    //printf("HIII!!!\n");
	    if (last != window_start->seqnum)
	    	build_packet(&pkt, window_start->seqnum, ack_num, 0, ack, window_start->length, window_start->payload);
	    else
		build_packet(&pkt, window_start->seqnum, ack_num, 1, ack, window_start->length, window_start->payload);
	    //printf("YOOOO\n");
	    sendto(send_sockfd, &(pkt), sizeof(pkt), 0, (struct sockaddr *)&server_addr_to, addr_size);
	    gettimeofday(&instant_time,NULL);
	    timestamps[window_start->seqnum] = (unsigned long)(1000000*instant_time.tv_sec+instant_time.tv_usec);;
	    last_transmitted = window_start->seqnum;
	    printSend(&pkt, 0);
	    printf("Retransmission sent!\n");
	    fr_running = 0;
	    if (in_timeout) {
		dup_ack = 1;
		transmitting_timeout = 0;
	    }

	} else if (!in_timeout && 0 < (int)cwnd && !(last_transmitted == window_start->seqnum && (int) cwnd == 1)) {
	    cur_window_seg = window_start;
	    for (int i = start_seq_num; i < (high_seq_num); ++i) cur_window_seg = cur_window_seg->next;
	    for (; high_seq_num < start_seq_num + (int)cwnd; ++high_seq_num)
	    {
		cur_seq_num = high_seq_num % MAX_SEQUENCE;
		//printf("0\n");
		if (!cur_window_seg->used) {
		    //printf("1\n");
		    bytes_read = fread(buffer, 1, PAYLOAD_SIZE, fp);
		    //printf("2\n");
		    if (bytes_read == -1) {
			perror("Error reading from file");
			fclose(fp);
			close(listen_sockfd);
			close(send_sockfd);
			return 1;
		    }
		    //else if (bytes_read == 0)
		//	break;
		    else if (bytes_read < PAYLOAD_SIZE) {
			last = cur_seq_num;
			printf("Last packet found! %li bytes read! Seqnum is: %i\n", bytes_read, cur_seq_num);
		    }
		    //printf("3\n");
		    memcpy(cur_window_seg->payload, (const char *)buffer, bytes_read);
		    //printf("4\n");
		    cur_window_seg->seqnum = cur_seq_num;
		    cur_window_seg->length = bytes_read;
		    cur_window_seg->used = 1;
		    if (last == cur_seq_num)
			build_packet(&pkt, cur_seq_num, ack_num, 1, ack, cur_window_seg->length, cur_window_seg->payload);
		    else
			build_packet(&pkt, cur_seq_num, ack_num, 0, ack, cur_window_seg->length, cur_window_seg->payload);
		    //printf("5\n");
		    last_transmitted = cur_seq_num;
		} else {
		    //printf("7\n");
		    if (last == cur_seq_num)
      	            	build_packet(&pkt, cur_window_seg->seqnum, ack_num, 1, ack, cur_window_seg->length, cur_window_seg->payload);
		    else
			build_packet(&pkt, cur_window_seg->seqnum, ack_num, 0, ack, cur_window_seg->length, cur_window_seg->payload);
		    //printf("6\n");
		    last_transmitted = cur_seq_num;
		    printf("Packet already cached! window->seqnum: %i, cur_seq_num: %i\n", cur_window_seg->seqnum, cur_seq_num);
		}
		//printf("8\n");
		sendto(send_sockfd, &(pkt), sizeof(pkt), 0, (struct sockaddr *)&server_addr_to, addr_size);
		printSend(&pkt, 0);
		timestamps[cur_seq_num] = (unsigned)time(NULL);
		if (last == cur_seq_num)
		    break;
		cur_window_seg = cur_window_seg->next;
	    }
	    high_seq_num = high_seq_num % MAX_SEQUENCE;
	    sleep(0.1);
	}

	// Handle timeouts	
	FD_ZERO(&read_fds);
        FD_SET(select_fd, &read_fds);

	int select_result = select(select_fd + 1, &read_fds, NULL, NULL, &timeout);

//	if (dup_ack > WINDOW_SIZE) { // force timeout
//            dup_ack = 1;
//	    if (cwnd/2 > 2)
//		ssthresh = (int)cwnd/2;
//	    else
//		ssthresh = 2;
//	    cwnd = 1;
//	    high_seq_num = start_seq_num;
//	    in_timeout = 1;	   
//	    printf("Forcing timeout!\n");
//	    continue;
//	}

	if (select_result == -1) {
	    perror("Error in select");
	    fclose(fp);
	    close(listen_sockfd);
	    close(send_sockfd);
	    return 1;
	}
	else if (select_result == 0) {
	    if (!timestamps[start_seq_num]) {
		printf("timestamp error\n");
		return 1;
	    }
	    gettimeofday(&instant_time,NULL);
	    if ((unsigned long)(1000000*instant_time.tv_sec+instant_time.tv_usec) - timestamps[start_seq_num] > (unsigned long)(TIMEOUT_SEC*1000000+TIMEOUT_USEC)) {
		dup_ack = 1;
	        printf("Timeout occurred. Retransmitting packet: %i\n", start_seq_num);
		if (cwnd/2 > 2)
		    ssthresh = (int)cwnd/2;
		else
		    ssthresh = 2;
		cwnd = 1;
		timeout.tv_sec = TIMEOUT_SEC;
		timeout.tv_usec = TIMEOUT_USEC;
		high_seq_num = start_seq_num;
	     //   printf("Start_seq_num, %i, Actual first window seqnum: %i, high_seq_num: %i\n", start_seq_num, window_start->seqnum, high_seq_num);
		in_timeout = 1;
		transmitting_timeout = 1;
	    }
	    continue;
	}
	bytes_recv = recvfrom(listen_sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&server_addr_from, &addr_size);
	printRecv(&ack_pkt);
	if (ack_pkt.seqnum)
	    break;
	if (bytes_recv > 0) {
	    if (ack_pkt.acknum == (unsigned int)prev_ack)
		dup_ack++;
	    else if (dup_ack > 1){
		if (dup_ack >= 3) { // Done with fast recovery
		    if (ack_pkt.acknum >= start_seq_num)
		    	cwnd_change = (ack_pkt.acknum - start_seq_num);
		    else
			cwnd_change = MAX_SEQUENCE - start_seq_num + ack_pkt.acknum;
		    cur_window_seg = window_start;
		    for (size_t i = 0; i < (size_t)cwnd_change; ++i) {
			cur_window_seg->seqnum = 0;
			cur_window_seg->used = 0;
			cur_window_seg = cur_window_seg->next;
		    }
		    window_start = cur_window_seg;
		    start_seq_num = ack_pkt.acknum;
		    cwnd = (float) ssthresh; // Done with fast recovery
		    if (start_seq_num > high_seq_num) high_seq_num = start_seq_num;
		    else if (high_seq_num > MAX_SEQUENCE - (2*WINDOW_SIZE) && start_seq_num < (2*WINDOW_SIZE)) high_seq_num = start_seq_num;
		    printf("END FR: Window size: %f, window start seqnum: %i, real window start: %i\n", cwnd, start_seq_num, window_start->seqnum);
		    dup_ack = 1;
		    continue;
		}
		dup_ack = 1;
	    }

	    // Fast Retransmit
	    if (!(dup_ack % 3)) {
		fr_running = 1;
		ca_freq = 0;
		cwnd = (float)(int)cwnd;
		cwnd_change = ssthresh/2;
		if (cwnd_change > 2)
		    ssthresh = (size_t)cwnd_change;
		else
		    ssthresh = 2;
		printf("3 dup ACKs! Starting FR! Retransmitting seqnum: %i. New ssthres: %i, cwnd: %i\n", window_start->seqnum, ssthresh, (int) cwnd);
		continue;
	    }

	    if (high_seq_num < ack_pkt.acknum)
		high_seq_num = ack_pkt.acknum;
	    else if (high_seq_num > MAX_SEQUENCE - (2*WINDOW_SIZE) && ack_pkt.acknum < (2*WINDOW_SIZE)) 
		high_seq_num = ack_pkt.acknum; 

	    // Fast recovery
	    if (dup_ack > 3) 
		cwnd++;

	    // Slow start
	    else if ((int)cwnd <= ssthresh) {
		if (ack_pkt.acknum > (unsigned int)start_seq_num) {
		    cwnd_change = (ack_pkt.acknum - start_seq_num);
		    cur_window_seg = window_start;
		    for (size_t i = 0; i < (size_t)cwnd_change; ++i) {
			cur_window_seg->seqnum = 0;
			cur_window_seg->used = 0;
			cur_window_seg = cur_window_seg->next;
		    } if (cwnd+cwnd_change > WINDOW_SIZE)
			cwnd_change = WINDOW_SIZE-cwnd;
		    if (cwnd_change > 1) cwnd_change = 1;
		    cwnd += cwnd_change;
		    start_seq_num = ack_pkt.acknum;
		    window_start = cur_window_seg;
		}
		else if (start_seq_num > (MAX_SEQUENCE-(2*WINDOW_SIZE)) && ack_pkt.acknum < 2*WINDOW_SIZE) {
		    cwnd_change = MAX_SEQUENCE-start_seq_num+ack_pkt.acknum;	 
		    cur_window_seg = window_start;
		    for (size_t i = 0; i < (size_t)cwnd_change; ++i) {
			cur_window_seg->seqnum = 0;
			cur_window_seg->used = 0;
			cur_window_seg = cur_window_seg->next;
		    } if (cwnd+cwnd_change > WINDOW_SIZE)
			cwnd_change = WINDOW_SIZE-cwnd;
		    if (cwnd_change > 1) cwnd_change = 1;
		    cwnd += cwnd_change;
		    start_seq_num = ack_pkt.acknum;
		    window_start = cur_window_seg;
		}	
	    }
	    else { // Congestion Avoidance
		printf("In congestion avoidance\n");
		if (ack_pkt.acknum > start_seq_num) {
		    ++ca_freq;
		    cwnd_change = (float)ca_freq/cwnd;
		    cur_window_seg = window_start;
		    for (int i = 0; i < (int)(ack_pkt.acknum - start_seq_num); ++i) {
			cur_window_seg->seqnum = 0;
			cur_window_seg->used = 0;
			cur_window_seg = cur_window_seg->next;
		    } if (cwnd + cwnd_change > WINDOW_SIZE)
			cwnd_change = WINDOW_SIZE-cwnd;
		    cwnd_change = (float)(int)cwnd_change;
		    if (cwnd_change) ca_freq = 0;
		    cwnd += (float)(int)cwnd_change;
		    start_seq_num = ack_pkt.acknum;
		    window_start = cur_window_seg;
	        }
		else if (start_seq_num > (MAX_SEQUENCE-(2*WINDOW_SIZE)) && ack_pkt.acknum < 2*WINDOW_SIZE) {
		    cwnd_change = (float)ca_freq/cwnd;
		    cur_window_seg = window_start;
		    for (int i = 0; i < (int)(MAX_SEQUENCE-start_seq_num+ack_pkt.acknum); ++i) {
			cur_window_seg->seqnum = 0;
			cur_window_seg->used = 0;
			cur_window_seg = cur_window_seg->next;
			} 
		    if (cwnd + cwnd_change > WINDOW_SIZE)
			cwnd_change = WINDOW_SIZE-cwnd;
		    cwnd_change = (float)(int)cwnd_change;
		    if (cwnd_change) ca_freq = 0;
		    cwnd += (float)(int)cwnd_change;
		    start_seq_num = ack_pkt.acknum;
		    window_start = cur_window_seg;
		}	
	    }
	    prev_ack = ack_pkt.acknum;
	    printf("Window size: %f, window start seqnum: %i, actual window start: %i\n", cwnd, start_seq_num, window_start->seqnum);
	    if (in_timeout) {
		in_timeout = 0;
		cur_window_seg = window_start;
		if (start_seq_num < 2*WINDOW_SIZE && high_seq_num > MAX_SEQUENCE - (2*WINDOW_SIZE))
		    cwnd_change = (MAX_SEQUENCE - high_seq_num + start_seq_num - 1);
		else
		    cwnd_change = start_seq_num - high_seq_num - 1;
                for (int i = 0; i < (int)cwnd_change; ++i) {
		    cur_window_seg->seqnum = 0;
		    cur_window_seg->used = 0;
                    cur_window_seg = cur_window_seg->next;
		}
                window_start = cur_window_seg;
		high_seq_num = start_seq_num;
	    }
	}
    }
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

