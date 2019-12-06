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
int networkWindowSize = 10;

typedef struct Segment
{
		int checksum;
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

	//sender (server), receiver (client) details (IP, ports, etc.)
	SAin servaddr, cliaddr;
	__bzero(&servaddr, sizeof(servaddr));
	__bzero(&cliaddr, sizeof(cliaddr));

	Segment seg, segRecv;
	seg.seqnum = 1412;
	seg.length = 50;
	seg.checksum = 110;
	strcpy(seg.message, "Sent by sender.");

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

	printf("Client: %d %d %d %s\n", segRecv.checksum, 
		segRecv.length, segRecv.seqnum, segRecv.message);

	FILE *sendFP = fopen(fileName, "r");
	if (!sendFP)
		return ERROR_FILE_FOPEN;

	seg.seqnum = 0;
	while ((byteRead = fread(seg.message, sizeof(char),
		 SEGMENT_MESSAGE_SIZE, sendFP)))
	{
		seg.length = byteRead;
		seg.checksum = 0;
		seg.seqnum = (seg.seqnum + 1) % (networkWindowSize/2);
		seg.checksum = checksum(seg);

		n = sendto(sockfd, (Segment *) &seg, SEGMENT_SIZE(seg), MSG_CONFIRM,
			(cSA*) &cliaddr, len);
	}
	n = sendto(sockfd, (Segment *) &seg, 0, MSG_CONFIRM,
			(cSA*) &cliaddr, len); //eof

	fclose(sendFP);
	close(sockfd);
	return 0;
}

int fileRecveiver(unsigned short port, char* fileName)
{
	printf("File receiver started with:\n\tfileName: \t%s\n\
		\tport: \t\t%d\n", fileName, port);

	SAin servaddr;
	__bzero(&servaddr, sizeof(servaddr));

	Segment seg, segRecv;
	seg.seqnum = 1315;
	seg.length = 100;
	seg.checksum = 1010;
	strcpy(seg.message, "Sent by receiver.");

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

	while ((n = recvfrom(sockfd, (Segment *)&segRecv, sizeof(segRecv),
		MSG_WAITALL, (SA*) &servaddr, &len)))
	{
		bytesWritten = fwrite(segRecv.message, sizeof(char),
			segRecv.length, recvFP);
		if (bytesWritten != segRecv.length)
		{
			fclose(recvFP);
			close(sockfd);
			return ERROR_FILE_WRITE;
		}
		segRecv.message[segRecv.length] = '\0';
		printf("Client: %d %d %d %s\n", segRecv.checksum, 
			segRecv.length, segRecv.seqnum, segRecv.message);
		if (segRecv.checksum == checksum(segRecv))
		{
			printf("Checksum Ok.\n");
		}
		else
		{
			printf("ERROR IN CHECKSUM! %x %x\n", segRecv.checksum, checksum(segRecv));
		}
		
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