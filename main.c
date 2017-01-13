#include "main.h"
#include "peerlist.h"
#include "archive.h"

/*port is always 51511*/
#define TCP_PORT "51511"

/*enum for message types, to make message treatment code clearer*/
enum {
	MSG_PEERREQ = 1,
	MSG_PEERLIST,
	MSG_ARCHREQ,
	MSG_ARCHRESP
};

/*The list of connected peers. This must be global to be shared amongst all
  threads (we could pass it around as a parameter, but that is too much of a
  hassle so we simplify by doing this)
  It should always be thread-safe to access it because main() initializes it
  before launching any threads, and thread access to it is controlled through
  its mutex variable*/
struct peer_list *peerlist;
pthread_mutex_t peerlist_mutex;

/*The currently active archive, which we will broadcast to any peers that send
  us ArchiveRequest messages. Must be global for the same reasons as the peer
	list. This will be initialized by the main thread as soon as execution begins,
	and we make sure it contains a proper archive before broadcasting it.
	For syncronizing archives, we use a rwlock instead of a mutex, because only
	1 thread will ever write changes to it (to add messages), while other threads
	will only either replace the current active archive (which counts as writing,
  but will hardly happen often), or read values like its size/print it.*/
struct archive *active_arch;
pthread_rwlock_t archive_lock;

/*local device's public IP address, to avoid self-connection attempts*/
uint32_t myaddr;

/*Initializes a TCP socket for a given peer's IP in port 51511, establishes the
  TCP connection to the peer, and returns the socket's file descriptor ID.
  Returns -1 if it's not able to setup the connection.
  We use select() and some non-blocking magic to force a half-second timeout on
  connections, to avoid threads being blocked for long periods of time when
	attempting to connect to unresponsive peers*/
int init_peer_socket (char *ip) {
	struct addrinfo hints, *peerinfo, *aux;
	int addrinfo_rv, sock = -1;

	/*initialize hints struct*/
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	/*get list of addresses for given peer*/
	if ((addrinfo_rv = getaddrinfo(ip, TCP_PORT, &hints, &peerinfo)) != 0) {
		fprintf(stderr, "Error when retrieving peer address information!\n");
		fprintf(stderr, "Addrinfo status: %s\n", gai_strerror(addrinfo_rv));
		return -1;
	}

	/*loop through addresses, until we find a valid one*/
	for (aux = peerinfo; aux != NULL; aux = aux->ai_next) {
		if ((sock = socket(aux->ai_family, aux->ai_socktype, aux->ai_protocol))
			== -1) {
			continue;
		}

		/*set socket to non-blocking, then initiate connection attempt*/
		fcntl(sock, F_SETFL, O_NONBLOCK);
		connect(sock, aux->ai_addr, aux->ai_addrlen);

		/*create a select set with our socket in it, and configure 500ms timeout*/
		fd_set fdset;
		struct timeval timeout;
		FD_ZERO(&fdset);
		FD_SET(sock, &fdset);
		timeout.tv_sec = 0;
		timeout.tv_usec = 500000;

		/*now let's poll our socket with select, on a 500ms timeout*/
		if (select(sock+1, NULL, &fdset, NULL, &timeout) == 1) {
			int err;
			socklen_t len = sizeof(err);

			/*get socket state*/
			getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);

			/*succeeded! set sock to blocking again, and break the loop to return*/
			if (err == 0) {
				int flags = fcntl(sock, F_GETFL);
				flags &= ~O_NONBLOCK;
				fcntl(sock, F_SETFL, flags);
				break;
			}

			/*no connection after 500ms, time out!*/
			close(sock);
		}

		/*select failed, not sure why this happens when it does...*/
		else {
			close(sock);
		}
	}

	freeaddrinfo(peerinfo);

	/*check if we managed to connect to any address*/
	if (aux == NULL) {
		return -1;
	}

	return sock;
}

/*Initializes a TCP socket that binds to the local address, and returns its
  file descriptor id. This socket will be used to accept incoming connections
  from other peers. Returns -1 if it fails.*/
int init_incoming_socket () {
	int addrinfo_rv, sock = -1;
	int re = 1;
	struct addrinfo hints, *myinfo, *aux;

	/*initialize hints structure*/
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	/*get list of available interfaces*/
	if ((addrinfo_rv = (getaddrinfo(NULL, TCP_PORT, &hints, &myinfo)) != 0)) {
		fprintf(stderr, "Error when retrieving local address list!\n");
		fprintf(stderr, "Addrinfo status: %s\n", gai_strerror(addrinfo_rv));
		return -1;
	}

	/*loop through addresses until we find a bindable one*/
	for (aux = myinfo; aux != NULL; aux = aux->ai_next) {
		if ((sock = socket(aux->ai_family, aux->ai_socktype, aux->ai_protocol))
			== -1) {
			fprintf(stderr, "Error when creating socket for address!\n");
			continue;
		}

		if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &re, sizeof(re)) == -1) {
			fprintf(stderr, "Error, could not set socket as reusable.\n");
			return -1;
		}

		if (bind(sock, aux->ai_addr, aux->ai_addrlen) == -1) {
			close(sock);
			fprintf(stderr, "Error, could not bind socket to address.\n");
			continue;
		}

		break;
	}

	freeaddrinfo(myinfo);

	/*check if we managed to bind to any address*/
	if (aux == NULL) {
		fprintf(stderr, "Could not find a valid address to accept peers!\n");
		return -1;
	}

	return sock;
}

/*Processes a PeerList message received on the given socket, checking if there
  are any peers in it to which we are not currently connected, and connecting
  to any potential new peers.*/
void process_peerlist (int peersock, FILE *logfile) {
	uint32_t size;
	uint8_t buf[4];

	fprintf(logfile, "\n----------Processing peer list!----------\n");

	/*parse size bytes to compute the number of IPs in the list*/
	recv(peersock, buf, 4, MSG_WAITALL);
	size = ((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);
	fprintf(logfile, "%u clients:\n", size);

	/*iterate through addresses, checking if we're connected to them*/
	uint32_t i;
	for (i = 0; i < size; i++) {
		uint32_t uip = 0;
		recv(peersock, buf, 4, MSG_WAITALL);
		uip = ((buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0]);
		fprintf(logfile, "%d.%d.%d.%d\n", buf[0], buf[1], buf[2], buf[3]);

		/*don't try to connect to ourselves :)*/
		if (uip == myaddr) {
			continue;
		}

		/*make sure we're the only ones accessing the list to avoid doubles*/
		pthread_mutex_lock(&peerlist_mutex);

		/*if peer is not connected, get their IP and create socket*/
		if (!is_connected(peerlist, uip)) {
			char ip[17];
			snprintf(ip, 17, "%d.%d.%d.%d", buf[0], buf[1], buf[2], buf[3]);
			fprintf(stdout, "Attempting to connect to new peer %s... \n", ip);
			int newpeersock = init_peer_socket(ip);

			/*couldn't connect after 500ms, move on*/
			if (newpeersock == -1) {
				fprintf(stderr, "Failed to connect to peer %s!\n", ip);
				pthread_mutex_unlock(&peerlist_mutex);
				continue;
			}

			/*if connection was successful, launch threads to deal with peer*/
			pthread_t peerReq, peerRecv;
			pthread_create(&peerReq, NULL, peer_requester_thread, &newpeersock);
			pthread_create(&peerRecv, NULL, peer_receiver_thread, &newpeersock);
		}

		pthread_mutex_unlock(&peerlist_mutex);
	}
	fprintf(logfile, "----------Done processing peerlist!----------\n\n");
}

/*Processes an ArchiveResponse received on the given socket. First, we parse and
  store the content of the received archive appropriately. Then, we check if the
	new archive is larger than the one currently active. If so, we validate this
	new archive. If the validity check is successful, we replace the current
	archive by the new one, and dump the old archive*/
void process_archive (int peersock, FILE *logfile) {
	fprintf(logfile, "\n----------Processing ArchiveResponse!---------\n");

	/*get number of chats in archive*/
	uint8_t buf[4]; uint32_t usize = 0;
	recv(peersock, buf, 4, MSG_WAITALL);
	usize = ((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);

	fprintf(logfile, "Number of chats: %u\n", usize);

	/*allocate an archive struct to store the received archive*/
	struct archive *new_archive = init_archive();
	uint8_t *ptr, *aux;

	/*initialize archive size, and allocate memory for its content.
	At first, we need to allocate memory for the biggest possible archive*/
	new_archive->size = usize;
	ptr = (uint8_t *) malloc(5 + (usize * 289));
	aux = ptr;

	/*compute archive message type and size into new string*/
	*aux++ = 4;
	memcpy(aux, buf, 4);
	aux += 4;

	/*and initialize a counter for total message length (in bytes)*/
	uint32_t len = 5;

	/*now iterate over every message in the archive*/
	unsigned int i;
	uint8_t codes[32], msg[256], msglen;
	for (i = 0; i < usize; i++) {
		/*read message from socket*/
		memset(msg, 0, 256);
		recv(peersock, &msglen, 1, MSG_WAITALL);
		recv(peersock, msg, msglen, MSG_WAITALL);
		recv(peersock, codes, 32, MSG_WAITALL);

		/*store it into our string*/
		memcpy(aux, &msglen, 1);
		aux++;
		memcpy(aux, msg, msglen);
		aux += msglen;
		memcpy(aux, codes, 32);
		aux += 32;

		/*update total length (33 = 32 bytes of md5+code and 1 byte for msg size)*/
		len += (msglen+33);
	}

	/*now realloc the final string with only the amount of memory necessary*/
	new_archive->str = realloc(ptr, len);
	new_archive->len = len;

	fprintf(logfile, "Content of archive received:\n");
	print_archive(new_archive, logfile);

	/*if the new archive is valid and larger than the active, substitute it
	  (short circuiting saves some time here if new archive is already smaller)*/
	pthread_rwlock_rdlock(&archive_lock);
	if (new_archive->size > active_arch->size && is_valid(new_archive)) {
		pthread_rwlock_unlock(&archive_lock);
		pthread_rwlock_wrlock(&archive_lock);
		free(active_arch->str);
		free(active_arch);
		active_arch = new_archive;
		fprintf(stdout, "---------- Active archive replaced! ----------\n");
	}

	/*otherwise, the active stays, so dump the new one*/
	else {
		free(new_archive->str);
		free(new_archive);
	}
	pthread_rwlock_unlock(&archive_lock);
	fprintf(logfile, "----------Done processing ArchiveResponse!----------\n\n");
}

/*Publishes a newly created archive by iterating over the peerlist and sending
  the currently active archive to each peer. This function looks weird, because
	all the data it accesses is contained in both of our global data structures,
	the peerlist structure and the active archive structure.*/
void publish_archive() {
	struct node *aux;

	fprintf(stdout, "\n----------Publishing new archive!----------\n");

	aux = peerlist->head->next;

	/*iterate over peer list, and send archive to each peer*/
	while (aux != NULL) {
		fprintf(stdout, "Sending to peer at sock %u\n", aux->sock);
		send(aux->sock, active_arch->str, active_arch->len, 0);
		aux = aux->next;
	}

	fprintf(stdout, "----------Done publishing!---------\n\n");
}

/*Implements the work done by threads launched for each peer, that periodically
  send PeerRequest messages ("0x1") to the connected peer. It takes the socket
  associated to the peer as input, and simply loops forever, sending out request
  messages in a given interval (5 seconds)
  As a bonus, since the specification did not mention when we should send
  ArchiveRequests, we'll send them periodically as well, on a longer interval
 (every 60 seconds)*/
void *peer_requester_thread (void *sock) {
	int peersock = *((int*) sock);
	uint8_t msg[2];

	/*open logfile for thread's socket*/
	char filename[7];
	memset(filename, 0, 7);
	snprintf(filename, 7, "%d.log", peersock);
	FILE *logfile = fopen(filename, "a");

	/*we have two msg bytes, one for peer requests, the other for archive*/
	msg[0] = MSG_PEERREQ;
	msg[1] = MSG_ARCHREQ;

	/*send PeerRequests every 5 seconds, exit if broken pipe*/
	int count = 0;
	while (1) {
		if (send(peersock, msg, 1, 0) == -1) {
			fprintf(logfile,"Error sending peer request, broken pipe?\n");
			fprintf(logfile,"Terminating requester thread.\n");
			pthread_exit(NULL);
		}
		count++;

		/*send ArchiveRequests every 60 seconds (5*12 = 60)*/
		if (count == 12) {
			if (send(peersock, msg+1, 1, 0) == -1) {
				fprintf(logfile,"Error sending archive request, broken pipe?\n");
				fprintf(logfile, "Terminating requester thread.\n");
				pthread_exit(NULL);
			}
			count = 0;
		}
		sleep(5);
	}
}

/*Implements the work done by threads launched for each peer that receive and
  process data sent by the connected peer. It takes the socket associated to the
  peer as input, and recv()s on the socket, waiting for messages to arrive, and
  processes each type of message accordingly.
	The socket is configured with a timeout. If a recv() operation times out, we
	assume the connection was interrupted, and close the socket, disconnect from
	the peer and remove them from the list of connected peers.*/
void *peer_receiver_thread (void *sock) {
	int peersock = *((int*) sock);

	/*open logfile for thread's socket*/
	char filename[7];
	memset(filename, 0, 7);
	snprintf(filename, 7, "%d.log", peersock);
	FILE *logfile = fopen(filename, "a");

	/*get peer name+ip information*/
	struct sockaddr_storage peeraddr;
	socklen_t peersize = sizeof(peeraddr);
	getpeername(peersock, (struct sockaddr*)&peeraddr, &peersize);
	struct sockaddr_in *peeraddr_in = (struct sockaddr_in*) &peeraddr;
	uint32_t upeerip = peeraddr_in->sin_addr.s_addr;
	char *cpeerip = inet_ntoa(peeraddr_in->sin_addr);

	/*add peer to list of connected peers*/
	pthread_mutex_lock(&peerlist_mutex);
	add_peer(peerlist, upeerip, peersock);
	fprintf(stdout, "Successfully connected to peer %s\n", cpeerip);
	pthread_mutex_unlock(&peerlist_mutex);

	/*set socket to timeout on receive operations after 60 seconds*/
	struct timeval tout;
	tout.tv_sec = 60;
	tout.tv_usec = 0;
	setsockopt(peersock,SOL_SOCKET,SO_RCVTIMEO,(char*)&tout,sizeof(tout));

	/*loop waiting for messages*/
	while (1) {
		/*get first byte to determine message type*/
		uint8_t type;
		if(recv(peersock, &type, 1, MSG_WAITALL) <= 0) {
			/*connection was closed or socket timed out*/
			fprintf(stderr, "Timed out when waiting for peer %s.\n", cpeerip);
			fprintf(stderr, "Peer likely disconnected. Closing connection...\n");
			close(peersock);
			pthread_mutex_lock(&peerlist_mutex);
			remove_peer(peerlist, upeerip);
			pthread_mutex_unlock(&peerlist_mutex);
			pthread_exit(NULL);
		}

		/*process each message type accordingly*/
		switch(type) {
			case MSG_PEERREQ: {
				fprintf(logfile, "Received PeerRequest, sending list!\n");
				send(peersock, peerlist->str, (5+(4*peerlist->size)), 0);
				break;
			}

			case MSG_PEERLIST: {
				process_peerlist(peersock, logfile);
				break;
			}

			case MSG_ARCHREQ: {
				fprintf(logfile, "Received ArchiveRequest!\n");
				if (!active_arch->size) {
					fprintf(logfile, "Current archive is empty, ignoring request!\n");
					break;
				}
				fprintf(logfile, "Sending archive!\n");
				send(peersock, active_arch->str, active_arch->len, 0);
				break;
			}

			case MSG_ARCHRESP: {
				process_archive(peersock, logfile);
				break;
			}

			default: {
				fprintf(logfile, "Unknown msg type, ignoring... (byte = %d)\n", type);
				break;
			}
		}
	}
}

/*This function implements all the work that must be done by the thread that
  treats incoming peer connections. It initializes a passive socket, binds to
  it, then listens and waits for incoming connections, accept()ing them and
  launching threads to exchange data with each peer.
  The thread that runs this function will be called once at the beginning of
  program execution, and run indefinitely.*/
void *incoming_peers_thread () {
	int mysock, peersock;
	struct sockaddr_storage peeraddr;
	socklen_t peersize;

	/*initialize the listen socket*/
	mysock = init_incoming_socket();

	/*attempt to listen on the created socket*/
	if (listen(mysock, 10) == -1) {
		fprintf(stderr, "Failed to listen on incoming peer socket!\n");
		pthread_exit(NULL);
	}

	fprintf(stdout, "[Incoming peers thread is awaiting connections]\n");

	/*while (hopefully) forever, accept incoming connections from peers*/
	char pigs_can_fly = 0;
	while (!pigs_can_fly) {
		peersize = sizeof(peeraddr);
		if ((peersock = accept(mysock, (struct sockaddr*) &peeraddr, &peersize))
			== -1) {
			fprintf(stderr, "Error, could not accept connection from peer!\n");
			continue;
		}

		/*launch request and receiver threads for incoming peer*/
		fprintf(stdout, "Accepted incoming peer connection!\n");
		pthread_t peerReq, peerRecv;
		pthread_create(&peerReq, NULL, peer_requester_thread, &peersock);
		pthread_create(&peerRecv, NULL, peer_receiver_thread, &peersock);
	}

	pthread_exit(NULL);
}

/*Beginning of program execution*/
int main(int argc, char *argv[]) {
	/*insufficient arguments, we need an initial peer to connect to and the
	 public IP address for the local device*/
	if (argc != 3) {
		fprintf(stderr, "Usage: ./blockchain <ip/hostname> <public IP>\n");
		return 0;
	}

	/*get int representation for public IP and store it, to avoid self-connect*/
	struct in_addr testing;
	inet_aton(argv[2], &testing);
	myaddr = testing.s_addr;

	/*initialize our peer list structure and its mutex variable*/
	peerlist = init_list();
	pthread_mutex_init(&peerlist_mutex, NULL);

	/*and the active archive, which is initially empty*/
	active_arch = init_archive();
	pthread_rwlock_init(&archive_lock, NULL);

	/*first thing we do is start a thread to accept incoming connections*/
	pthread_t incoming_thread;
	pthread_create(&incoming_thread, NULL, incoming_peers_thread, NULL);

	/*now init a socket for the first peer and launch threads to talk to them*/
	int sock = init_peer_socket(argv[1]);
	if (sock == -1) {
		fprintf(stderr, "Failed to connect to initial peer!\n");
	}

	else {
		pthread_t reqthread, recvthread;
		pthread_create(&reqthread, NULL, peer_requester_thread, &sock);
		pthread_create(&recvthread, NULL, peer_receiver_thread, &sock);
	}

	/*prompt the user for messages to add to archive*/
	while(1) {
		uint8_t msg[256];

		memset(msg, 0, 256);
		fprintf(stdout, "Input a chat message to send (255 chars max):\n");
		fgets((char*)msg, 256, stdin);

		/*we'll write to the archive, so writelock it*/
		pthread_rwlock_wrlock(&archive_lock);

		if (strcmp((char*) msg, "exit\n") == 0) {
			exit(0);
		}

		/*couldn't add message, probably illegal message content*/
		if (!add_message(active_arch, msg)) {
			fprintf(stderr, "Invalid message! Try again :)\n");
			pthread_rwlock_unlock(&archive_lock);
			continue;
		}

		/*added message to archive, print new archive, publish and unlock it*/
		fprintf(stdout, "Message successfully added to archive!\n");
		fprintf(stdout, "New active archive:\n");
		print_archive(active_arch, stdout);

		publish_archive();
		pthread_rwlock_unlock(&archive_lock);
	}
}
