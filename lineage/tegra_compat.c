/* SPDX-License-Identifier: Apache-2.0 */
/* Export the platform header implementations for legacy binary HALs. */
#define ANDROID_ATOMIC_INLINE
#include <cutils/atomic.h>

#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

/* Legacy libcutils API used by the XGold modem: connect only to IPv4 loopback. */
int socket_loopback_client(int port, int type)
{
    struct sockaddr_in address = {0};
    int fd = socket(AF_INET, type, 0);
    if (fd < 0) return -1;
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (const struct sockaddr*)&address, sizeof(address)) < 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    return fd;
}
