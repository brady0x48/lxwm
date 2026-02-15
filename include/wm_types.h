#ifndef LXWM_TYPES_H
#define LXWM_TYPES_H

#define _POSIX_C_SOURCE 200809L
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#if __has_include(<X11/extensions/Xinerama.h>)
#include <X11/extensions/Xinerama.h>
#define HAVE_XINERAMA 1
#else
#define HAVE_XINERAMA 0
#endif

#define MAX_MONITORS 16
#define MAX_WORKSPACES 10
#define MAX_KEYBINDS 128
#define MAX_AUTOSTART 64
#define MAX_RULES 64
#define MAX_VARS 64
#define BORDER_WIDTH 2
#define BAR_HEIGHT 22
#define TITLEBAR_HEIGHT 20
#define TITLE_MAX 256

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct Client Client;
typedef struct Node Node;
typedef struct Workspace Workspace;
typedef struct Monitor Monitor;

typedef enum
{
    BAR_MOD_CPU = 0,
    BAR_MOD_MEM,
    BAR_MOD_TIME,
    BAR_MOD_WIFI,
    BAR_MOD_ETHERNET,
    BAR_MOD_IP,
    BAR_MOD_BATTERY,
    BAR_MOD_CPUTEMP,
    BAR_MOD_DISK,
    BAR_MOD_COUNT
} BarModule;

typedef enum
{
    ACT_NONE = 0,
    ACT_SPAWN,
    ACT_FOCUS_NEXT,
    ACT_FOCUS_PREV,
    ACT_FOCUS_LEFT,
    ACT_FOCUS_RIGHT,
    ACT_FOCUS_UP,
    ACT_FOCUS_DOWN,
    ACT_KILL,
    ACT_QUIT,
    ACT_WS,
    ACT_MOVE_TO_WS,
    ACT_RELOAD,
    ACT_TOGGLE_FLOAT,
    ACT_MOVE_FLOAT,
    ACT_RESIZE_FLOAT,
    ACT_SPLIT_TOGGLE,
    ACT_ROTATE_TREE,
    ACT_SWAP_NEXT,
    ACT_TOGGLE_FULLSCREEN,
    ACT_SCRATCHPAD_TOGGLE,
    ACT_RESTART,
} ActionType;

typedef enum
{
    WM_LOG_OFF = 0,
    WM_LOG_ERROR = 1,
    WM_LOG_INFO = 2,
    WM_LOG_DEBUG = 3,
} WmLogLevel;

typedef enum
{
    FOCUS_CLICK = 0,
    FOCUS_HOVER
} FocusPolicy;

typedef struct
{
    unsigned int mod;
    KeySym keysym;
    KeyCode keycode;
    ActionType action;
    char arg[256];
    int iarg;
} KeyBind;

typedef struct
{
    char class_name[64];
    char instance_name[64];
    char title_substr[128];
    int set_floating;
    int floating;
    int workspace;
    int monitor;
} Rule;

typedef struct
{
    char name[64];
    char value[256];
} ConfigVar;

struct Node
{
    int x;
    int y;
    int w;
    int h;
    float ratio;
    int split_vertical;
    Node *parent;
    Node *first;
    Node *second;
    Client *client;
};

struct Client
{
    Window win;
    Window titlebar;
    char last_title[TITLE_MAX];
    int x;
    int y;
    int w;
    int h;
    int old_bw;
    int mapped;
    int ignore_unmap;
    int is_hidden;
    int is_floating;
    int is_fullscreen;
    int was_floating_before_fullscreen;
    int old_x;
    int old_y;
    int old_w;
    int old_h;
    int is_urgent;
    int is_scratchpad;
    int scratch_was_floating;
    int workspace;
    Monitor *mon;
    Node *leaf;
    Client *next;
};

struct Workspace
{
    Node *root;
    Client *focus;
};

struct Monitor
{
    int id;
    int x;
    int y;
    int w;
    int h;
    int bar_h;
    Window bar;
    Workspace ws[MAX_WORKSPACES];
    int current_ws;
    unsigned int ws_mask;
};

#endif
