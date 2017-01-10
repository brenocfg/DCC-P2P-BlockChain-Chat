#include "archive.h"

/*This file implements all the data structures and operations related to the
  chat archive. This means the structure that stores an archive, as well as the
  operations related to changing it. It also contains all the operations related
  to incoming potential archives, such as hash validation and whatnot.*/

/*parses the message, checking if all characters are valid (printable). For
  valid messages, returns number of characters in the message. Returns 0 for
  invalid strings (empty or containing illegal characters)*/
int parse_message (uint8_t *msg) {
  int count = 0;

  /*iterate over characters in string, validating (and counting) each*/
  while (*msg) {
    /*newline=end of message, don't increment count, it's not part of payload*/
    if (*msg == 10) {
      break;
    }

    /*illegal characters*/
    if (*msg < 32 || *msg > 126) {
      return 0;
    }

    count++;
    msg++;
  }

  return count;
}

/*Attempts to insert message 'msg' in the given chat archive. To do so, we
  check if the message is valid, then mine a 16 byte code that generates a valid
  MD5 hash for the string. Then format the string for the entire msg+metadata
  properly, and include it in the archive structure, updating it accordingly.
  Returns 1 if message was added successfully, 0 otherwise.

  Note that we do not validate the archive before attempting to add the message,
  we just assume it is already valid, since all archives are validated when
  initially received.*/
int add_message (struct archive *arch, uint8_t *msg) {
  uint16_t len;
  uint8_t *newmsg, *code, *md5;

  /*parse message and get length, return if message is invalid*/
  len = parse_message(msg);
  if (len == 0) {
    return 0;
  }

  /*print message back to user*/
  int i;
  fprintf(stdout, "\nMessage length = %d\nContent: ", len);
  for (i = 0; i < len; i++) {
    fprintf(stdout, "%c", msg[i]);
  }
  fprintf(stdout, "\n");

  /*allocate memory for the final string and copy over the "tail" of the current
  archive*/
  newmsg = (uint8_t*) malloc(arch->len + len + 33);
  memcpy(newmsg, (arch->str + arch->offset), (arch->len - arch->offset));

  /*concatenate our new message to the archive's tail*/
  *(newmsg + (arch->len - arch->offset)) = len;
  memcpy(newmsg + (arch->len - arch->offset + 1), msg, len);

  /*get pointers to the beginning of the code/md5 hash sections of sequence*/
  code = (newmsg + arch->len - arch->offset + 1 + len);
  md5 = code+16;

  /*128bit pointer for hash comparison, 16bit pointer for 2 0-byte check*/
  unsigned __int128 *mineptr = (unsigned __int128*) code;
  uint16_t *check = (uint16_t*) md5;

  /*mine a code that generates a valid MD5 hash*/
  *mineptr = (unsigned __int128) 0;
  while (1) {
    MD5(newmsg, (arch->len - arch->offset + len + 17), md5);
    /*found it (first 2 bytes are 0)*/
    if (*check == 0) {
      break;
    }
    *mineptr += 1;
  }

  /*print the mined code and message hash*/
  fprintf(stdout, "code: ");
  for (i = 0; i < 16; i++) {
    fprintf(stdout, "%02x", *(code+i));
  }
  fprintf(stdout, "\nmd5: ");
  for (i = 0; i < 16; i++) {
    fprintf(stdout, "%02x", *(md5+i));
  }
  fprintf(stdout, "\n\n");

  /*substitute the current archive content to include new message*/
  arch->str = realloc(arch->str, arch->len+len+33);
  memcpy(arch->str+arch->len, newmsg + arch->len - arch->offset, len+33);

  /*update archive size and length, and offset if necessary*/
  arch->size += 1;
  arch->len += len+33;
  if (arch->size >= 20) {
    arch->offset += *(arch->str+arch->offset)+33;
  }

  /*update archive size byte representation*/
  uint8_t *aux = arch->str+1;
  uint32_t old_size=((aux[0] << 24) | (aux[1] << 16) | (aux[2] << 8) | aux[3]);
  old_size++;
  aux[0] = (old_size >> 24) & 0xFF;
  aux[1] = (old_size >> 16) & 0xFF;
  aux[2] = (old_size >> 8) & 0xFF;
  aux[3] = old_size & 0xFF;

  return 1;
}

/*Given an input archive, validates the MD5 hashes of all of its messages, and
  returns whether the entire archive is valid or not. 1 -> valid archive, 0
  otherwise.*/
int is_valid (struct archive *arch) {
  uint8_t *begin, *end, md5[16];
  unsigned __int128 *calc_hash, *orig_hash;

  /*skip message type/size bytes*/
  begin = arch->str+5;
  end = arch->str+5;

  /*our calculated hash is always at the same memory address*/
  calc_hash = (unsigned __int128*) md5;

  /*now let's iterate over every message in the archive*/
  uint32_t i, md5len = 0;
  for (i = 1; i <= arch->size; i++) {
    /*first compute the length of the current message*/
    uint8_t len = *end;

    /*iterate to end of message, and keep track of how many bytes we'll hash*/
    end += len+17;
    md5len += len+17;

    /*check first 2 bytes of hash, we use a 2 byte pointer to simplify things*/
    uint16_t *f2bytes = (uint16_t*) end;
    if (*f2bytes != 0) {
      fprintf(stderr, "Non-zero bytes in MD5 Hash. Invalid archive!\n");
      return 0;
    }

    /*update offset starting from 20th message*/
    if (i > 19) {
      arch->offset += ((*begin) + 33);
    }

    /*if sequence is over 20 messages long, remove first message from md5 input
    string, and recompute its length*/
    if (i > 20) {
      md5len -= ((*begin) + 33);
      begin += ((*begin) + 33);
    }

    /*calculate hash for byte sequence, and compare with original hash*/
    MD5(begin, md5len, md5);

    orig_hash = (unsigned __int128*) end;

    if (*calc_hash != *orig_hash) {
      fprintf(stderr, "Hash Mismatch! Invalid archive.\n");
      return 0;
    }

    /*update end pointer past the md5 hash, and update md5 input string length*/
    end += 16;
    md5len += 16;
  }
  return 1;
}

/*prints an archive to given stream, for either debugging or updating archive*/
void print_archive (struct archive *arch, FILE *stream) {
  uint8_t *ptr;
  uint32_t size;

  ptr = arch->str;
  size = arch->size;

  fprintf(stream, "\n----------ARCHIVE BEGINNING----------\n");
  /*message type and syze bytes*/
  fprintf(stream, "size: %u, length: %u\n", arch->size, arch->len);

  ptr+=5;

  /*loop through messages*/
  uint32_t i, j;
  for (i = 0; i < size; i++) {
    uint8_t len;
    len = *ptr++;

    fprintf(stream, "msg[%d]: ", len);

    /*message content*/
    for (j = 0; j < len; j++, ptr++) {
      fprintf(stream, "%c", *ptr);
    }

    /*16 byte hashing code*/
    fprintf(stream, "\ncode: ");
    for (j = 0; j < 16; j++, ptr++) {
      fprintf(stream, "%02x", *ptr);
    }

    /*16 byte MD5 hash*/
    fprintf(stream, "\nmd5: ");
    for (j = 0; j < 16; j++, ptr++) {
      fprintf(stream, "%02x", *ptr);
    }
    fprintf(stream, "\n");
  }

  fprintf(stream, "---------- ARCHIVE FINISH ----------\n");
}

/*Initializes a new archive structure, and returns it. New archives have size 0,
  so that any new valid archive can overwrite them. Its string representation is
  initially 5 characters long, containing only the message type and the 4 bytes
  indicating amount of messages (which is obviously 0).
  Offset is initially 5, since there are no messages in the archive (obvs), and
  we ignore the type+size bytes*/
struct archive *init_archive() {
  struct archive *newarchive;

  newarchive = (struct archive*) malloc(sizeof(struct archive));

  uint8_t *str = (uint8_t*) malloc(5);
  str[0] = 4;
  str[1] = str[2] = str[3] = str[4] = 0;
  newarchive->str = str;
  newarchive->offset = 5;

  newarchive->len = 5;
  newarchive->size = 0;

  return newarchive;
}
