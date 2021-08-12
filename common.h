  
//
//common.h
//description: headerfile with common variables and functions for the purposes of rdp
//authors: Hannes Elfving, Erik Wetterholm
//date: 2021-05-26
//

#define PORT     8080
#define MAXLINE 1024
#define ACK 0
#define SYN 1
#define FIN 2
#define REG 3
#define SYN_ACK 4
#define EMIT 5
#define ACK_FIN 6
#define MAX_RESENDS 150

//The following three variables are used during handshake
int ID = 0;
int seqNum = 0; 
int wSize = 5;

//user will be asked to input risk of corrupt packet
int errPercentage = 0;

typedef struct packet {

    int flags;
    int id;
    int seq;
    int windowSize;
    uint16_t crc;
    char* data;

} PACKET;

void resendCounter(int* resends, int fd) {
    (*resends)++;
    if((*resends) >= MAX_RESENDS) {
        printf("************resendCounter: Resends exceed MAX_RESENDS, closing***********\n");
        close(fd);
        exit(0);
    }
}

//this function possibly emits a packet
void mightEmitPacket(PACKET* pkt) {


	int r = rand()%100;
	if(r < (errPercentage/2)) {
		pkt->flags = EMIT;
	}	
}

//this function takes a packet and possibly corrupts it
void mightCorruptPacket(PACKET* pkt) {


	int r = rand()%100;
	if(r < (errPercentage/2)) {
		pkt->crc = 1;
	}	
}

//ask user to enter risk of packet corruption in %
int askError() {

	printf("Please enter corruption/loss risk: ");
	scanf("%d", &errPercentage);
	//while(0<errPercentage 100<errPercentage)
	printf("errPercentage set\n");
	return 0;
}

void printPacket(PACKET* pkt) 
{
    time_t t = time(NULL);
    char* tstr = ctime(&t);
    tstr[strlen(tstr)-1] = '\0';
    printf("-----------Packet Print--------------\n");
    printf("-TIMESTAMP-\n%s\n", tstr);
    printf("Flags: %d: \n",pkt->flags);
    printf("Id: %d: \n",pkt->id);
    printf("Sequence number: %d\n",pkt->seq);
    printf("Windowsize: %d: \n",pkt->windowSize);
    printf("Cycle redundancy check variable %d: \n",pkt->crc);
      if(pkt->data==NULL)
        printf("Data: null\n");
        //else
        //printf("Data: %s", pkt->data);
    printf("\n----------------------------\n\n");
    
    
}

    uint16_t checksum(void* indata,size_t datalength) {
    char* data = (char*)indata;
    
    // Initialise the sum.
    uint32_t sum = 0xffff;

    //Adds the 16bit blocks and sumates it to sum variable. 
    for (size_t i = 0; i+1 < datalength; i += 2) 
    {
        uint16_t word;
        memcpy(&word, data+i, 2);

        sum += ntohs(word);

        if (sum > 0xffff) 
            sum -= 0xffff;
    }

    // deal with any partial block at the end of the data.
    //If the sum is not evenly divideable we need to deal with it.
    if (datalength & 1) 
    {
        uint16_t word = 0;
        memcpy(&word, data+datalength-1, 1);

        sum += ntohs(word);

        if (sum > 0xffff) 
            sum -= 0xffff;
    }

    // Return the checksum. ~ Means that we do a bitwise complement of the sum.

    return htons(~sum);
}

uint16_t getBadCrc()
{
    return 1;    
}

PACKET* makePkt(int seq, const int flag, char* data) {

    PACKET* packet = calloc(1, sizeof(PACKET));
    packet->seq=seq;
    packet->flags=flag;
    packet->windowSize=wSize;
    packet->id=ID;
    packet->data=data;
    packet->crc=0;
    mightEmitPacket(packet);
    packet->crc=checksum((void*)packet, sizeof(*packet));
    mightCorruptPacket(packet);
    

    return packet;

}

PACKET makePkt2(int seq, const int flag, char* data) {

    PACKET packet;
    packet.seq=seq;
    packet.flags=flag;
    packet.windowSize=wSize;
    packet.id=ID;
    packet.data=data;
    packet.crc=0;
    mightEmitPacket(&packet);
    packet.crc=checksum((void*)&packet, sizeof(packet));
    mightCorruptPacket(&packet);

    return packet;

}