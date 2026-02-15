#ifndef LXWM_TREE_H
#define LXWM_TREE_H

#include "wm_state.h"

Node *create_leaf(Client *c);
bool is_leaf(Node *n);
Node *find_first_leaf(Node *n);
void free_tree(Node *n);
void traverse_arrange(Node *n);
void map_tree(Node *n);
void unmap_tree(Node *n);

#endif
