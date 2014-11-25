/* Server */



#include <iostream>
#include <fstream>
#include <winsock2.h>
#include <windows.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <process.h>
#include<string>
#include<Tchar.h>
#include<vector>
#include "Server.h"
#include "Timer.cpp"

using namespace std;
#pragma comment(lib,"wsock32.lib")

bool deleteFile(char *s)
{
	char  filename[150] = { "\0" };

	string s1 = SERVER_DIR_PATH;
	string s2 = s;
	s1.append("\\");
	s1.append(s2);
	s1.copy(filename, 150);

	if (remove(filename) != 0)
		return false;
	else
		return true;
}

void Generatelist(){
	std::ofstream file;
	WIN32_FIND_DATA file_data;
	HANDLE hFile;
	vector<string> files;

	string dir = SERVER_DIR_PATH;


	hFile = FindFirstFile((dir + "/*").c_str(), &file_data);

	file.open("C:\\Users\\Ankurp\\Documents\\Visual Studio 2013\\Projects\\SlidingWindowServer\\List.txt");

	file << file_data.cFileName;
	do{
		string fileName = file_data.cFileName;
		files.push_back(fileName);
	} while ((FindNextFile(hFile, &file_data)) != 0);

	file << endl
		<< "========================" << endl;
	for (auto & i : files){
		file << i << endl;
	}
	file << endl
		<< "========================" << endl;
}



bool FileExists(char * filename)
{
	int result;
	struct _stat stat_buf;
	result = _stat(filename, &stat_buf);
	return (result == 0);
}

long GetFileSize(char * filename)
{
	int result;
	struct _stat stat_buf;
	result = _stat(filename, &stat_buf);
	if (result != 0) return 0;
	return stat_buf.st_size;
}


//*************************************************send file********************************************************
bool UdpServer::SendFile(int sock, char * filename, char * sending_hostname, int client_number)
{
	if (RECORD) fout << "SENDER started on host : " << sending_hostname << endl;
	cout << "SENDER started on host : " << sending_hostname << endl;

	Timer * timer = new Timer(); Frame frame; FrameResponse response;
	int sequence_number = client_number % SEQUENCE_WIDTH; // expected sequence start
	int packets_sent = 0, packets_received = 0, revised_sequence_number, i, j, last_ack, last_nak, counter_last_frame_sent = 0;
	long file_size, bytes_to_read = 0, bytes_read = 0, total_bytes_read = 0, bytes_offset = 0;
	bool LastOutgoingFrame = false, FirstPacket = true, Add = false, Found;
	bool AtLeastOneResponse, LastFrameAcked, GoBackNWindow = false, TimedOut = false;
	int sequence_history[WINDOW_SIZE]; fpos_t history_stream_pos[WINDOW_SIZE];

	/* Open file stream in read-only binary mode */
	FILE * stream = fopen(filename, "r+b"); rewind(stream);
	if (stream != NULL)
	{
		file_size = GetFileSize(filename);
		/* Send packets */
		while (1)
		{
			/* Send each frame in the window */
			for (i = 0; i < WINDOW_SIZE; i++)
			{
				/* Increase sequence number for next dispatch */
				if (!FirstPacket) sequence_number = (sequence_number + 1) % (sequence_ubound + 1);

				if (GoBackNWindow) /* We will need to re-read from file and re-send packets */
				{
					/* Decrease the sequence number according to go_back_n variable */
					sequence_number = revised_sequence_number;
					GoBackNWindow = false;

					/* Go back in the history and set the correct file position */
					for (j = 0; j < WINDOW_SIZE; j++)
					if (sequence_history[j] == sequence_number)
					{
						fsetpos(stream, &history_stream_pos[j]);
						break;
					}
				}

				LastOutgoingFrame = ((file_size - ftell(stream)) <= FRAME_BUFFER_SIZE);
				bytes_to_read = (LastOutgoingFrame ? (file_size - ftell(stream)) : FRAME_BUFFER_SIZE);

				/* Keep a history of sequence numbers used in the window */
				sequence_history[i] = sequence_number;
				fgetpos(stream, &history_stream_pos[i]); // save indicator of file right before reading

				/* Read file into frame buffer */
				bytes_read = fread(frame.buffer, sizeof(char), bytes_to_read, stream);
				total_bytes_read += bytes_read;

				/* Set frame parameters */
				frame.buffer_length = bytes_read;
				frame.sequence = sequence_number;	// set the frame sequence bits
				frame.last = LastOutgoingFrame;

				/* Place the frame in the send packet buffer and send it off */
				memcpy(send_packet.buffer, &frame, sizeof(frame));
				send_packet.type = FRAME;
				send_packet.buffer_length = sizeof(frame);
				if (SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet)) { return false; }

				/* Start timer after first frame in window is sent */
				if (i == 0)
					timer->SetInterval(OLDEST_FRAME_TIMER);

				/* Keep track of statistics and log */
				packets_sent++;
				if (RECORD) fout << "SENDER: sent frame " << sequence_number << " [bytes offset " << ftell(stream) << "]" << endl;
				cout << "SENDER: sent frame " << sequence_number << " [bytes offset " << ftell(stream) << "]" << endl;

				FirstPacket = false;
				if (LastOutgoingFrame)
					break;
			}

			AtLeastOneResponse = false; // keep track of whether we've received at least one response
			LastFrameAcked = false; // keep track of whether the last frame has been acked
			TimedOut = false;
			/* Receive responses */
			for (i = 0; i < WINDOW_SIZE; i++)
			{
				while (ReceivePacket(sock, &recv_packet) != FRAME_RESPONSE)
				{
					if (timer->TimedOut())
					{
						if (RECORD) fout << "SENDER: timed out!" << endl;
						cout << "SENDER: timed out!" << endl;
						TimedOut = true; GoBackNWindow = true;
						break;
					}
				}
				if (TimedOut) break;
				packets_received++;
				AtLeastOneResponse = true;

				/* Copy the recv_packet's buffer to the response */
				memcpy(&response, recv_packet.buffer, sizeof(response));
				if (response.type == ACK)
				{
					if (RECORD) fout << "SENDER: received ACK " << response.number << endl;
					cout << "SENDER: received ACK " << response.number << endl;
					if (response.number == sequence_number)
					{
						LastFrameAcked = true; GoBackNWindow = false;
						break;
					}
					else
						last_ack = response.number;
				}
				else if (response.type == NAK)
				{
					if (RECORD) fout << "SENDER: received NAK " << response.number << endl;
					cout << "SENDER: received NAK " << response.number << endl;
					GoBackNWindow = true;
					last_nak = response.number;
					if (response.number == sequence_number)
						break;
				}
			}

			if (GoBackNWindow)
			{
				if (AtLeastOneResponse) // decide from where to restart
				{
					Found = false;
					for (j = WINDOW_SIZE - 1; j >= 0; j--)
					{
						if (sequence_history[j] == last_ack) // Has it been ACK'd ?
						{
							revised_sequence_number = (last_ack + 1) % (sequence_ubound + 1);
							last_ack = -1; Found = true; break;
						}
						else if (sequence_history[j] == last_nak) // Has it been NAK'd?
						{
							revised_sequence_number = last_nak;
							last_nak = -1; Found = true; break;
						}
					}
					if (!Found)
					{
						revised_sequence_number = last_nak;
					}
				}
				else // go back to the beginning of the window
				{
					revised_sequence_number = sequence_history[0];
				}
			}

			/* Check for exit */
			if (LastOutgoingFrame && (LastFrameAcked || counter_last_frame_sent > MAX_RETRIES))
				break;
		}//end of sending files


		// finishing
		fclose(stream);
		cout << "SENDER: file transfer completed successfully!" << endl;
		cout << "SENDER: number of packets sent     : " << packets_sent << endl;
		cout << "SENDER: number of packets received : " << packets_received << endl;
		cout << "SENDER: number of bytes read       : " << total_bytes_read << endl << endl;
		if (RECORD)
		{
			fout << "SENDER: file transfer completed successfully!" << endl;
			fout << "SENDER: number of packets sent     : " << packets_sent << endl;
			fout << "SENDER: number of packets received : " << packets_received << endl;
			fout << "SENDER: number of bytes read       : " << total_bytes_read << endl << endl;
		}
		return true;
	}
	else
	{
		if (RECORD) fout << "SENDER: problem opening the file." << endl;
		cout << "SENDER: problem opening the file." << endl;
		return false;
	}
}


//**********************************************receive file*****************************************************
bool UdpServer::ReceiveFile(int sock, char * filename, char * receiving_hostname, int server_number)
{
	if (RECORD) fout << "Receiver started on host " << receiving_hostname << endl;
	cout << "Receiver started on host " << receiving_hostname << endl;

	Frame frame;
	FrameResponse response;

	long byte_count = 0;
	int packets_received = 0, packets_sent = 0, packets_required = 0, bytes_written = 0, total_bytes_written = 0;
	int i, sequence_number = server_number % SEQUENCE_WIDTH; // expected sequence start

	/* Open file stream in writable binary mode */
	FILE * stream = fopen(filename, "w+b"); rewind(stream);

	if (stream != NULL)
	{
		/* Receive packets */
		while (1)
		{
			/* Block until a packet comes in */
			while (ReceivePacket(sock, &recv_packet) == TIMEOUT) { ; }
			packets_received++;

			if (recv_packet.type == HANDSHAKE) // Send last handshake again, server didnt receive properly
			{
				/* Copy the recv_packet's buffer to the handshake */
				memcpy(&handshake, recv_packet.buffer, sizeof(frame));

				if (handshake.handshake_state == SERVER_ACKS)
				{
					if (RECORD) fout << "Receiver: received handshake 2 (C" << client_number << " S" << handshake.server_number << ")" << endl;
					cout << "Receiver: received handshake 2 (C" << client_number << " S" << handshake.server_number << ")" << endl;

					/* Place handshake in send_packet's buffer and send it out */
					handshake.handshake_state = CLIENT_ACKS;
					send_packet.type = HANDSHAKE;
					send_packet.buffer_length = sizeof(handshake);
					memcpy(send_packet.buffer, &handshake, sizeof(handshake));
					if (SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet)) { printError("Sending packet error!"); }

					if (RECORD) fout << "Receiver: sent handshake 3 (C" << client_number << " S" << handshake.server_number << ")" << endl;
					cout << "Receiver: sent handshake 3 (C" << client_number << " S" << handshake.server_number << ")" << endl;
					packets_sent++;
				}
			}
			else if (recv_packet.type == FRAME)
			{
				/* Copy the recv_packet's buffer to the frame */
				memcpy(&frame, recv_packet.buffer, sizeof(frame));

				if (RECORD) fout << "Receiver: received frame " << (int)frame.sequence;
				cout << "Receiver: received frame " << (int)frame.sequence;

				if ((int)frame.sequence == sequence_number)
				{
					/* Prepare ACK, place it in send_packet's buffer and send it out */
					response.type = ACK;
					response.number = sequence_number;
					send_packet.type = FRAME_RESPONSE;
					send_packet.buffer_length = sizeof(response);
					memcpy(send_packet.buffer, &response, sizeof(response));
					if (SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet)) { printError("Sending packet error!"); }


					//display every frame
					/*cout<<endl;
					for(int i=0;i<frame.buffer_length;++i)
					cout<<frame.buffer[i];
					cout<<endl;	*/

					/* Write frame buffer to file stream */
					byte_count = frame.buffer_length;
					bytes_written = fwrite(frame.buffer, sizeof(char), byte_count, stream);
					total_bytes_written += bytes_written;

					if (RECORD) fout << " ... sent ACK " << response.number << " (bytes written " << total_bytes_written << ")" << endl;
					cout << " ... sent ACK " << response.number << " (bytes written " << total_bytes_written << ")" << endl;
					packets_sent++;
					packets_required++;
					/* Next expected sequence number */
					sequence_number = (sequence_number + 1) % (sequence_ubound + 1);

					/* Exit loop if last frame */
					if (frame.last)
					{
						/* Send the last ACK MAX_RETRIES times to ensure it's received */
						for (i = 0; i < MAX_RETRIES; i++)
						{
							if (SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet)) { printError("Sending packet error!"); }
							if (RECORD) fout << "Receiver: sent ACK " << response.number << " (final)" << endl;
							cout << "Receiver: sent ACK " << response.number << " (final)" << endl;
							packets_sent++;
						}
						break;
					}
				}
				else
				{
					/* Prepare NAK, place it in send_packet's buffer and send it out */
					response.type = NAK;
					response.number = sequence_number;
					send_packet.type = FRAME_RESPONSE;
					send_packet.buffer_length = sizeof(response);
					memcpy(send_packet.buffer, &response, sizeof(response));

					if (SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet)) { printError("Sending packet error!"); }

					if (RECORD) fout << " ... sent NAK " << response.number << endl;
					cout << " ... sent NAK " << response.number << endl;
					packets_sent++;
				}
			}
		}

		// finishing
		fclose(stream);
		cout << "RECEIVER: file transfer completed successfully!" << endl;
		cout << "RECEIVER: number of packets received : " << packets_received << endl;
		cout << "RECEIVER: Number of packets sent (needed): " << packets_required << endl;
		cout << "RECEIVER: number of packets sent     : " << packets_sent << endl;
		cout << "RECEIVER: number of bytes written    : " << total_bytes_written << endl << endl;
		if (RECORD)
		{
			fout << "RECEIVER: file transfer complete!" << endl;
			fout << "RECEIVER: number of packets received : " << packets_received << endl;
			fout << "RECEIVER: number of packets sent     : " << packets_sent << endl;
			fout << "RECEIVER: Number of packets sent (needed): " << packets_required << endl;
			fout << "RECEIVER: number of bytes written    : " << total_bytes_written << endl << endl;
		}
		return true;
	}
	else
	{
		cout << "RECEIVER: problem opening the file." << endl;
		if (RECORD) { fout << "RECEIVER: problem opening the file." << endl; }
		return false;
	}
}


//****************************************send package************************************************
int UdpServer::SendPacket(int sock, Packet * ptr_packet, struct sockaddr_in * sa_in) // fills sa_in struct
{
	return sendto(sock, (const char *)ptr_packet, sizeof(*ptr_packet), 0, (struct sockaddr *)sa_in, sizeof(*sa_in));
}


//****************************************receive package, return value is package type*****************************
PacketType UdpServer::ReceivePacket(int sock, Packet * ptr_packet)
{
	fd_set readfds;			// fd_set is a type
	FD_ZERO(&readfds);		// initialize
	FD_SET(sock, &readfds);	// put the socket in the set

	int bytes_recvd;
	int outfds = select(1, &readfds, NULL, NULL, &timeouts);

	switch (outfds)
	{
	case 0:
		return TIMEOUT; break;
	case 1:
		bytes_recvd = recvfrom(sock, (char *)ptr_packet, sizeof(*ptr_packet), 0, (struct sockaddr*)&sa_in, &sa_in_size);
		return ptr_packet->type; break;
	default:
		printError("select() error!");
	}
}


//********************************************run() ****************************************************************
void UdpServer::run()
{
	bool bContinue;
	char path[100] = "C:\\Users\\Ankurp\\Documents\\Visual Studio 2013\\Projects\\SlidingWindowServer\\";
	/* Initialize winsocket */
	if (WSAStartup(0x0202, &wsadata) != 0)
	{
		WSACleanup();
		printError("Error in starting WSAStartup()\n");
	}

	/* Get hostname of server */
	if (gethostname(server_name, HOSTNAME_LENGTH) != 0)
		printError("Server gethostname() error.");

	if (RECORD) fout << "WINDOW SIZE: " << WINDOW_SIZE << endl << endl;
	cout << "WINDOW SIZE: " << WINDOW_SIZE << endl << endl;

	/*printf("=========== ftpd_server v0.3 ===========\n");
	printf("Server started on host [%s]\n", server_name);
	printf("Awaiting request for file transfer...\n", server_name);
	printf("========================================\n\n");*/
	cout << "Server is ready on host" << server_name << endl;
	cout << "Waiting for client..." << endl;
	cout << "===========================================\n" << endl;
	/* Create a datagram/UDP socket */
	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		printError("Could not create datagram socket : socket() failed");

	/* Fill in server network information */
	memset(&sa, 0, sizeof(sa));				// Zero out structure
	sa.sin_family = AF_INET;                // Internet address family
	sa.sin_addr.s_addr = htonl(INADDR_ANY); // Any incoming interface
	sa.sin_port = htons(SERVER_PORT);       // local server port (5001)
	sa_in_size = sizeof(sa_in);

	/* Bind server socket to port 5001*/
	if (bind(sock, (LPSOCKADDR)&sa, sizeof(sa)) < 0)
		printError("Socket binding error");

	/* Wait until a handshake (of type CLIENT_REQ) comes in */
	while (1)
	{
		if (ReceivePacket(sock, &recv_packet) == HANDSHAKE)
		{
			/* Copy the received packet's buffer back in handshake */
			memcpy(&handshake, recv_packet.buffer, sizeof(handshake));
			if (handshake.handshake_state == CLIENT_REQ)
			{
				client_number = handshake.client_number; //get the client_number
				if (RECORD) fout << "Server: received handshake 1 (C" << client_number << ")" << endl;
				cout << "Server: received handshake 1 (C" << client_number << ")" << endl;
				break;
			}
		}
	}//now get the client_number.

	if (handshake.operation == GET)
	{
		if (RECORD) fout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests GET file: \"" << handshake.filename << "\"" << endl;
		cout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests GET file: \"" << handshake.filename << "\"" << endl;

		if (FileExists(handshake.filename))		 //check if the file exists
		{
			bContinue = true;						 // sent boolean continue to be true
			handshake.handshake_state = SERVER_ACKS; // server ACKs client's request, to be sent to client
		}
		else
		{
			bContinue = false;						 // set the bool continue to be false:
			handshake.handshake_state = FILE_NOT_EXIST;
			if (RECORD) fout << "Server: requested file does not exist." << endl;
			cout << "Server: requested file does not exist." << endl;
		}
	}//end of get
	else if (handshake.operation == PUT)
	{
		bContinue = true;
		handshake.handshake_state = SERVER_ACKS; // server ACKs client's request
		if (RECORD) fout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests PUT file: \"" << handshake.filename << "\"" << endl;
		cout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests PUT file: \"" << handshake.filename << "\"" << endl;
	}
	else if (handshake.operation == LIST)
	{
		cout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests list Of files in server directory." << endl;
		if (RECORD) { fout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests list Of files in server directory." << endl; }
		bContinue = true;						 // sent boolean continue to be true
		handshake.handshake_state = SERVER_ACKS; // server ACKs client's request, to be sent to client	
	}
	else if (handshake.operation == DEL)
	{
		cout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests DELETE operation on File: \"" << handshake.filename << "\"" << endl;
		if (RECORD) { fout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" DELETE operation on File: \"" << handshake.filename << "\"" << endl; }

		if (deleteFile(handshake.filename))
		{
			bContinue = false;						 // sent boolean continue to be true
			handshake.handshake_state = FILE_DELETED; // server ACKs client's request, to be sent to client
			cout << "Server: requested file deleted successfully." << endl;
			if (RECORD) { fout << "Server: requested file deleted successfully." << endl; }
		}
		else
		{
			bContinue = false;						 // sent boolean continue to be false
			handshake.handshake_state = HANDSHAKE_ERROR; // server ACKs client's request, to be sent to client
			cout << "Server: Unexpected problem in file delete." << endl;
			if (RECORD) {
				fout << "Server: Unexpected problem in file delete." << endl;
			}
		}
	}
	else
	{
		bContinue = false;
		handshake.handshake_state = INVALID;
		if (RECORD) fout << "Server: invalid request." << endl;
		cout << "Server: invalid request." << endl;
	}

	if (!bContinue) // just send, don't expect a reply.
	{
		/* Place handshake in send_packet's buffer */
		send_packet.type = HANDSHAKE;
		send_packet.buffer_length = sizeof(handshake);
		memcpy(send_packet.buffer, &handshake, sizeof(handshake));

		/* Send MAX_RETRIES times */
		if (handshake.handshake_state == FILE_DELETED){
			for (int l = 0; l < MAX_RETRIES; l++){
				if (SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet)) { printError("Error in sending packet."); }
			}
		}
		else{
			for (int l = 0; l < MAX_RETRIES; l++){
				if (SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet)) { printError("Error in sending packet."); }
			}
			if (RECORD) fout << "Server: sent error message to client" << endl;
			cout << "Server: sent error message to client" << endl;
		}
		
	}
	else // continue
	{
		srand((unsigned)time(NULL));
		server_number = rand() % MAX_RANDOM; // [0..255] generate a random server_number	

		/* Prepare handshake and place in packet's buffer */
		handshake.server_number = server_number;
		send_packet.type = HANDSHAKE;
		send_packet.buffer_length = sizeof(handshake);
		memcpy(send_packet.buffer, &handshake, sizeof(handshake));

		/* Send the second handshake and keep it until a response comes */
		do {
			if (SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet)) { printError("Error in sending packet."); }

			if (RECORD) fout << "Server: sent handshake 2 [Client_number" << client_number << " Server_number" << server_number << "]" << endl;
			cout << "Server: sent handshake 2 [Client_number" << client_number << " Server_number" << server_number << "]" << endl;

			if (ReceivePacket(sock, &recv_packet) == HANDSHAKE)
			{
				/* Copy the received packet's buffer back in handshake */
				memcpy(&handshake, recv_packet.buffer, sizeof(handshake));

				if (handshake.handshake_state == CLIENT_ACKS && handshake.server_number == server_number)
				{
					if (RECORD) fout << "Server: received handshake 3 (C" << client_number << " S" << server_number << ")" << endl;
					cout << "Server: received handshake 3 (C" << client_number << " S" << server_number << ")" << endl;
					break;//break only of we receive the right handshake
				}
			}
		} while (1);

		/* We know for sure that the right handshake has been received */
		switch (handshake.operation)
		{
		case GET:
			if (!SendFile(sock, handshake.filename, server_name, client_number)) //goes to the sendfile
				printError("An error occurred while sending the file.");
			break;

		case PUT:
			if (!ReceiveFile(sock, handshake.filename, server_name, server_number))//goes to the receivefile
				printError("An error occurred while receiving the file.");
			break;

		case LIST:
			strcat(path, "list.txt");
			if (!SendFile(sock, path, server_name, handshake.client_number))
				printError("An error occurred while sending the file.");
			SetFileAttributes(handshake.filename, FILE_ATTRIBUTE_HIDDEN);
			break;
		default:
			break;
		}

	}

	if (RECORD) fout << "Closing server socket." << endl;
	cout << "Closing server socket." << endl;

	closesocket(sock);
	cout << "Press Enter to exit..." << endl; getchar();
}


//*********************************************constructor, distructor, main*************************************
void UdpServer::printError(TCHAR* msg) {
	DWORD eNum;
	TCHAR sysMsg[256];
	TCHAR* p;

	eNum = GetLastError();
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, eNum,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		sysMsg, 256, NULL);

	// Trim the end of the line and terminate it with a null
	p = sysMsg;
	while ((*p > 31) || (*p == 9))
		++p;
	do { *p-- = 0; } while ((p >= sysMsg) &&
		((*p == '.') || (*p < 33)));

	// Display the message
	_tprintf(TEXT("\n\t%s failed with error %d (%s)"), msg, eNum, sysMsg);
}


unsigned long UdpServer::ResolveName(char name[])
{
	struct hostent *host; // Structure containing host information
	if ((host = gethostbyname(name)) == NULL)
		printError("gethostbyname() failed");
	return *((unsigned long *)host->h_addr_list[0]); // return the binary, network byte ordered address
}


UdpServer::UdpServer(char * fn) // *****************constructor***********************
{
	/* For timer */
	timeouts.tv_sec = STIMER;
	timeouts.tv_usec = UTIMER;

	/* Set the window size and sequence upperbound */
	sequence_ubound = 2 * WINDOW_SIZE - 1;

	/* Open log file */
	fout.open(fn);
}

UdpServer::~UdpServer() // destructor
{
	/* Close log file */
	fout.close();

	/* Uninstall winsock.dll */
	WSACleanup();
}

int main(void)
{
	UdpServer * server = new UdpServer();
	server->run();
	return 0;
}
