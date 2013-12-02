#ifndef _HD_CLIENT_NET_UTILS_H
#define _HD_CLIENT_NET_UTILS_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "opal/util/output.h"

// 2-byte number
short short_endian_swap(short i);

// 4-byte number
int int_endian_swap(int i);

// swap endian for an int and write it to socket
void write_endian_swap_int(int socket, int num);

// swap endian for a short and write it to socket
void write_endian_swap_short(int socket, short num);

// write protobuf-style varint-int to buffer
int write_raw_varint32(char* buffer, int value);

// read protbuf-style varint-int from socket
// return 0 if succeed
int read_raw_varint32(int socket_id, int* readlen, int* value);

/* try to write all buffer to socket incase not all data written
 * will failed when write failed,
 * return 0 when succeed
 */
int write_all(int socket_id, const char* buffer, int size);

/* try to read buffer with length=size to socket, will fail when 
 * read failed,
 * return 0 when succeed
 */ 
int read_all(int socket_id, char* buffer, int size);

/* get local hostname, return hostname if succeed, otherwise, return NULL
 * must free the value when you no longer use it.
 */
char* get_local_hostname();

#endif // _HD_CLIENT_NET_UTILS_H
