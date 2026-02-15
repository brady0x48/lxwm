#include "wm_internal.h"

void spawn_command(const char *cmd)
{
    if (!cmd || !*cmd) {
        return;
    }

    char run_cmd[512];
    run_cmd[0] = '\0';

    bool is_firefox = false;
    const char *p = cmd;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (!strncasecmp(p, "firefox", 7) && (p[7] == '\0' || isspace((unsigned char)p[7]))) {
        is_firefox = true;
    } else {
        const char *base = strrchr(p, '/');
        if (base) {
            base++;
            if (!strncasecmp(base, "firefox", 7) &&
                (base[7] == '\0' || isspace((unsigned char)base[7]))) {
                is_firefox = true;
            }
        }
    }

    strncpy(run_cmd, cmd, sizeof(run_cmd) - 1);
    run_cmd[sizeof(run_cmd) - 1] = '\0';
    if (is_firefox) {
        if (!strstr(run_cmd, "--profile") && !strstr(run_cmd, " -P ")) {
            size_t used = strlen(run_cmd);
            const char *suffix = " --profile \"$HOME/.cache/lxwm/firefox-profile\"";
            if (used + strlen(suffix) < sizeof(run_cmd)) {
                strncat(run_cmd, suffix, sizeof(run_cmd) - used - 1);
            }
        }
        if (!strstr(run_cmd, "--new-window") && !strstr(run_cmd, "--new-tab") &&
            !strstr(run_cmd, "--new-instance")) {
            size_t used = strlen(run_cmd);
            const char *suffix = " --new-window";
            if (used + strlen(suffix) < sizeof(run_cmd)) {
                strncat(run_cmd, suffix, sizeof(run_cmd) - used - 1);
            }
        }
    }

    pid_t pid = fork();
    if (pid == 0) {
        pid_t child = fork();
        if (child == 0) {
            if (dpy) {
                close(ConnectionNumber(dpy));
            }
            setsid();

            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                if (devnull > STDERR_FILENO) {
                    close(devnull);
                }
            }

            int logfd = -1;
            if (wm_log_level > WM_LOG_OFF) {
                const char *path = wm_log_path[0] ? wm_log_path : "/tmp/lxwm.log";
                logfd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
            }
            if (logfd >= 0) {
                dprintf(logfd, "\n[%ld] INFO: spawn: %s\n", (long)time(NULL), run_cmd);
                dup2(logfd, STDOUT_FILENO);
                dup2(logfd, STDERR_FILENO);
                if (logfd > STDERR_FILENO) {
                    close(logfd);
                }
            } else {
                dup2(STDIN_FILENO, STDOUT_FILENO);
                dup2(STDIN_FILENO, STDERR_FILENO);
            }

            if (is_firefox) {
                setenv("MOZ_DBUS_REMOTE", "0", 1);
                const char *home = getenv("HOME");
                if (home && *home) {
                    char cache_dir[PATH_MAX];
                    char lxwm_dir[PATH_MAX];
                    char ff_dir[PATH_MAX];
                    snprintf(cache_dir, sizeof(cache_dir), "%s/.cache", home);
                    snprintf(lxwm_dir, sizeof(lxwm_dir), "%s/.cache/lxwm", home);
                    snprintf(ff_dir, sizeof(ff_dir), "%s/.cache/lxwm/firefox-profile", home);
                    mkdir(cache_dir, 0755);
                    mkdir(lxwm_dir, 0755);
                    mkdir(ff_dir, 0700);
                }
            }

            execl("/bin/sh", "sh", "-lc", run_cmd, (char *)NULL);
            _exit(127);
        }
        _exit(0);
    }
}

void run_autostart_commands(void)
{
    for (int i = 0; i < autostart_count; i++) {
        if (autostart_cmds[i][0]) {
            spawn_command(autostart_cmds[i]);
        }
    }
}
