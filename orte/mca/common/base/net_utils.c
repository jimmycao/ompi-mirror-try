#include "net_utils.h"
#include <unistd.h>
#include <string.h>

// 2-byte number
short short_endian_swap(short i) {
    return ((i>>8)&0xff)+((i << 8)&0xff00);
}

// 4-byte number
int int_endian_swap(int i) {
    return ((i&0xff)<<24)+((i&0xff00)<<8)+((i&0xff0000)>>8)+((i>>24)&0xff);
}

// swap endian for an int and write it to socket
void write_endian_swap_int(int socket, int num) {
    int len_for_send = int_endian_swap(num);
    write_all(socket, (char*)(&len_for_send), 4);
}

// swap endian for a short and write it to socket
void write_endian_swap_short(int socket, short num) {
    short len_for_send = short_endian_swap(num);
    write_all(socket, (char*)(&len_for_send), 2);   
}

// write protobuf-style varint-int to buffer
int write_raw_varint32(char* buffer, int value) {
    int index = 0;
    while (1) {
        if ((value & ~0x7F) == 0) {
            buffer[index] = (char)value;
            index++;
            return index;
        } else {
            buffer[index] = (char)((value & 0x7F) | 0x80);
            index++;
            value >>= 7;
        }
    }
    return index;
}

/* try to read buffer with length=size to socket, will fail when 
 * read failed,
 * return 0 when succeed
 */ 
int read_all(int socket_id, char* buffer, int size) {
    int bytes_read = 0;
    int retval;

    if (size < 0) {
        opal_output(0, "size is <= 0, please check.\n");
        return -1;
    }

    // we will not do anything for null buffer
    if (!buffer) {
        opal_output(0, "buffer is null, please check.\n");
        return -1;
    }

    while (bytes_read < size) {
        retval = read(socket_id, buffer + bytes_read, size - bytes_read);
        if (retval >= 0){
            bytes_read += retval;
        } else {
            opal_output(0, "error in reading data.\n");
            return -1;
        }
    }

    return 0;
}

/* try to write all buffer to socket incase not all data written
 * will failed when write failed
 */
int write_all(int socket_id, const char* buffer, int size) {
    int bytes_written = 0;
    int retval;

    if (size < 0) {
        opal_output(0, "size is <= 0, please check.\n");
        return -1;
    }

    // we will not do anything for null buffer
    if (!buffer) {
        opal_output(0, "buffer is null, will return.\n");
        return 0;
    }

    while (bytes_written < size) {
        retval = write(socket_id, buffer + bytes_written, size - bytes_written);
        if (retval >= 0){
            bytes_written += retval;
        } else {
            opal_output(0, "error in writting data.\n");
            return -1;
        }
    }

    return 0;
}

char* get_local_hostname() {
    char* host = (char*)malloc(1024);
    host[1023] = '\0';
    gethostname(host, 1023);
    opal_output(0, "get host name: %s.\n", host);
    return host;
}

// read protbuf-style varint-int from socket
int read_raw_varint32(int socket_id, int* readlen, int* value) {
    *readlen = 0;
    char tmp;
    read(socket_id, &tmp, 1);
    (*readlen)++;
    if (tmp >= 0) {
        *value = tmp;
        return 0;
    }
    int result = tmp & 0x7f;
    read(socket_id, &tmp, 1);
    (*readlen)++;
    if (tmp >= 0)
    {
        result |= tmp << 7;
    }
    else
    {
        result |= (tmp & 0x7f) << 7;
        read(socket_id, &tmp, 1);
        (*readlen)++;
        if (tmp >= 0)
        {
            result |= tmp << 14;
        }
        else
        {
            result |= (tmp & 0x7f) << 14;
            read(socket_id, &tmp, 1);
            (*readlen)++;
            if (tmp >= 0)
            {
                result |= tmp << 21;
            }
            else
            {
                result |= (tmp & 0x7f) << 21;
                read(socket_id, &tmp, 1);
                (*readlen)++;
                result |= (tmp << 28);
                if (tmp < 0)
                {
                    int i;
                    for (i = 0; i < 5; i++)
                    {
                        read(socket_id, &tmp, 1);
                        (*readlen)++;
                        if (tmp >= 0)
                        {
                            *value = result;
                            return -1;
                        }
                    }
                    return 0;
                }
            }
        }
    }
    *value = result;
    return 0;
}
