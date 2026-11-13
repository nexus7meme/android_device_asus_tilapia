/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <linux/capability.h>
#include <linux/memfd.h>
#include <poll.h>
#include <sys/uio.h>
#include <unistd.h>

#define SET_VMA 0x53564d41
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s (%s)\n", __LINE__, #test, strerror(errno)); return 1; } } while (0)

static int test_credentials(int type)
{
    int listener = socket(AF_UNIX, type, 0), on = 1;
    CHECK(listener >= 0);
    CHECK(setsockopt(listener, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on)) == 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    int len = snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1,
                       "tilapia-cred-%d-%d", getpid(), type);
    socklen_t address_len = offsetof(struct sockaddr_un, sun_path) + 1 + len;
    CHECK(bind(listener, (struct sockaddr *)&addr, address_len) == 0);
    CHECK(listen(listener, 1) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        int fd = socket(AF_UNIX, type, 0);
        if (fd < 0 || connect(fd, (struct sockaddr *)&addr, address_len) ||
            write(fd, "1", 1) != 1) _exit(2);
        close(fd);
        _exit(0);
    }
    int fd = accept(listener, NULL, NULL);
    CHECK(fd >= 0);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    char byte, control[CMSG_SPACE(sizeof(struct ucred))];
    struct iovec iov = {&byte, 1};
    struct msghdr msg = {.msg_iov = &iov, .msg_iovlen = 1,
                         .msg_control = control, .msg_controllen = sizeof(control)};
    CHECK(recvmsg(fd, &msg, 0) == 1);
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    CHECK(cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_CREDENTIALS);
    struct ucred *cred = (struct ucred *)CMSG_DATA(cmsg);
    CHECK(cred->pid == child && cred->uid == getuid() && cred->gid == getgid());
    close(fd);
    close(listener);
    return 0;
}

static int test_alarm_timer(clockid_t clock)
{
    int fd = timerfd_create(clock, TFD_CLOEXEC | TFD_NONBLOCK);
    CHECK(fd >= 0);
    struct itimerspec spec = {.it_value = {.tv_nsec = 20000000}};
    CHECK(timerfd_settime(fd, 0, &spec, NULL) == 0);
    struct pollfd pollfd = {.fd = fd, .events = POLLIN};
    CHECK(poll(&pollfd, 1, 1000) == 1);
    uint64_t count;
    CHECK(read(fd, &count, sizeof(count)) == sizeof(count) && count == 1);
    spec.it_interval.tv_nsec = 10000000;
    spec.it_value.tv_nsec = 10000000;
    CHECK(timerfd_settime(fd, 0, &spec, NULL) == 0);
    usleep(70000);
    CHECK(read(fd, &count, sizeof(count)) == sizeof(count) && count >= 2);
    memset(&spec, 0, sizeof(spec));
    CHECK(timerfd_settime(fd, 0, &spec, NULL) == 0);
    CHECK(timerfd_gettime(fd, &spec) == 0 && spec.it_value.tv_sec == 0 && spec.it_value.tv_nsec == 0);
    CHECK(read(fd, &count, sizeof(count)) == -1 && errno == EAGAIN);
    if (clock == CLOCK_BOOTTIME_ALARM) {
        pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0) {
            struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
            struct __user_cap_data_struct data[2] = {{0}, {0}};
            if (syscall(__NR_capset, &header, data)) _exit(2);
            int other = timerfd_create(clock, TFD_CLOEXEC);
            if (other != -1 || errno != EPERM) _exit(3);
            spec.it_value.tv_nsec = 1000000;
            if (timerfd_settime(fd, 0, &spec, NULL) != -1 || errno != EPERM) _exit(4);
            _exit(0);
        }
        int status;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    close(fd);
    return 0;
}

static int test_process_vm(void)
{
    char source[16] = "tilapia-abi", destination[32] = {0};
    struct iovec local = {destination, sizeof(source)};
    struct iovec remote[2] = {{source, sizeof(source)}, {(void *)1, sizeof(source)}};
    CHECK(process_vm_readv(getpid(), &local, 1, remote, 1, 0) == sizeof(source));
    CHECK(memcmp(source, destination, sizeof(source)) == 0);
    char changed[16] = "tilapia-write";
    struct iovec input = {changed, sizeof(changed)};
    CHECK(process_vm_writev(getpid(), &input, 1, remote, 1, 0) == sizeof(source));
    CHECK(memcmp(source, changed, sizeof(source)) == 0);
    local.iov_len = sizeof(destination);
    CHECK(process_vm_readv(getpid(), &local, 1, remote, 2, 0) == sizeof(source));
    CHECK(process_vm_readv(getpid(), &local, 1, remote + 1, 1, 0) == -1 && errno == EFAULT);
    CHECK(process_vm_readv(getpid(), &local, 1, remote, 1, 1) == -1 && errno == EINVAL);
    CHECK(prctl(PR_SET_DUMPABLE, 0) == 0);
    pid_t parent = getpid(), child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
        struct __user_cap_data_struct data[2] = {{0}, {0}};
        if (syscall(__NR_capset, &header, data)) _exit(2);
        if (process_vm_readv(parent, &local, 1, remote, 1, 0) != -1 || errno != EPERM) _exit(3);
        _exit(0);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}

static int check_ambient_block_suspend(void)
{
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct data[2] = {{0}, {0}};
    CHECK(getuid() == 1000);
    CHECK(syscall(__NR_capget, &header, data) == 0);
    CHECK((data[1].permitted & (1U << 4)) && (data[1].effective & (1U << 4)));
    CHECK(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, 36, 0, 0) == 1);
    memset(data, 0, sizeof(data));
    CHECK(syscall(__NR_capset, &header, data) == 0);
    CHECK(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, 36, 0, 0) == 0);
    CHECK(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, 36, 0, 0) == -1 && errno == EPERM);
    CHECK(prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, 37, 0, 0) == -1 && errno == EINVAL);
    puts("PASS: capability 36 survives non-root exec; dropping it clears ambient; denied escalation and invalid capability rejected");
    return 0;
}

static int test_ambient_block_suspend(const char* executable)
{
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
        struct __user_cap_data_struct data[2] = {{0}, {0}};
        if (prctl(PR_SET_KEEPCAPS, 1) || setresgid(1000, 1000, 1000) ||
            setresuid(1000, 1000, 1000)) _exit(2);
        data[1].permitted = data[1].effective = data[1].inheritable = 1U << 4;
        if (syscall(__NR_capset, &header, data) ||
            prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, 36, 0, 0)) _exit(3);
        execl(executable, executable, "--ambient-block-suspend", NULL);
        _exit(4);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}

static int test_memfd(const char* executable)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    CHECK(syscall(__NR_memfd_create, "bad-flags", ~0U) == -1 && errno == EINVAL);
    CHECK(syscall(__NR_memfd_create, (void*)1, 0) == -1 && errno == EFAULT);
    CHECK(syscall(__NR_memfd_create, "unsupported-sealing", MFD_ALLOW_SEALING) == -1 &&
          errno == EINVAL);
    char long_name[251];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = 0;
    CHECK(syscall(__NR_memfd_create, long_name, 0) == -1 && errno == EINVAL);
    int fd = syscall(__NR_memfd_create, "tilapia-wx-test", MFD_CLOEXEC);
    CHECK(fd >= 0);
    CHECK((fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0);
    CHECK((fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDWR);
    CHECK(fcntl(fd, F_GET_SEALS) == F_SEAL_SEAL);
    CHECK(fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) == -1 && errno == EPERM);
    CHECK(fcntl(fd, F_ADD_SEALS, 0x80000000U) == -1 && errno == EINVAL);
    CHECK(ftruncate(fd, page) == 0);
    uint8_t* rw = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    uint8_t* rx = mmap(NULL, page, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
    CHECK(rw != MAP_FAILED && rx != MAP_FAILED);
#if defined(__arm__)
    const uint16_t code[] = {0x202a, 0x4770}; /* Thumb: movs r0, #42; bx lr */
    memcpy(rw, code, sizeof(code));
    __builtin___clear_cache((char*)rw, (char*)rw + sizeof(code));
    __builtin___clear_cache((char*)rx, (char*)rx + sizeof(code));
    int (*function)(void) = (int (*)(void))((uintptr_t)rx | 1U);
    CHECK(function() == 42);
#endif
    CHECK(pwrite(fd, "shared", 6, 32) == 6 && !memcmp(rx + 32, "shared", 6));
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        char descriptor[16];
        snprintf(descriptor, sizeof(descriptor), "%d", fd);
        execl(executable, executable, "--memfd-closed", descriptor, NULL);
        _exit(2);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(munmap(rw, page) == 0 && munmap(rx, page) == 0);
    close(fd);
    fd = syscall(__NR_memfd_create, "", 0);
    CHECK(fd >= 0 && !(fcntl(fd, F_GETFD) & FD_CLOEXEC));
    close(fd);
    fd = open("/dev/null", O_RDONLY);
    CHECK(fd >= 0 && fcntl(fd, F_GET_SEALS) == -1 && errno == EINVAL);
    close(fd);
    puts("PASS: memfd shared RW/RX aliases execute code, resize and I/O, CLOEXEC across exec, error paths, and honest unsupported-sealing detection");
    return 0;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 2 && !strcmp(argv[1], "--ambient-block-suspend"))
        return check_ambient_block_suspend();
    if (argc == 3 && !strcmp(argv[1], "--memfd-closed")) {
        CHECK(fcntl(atoi(argv[2]), F_GETFD) == -1 && errno == EBADF);
        return 0;
    }
    CHECK(test_ambient_block_suspend(argv[0]) == 0);
    CHECK(test_memfd(argv[0]) == 0);
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    char *map = mmap(NULL, page * 3, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(map != MAP_FAILED);
    CHECK(prctl(SET_VMA, 0, (unsigned long)(map + page), page,
                (unsigned long)"tilapia-probe") == 0);
    FILE *maps = fopen("/proc/self/maps", "r");
    CHECK(maps);
    char *line = NULL;
    size_t capacity = 0;
    int found = 0;
    while (getline(&line, &capacity, maps) >= 0) {
        unsigned long start, end;
        if (strstr(line, "[anon:tilapia-probe]") &&
            sscanf(line, "%lx-%lx", &start, &end) == 2 &&
            start == (unsigned long)(map + page) && end == start + page)
            found = 1;
    }
    free(line);
    fclose(maps);
    CHECK(found);
    CHECK(prctl(SET_VMA, 0, (unsigned long)map + 1, page, 0UL) == -1 && errno == EINVAL);
    CHECK(prctl(SET_VMA, 0, (unsigned long)map, page * 3, 0UL) == 0);
    CHECK(munmap(map, page * 3) == 0);
    puts("PASS: anonymous VMA naming, subrange split, maps output, clearing and alignment rejection");

    // Keep many mappings alive to expose per-allocation random-gap exhaustion.
    void *allocations[512];
    uintptr_t low = UINTPTR_MAX, high = 0;
    for (size_t i = 0; i < 512; ++i) {
        allocations[i] = mmap(NULL, page, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(allocations[i] != MAP_FAILED);
        uintptr_t address = (uintptr_t)allocations[i];
        if (address < low) low = address;
        if (address > high) high = address;
    }
    CHECK(high - low < 32 * 1024 * 1024);
    for (size_t i = 0; i < 512; ++i) CHECK(munmap(allocations[i], page) == 0);
    printf("PASS: 512 live mappings span %lu bytes without per-mmap random gaps\n",
           (unsigned long)(high - low + page));

    unsigned char entropy1[32], entropy2[32];
    CHECK(syscall(__NR_getrandom, entropy1, sizeof(entropy1), 0) == sizeof(entropy1));
    CHECK(syscall(__NR_getrandom, entropy2, sizeof(entropy2), 1) == sizeof(entropy2));
    CHECK(memcmp(entropy1, entropy2, sizeof(entropy1)) != 0);
    CHECK(syscall(__NR_getrandom, entropy1, 1, 4) == -1 && errno == EINVAL);
    CHECK(syscall(__NR_getrandom, (void *)1, 1, 0) == -1 && errno == EFAULT);
    CHECK(syscall(__NR_getrandom, entropy1, 0, 0) == 0);
    puts("PASS: getrandom seeded reads, nonblocking reads, flags, EFAULT and zero length");

    int ready[2];
    CHECK(pipe(ready) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        close(ready[0]);
        if (write(ready[1], "1", 1) != 1) _exit(2);
        for (;;) pause();
    }
    close(ready[1]);
    char byte;
    CHECK(read(ready[0], &byte, 1) == 1);
    close(ready[0]);
    int status;
    int failed = 0;
    if (ptrace(PTRACE_SEIZE, child, (void *)1, NULL) != -1 || errno != EIO) failed = 1;
    if (ptrace(PTRACE_SEIZE, child, NULL, (void *)PTRACE_O_TRACECLONE) != 0) failed = 1;
    if (!failed && waitpid(child, &status, WNOHANG) != 0) failed = 1;
    if (!failed && ptrace(PTRACE_INTERRUPT, child, NULL, NULL) != 0) failed = 1;
    if (!failed && (waitpid(child, &status, 0) != child || !WIFSTOPPED(status) ||
                    (status >> 16) != PTRACE_EVENT_STOP)) failed = 1;
    if (!failed && ptrace(PTRACE_DETACH, child, NULL, NULL) != 0) failed = 1;
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    CHECK(!failed);
    puts("PASS: PTRACE_SEIZE validation/options/no initial stop, INTERRUPT event, DETACH");
    CHECK(test_credentials(SOCK_STREAM) == 0);
    CHECK(test_credentials(SOCK_SEQPACKET) == 0);
    puts("PASS: accepted STREAM and SEQPACKET sockets retain SO_PASSCRED and report real sender PID/UID/GID");
    CHECK(test_alarm_timer(CLOCK_BOOTTIME) == 0);
    CHECK(test_alarm_timer(CLOCK_REALTIME_ALARM) == 0);
    CHECK(test_alarm_timer(CLOCK_BOOTTIME_ALARM) == 0);
    puts("PASS: BOOTTIME/alarm timerfd delivery, periodic overrun, disarm and CAP_WAKE_ALARM enforcement");
    CHECK(test_process_vm() == 0);
    puts("PASS: process_vm self read/write, partial iovecs, EFAULT, flags and denied access to a non-dumpable peer");
    return 0;
}
