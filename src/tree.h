
#ifndef SPIN_TREE_H
#define SPIN_TREE_H 1

#include <stdlib.h>
#include <string.h>

typedef struct tree_entry_s {
    size_t key_size;
    void* key;
    size_t data_size;
    void* data;
    struct tree_entry_s* parent;
    struct tree_entry_s* left;
    struct tree_entry_s* right;
} tree_entry_t;

typedef struct {
    tree_entry_t* root;
    int (*cmp_func)(size_t key_a_size, void* key_a, size_t key_b_size, void* key_b);
} tree_t;

tree_entry_t* tree_entry_create(size_t key_size, void* key, size_t data_size, void* data, int copy);
void tree_entry_destroy(tree_entry_t* tree_entry, int destroy_children);

tree_t* tree_create(int (*cmp_func)(size_t key_a_size, void* key_a, size_t key_b_size, void* key_b));
void tree_destroy(tree_t* tree);
int tree_add(tree_t* tree, size_t key_size, void* key, size_t data_size, void* data, int copy);
tree_entry_t* tree_find(tree_t* tree, size_t key_size, void* key);
void tree_remove(tree_t* tree, size_t key_size, void* key);
tree_entry_t* tree_first(tree_t* tree);
tree_entry_t* tree_entry_first(tree_entry_t* current);
tree_entry_t* tree_entry_last(tree_entry_t* current);
tree_entry_t* tree_next(tree_entry_t* current);
int tree_entry_depth(tree_entry_t* current);
tree_entry_t* tree_entry_balance(tree_entry_t* current);

#endif // SPIN_TREE_H
