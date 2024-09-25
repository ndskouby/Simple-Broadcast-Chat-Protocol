#include <iostream>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <cstdint>
#include <vector>
#include <unordered_map>

#define BUFFER_LEN 1024

using namespace std;

struct SBCP_Header {
    uint16_t vrsn : 9;   // 9 bits for version
    uint16_t type : 7;   // 7 bits for type
    uint16_t length;     // 2 bytes for length (16 bits)
};

struct SBCP_Attribute {
    uint16_t type;     // 2 bytes for type
    uint16_t length;   // 2 bytes for length (total size including header)
    uint8_t* payload;  // dynamically allocated payload

    // constructor to allocate the payload dynamically based on length
    SBCP_Attribute(uint16_t t, uint16_t len) : type(t), length(len) {
        payload = new uint8_t[len - 4];  // allocate payload (subtract 4 bytes for type and length)
    }

    ~SBCP_Attribute() {
        delete[] payload;
    }
};

struct SBCP_Message {
    struct SBCP_Header     header;
    vector<SBCP_Attribute> attrs;
};

/*
    Sending SBCP-formatted Messages to the server
*/ 
void send_sbc_message(int CSocket, SBCP_Message &message) 
{
    //serializing header
    uint16_t header[2];
    header[0] = (message.header.vrsn << 7) | message.header.type;
    header[1] = htons(message.header.length);

    //writing the header
    SocketWriter(CSocket, (char *)header, sizeof(header));

    //serializing and writing attributes
    for (const SBCP_Attribute &attr : message.attrs) 
    {
        uint16_t attr_header[2] = {htons(attr.type), htons(attr.length)};
        SocketWriter(CSocket, (char *)attr_header, sizeof(attr_header));
        SocketWriter(CSocket, (char *)attr.payload, attr.length - 4);
    }
}

/*
    Receiving SBCP-formatted Messages from the server
*/
SBCP_Message receive_sbc_message(int CSocket) 
{
    SBCP_Message message;

    //reading SBCP header
    uint16_t header[2];
    SocketReader(CSocket, (char *)header, sizeof(header));
    message.header.vrsn = header[0] >> 7;
    message.header.type = header[0] & 0x7F;
    message.header.length = ntohs(header[1]);

    int remaining_bytes = message.header.length - sizeof(SBCP_Header);

    //reading attributes
    while (remaining_bytes > 0) 
    {
        uint16_t attr_header[2];
        SocketReader(CSocket, (char *)attr_header, sizeof(attr_header));
        uint16_t attr_type = ntohs(attr_header[0]);
        uint16_t attr_length = ntohs(attr_header[1]);

        SBCP_Attribute attr(attr_type, attr_length);
        SocketReader(CSocket, (char *)attr.payload, attr_length - 4);

        message.attrs.push_back(attr);
        remaining_bytes = remaining_bytes - attr_length;
    }

    return message;
}

/*
    Reading Message from Client Socket
*/ 
int SocketReader(int CSocket, char *Message, int nB) //nB is number Bytes
{
    int uB = nB; //Bytes not yet sent (unsent)
    while (uB > 0) 
    {
        int rB = read(CSocket, Message, uB);
        if (rB <= 0) 
        {
            if (errno == EINTR) //in case of error code in Bytes
            {
                continue;  //retry 
            } 
            else 
            {
                return -1;  //error exit
            }
        }
        Message = Message + rB;
        uB = uB - rB;
    }
    return nB;
}

/*
    Writing Message to Client Socket to Server
*/ 
int SocketWriter(int CSocket, char *Message, int nB) //nB is number Bytes
{
    int uB = nB; //Bytes unsent
    while (uB > 0) //Bytes sent
    {
        int sB = write(CSocket, Message, uB);
        if (sB <= 0) 
        {
            if (errno == EINTR) //in case of error code in Bytes
            {
                sB = 0;  //reset Byte count 
            } 
            else 
            {
                return -1;  //error exit
            }
        }
        Message = Message + sB;
        uB = uB - sB;
    }
    return nB;
}

bool validate_join_msg(SBCP_Message msg) {
    if (msg.header.type == 2) {
        if (msg.attrs.size() > 0) {
            if (msg.attrs[0].type == 2) {
                return true;
            }
        }
    }
    return false;
}

bool validate_send_msg(SBCP_Message msg) {
    if (msg.header.type == 4) {
        if (msg.attrs.size() > 0) {
            if (msg.attrs[0].type == 4) {
                return true;
            }
        }
    }
    return false;
}

bool check_usernames(unordered_map<int, string> usernames, string username) {
    for (const auto& user : usernames){
        if (username == user.second){
            return false;
        }
    }
    return true;
}

void forward_msg_to_clients(SBCP_Message client_msg, int fd, unordered_map<int,string> usernames) {
    //preparing SBCP message to send to the server
    SBCP_Message fwd_msg;
    fwd_msg.header.vrsn = 3;  //sample version
    fwd_msg.header.type = 3;  //sample message type
    fwd_msg.header.length = sizeof(SBCP_Header);

    string text = string((char*)client_msg.attrs[0].payload);

    //creating attribute for forwarding message
    SBCP_Attribute attr(4, text.length() + 4); //type 4 for message
    memcpy(attr.payload, text.c_str(), text.length());
    fwd_msg.attrs.push_back(attr);

    fwd_msg.header.length = fwd_msg.header.length + attr.length;

    for (const auto& user : usernames) {
        if (user.first != fd) {
            //send SBCP message
            send_sbc_message(fd, fwd_msg);
        }
    }
}

int main(int argc, char* argv[]) {
    int port;
	int max_clients;
	if (argc == 1) {
		port = 8080;
		max_clients = 10;
	} else if (argc == 2) {
		port = atoi(argv[1]);
		max_clients = 10;
	} else if (argc == 3) {
		port = atoi(argv[1]);
		max_clients = atoi(argv[2]);
	} else {
		cerr << "too many arguments" << endl;
		return 1;
	}

	cout << "Server starting on port: " << port << endl;

    // create server address
	sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	// allegedly this should be set to zero
	memset(&(server_addr.sin_zero), '\0', 8);

    // create server socket with domain and socket type (internet address and TCP)
	int server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket == -1) {
		cerr << "socket creation error: " << errno << " - " << strerror(errno) << endl;
		return 1;
	}

    // bind an address to the server socket
	if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
		cerr << "socket bind error: " << errno << " - " << strerror(errno) << endl;
		close(server_socket);
		return 1;
	}

    // listen for socket connections (limited by max_clients)
	if (listen(server_socket, max_clients) == -1) {
		cerr << "socket listen error: " << errno << " - " << strerror(errno) << endl;
		close(server_socket);
		return 1;
	}

    fd_set master_set, worker_set;
    int max_fd;

    FD_ZERO(&master_set);               // initialize fd set
    FD_SET(server_socket, &master_set); // Add server socket to fd set
    max_fd = server_socket;             // track highest fd

    int ready_fds, client_socket;
    unordered_map<int, string> usernames;

    while (true) {
        worker_set = master_set;
        ready_fds = select(max_fd+1, &worker_set, nullptr, nullptr, nullptr);

        for (int fd = 0; fd <= max_fd; fd++) {
            if (FD_ISSET(fd, &worker_set)) {
                if (fd == server_socket) {
                    client_socket = accept(server_socket, nullptr, nullptr);
                    if (client_socket == -1) {
                        cerr << "socket accept error: " << errno << " - " << strerror(errno) << endl;
                        continue;
                    } else {
                        if (usernames.size() < max_clients){
                            char buffer[BUFFER_LEN];
                            if (recv(client_socket, buffer, sizeof(buffer), MSG_PEEK) == 0) {
                                cerr << "unceremonious exit" << endl;
                                close(client_socket);
                                continue;
                            }
                            SBCP_Message join_msg = receive_sbc_message(client_socket);
                            if (validate_join_msg(join_msg)){
                                string username = string((char*)join_msg.attrs[0].payload);
                                if (check_usernames(usernames, username)){
                                    usernames[client_socket] = username;
                                    FD_SET(client_socket, &master_set);
                                    max_fd = max(max_fd, client_socket);

                                    // TODO: send client_ack with username_list
                                } else {
                                    // TODO: send client_nak with reason
                                    close(client_socket);
                                }
                            } else {
                                // TODO: send client_nak with reason
                                close(client_socket);
                            }
                        } else {
                            // TODO: send client_nak with reason
                            close(client_socket);
                        }
                    }
                } else {
                    char buffer[BUFFER_LEN];
                    if (recv(fd, buffer, sizeof(buffer), MSG_PEEK) == 0) {
                        cerr << "unceremonious exit" << endl;
                        close(fd);
                        usernames.erase(fd);
                        // TODO: update other clients of disconnect
                        continue;
                    }
                    SBCP_Message client_msg = receive_sbc_message(fd);
                    if (validate_send_msg(client_msg)) {
                        forward_msg_to_clients(client_msg, fd, usernames);
                    } else {
                        // TODO: send message error
                    }
                }
            }
        }
    }
}