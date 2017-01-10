#include <stdint.h>       //portable types (uint8_t, uint32_t, etc...)
#include <stdlib.h>       //mallocs, callocs, frees and the like
#include <stdio.h>        //printing! :D, mostly for debugging and error reports
#include <string.h>       //memsets, memcpys and other memory shenanigans
#include <openssl/md5.h>	//MD5 hashing is fun

/*struct that stores an archive. Brief description of its member fields:
  size  ->  number of chat messages in the archive
  str   ->  string representation of the entire archive, in the format it is
            sent to peers (in network bytes and whatnot)
  len   ->  length of the archive's string representation, in bytes
  offset->  stores an offset from the base pointer to where the 19th message
            from the end of the archive is, so we can easily access which
            sequence we need to hash to add new messages
            this offset is first defined when validating an archive for the
            first time, and is then updated if messages are added*/
struct archive {
  uint8_t *str;
  uint32_t offset;
  uint32_t size;
  uint32_t len;
};

/*parses the message, checking if all characters are valid (printable). For
  valid messages, returns number of characters in the message. Returns 0 for
  invalid strings (empty or containing illegal characters)*/
int parse_message (uint8_t *msg);

/*Attempts to insert message 'msg' in the given chat archive. To do so, we
  check if the message is valid, then mine a 16 byte code that generates a valid
  MD5 hash for the string. Then format the string for the entire msg+metadata
  properly, and include it in the archive structure, updating it accordingly.
  Returns 1 if message was added successfully, 0 otherwise.

  Note that we do not validate the archive before attempting to add the message,
  we just assume it is already valid, since all archives are validated when
  initially received.*/
int add_message (struct archive *arch, uint8_t *msg);

/*Given an input archive, validates the MD5 hashes of all of its messages, and
  returns whether the entire archive is valid or not. 1 -> valid archive, 0
  otherwise.*/
int is_valid (struct archive *arch);

/*prints an archive to given stream, for either debugging or updating archive*/
void print_archive (struct archive *arch, FILE *stream);

/*Initializes a new archive structure, and returns it. New archives have size 0,
  so that any new valid archive can overwrite them. Its string representation is
  initially 5 characters long, containing only the message type and the 4 bytes
  indicating amount of messages (which is obviously 0).
  Offset is initially 5, since there are no messages in the archive (obvs), and
  we ignore the type+size bytes*/
struct archive *init_archive();
