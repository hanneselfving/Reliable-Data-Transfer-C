//
//client.c
//description: client side implementation of rdt
//usage: compile with makefile. execute using ./client in terminal.
//authors: Hannes Elfving, Erik Wetterholm
//date: 2021-05-27
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include "common.h"



//Sliding window variables
int base; //base is equal to the lowest non-ACKED packet!
int resetSeqNum = 0;
int numToSend = 15;

//Packet variables
PACKET sendPktArr[999];
PACKET* recvPkt;
PACKET* sendPkt;

//Socket variables
int sockfd;
fd_set fdSet;
struct sockaddr_in servaddr;

//Threads
pthread_t tthread;
pthread_t thread1, thread2;

//Timeval
struct timeval tv;
int tv_waitu = 0;
int tv_waitS = 1;

//Bools
int resending = 0;

int selRetVal;

int resends = 0;


void *timerThread(void* sock);
void *sendThread(void* sock);
void *recvThread(void* sock);

int makeSocket();
int connectionSetup();
int established();
int teardown();
void mightCorruptPacket(PACKET* pkt);

int resetTv() {
    tv.tv_sec = tv_waitS;
    tv.tv_usec = tv_waitu;
}

int setTv(int s, int u) {
    tv.tv_sec = s;
    tv.tv_usec = u;
}

int isCorrupt(PACKET* pkt) {

    int oldCrc = pkt->crc;
    pkt->crc = 0;
    int newCrc = checksum(pkt, sizeof(*pkt));
    printf("new Cycle Redundancy Check: %d\n", newCrc);
    if(newCrc == oldCrc) {
        return 0;
    }
    else
    {
        return 1;
    }
}

//function encapsulating select
int waitInput() {

	FD_ZERO(&fdSet);
    FD_SET(sockfd, &fdSet);

    int rVal = select(sockfd+1,&fdSet,NULL,NULL,&tv);
    printf("waitInput, select returned: %d\n", rVal);
    if(rVal == -1) {
        perror("select error, shutting down");
        close(sockfd);
        exit(1);
    }
    return rVal;
}

int trySend(int sq, int flag, char* data) {

    sendPkt = makePkt(sq, flag, data);
    printf("trySend: Packet made\n");
	printf("trySend: Attempting to send  %d packet of size %d...\n", sendPkt->flags,sizeof(*sendPkt));
	if(sendPkt->flags != EMIT) {
        sendto(sockfd, sendPkt, sizeof(*sendPkt), MSG_CONFIRM, ( struct sockaddr *) &servaddr, sizeof(servaddr));
        return 1;
    }
    else
    {
        printf("trySend: Packet emitted\n");
        return 0;
    }

}

int tryRecv() {
	printf("tryRecv: started function\n");
	int n;
    if(recvPkt){
        free(recvPkt);
    }
    recvPkt = malloc(sizeof(PACKET));
	printf("tryRecv: Allocated Memory for receive\n");
	
    n = recvfrom(sockfd, (PACKET*)recvPkt, sizeof(PACKET), 
        0, ( struct sockaddr *) &servaddr,
        (struct socklen_t*)sizeof(servaddr));    

    printf("tryRecv: Received packet flag: %d \n", recvPkt->flags);
    return 1;
    

}


//This function will be called by timerThread if a window is to be resent. Resends from base to wSize-1
void resend(void* sock) {//
	int i = 0, b = base;
	resending = 1;
	printf("---------------------------------------------\n");
	for(int i = 0; i < wSize; i++) {
		if(b+i >= numToSend) {
			break;
		}
		sendPktArr[b+i].crc = 0;
		sendPktArr[b+i].crc = checksum((void*)&sendPktArr[b+i], sizeof(sendPktArr[b+i]));
		sendto(*((int*)sock), &sendPktArr[b + i], sizeof(sendPktArr[b+i]), 0, 
		( struct sockaddr *) &servaddr, sizeof(servaddr));
		printf("Resent packet %d\n", sendPktArr[b + i].seq);	
		printPacket(&sendPktArr[b + i]);
		printf("base: %d wSize: %d\n",b, wSize);
		usleep(1000);
	} 
	printf("---------------------------------------------\n");
	resending = 0;


	pthread_cancel(tthread);
	pthread_create(&tthread, NULL, timerThread, &sock);

}

//This function will be passed to tthread. If 10 seconds has passed, call resend.
void *timerThread(void* sock) {

	//printf("timerThread: Timer thread started\n");

	sleep(10);

	printf("timerThread: Timeout triggered, resending from %d to %d\n", base, base + wSize-1);
	resend(sock);
  	pthread_exit(NULL);

}

//function passed to sending thread. Continously sends packets..
void *sendThread(void* sock) {
	time_t t;
	 while(seqNum < numToSend) {

	if(seqNum < base + wSize && resending == 0) {
		//printf("sendThread: Attempting to make packet...\n");
		sendPktArr[seqNum] = makePkt2(seqNum, REG, NULL); // MAKE REG PKT
		//sendPktArr[seqNum].crc = checksum((void*)&sendPktArr[seqNum], sizeof(sendPktArr[seqNum]));6
		//printf("REG Packet complete\n");
		//if(seqNum != 3) {  //Comment out to stop testing lost packet
		//printf("Attempting to send packet...\n", sizeof(sendPktArr[seqNum]));
		sendto(*((int*)sock), &sendPktArr[seqNum], sizeof(sendPktArr[seqNum]), 0, 
		( struct sockaddr *) &servaddr, sizeof(servaddr));
	
		printf("Sent packet %d\n",sendPktArr[seqNum].seq);	
		printPacket(&sendPktArr[seqNum]);
		//}
		if(base == seqNum) { //IF base==seqnum restart timer
				printf("sendThread:Restart timer\n");
				pthread_cancel(tthread);
			 	pthread_create(&tthread, NULL, timerThread, sock);
		}
		seqNum++;

  	}
	  usleep(1000);
	}
	printf("sendThread: exiting");
  pthread_exit(NULL);
} 


/*readServerThread
*Function called by seperate thread for iteratively reading message from server
*/
void *recvThread(void* sock) {
	recvPkt = (PACKET*)malloc(sizeof(PACKET));

while(base < numToSend) {
		//printf("recvThread: Packet made\n");
		tryRecv();
		base = (recvPkt->seq) + 1;
		printf("recvThread: received ACK w seq :%d, base is now: %d\n", recvPkt->seq, base);
		if(base == seqNum && resending == 0) {
			// stop timer
			printf("recvThread: stop timer\n");
			pthread_cancel(tthread);
		}
		else if (resending == 0) {
			// start timer
			printf("recvThread: restart timer\n");
			pthread_cancel(tthread);
			pthread_create(&tthread, NULL, timerThread, sock);
		}
	}
	
	printf("Exiting recvThread\n");

	pthread_exit(NULL);
}

//create socket file descriptor
int makeSocket()
{
	srand(98);
	// Creating socket file descriptor
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	printf("Socket created \n");

	memset(&servaddr, 0, sizeof(servaddr));
	
	// Filling server information
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(PORT);
	servaddr.sin_addr.s_addr = INADDR_ANY;

	//INIT FD SET and timeval for timeout
	FD_ZERO(&fdSet);
	FD_SET(sockfd,&fdSet);
  	tv.tv_sec = 4; tv.tv_usec = 0; //Set timeval to 4 seconds..

	int n;
	socklen_t len;

	printf("Makesocket complete\n\n");
}

int sendSyn() {

	do {
		setTv(1,0);
		trySend(seqNum,SYN,NULL);	
		printf("sendSyn: Sent SYN Package, waiting for response...\n");
	} while(waitInput() <= 0);
	return 1;
}

int conAck() {

		trySend(seqNum, ACK, NULL);
		printf("conAck: Waiting 5 seconds...\n");
		setTv(5,0);
	if(waitInput() <= 0) { //waited full time, no problem
	printf("conAck: waited full time, no problem");
		return 1;
	}
	else { //got a packet, yes problem
		printf("conAck: detected packet before timer ran out. Problem!\n");
		tryRecv();
		return 0;
	}
}

//first state of rdp. 
int connectionSetup()
{
	//*****************
	//Connection setup
	//***************** 

	do {
		sendSyn();
		tryRecv();
		resendCounter(&resends, sockfd);
	} while(recvPkt->flags != SYN_ACK || isCorrupt(recvPkt));
	resends = 0;

    printf("Server packet received, flag : %d\n", recvPkt->flags);
	printf("Handshake: Setting ID, seqNum and windowsize...\n");
	ID=recvPkt->id;
	seqNum=recvPkt->seq;
	resetSeqNum = seqNum;
	wSize=recvPkt->windowSize;
	printf("Variables set, ID: %d Sequence Number: %d, Window Size %d\n", ID, seqNum, wSize);
	printf("Responding to server with ACK\n");

	while(1) {
	if(conAck() == 1) {
		printf("conAck success");
		break;
	}
	else {
		conAck();
		resendCounter(&resends,sockfd);
	}
	}
	
	printf("Connection Established\n\n\n\n\n");
}

//function for handling sending and receiving threads during established state.
int established()
{
	//**************
	//Sliding Window
	//***************
	base = resetSeqNum;
	printf("Starting sliding window\n\nbase:%d, seq:%d\n\n", base, seqNum);

	pthread_create(&thread1, NULL, sendThread, &sockfd);
	pthread_create(&thread2, NULL, recvThread, &sockfd);

	pthread_join(thread1, NULL);
	pthread_join(thread2, NULL);
	pthread_cancel(tthread);

	printf("Transmission completed\n\n\n\n\n");
}

int sendFin() {

	do {
	setTv(1,0);
    trySend(seqNum, FIN, NULL);
	resendCounter(&resends, sockfd);
	} while(waitInput() <= 0);
	printf("sendFin: returning 1");
	return 1;

}

//function for sending FIN and proceeding with teardown process
int teardown()
{
	//****************
	//Teardown
	//****************
	//Allocate memory..
	recvPkt = malloc(sizeof(PACKET));


 
	do {
		sendFin();
	
		tryRecv();

		printf("Server packet received, flag : %d\n", recvPkt->flags);
		resendCounter(&resends, sockfd);
	} while(recvPkt->flags != ACK_FIN || isCorrupt(recvPkt));

	
	printf("ACK_FIN received, sending ACK and closing after 5 sec.\n");
	
	do {
		tryRecv(); //tryrecv to clear buffer
		trySend(seqNum, ACK, NULL);
		tv.tv_sec = 5;
		tv.tv_usec = 0;
	} while(waitInput() > 0);

	printf("Send completed, closing\n");

	free(recvPkt);
	free(sendPkt);
	close(sockfd);
	return 0;
}

// Driver code
int main() 
{
	askError();
	makeSocket();
	connectionSetup();
	established();
	teardown();

	return;
}
