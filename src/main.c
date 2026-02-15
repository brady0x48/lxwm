#include "lxwm.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (!strcmp(argv[1], "--check-config")) {
            return lxwm_check_config();
        }
        if (!strcmp(argv[1], "--help")) {
            printf("Usage: %s [--check-config]\n", argv[0]);
            return 0;
        }
    }

    for (;;) {
        int rc = lxwm_start();
        if (rc != 23) {
            return rc;
        }
        execvp(argv[0], argv);
        perror("execvp");
        return 1;
    }
}
