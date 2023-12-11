#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"


// Slide client window, return number of segments in new window, returns number of places window slides 
size_t slide_window(struct queue **window_start, FILE *fp) { 
    if ((*window_start)->last) return 0;
    // Find first nonused segment in window
    struct queue *cur_window_seg = *window_start;
//    printf("Window\nOLD:   ");
//    for (size_t j = 0; j < 6; ++j){
//        printf("%i    ", cur_window_seg->seqnum);
//        cur_window_seg = cur_window_seg->next;
//    }
    (*window_start)->used = 0;
    (*window_start)->seqnum = 0;
    (*window_start)->length = 0;
    cur_window_seg = (*window_start)->next;
    size_t i = 1;
    for (;i < MAX_SEQUENCE; ++i) {
	if (!cur_window_seg->used)
	    break;
	else {
	    int output = fwrite((const char *)cur_window_seg->payload, 1, cur_window_seg->length, fp);
	    printf("WRITE %i\n", cur_window_seg->seqnum);
	    cur_window_seg->used = 0;
	    cur_window_seg->seqnum = 0;
	    cur_window_seg->length = 0;
	    if (cur_window_seg->last) {
		break;
            }
	}
	cur_window_seg = cur_window_seg->next;
    }
    *window_start = cur_window_seg;
//    printf("Window\nNEW:   ");
//    for (size_t j = 0; j < 6; ++j){
//	printf("%i    ", cur_window_seg->seqnum);
//	cur_window_seg = cur_window_seg->next;
//    }
//    printf("\n");
    return i;
} 

int main() {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in server_addr, client_addr_from, client_addr_to;
    struct packet buffer;
    socklen_t addr_size = sizeof(client_addr_from);
    int expected_seq_num = 0;
    struct packet ack_pkt;

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Configure the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the server address
    if (bind(listen_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Configure the client address structure to which we will send ACKs
    memset(&client_addr_to, 0, sizeof(client_addr_to));
    client_addr_to.sin_family = AF_INET;
    client_addr_to.sin_addr.s_addr = inet_addr(LOCAL_HOST);
    client_addr_to.sin_port = htons(CLIENT_PORT_TO);

    // Open the target file for writing (always write to output.txt)
    FILE *fp = fopen("output.txt", "wb");

    // TODO: Receive file from the client and save it as output.txt
    struct queue *window_start;
    struct queue *cur_window_seg;
    struct queue nodes[MAX_SEQUENCE];
    for (size_t i = 0; i < MAX_SEQUENCE-1; ++i) {
	nodes[i].next = &nodes[i+1];
	nodes[i].used = 0;
	nodes[i].seqnum = 0;
	nodes[i].last = 0;
    }
    nodes[MAX_SEQUENCE-1].next = &nodes[0];
    nodes[MAX_SEQUENCE-1].used = 0;
    window_start = &nodes[0];
    int last_pkt = 0;

    int window_change = 0;
    while(1){
        recvfrom(listen_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr_from, &addr_size);
	printRecv(&buffer);

	if (window_start->last) {
	    build_packet(&ack_pkt, 1, expected_seq_num, 0, 1, 1, "0");
	    sendto(send_sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&client_addr_to, addr_size);
	    break;
        }

	window_change = 0;
	if (buffer.seqnum > (MAX_SEQUENCE - 2*WINDOW_SIZE) && expected_seq_num < 2*WINDOW_SIZE)
            ; // seqnum is behind expectation in wraparound case, so ignore
        else if (expected_seq_num > (MAX_SEQUENCE - 2*WINDOW_SIZE) && buffer.seqnum < 2*WINDOW_SIZE && (WINDOW_SIZE-expected_seq_num+buffer.seqnum)) {
	    // Expectation is behind seqnum in wraparound case, so cache if possible
	    cur_window_seg = window_start;
	    for (int i = 0; i < (int)(WINDOW_SIZE-expected_seq_num+buffer.seqnum); ++i) cur_window_seg = cur_window_seg->next;
	    if (!cur_window_seg->used) {
		memcpy(cur_window_seg->payload, buffer.payload, buffer.length);
		cur_window_seg->seqnum = buffer.seqnum;
		cur_window_seg->length = buffer.length;
		cur_window_seg->used = 1;
		cur_window_seg->last = buffer.last;
	    }
	} else if (expected_seq_num < (int)buffer.seqnum) {
	    // Expectation is behind seqnum in traditional case, so cache if possible
	    cur_window_seg = window_start;
	    for (int i = 0; i < (int)(buffer.seqnum-expected_seq_num); ++i) cur_window_seg = cur_window_seg->next;
	    if (!cur_window_seg->used) {
		memcpy(cur_window_seg->payload, buffer.payload, buffer.length);
		cur_window_seg->seqnum = buffer.seqnum;
		cur_window_seg->length = buffer.length;
		cur_window_seg->used = 1;
		cur_window_seg->last = buffer.last;
 	}} else if (expected_seq_num > (int)buffer.seqnum)
            ; // seqnum is behind expectation in traditional case, so ignore
        else if (expected_seq_num == (int)buffer.seqnum) {
	    if (window_start->used) {
 		printf("Shouldn't be possible! First seg of window is used!\n");
		return 0;
      	    }
	    memcpy(window_start->payload, buffer.payload, buffer.length);
	    window_start->seqnum = buffer.seqnum;
	    window_start->length = buffer.length;
	    window_start->last = buffer.last;
	    int output = fwrite((const char *)window_start->payload, 1, window_start->length, fp);
	    printf("WRITE %i\n", window_start->seqnum);
            window_change += slide_window(&window_start, fp); // Expected_seq_num is equals seqnum, so slide the window
	} else
	    return 0; // Don't think this is possible

	expected_seq_num = (expected_seq_num + window_change) % MAX_SEQUENCE;
        build_packet(&ack_pkt, 0, expected_seq_num, 0, 1, 1, "0");
        sendto(send_sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&client_addr_to, addr_size);
	printSend(&ack_pkt, 0);
	if (window_start->last) {
	    build_packet(&ack_pkt, 1, expected_seq_num, 0, 1, 1, "0");
	    sendto(send_sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&client_addr_to, addr_size);
	    break;
	}
    }
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
