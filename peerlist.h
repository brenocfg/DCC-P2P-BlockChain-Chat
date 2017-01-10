#include <stdio.h>	//for printing debug info
#include <stdlib.h>	//mallocs, frees and whatnot
#include <stdint.h>	//portable size types (uint8_t, uint32_t, etc)

/*struct that represents a node in a list of connected peers, we store IPs as
 4 byte unsigned integers for faster comparison. This is safe because all IPs
 are guaranteed to be IPv4. We also store the socket associated with that peer,
 so we can broadcast messages by iterating across the list*/
struct node {
	uint32_t ip;
  uint32_t sock;
	struct node *next;
};

/*struct that represents an entire list of peers, with pointers to first and
  last nodes, size (in number of nodes) and string representation, for faster
  message building*/
struct peer_list {
	struct node *head, *last;
	uint32_t size;
	uint8_t *str;
};

/*Recomputes the list's string representation, to update connected peers after
  the removal or addition of a peer*/
void list_to_str(struct peer_list *list);

/*Adds a given IP to the list of connected peers, and updates the list's size
  and string representation accordingly*/
void add_peer(struct peer_list *list, uint32_t ip, uint32_t sock);

/*Removes a given IP from the list of connected peers, and updates the list's
  size and string representation accordingly*/
void remove_peer(struct peer_list *list, uint32_t ip);

/*returns 1 if the given ip is currently in the list of connected peers, 0
  otherwise, obviously used to check whether we are already connected to an ip*/
int is_connected(struct peer_list *list, uint32_t ip);

/*prints a list of connected peers. Only for debugging purposes*/
void print_list(struct peer_list *list);

/*Initializes a peer list structure. Initially the list has size 0, and the
  last and head nodes are the same (no data). Its string representation is
  also a NULL pointer*/
struct peer_list *init_list();
