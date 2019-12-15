//cnproj.c - protocol for secured data transfer over UDP 
//			 Sends and receives the given file
//
//Usage: 	./cnproj Sender/Recveiver filename listenport
//
//Authors:	Syed Muazzam Ali Shah Kazmi
//		Muhammad Hasnain Naeem
//
//NOTE: this file contains code for both the sender and the receiver 

#include <stdio.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

enum allErrors
{
	NO_ERRORS,
	ERROR_NOT_ENOUGH_ARGS,
	ERROR_WRONG_USAGE,
	ERROR_SOCKET,
	ERROR_BIND,
	ERROR_FILE_FOPEN,
	ERROR_FILE_WRITE
};

typedef struct sockaddr_in SAin;
typedef const struct sockaddr cSA;
typedef struct sockaddr SA;

#define SEGMENT_SIZE(s) (sizeof(s) - SEGMENT_MESSAGE_SIZE + s.length)
#define SEGMENT_MESSAGE_SIZE 10 
int networkWindowSize = 5;

typedef struct Segment
{
		uint16_t checksum;
		int length;
		int seqnum;
		char message[SEGMENT_MESSAGE_SIZE];
} Segment;

int main(int argc, char *argv[]);
int fileSender(unsigned short port, char* fileName);
int fileRecveiver(unsigned short port, char* fileName);
uint16_t checksum(Segment seg);

uint16_t checksum(Segment seg)
{
    uint16_t *segBytes = (uint16_t*) &seg;
    int n = seg.length;
	
	if (n <= sizeof(seg.checksum))
		return 0;

    uint16_t csum = 0xffff; 

    for (int i=sizeof(seg.checksum); i < n/2; i++)
    {
        csum += ntohs(segBytes[i]);
        if (csum > 0xffff)
            csum -= 0xffff;
    }
    //last remaining bytes (if odd length)
    if (n & 1)
    { 
        csum += ntohs(segBytes[n/2]);
        if (csum > 0xffff)
            csum -= 0xffff;
    }
    return csum;
}

int fileSender(unsigned short port, char* fileName)
{
	printf("File sender started with:\n\tfileName: \t%s\n\
		\tport: \t\t%d\n", fileName, port);

	Segment *segWin = (Segment*) malloc(sizeof(Segment) * networkWindowSize);
	//sender (server), receiver (client) details (IP, ports, etc.)
	SAin servaddr, cliaddr;

	memset(&servaddr, 0, sizeof(servaddr));
	memset(&cliaddr, 0, sizeof(cliaddr));

	Segment segRecv;

	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
		return ERROR_SOCKET;

	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = INADDR_ANY;
	
	if (bind(sockfd, (cSA*)&servaddr, sizeof(servaddr)) < 0)
	{
		close(sockfd);
		return ERROR_BIND;
	}

	int len, n, byteRead;
	len = sizeof(cliaddr);

	n = recvfrom(sockfd, (Segment *)&segRecv, sizeof(segRecv), MSG_WAITALL,
		(SA*) &cliaddr, &len);

	len = sizeof(servaddr);

	printf("Client: %d %d %d %s\n", segRecv.checksum, 
		segRecv.length, segRecv.seqnum, segRecv.message);

	FILE *sendFP = fopen(fileName, "r");
	if (!sendFP)
		return ERROR_FILE_FOPEN;

	int pktCount = 0;
	segWin[pktCount].seqnum = 0;

	while ((byteRead = fread(segWin[pktCount].message, sizeof(char),
		 SEGMENT_MESSAGE_SIZE, sendFP)))
	{
		segWin[pktCount].length = byteRead;
		segWin[pktCount].seqnum = (segWin[pktCount].seqnum + 1);
		segWin[pktCount].checksum = checksum(segWin[pktCount]);

		n = sendto(sockfd, (Segment *) &segWin[pktCount], sizeof(segWin[pktCount]), MSG_CONFIRM,
			(cSA*) &cliaddr, len);

		//wait for ack
		if (pktCount == networkWindowSize-1)
		{
			n = recvfrom(sockfd, (Segment *)&segRecv, sizeof(segRecv), MSG_WAITALL,
		(SA*) &cliaddr, &len);

			printf("RECEIVING ACK.........%d\n", pktCount);
			pktCount = -1;
		}
		pktCount++;
	}

	//eof
	segWin[0].length = 0;
	segWin[0].checksum = checksum(segWin[0]);
	n = sendto(sockfd, (Segment *) &segWin[0], sizeof(segWin[0]), MSG_CONFIRM,
			(cSA*) &cliaddr, len); 

	fclose(sendFP);
	close(sockfd);
	return 0;
}

int fileRecveiver(unsigned short port, char* fileName)
{
	printf("File receiver started with:\n\tfileName: \t%s\n\
		\tport: \t\t%d\n", fileName, port);

	SAin servaddr;
	memset(&servaddr, 0, sizeof(servaddr));

	Segment *segWin = (Segment*) malloc(sizeof(Segment) * networkWindowSize);
	Segment seg;
	seg.seqnum = 0;
	seg.length = -1;

	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
		return ERROR_SOCKET;

	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = INADDR_ANY;

	int n, len, bytesWritten;

	sendto(sockfd, (Segment *)&seg, sizeof(seg), MSG_CONFIRM,
		(cSA*) &servaddr, sizeof(servaddr));
	printf("Hello message sent.\n");

	FILE *recvFP = fopen(fileName, "w");
	if (!recvFP)
	{
		close(sockfd);
		return ERROR_FILE_FOPEN;
	}

	len = sizeof(servaddr);
	int pktCount = 0;

	while ((n = recvfrom(sockfd, (Segment *)&segWin[pktCount], sizeof(segWin[pktCount]),
		MSG_WAITALL, (SA*) &servaddr, &len)))
	{
		int isEOF = (segWin[pktCount].length == 0);

		//for (int i=0; i<1000000; i++) {int j = i*i*3;}
		if (segWin[pktCount].checksum != checksum(segWin[pktCount]))
		{
			segWin[pktCount].length = -1;	//mark error
			printf("ERROR IN CHECKSUM! %x %x\n",
				segWin[pktCount].checksum, checksum(segWin[pktCount]));
		}

		//send ack
		if (pktCount == networkWindowSize - 1
		 || isEOF)
		{
			//buffer --> disk
			for (int i=0; i<=pktCount; i++)
			{
				if (segWin[i].length < 1) break;	

				bytesWritten = fwrite(segWin[i].message,
					sizeof(char), segWin[i].length, recvFP);
				if (bytesWritten != segWin[i].length)
				{
					fclose(recvFP);
					close(sockfd);
					return ERROR_FILE_WRITE;
				}
			}

			seg.seqnum += pktCount;
			sendto(sockfd, (Segment *)&seg, sizeof(seg), MSG_CONFIRM,
				(cSA*) &servaddr, sizeof(servaddr));
			printf("SENDING ACK.........%d\n", pktCount);
			pktCount = -1;
		}
			
		if (isEOF)
		{
			printf("EOF reached.........Packets transmitted: %d\n", seg.seqnum);
			break;	
		}

		pktCount++;
		len = sizeof(servaddr);

		//segWin[pktCount].message[segWin[pktCount].length] = '\0';
		//printf("Client: %d %d %d %s\n", segWin[pktCount].checksum, 
		//segWin[pktCount].length, segWin[pktCount].seqnum, segWin[pktCount].message);
	}
		
	fclose(recvFP);
	close(sockfd);
	return 0;
}

int main(int argc, char *argv[])
{
	int status = ERROR_NOT_ENOUGH_ARGS; 
	do
	{
		if (argc != 4) break;

		unsigned short port = atoi(argv[3]);
		signal(SIGPIPE, SIG_IGN);
		status = ERROR_WRONG_USAGE;	
		
		if (strcmp(argv[1], "Sender") == 0)
		{
			status = fileSender(port, argv[2]);
		}
		else if (strcmp(argv[1], "Receiver") == 0)
		{
			status = fileRecveiver(port, argv[2]);
		}
		else break;	
		
	} while (false);

	//print errors
	switch (status)
	{
		case 0:
			break; //no error
		case ERROR_NOT_ENOUGH_ARGS:
		case ERROR_WRONG_USAGE:
			printf("Usage: ./cnproj Sender/Recveiver filename listenport\n");
			return 1;
		case ERROR_SOCKET:
			perror("Socket"); return 1;
		case ERROR_BIND:
			perror("Bind"); return 1;
		case ERROR_FILE_FOPEN:
			perror("fopen"); return 1;
		case ERROR_FILE_WRITE:
			perror("fwrite"); return 1;
		default:
			printf("Unknown error.\n"); return 1; 
	}
	return 0;
}
