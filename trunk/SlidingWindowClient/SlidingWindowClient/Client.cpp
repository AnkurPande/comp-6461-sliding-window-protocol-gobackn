/* Client */


#include <winsock2.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <windows.h>
#include <time.h>
#include<tchar.h>
#include<tchar.h>
#include<vector>
#include "Client.h"
#include "Timer.cpp"

using namespace std;
#pragma comment(lib,"wsock32.lib")

bool deleteFile(char *s)
{
	char  filename[150] = { "\0" };

	string s1 = CLIENT_DIR_PATH;
	string s2 = s;
	s1.append("\\");
	s1.append(s2);
	s1.copy(filename, 150);

	if (remove(filename) != 0)
		return false;
	else
		return true;
}

void list(string s){

	WIN32_FIND_DATA file_data;
	HANDLE hFile;
	vector<string> files;

	string dir = s;


	hFile = FindFirstFile((dir + "/*").c_str(), &file_data);

	cout << file_data.cFileName;
	do{
		string fileName = file_data.cFileName;
		files.push_back(fileName);
	} while ((FindNextFile(hFile, &file_data)) != 0);

	cout << endl
		<< "========================" << endl;
	for (auto & i : files){
		cout << i << endl;
	}
	cout << endl
		<< "========================" << endl;
}

void printlist(){
	std::ifstream file;
	file.open("C:\\Users\\Ankurp\\Documents\\Visual Studio 2013\\Projects\\SlidingWindowClient\\List.txt");
	char data[50] = { "\0" };
	cout << endl << "List Of Files / Folders in Server Directory." << endl;
	while (!file.eof()){
		file >> data;
		cout << data << endl;
	}
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

void UdpClient::printError(TCHAR* msg) {
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
	_tprintf(TEXT("\n%s: Failed with ERROR NO: %d \nERROR Msg:(%s)\n"), msg, eNum, sysMsg);
}

bool UdpClient::SendFile(int sock, char * filename, char * sending_hostname, int server_number)
{
	if (RECORD) fout << "Sender started on host " << sending_hostname << endl;
	cout << "Sender started on host " << sending_hostname << endl;

	Timer * timer = new Timer(); Frame frame; FrameResponse response;
	int sequence_number = server_number % SEQUENCE_WIDTH; // expected sequence start
	int packets_sent = 0, packets_received = 0, revised_sequence_number, i, j, last_ack, last_nak, counter_last_frame_sent = 0;
	long file_size, bytes_to_read = 0, bytes_read = 0, total_bytes_read = 0, bytes_offset = 0;
	bool LastOutgoingFrame = false, FirstPacket = true, bAdd = false, Found;
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
				////display every frame
				//for (int i = 0; i<frame.buffer_length; ++i)
				//	cout << endl << frame.buffer << endl;
				if (SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet)) { return false; }

				/* Start timer after first frame in window is sent */
				if (i == 0)
					timer->SetInterval(OLDEST_FRAME_TIMER);

				/* Keep track of statistics and log */
				packets_sent++;
				if (RECORD) fout << "Sender: sent frame " << sequence_number << " (bytes offset " << ftell(stream) << ")" << endl;
				cout << "Sender: sent frame " << sequence_number << " (bytes offset " << ftell(stream) << ")" << endl;

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
						if (RECORD) fout << "Sender: timed out!" << endl;
						cout << "Sender: timed out!" << endl;
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
					if (RECORD) fout << "Sender: received ACK " << response.number << endl;
					cout << "Sender: received ACK " << response.number << endl;
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
					if (RECORD) fout << "Sender: received NAK " << response.number << endl;
					cout << "Sender: received NAK " << response.number << endl;
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
		}


		// finishing
		fclose(stream);
		cout << "Sender: file transfer completed successfully" << endl;
		cout << "Sender: number of packets sent     : " << packets_sent << endl;
		cout << "Sender: number of packets received : " << packets_received << endl;
		cout << "Sender: number of bytes read       : " << total_bytes_read << endl << endl;
		if (RECORD)
		{
			fout << "Sender: file transfer complete" << endl;
			fout << "Sender: number of packets sent     : " << packets_sent << endl;
			fout << "Sender: number of packets received : " << packets_received << endl;
			fout << "Sender: number of bytes read       : " << total_bytes_read << endl << endl;
		}
		return true;
	}
	else
	{
		if (RECORD) fout << "Sender: problem opening the file." << endl;
		cout << "Sender: problem opening the file." << endl;
		return false;
	}
}

bool UdpClient::ReceiveFile(int sock, char * filename, char * receiving_hostname, int client_number)
{
	if (RECORD) fout << "Receiver started on host " << receiving_hostname << endl;
	cout << "Receiver started on host " << receiving_hostname << endl;

	Frame frame;
	FrameResponse response;

	long byte_count = 0;
	int packets_received = 0, packets_sent = 0, packets_required = 0, bytes_written = 0, bytes_written_total = 0;
	int i, sequence_number = client_number % SEQUENCE_WIDTH; // expected sequence start

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

				if (handshake.handshake_type == SERVER_ACKS)
				{
					if (RECORD) fout << "Receiver: received handshake 2 (C" << client_number << " S" << handshake.server_number << ")" << endl;
					cout << "Receiver: received handshake 2 (C" << client_number << " S" << handshake.server_number << ")" << endl;

					/* Place handshake in send_packet's buffer and send it out */
					handshake.handshake_type = CLIENT_ACKS;
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
					bytes_written_total += bytes_written;

					if (RECORD) fout << " ... sent ACK " << response.number << " (bytes written " << bytes_written_total << ")" << endl;
					cout << " ... sent ACK " << response.number << " (bytes written " << bytes_written_total << ")" << endl;
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
		cout << "Receiver: file transfer complete!" << endl;
		cout << "Receiver: number of packets received : " << packets_received << endl;
		cout << "Receiver: number of packets sent     : " << packets_sent << endl;
		cout << "Receiver: Number of packets sent (needed): " << packets_required << endl;
		cout << "Receiver: number of bytes written    : " << bytes_written_total << endl << endl;
		if (RECORD)
		{
			fout << "Receiver: file transfer complete!" << endl;
			fout << "Receiver: number of packets received : " << packets_received << endl;
			fout << "Receiver: number of packets sent     : " << packets_sent << endl;
			fout << "Receiver: Number of packets sent (needed): " << packets_required << endl;
			fout << "Receiver: number of bytes written    : " << bytes_written_total << endl << endl;
		}
		return true;
	}
	else
	{
		cout << "Receiver: problem opening the file." << endl;
		if (RECORD) { fout << "Receiver: problem opening the file." << endl; }
		return false;
	}
}

int UdpClient::SendPacket(int sock, Packet * ptr_packet, struct sockaddr_in * sa_in) // fills sa_in struct
{
	return sendto(sock, (const char *)ptr_packet, sizeof(*ptr_packet), 0, (struct sockaddr *)sa_in, sizeof(*sa_in));
}

PacketType UdpClient::ReceivePacket(int sock, Packet * ptr_packet)
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

void UdpClient::run()
{
	char server[INPUT_LENGTH];
	char filename[INPUT_LENGTH];
	char direction[INPUT_LENGTH];
	char hostname[HOSTNAME_LENGTH];
	TCHAR username[USERNAME_LENGTH];
	DWORD susername = sizeof(username);
	char remotehost[HOSTNAME_LENGTH];
	unsigned long filename_length = (unsigned long)FILENAME_LENGTH;
	bool InputDetailsValid; bool bContinue;
	char path[100] = "C:\\Users\\Ankurp\\Documents\\Visual Studio 2013\\Projects\\SlidingWindowClient\\";
	/* Initialize winsocket */
	if (WSAStartup(0x0202, &wsadata) != 0)
	{
		WSACleanup();
		printError("Error in starting WSAStartup()\n");
	}

	/* Get username of client */
	if (!GetUserName(username, &susername/*&filename_length*/))
		printError("Cannot get the user name");

	/* Get hostname of client */
	if (gethostname(hostname, (int)HOSTNAME_LENGTH) != 0)
		printError("Cannot get the host name");

	if (RECORD) fout << "WINDOW SIZE: " << WINDOW_SIZE << endl << endl;
	cout << "WINDOW SIZE: " << WINDOW_SIZE << endl << endl;

	/* Display program header */
	/*printf("============== ftpd_client v0.3 ==============\n");
	printf("User [%s] started client on host [%s]\n", username, hostname);
	printf("To quit, type \"quit\" or \"exit\" as server name\n");
	printf("==============================================\n\n");*/
	cout << "User : " << username << " started client on host" << hostname << endl;
	cout << "Type \"quit\" or \"exit\" as server name to quit" << endl;
	cout << "==================================================\n" << endl;
	/* Loop until username inputs "quit" or "exit" as servername */
	cout << "Enter server name : "; cin >> server;
	while (strcmp(server, "quit") != 0 && strcmp(server, "exit") != 0)
	{
		InputDetailsValid = false;
		bContinue = true;

		/* Create a datagram/UDP socket */
		if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) { printError("UDP socket creation error!"); }

		/* Client network information */
		memset(&sa, 0, sizeof(sa));				// zero out structure
		sa.sin_family = AF_INET;				// internet address family
		sa.sin_addr.s_addr = htonl(INADDR_ANY); // any incoming interface
		sa.sin_port = htons(CLIENT_PORT);		// set the port

		/* Bind client socket to port 5000 */
		if (bind(sock, (LPSOCKADDR)&sa, sizeof(sa)) < 0) { printError("Socket binding error!"); }

		srand((unsigned)time(NULL));
		client_number = rand() % MAX_RANDOM; // [0..255]

		/* User input*/
		/* User input*/
		cout << "\nEnter router host : " << flush; cin >> remotehost;
		cout << "\nEnter direction   : " << flush;
		cout << "\n'Get' to request a file from Server." << flush;
		cout << "\n'Put' to put a file on Server." << flush;
		cout << "\n'List' to request a list of files ." << flush;
		cout << "\n'Delete' to delete file ." << flush;
		cout << "\nEnter your choice : " << flush;
		cin >> direction; cout << endl;


		char choice[10] = { "\0" };

		/* File transfer direction */

		if (strcmp(direction, "Delete") == 0)
		{

			cout << "\nEnter C for client directory" << flush
				<< "\nEnter S for server directory" << flush
				<< "\nEnter your choice : " << flush;
			cin >> choice; cout << endl;

			if (choice[0] == 'C')
			{
				//	cout << "Inside  Client Delete.";
				list(CLIENT_DIR_PATH);
				cout << "\nEnter file name   : " << flush; cin >> filename;

				if (!deleteFile(filename))
				{
					printError("Problem in deleting file.");
				}
				else if (deleteFile(filename))
				{
					cout << "File successfuly deleted." << endl;
				}

			}
			else if (choice[0] == 'S')
			{
				InputDetailsValid = true;
				
				handshake.direction = DEL;
				printlist();
				cout << "\nEnter file name   : " << flush; cin >> filename;
			}
		}
		else if (strcmp(direction, "List") == 0)
		{
			cout << "\nEnter C for client directory" << flush
				<< "\nEnter S for server directory" << flush
				<< "\nEnter your choice : " << flush;
			cin >> choice; cout << endl;

			if (choice[0] == 'C')
			{
				//	cout << "Inside Client List.";
				list(CLIENT_DIR_PATH);
			}
			else if (choice[0] == 'S')
			{
				//	cout << "Inside Server List.";
				handshake.direction = LIST;
				InputDetailsValid = true;
			}
		}
		else if (strcmp(direction, "Get") == 0)
		{
			printlist();
			cout << "\nEnter file name   : " << flush; cin >> filename;

			InputDetailsValid = true;
			handshake.direction = GET;
		}
		else if (strcmp(direction, "Put") == 0)
		{
			list(CLIENT_DIR_PATH);
			cout << "\nEnter file name   : " << flush; cin >> filename;
			if (!FileExists(filename)){
				printError("File does not exist on client side.");
				system("PAUSE");
				break;
			}
			else{
				handshake.direction = PUT;
				InputDetailsValid = true;
			}
		}
		else
		{
			InputDetailsValid = false;
			printError("Invalid Choice of Direction.");
		}

		

		/* Copy user-input into handshake */
		strcpy(handshake.hostname, hostname);
		strcpy(handshake.username, (char*)username);
		strcpy(handshake.filename, filename);
				
		if (InputDetailsValid)
		{
			/* Router network information */
			struct hostent * rp; // structure containing router
			rp = gethostbyname(remotehost);
			memset(&sa_in, 0, sizeof(sa_in));
			memcpy(&sa_in.sin_addr, rp->h_addr, rp->h_length); // fill sa_in with rp info
			sa_in.sin_family = rp->h_addrtype;
			sa_in.sin_port = htons(REMOTE_PORT);
			sa_in_size = sizeof(sa_in);

			handshake.client_number = client_number;
			handshake.handshake_type = CLIENT_REQ;

			/* Place handshake in send_packet's buffer */
			send_packet.type = HANDSHAKE;
			send_packet.buffer_length = sizeof(handshake);
			memcpy(send_packet.buffer, &handshake, sizeof(handshake));

			/* Initiate handshaking protocol */
			do
			{
				if (SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet)) { printError("Sending packet error!"); }

				if (RECORD) fout << "Client: sent handshake 1 (C" << client_number << ")" << endl;
				cout << "Client: sent handshake 1 (C" << client_number << ")" << endl;

				if (ReceivePacket(sock, &recv_packet) == HANDSHAKE)
				{
					/* Copy the received packet's buffer back in handshake */
					memcpy(&handshake, recv_packet.buffer, sizeof(handshake));

					/* Check how the server responds */
					if (handshake.handshake_type == SERVER_ACKS && handshake.client_number == client_number)
					{
						server_number = handshake.server_number;
						bContinue = true; break;
					}
					else if (handshake.handshake_type == FILE_NOT_EXIST)
					{
						if (RECORD) fout << "Client: requested file does not exist!" << endl;
						cout << "Client: requested file does not exist!" << endl;
						bContinue = false; break;
					}
					else if (handshake.handshake_type == INVALID)
					{
						if (RECORD) fout << "Client: invalid request." << endl;
						cout << "Client: invalid request." << endl;
						bContinue = false; break;
					}
					else if (handshake.handshake_type == FILE_DELETED)
					{
						cout << "Client: Server Responds File Deleted Successfully!" << endl;
						if (RECORD) { fout << "Client: Server Responds File Deleted successfully!" << endl; }
						cout << "Client: Need to Refresh the List of Server directory files /folders!" << endl;
						bContinue = false; break;
					}
					else if (handshake.handshake_type == HANDSHAKE_ERROR)
					{
						cout << "Client: Server Responds Handshake ERROR!" << endl;
						if (RECORD) { fout << "Client: Server Responds Handshake ERROR" << endl; }
						bContinue = false; break;
					}

				}
			} while (1);

			if (bContinue)
			{
				if (RECORD) fout << "Client: received handshake 2 (C" << client_number << " S" << server_number << ")" << endl;
				cout << "Client: received handshake 2 (C" << client_number << " S" << server_number << ")" << endl;

				// Third shake. Acknowledge server's number by sending back the handshake
				handshake.handshake_type = CLIENT_ACKS;

				/* Place handshake in send_packet's buffer and send it out */
				send_packet.type = HANDSHAKE;
				send_packet.buffer_length = sizeof(handshake);
				memcpy(send_packet.buffer, &handshake, sizeof(handshake));

				for (int k = 0; k < MAX_RETRIES; k++)
				if (SendPacket(sock, &send_packet, &sa_in) != sizeof(send_packet)) { printError("Error in sending packet."); }

				if (RECORD) fout << "Client: sent handshake 3 (C" << client_number << " S" << server_number << ")" << endl;
				cout << "Client: sent handshake 3 (C" << client_number << " S" << server_number << ")" << endl;

				switch (handshake.direction)
				{
				case GET: // Client is receiving host, server will send client seq
					if (!ReceiveFile(sock, handshake.filename, hostname, client_number))
						printError("An error occurred while receiving the file.");
					break;
				case PUT: // Client is sending host, server expects seq
					if (!SendFile(sock, handshake.filename, hostname, server_number))
						printError("An error occurred while sending the file.");
					break;
				case LIST:
					strcat(path, "list.txt");
					if (!ReceiveFile(sock, path, hostname, handshake.client_number))
						printError("An error occurred while receiving the file.");
					SetFileAttributes(handshake.filename, FILE_ATTRIBUTE_HIDDEN);
					printlist();
					break;
				default:
					break;
				}
			}
		}

		if (RECORD) fout << "Closing client socket." << endl;
		cout << "Closing client socket." << endl;

		closesocket(sock);
		cout << endl << "Enter server name : "; cin >> server;		// prompt user for server name
	}

}



unsigned long UdpClient::ResolveName(char name[])
{
	struct hostent *host; // Structure containing host information
	if ((host = gethostbyname(name)) == NULL)
		printError("gethostbyname() failed");
	return *((unsigned long *)host->h_addr_list[0]); // return the binary, network byte ordered address
}

UdpClient::UdpClient(char * fn) // constructor
{
	/* For timeout timer */
	timeouts.tv_sec = STIMER;
	timeouts.tv_usec = UTIMER;

	/* Set the window size and sequence upperbound */
	sequence_ubound = 2 * WINDOW_SIZE - 1;

	/* Open the log file */
	fout.open(fn);
}

UdpClient::~UdpClient() // destructor
{
	/* Close the log file */
	fout.close();

	/* Uninstall winsock.dll */
	WSACleanup();
}

int main(int argc, char *argv[])
{
	UdpClient * client = new UdpClient();
	client->run();
	return 0;
}
