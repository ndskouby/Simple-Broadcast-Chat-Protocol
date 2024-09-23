#include <iostream>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <cstdint>

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
    struct SBCP_Attribute* attrs;
};

using namespace std;

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
                        
                    }
                }

            }
        }
    }
}