/*data structure and memory manipulation headers*/
#include <stdio.h>				//standard input/output (fprintf and whatnot)
#include <stdlib.h>				//good old standard library
#include <unistd.h>				//POSIX API (close, open, etc)
#include <stdint.h>				//Portable type definitions (uint32_t, etc)
#include <string.h>				//memsets and general string manipulation shenanigans
#include <sys/types.h>		//timers, mutexes and other useful stuff
#include <fcntl.h>				//file descriptor manipulation (sockopts, etc)

/*network headers*/
#include <netdb.h>				//addrinfos and other networking automagic
#include <sys/socket.h>		//SOCKETS WE LOVE SOCKETS WHO DOESN'T LOVE SUM SOCKETS
#include <arpa/inet.h>		//inet ntoas, atons and others

/*multi-threading headers*/
#include <pthread.h>			//Threads and stuff

/*Initializes a TCP socket for a given peer's IP in port 51511, establishes the
  TCP connection to the peer, and returns the socket's file descriptor ID.
  Returns -1 if it's not able to setup the connection.*/
int init_peer_socket (char *ip);

/*Initializes a TCP socket that binds to the local address, and returns its
  file descriptor id. This socket will be used to accept incoming connections
  from other peers. Returns -1 if it fails.*/
int init_incoming_socket ();

/*Processes a PeerList message received on the given socket, checking if there
  are any peers in it to which we are not currently connected, and connecting
  to any potential new peers.*/
void process_peerlist (int peersock, FILE *logfile);

/*Processes an ArchiveResponse received on the given socket. First, we parse and
  store the content of the received archive appropriately. Then, we check if the
	new archive is larger than the one currently active. If so, we validate this
	new archive. If the validity check is successful, we replace the current
	archive by the new one, and dump the old archive*/
void process_archive (int peersock, FILE *logfile);

/*Publishes a newly created archive by iterating over the peerlist and sending
  the currently active archive to each peer. This function looks weird, because
	all the data it accesses is contained in both of our global data structures,
	the peerlist structure and the active archive structure.*/
void publish_archive();

/*Implements the work done by threads launched for each peer, that periodically
  send PeerRequest messages ("0x1") to the connected peer. It takes the socket
  associated to the peer as input, and simply loops forever, sending out request
  messages in a given interval (5 seconds)
  As a bonus, since the specification did not mention when we should send
  ArchiveRequests, we'll send them periodically as well, on a longer interval
 (every 60 seconds)*/
void *peer_requester_thread (void *sock);

/*Implements the work done by threads launched for each peer that receive and
  process data sent by the connected peer. It takes the socket associated to the
  peer as input, and recv()s on the socket, waiting for messages to arrive, and
  processes each type of message accordingly.
	The socket is configured with a timeout. If a recv() operation times out, we
	assume the connection was interrupted, and close the socket, disconnect from
	the peer and remove them from the list of connected peers.*/
void *peer_receiver_thread (void *sock);

/*This function implements all the work that must be done by the thread that
  treats incoming peer connections. It initializes a passive socket, binds to
  it, then listens and waits for incoming connections, accept()ing them and
  launching threads to exchange data with each peer.
  The thread that runs this function will be called once at the beginning of
  program execution, and run indefinitely.*/
void *incoming_peers_thread ();
