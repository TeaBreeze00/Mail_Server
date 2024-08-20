/* netbuffer.h
 * Creates a buffer for receiving data from a socket and reading individual lines.
 * Author  : Jonatan Schroeder
 * Modified: Nov 5, 2017
 */

#ifndef _NET_BUFFER_H_
#define _NET_BUFFER_H_

#include <string.h>

typedef struct net_buffer *net_buffer_t;

net_buffer_t nb_create(int fd, size_t max_buffer_size);
void nb_destroy(net_buffer_t nb);
int nb_read_line(net_buffer_t nb, char out[]);

#endif
