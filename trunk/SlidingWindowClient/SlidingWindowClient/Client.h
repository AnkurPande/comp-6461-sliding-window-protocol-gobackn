#include<iostream>
#include <fstream>
#include <time.h>
#include <winsock2.h>
using namespace std;
#pragma comment(lib,"wsock32.lib")

#define WINDOW_SIZE 4
#define MAX_RETRIES 10
#define RECORD 1

#define STIMER 0
#define UTIMER 30000             // in nanoseconds
#define OLDEST_FRAME_TIMER	400  // in milliseconds
#define CLIENT_PORT 5000
#define REMOTE_PORT 7000

#define INPUT_LENGTH    20
#define HOSTNAME_LENGTH 20
#define USERNAME_LENGTH 20
#define FILENAME_LENGTH 20
#define FRAME_BUFFER_SIZE 60
#define PACKET_BUFFER_SIZE 100
#define MAX_RANDOM 256;
#define SEQUENCE_WIDTH 3
#define CLIENT_DIR_PATH "C:\\Users\\Ankurp\\Documents\\Visual Studio 2013\\Projects\\SlidingWindowClient\\SlidingWindowClient" //Path of Client directory


typedef enum { HANDSHAKE = 1, FRAME, FRAME_RESPONSE, TIMEOUT, PACKET_RECV_ERROR } PacketType;
typedef enum { CLIENT_REQ = 1, SERVER_ACKS, CLIENT_ACKS, FILE_NOT_EXIST, INVALID,FILE_DELETED, HANDSHAKE_ERROR,FILE_RENAMED} HandshakeType;
typedef enum { GET = 1, PUT, LIST, DEL, REN,CANCEL } Operation;
typedef enum { ACK = 1, NAK } FrameResponseType;

typedef struct {
	PacketType type;
	int buffer_length;
	char buffer[PACKET_BUFFER_SIZE];
} Packet;

typedef struct {
	HandshakeType handshake_type;
	Operation direction;
	int client_number;
	int server_number;
	bool filePresentAtServer;
	bool bRename;
	bool bReplace;
	bool bCancel;
	char hostname[HOSTNAME_LENGTH];
	char username[USERNAME_LENGTH];
	char filename[FILENAME_LENGTH];
	char newfilename[FILENAME_LENGTH];
} Handshake;

typedef struct {
	unsigned int sequence;
	bool last;
	int buffer_length;
	char buffer[FRAME_BUFFER_SIZE];
} Frame;

typedef struct {
	FrameResponseType type;
	int number;
} FrameResponse;

class UdpClient
{
	WSADATA wsadata;
	int sock;						// socket descriptor
	struct sockaddr_in sa;			// client info, IP, port 5000
	struct sockaddr_in sa_in;		// router info, IP, port 7000
	int sa_in_size;
	int client_number;
	int server_number;
	struct timeval timeouts;

	int window_size;
	int sequence_ubound;

	Handshake handshake;
	Packet send_packet;
	Packet recv_packet;

private:
	ofstream fout;			//log file

public:
	UdpClient(char *fn = "client_log.txt"); // constructor
	~UdpClient();	// destructor
	void run();

	bool SendFile(int, char *, char *, int);
	int SendPacket(int, Packet *, struct sockaddr_in *);

	bool ReceiveFile(int, char *, char *, int);
	PacketType ReceivePacket(int, Packet *);

	unsigned long ResolveName(char name[]);
	void printError(TCHAR* msg);
};
