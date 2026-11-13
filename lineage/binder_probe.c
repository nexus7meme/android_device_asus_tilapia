/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <fcntl.h>
#include <linux/android/binder.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    const char *paths[] = {"/dev/binder", "/dev/hwbinder", "/dev/vndbinder"};
    dev_t devices[3];
    unsigned int i;
    struct stat metadata;
    int failed = 0;
    int path_fd = open("/system/bin/init", O_PATH | O_CLOEXEC);
    if (path_fd < 0 || fstat(path_fd, &metadata) != 0) {
        fprintf(stderr, "O_PATH fstat failed: %s\n", strerror(errno));
        failed = 1;
    } else {
        printf("PASS: O_PATH fstat supports Android 11 realpath\n");
    }
    if (path_fd >= 0) close(path_fd);
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        struct binder_version version = {0};
        int fd = open(paths[i], O_RDWR | O_CLOEXEC);
        if (fd < 0 || ioctl(fd, BINDER_VERSION, &version) != 0 || fstat(fd, &metadata) != 0) {
            fprintf(stderr, "%s: %s\n", paths[i], strerror(errno));
            if (fd >= 0) close(fd);
            return 1;
        }
        devices[i] = metadata.st_rdev;
        printf("%s: protocol %d, device %llu\n", paths[i], version.protocol_version,
               (unsigned long long)devices[i]);
        if (version.protocol_version != 8) failed = 1;
        close(fd);
    }
    if (devices[0] == devices[1] || devices[0] == devices[2] || devices[1] == devices[2]) {
        fprintf(stderr, "Binder contexts do not have distinct device nodes\n");
        failed = 1;
    }
    return failed;
}
