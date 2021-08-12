// 
//server.c
//description: server-side rdp
//usage: execute in terminal with ./server. Await client connection. Await transfer completetion.
//authors: Hannes Elfving Erik Wetterholm
//date
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

//Handshake variablessssk
int cliID=1; //ID of client, increment for every new connection

//Socket variables
int sockfd;
struct sockaddr_in servaddr, cliaddr;

//Packet variables
PACKET* recvPkt = NULL;
PACKET* sendPkt = NULL;
PACKET acks[999];
fd_set fdSet;
socklen_t len;

//Timevals
struct timeval tv;
int tv_waitu = 1500;
int tv_waitS = 0;

int n, selRetVal, expectedSeqNum;

//Resend var
int resends=0;


//Threads
pthread_t threadS, threadR, threadShutdown;

int makeSocket();
int connectionSetup();
int established();
int teardown();
int processdata();
int isCorrupt(PACKET* pkt);
int getID();
int processdata();
int trySend();
int tryRecv();

//Tries to send packet. Returns 1 if properly sent, returns 0 if packet emitted.
int trySend(int sq, int flag, char* data) {

    sendPkt = makePkt(sq, flag, data);
    printf("trySend: Packet made\n");
	printf("trySend: Attempting to send  %d packet of size %d...\n", sendPkt->flags,sizeof(*sendPkt));
	if(sendPkt->flags != EMIT) {
        sendto (sockfd, (PACKET*)sendPkt, sizeof(PACKET), 
        0, (const struct sockaddr *) &cliaddr,
        len);
        return 1;
    }
    else
    {
            printf("trySend: Packet emitted\n");
            return 0;
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

//Tries to receive packet.
int tryRecv() {

    if(recvPkt)
    {
        free(recvPkt);
    }

    recvPkt = malloc(sizeof(PACKET));

    n = recvfrom(sockfd, (PACKET*)recvPkt, sizeof(PACKET), 
        0, (struct sockaddr *) &cliaddr,
        &len);
      
    printf("tryRecv: Received packet flag: %d \n", recvPkt->flags);
    return 1;
    
   

}


//Make server socket and bind address.
int makeSocket()
{
    srand(3);

    // Creating socket file descriptor
    if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
      
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));
      
    // Filling server information
    servaddr.sin_family    = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);
      
    // Bind the socket with the server address
    if ( bind(sockfd, (const struct sockaddr *)&servaddr, 
            sizeof(servaddr)) < 0 )
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    //INIT FD SET and timeval for timeout
	FD_ZERO(&fdSet);
	FD_SET(sockfd,&fdSet);
  	tv.tv_sec = 4; tv.tv_usec = 0; //Set timeval to 4 seconds

    printf("Socket set up complete. Starting connection setup.\n\n\n\n\n");

    return 1;
}


int resetTv() {
    tv.tv_sec = tv_waitS;
    tv.tv_usec = tv_waitu;
}

int recvSyn() {

    do {
    printf("recvSyn: Awaiting packet of size %d from client...\n", sizeof(PACKET));
    tryRecv();
    
    printf("recvSyn: Client sent a packet with flag : %d, proceeding to send SYN_ACK\n", recvPkt->flags);
    
    } while(recvPkt->flags != SYN || isCorrupt(recvPkt));

    return 1;
}

//Send SYN_ACK and await response from client.
int sendSynAck() {

    do {
    trySend(seqNum, SYN_ACK, NULL);
    printf("Responded to client with SYN_ACK seqNum :%d, ID: %d, wSize: %d\nWaiting for ACK...\n", sendPkt->seq, sendPkt->id, sendPkt->windowSize); 
		tv.tv_sec = 0;
        tv.tv_usec = 1000;
    } while(waitInput() <= 0);

    return 1;
}

//Connection setup between server and client. Await SYN packet, then proceed with the connection process. 
int connectionSetup() 
{
    //****************
    //Connection setup
    //****************
    
    seqNum = 0;
    expectedSeqNum = seqNum;
    len = sizeof(cliaddr);
    resetTv();
    
    recvSyn();



    do {
        sendSynAck();
        tryRecv();
        resendCounter(&resends, sockfd);
    } while(recvPkt->flags != ACK || isCorrupt(recvPkt));
    resends=0;

    printf("Client sent a packet with flag : %d\n", recvPkt->flags);

    printf("Connection established\n\n\n\n\n", recvPkt->flags);

    return 1;

}

//Thread for continously sending ACKS during established state
void *sendThread() {
    printf("sendThread started\n");
    int ackToSend = expectedSeqNum;
    while(ackToSend < 999) {
        if(recvPkt != NULL) {
        if(recvPkt->flags == FIN) {
            break;
        }
        }
        if(ackToSend < expectedSeqNum) {
        printf("sendThread: allocating memory for ACK\n");
        acks[ackToSend] = makePkt2(ackToSend, ACK, NULL);
        printf("sendThread: sending ACK\n");
        sendto(sockfd, (PACKET*)&acks[ackToSend], sizeof(PACKET), 
            0, (const struct sockaddr *) &cliaddr,
                len);
        printf("Sent ACK package for %d\n", ackToSend); 
        ackToSend++;
        }
    usleep(1000);  
    }
    printf("sendThread terminated internally \n");
    pthread_exit(NULL);
}

//Function for checking whether crc is correct!
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

//This function will be passed to tthread. If 10 seconds has passed, call resend.
void *connTimer() {

	printf("timerThread: Timer thread started\n");

	sleep(30);

	printf("connTimer: Timeout triggered shut down\n");
    close(sockfd);
	exit(0);
  	pthread_exit(NULL);

}

//Thread for continously receiving packets from client. If packet is legitimate, increments expectedSeqNum.
void *recvThread() { 
    printf("recvThread started \n");
    printf("recvThread: Alloc memory for recvPkt\n");  
    while(recvPkt->flags != FIN) {
        recvPkt = (PACKET*)malloc(sizeof(PACKET));
        printf("recvThread: waiting for receive\n");
          tryRecv();
        printPacket(recvPkt);
        if(recvPkt->flags == FIN) {
            printf("recvThread: FIN Received breaking...\n");
            break;
        }
        if(expectedSeqNum == recvPkt->seq  && !isCorrupt(recvPkt)) {

        expectedSeqNum++;      
        processdata();

        }
        else {
        //Don't proc data
        }
        free(recvPkt);
    }
    
    printf("recvThread terminated internally \n");
    pthread_exit(NULL);
    
}

//This function handles the threads that are used during the established state. The threads terminate upon
//receiving FIN.
int established () {

    //**************
	//Sliding Window
	//***************
	
	printf("Starting sliding window\n");

    pthread_create(&threadR, NULL, recvThread, NULL);
    pthread_create(&threadS, NULL, sendThread, NULL);

    pthread_join(threadR, NULL);
    pthread_cancel(threadS);

    printf("Transfer complete, shutting down\n\n\n\n\n");

    return 1;
}

int sendAckFin() {

    do {
    tv.tv_sec=1;
    tv.tv_usec = 0;
    trySend(expectedSeqNum, ACK_FIN, NULL);
    resendCounter(&resends, sockfd);
    } while(waitInput()  <= 0 );

    return 1;
}

//Teardown. Responds to FIN with ACK + FIN. Continues the teardown process. ..
int teardown() {

    //*****************
    //Teardown
    //*****************

    printf("Received FIN. Teardown started. Responding with ACK + FIN\n");
   

    do {

        sendAckFin();

        tryRecv();

    } while(recvPkt->flags != ACK || isCorrupt(recvPkt));
    resends=0;
    printf("Waiting 30 then closing...\n");

    sleep(30);
    printf("Closing");
    close(sockfd);

}

//Returns and increments ID (non-essential unless multiclient)
int getID() {
    return ID++;
}

//For demonstration purposes
int processdata()
{
    return 0;
}

int main() {
      
    askError();

    if(makeSocket())

    if(connectionSetup())

    if(established())
    
    if(teardown())


    return 0;
}