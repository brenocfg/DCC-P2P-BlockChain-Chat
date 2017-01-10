#include "peerlist.h"

/*This file implements a list data structure, and its associated functions.
  The specific list implementation contained here is meant to store the list of
  connected peers, supporting addition/removal of IP addresses to the list, as
  well as keeping a pre-computed string representation of it, so that building
	network packets containing the list is reasonably fast.

  NOTE: A hash table or even an ordered set (using a tree) would most definitely
  have been much better options for storing the list of peers, but their imple-
  mentations would have been more time consuming, and given that the size of the
  list is unlikely to grow significantly, not worth the commitment*/

/*Recomputes the list's string representation, to update connected peers after
  the removal or addition of a peer*/
void list_to_str(struct peer_list *list) {
	uint8_t *buf;
	uint32_t size;

	/*get list size*/
	size = list->size;

	/*free old string and allocate memory for new one*/
	free(list->str);
	buf = (uint8_t *) malloc((5 + (size*4)) * sizeof(uint8_t));

	/*first byte is message type (2), other four bytes are the number of peers*/
	buf[0] = 2;
	buf[1] = (size >> 24) & 0xFF;
	buf[2] = (size >> 16) & 0xFF;
	buf[3] = (size >> 8) & 0xFF;
	buf[4] = size & 0xFF;

	/*now for each node in the list, convert the IP from integer to byte array,
	 in network byte order*/
	struct node *aux = list->head->next;
	uint32_t i;
	for (i = 5; i < (5 + (size*4)); i+=4) {
		buf[i+3] = (aux->ip >> 24) & 0xFF;
		buf[i+2] = (aux->ip >> 16) & 0xFF;
		buf[i+1] = (aux->ip >> 8) & 0xFF;
		buf[i] = aux->ip & 0xFF;

		aux = aux->next;
	}

	list->str = buf;
}

/*Adds a given IP to the list of connected peers, and updates the list's size
  and string representation accordingly*/
void add_peer(struct peer_list *list, uint32_t ip, uint32_t sock) {
	struct node *aux;

	aux = list->last;

	aux->next = (struct node*) malloc(sizeof(struct node));
	aux->next->ip = ip;
	aux->next->sock = sock;
	aux->next->next = NULL;
	list->last = aux->next;

	list->size += 1;

	list_to_str(list);
}

/*Removes a given IP from the list of connected peers, and updates the list's
  size and string representation accordingly*/
void remove_peer(struct peer_list *list, uint32_t ip) {
	struct node *prev;

	prev = list->head;

	/*try to find IP in list, if we find it break with prev = previous node*/
	while (prev != list->last) {
		if (prev->next->ip == ip) {
			break;
		}
		prev = prev->next;
	}

	/*IP is not in the list, return!*/
	if (prev->next == NULL) {
		return;
	}

	/*get pointer to the node to be removed, and update prev's pointer*/
	struct node *to_remove = prev->next;

	prev->next = to_remove->next;

	/*if we're removing the last node, update pointer*/
	if (to_remove == list->last) {
		list->last = prev;
	}

	/*free memory and update list size*/
	free(to_remove);
	list->size -= 1;

	/*update list string representation*/
	list_to_str(list);
}

/*returns 1 if the given ip is currently in the list of connected peers, 0
  otherwise, obviously used to check whether we are already connected to an ip*/
int is_connected(struct peer_list *list, uint32_t ip) {
	struct node *aux;

	aux = list->head;

	/*look for IP in the list, return 1 if we find it*/
	while (aux != NULL) {
		if (aux->ip == ip) {
			return 1;
		}
		aux = aux->next;
	}

	/*if we got here, it's not in the list*/
	return 0;
}

/*prints a list of connected peers. Only for debugging purposes*/
void print_list(struct peer_list *list) {
	struct node *aux;

	fprintf(stderr, "Peer list [size %u]:\n", list->size);

	/*empty list, return*/
	if (list->head == list->last) {
		return;
	}

	/*head is left empty intentionally, we begin printing from node 2*/
	aux = list->head->next;

	/*print all nodes, then the last*/
	while (aux != list->last) {
		/*since this is for debugging only, don't bother converting to string*/
		fprintf(stderr, "%u[%u] -> ", aux->ip, aux->sock);
		aux = aux->next;
	}
	fprintf(stderr, "%u[%u]\n", aux->ip, aux->sock);
}

/*Initializes a peer list structure. Initially the list has size 0, and the
  last and head nodes are the same (no data). Its string representation is
  also a NULL pointer*/
struct peer_list *init_list() {
	struct peer_list *newlist;

	newlist = (struct peer_list*) malloc(sizeof(struct peer_list));

	newlist->size = 0;
	newlist->head = (struct node*) malloc(sizeof(struct node));
	newlist->head->next = NULL;
	newlist->head->ip = 0;
	newlist->last = newlist->head;

	newlist->str = NULL;

	return newlist;
}
