#include "wm_internal.h"

Node *create_leaf(Client *c)
{
    Node *n = ecalloc(1, sizeof(Node));
    n->client = c;
    n->ratio = 0.5f;
    n->split_vertical = 1;
    return n;
}

bool is_leaf(Node *n) { return n && !n->first && !n->second; }

Node *find_first_leaf(Node *n)
{
    if (!n) {
        return NULL;
    }
    if (is_leaf(n)) {
        return n;
    }
    Node *left = find_first_leaf(n->first);
    return left ? left : find_first_leaf(n->second);
}

void free_tree(Node *n)
{
    if (!n) {
        return;
    }
    free_tree(n->first);
    free_tree(n->second);
    free(n);
}

static void apply_split(Node *n)
{
    if (!n || !n->first || !n->second) {
        return;
    }

    int avail_w = n->w;
    int avail_h = n->h;
    if (avail_w < 1 || avail_h < 1) {
        return;
    }

    if (n->split_vertical) {
        int w1 = (int)(avail_w * n->ratio);
        if (w1 < 1) {
            w1 = 1;
        }
        if (w1 > avail_w - 1) {
            w1 = avail_w - 1;
        }
        n->first->x = n->x;
        n->first->y = n->y;
        n->first->w = w1;
        n->first->h = avail_h;

        n->second->x = n->x + w1;
        n->second->y = n->y;
        n->second->w = avail_w - w1;
        n->second->h = avail_h;
    } else {
        int h1 = (int)(avail_h * n->ratio);
        if (h1 < 1) {
            h1 = 1;
        }
        if (h1 > avail_h - 1) {
            h1 = avail_h - 1;
        }
        n->first->x = n->x;
        n->first->y = n->y;
        n->first->w = avail_w;
        n->first->h = h1;

        n->second->x = n->x;
        n->second->y = n->y + h1;
        n->second->w = avail_w;
        n->second->h = avail_h - h1;
    }
}

void traverse_arrange(Node *n)
{
    if (!n) {
        return;
    }

    if (is_leaf(n)) {
        if (!n->client || n->client->is_floating || !n->client->mapped) {
            return;
        }
        Client *c = n->client;
        int bw = BORDER_WIDTH;
        int w = n->w - (2 * bw);
        int h = n->h - TITLEBAR_HEIGHT - (2 * bw);
        if (w < 1) {
            w = 1;
        }
        if (h < 1) {
            h = 1;
        }
        c->x = n->x;
        c->y = n->y + TITLEBAR_HEIGHT;
        c->w = w;
        c->h = h;

        XMoveResizeWindow(dpy, c->titlebar, n->x, n->y, (unsigned int)(w + (2 * bw)),
                          TITLEBAR_HEIGHT);
        XMoveResizeWindow(dpy, c->win, c->x, c->y, (unsigned int)c->w, (unsigned int)c->h);
        draw_titlebar(c);
        return;
    }

    apply_split(n);
    traverse_arrange(n->first);
    traverse_arrange(n->second);
}

void map_tree(Node *n)
{
    if (!n) {
        return;
    }
    if (is_leaf(n)) {
        if (n->client && n->client->mapped) {
            n->client->is_hidden = 0;
            XMapWindow(dpy, n->client->titlebar);
            XMapWindow(dpy, n->client->win);
            draw_titlebar(n->client);
        }
        return;
    }
    map_tree(n->first);
    map_tree(n->second);
}

void unmap_tree(Node *n)
{
    if (!n) {
        return;
    }
    if (is_leaf(n)) {
        if (n->client && n->client->mapped) {
            n->client->is_hidden = 1;
            n->client->ignore_unmap++;
            XUnmapWindow(dpy, n->client->titlebar);
            XUnmapWindow(dpy, n->client->win);
        }
        return;
    }
    unmap_tree(n->first);
    unmap_tree(n->second);
}
