#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void build_default_socket(char *out, size_t outsz)
{
    const char *disp = getenv("DISPLAY");
    if (!disp || !*disp) {
        disp = ":0";
    }

    char tag[64];
    size_t n = 0;
    for (size_t i = 0; disp[i] && n + 1 < sizeof(tag); i++) {
        unsigned char ch = (unsigned char)disp[i];
        tag[n++] = isalnum(ch) ? (char)ch : '_';
    }
    tag[n] = '\0';

    snprintf(out, outsz, "/tmp/lxwm-%u-%s.sock", (unsigned int)getuid(), tag);
}

int main(int argc, char **argv)
{
    const char *sock_path = NULL;
    int argi = 1;

    if (argc > 2 && !strcmp(argv[1], "-s")) {
        sock_path = argv[2];
        argi = 3;
    }
    if (argi >= argc) {
        fprintf(stderr, "Usage: %s [-s socket] <command>\n", argv[0]);
        return 1;
    }

    char default_sock[PATH_MAX];
    if (!sock_path) {
        build_default_socket(default_sock, sizeof(default_sock));
        sock_path = default_sock;
    }

    char cmd[1024] = {0};
    for (int i = argi; i < argc; i++) {
        size_t used = strlen(cmd);
        if (used + strlen(argv[i]) + 2 >= sizeof(cmd)) {
            fprintf(stderr, "command too long\n");
            return 1;
        }
        if (used > 0) {
            strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
        }
        strncat(cmd, argv[i], sizeof(cmd) - strlen(cmd) - 1);
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(sock_path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "socket path too long\n");
        close(fd);
        return 1;
    }
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return 1;
    }

    char wire[1100];
    snprintf(wire, sizeof(wire), "%s\n", cmd);
    if (write(fd, wire, strlen(wire)) < 0) {
        perror("write");
        close(fd);
        return 1;
    }

    char reply[512];
    ssize_t n = read(fd, reply, sizeof(reply) - 1);
    if (n > 0) {
        reply[n] = '\0';
        fputs(reply, stdout);
    }

    close(fd);
    return 0;
}
