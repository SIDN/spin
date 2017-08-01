
#include "tree.h"
#include <stdio.h>

tree_entry_t*
tree_entry_create(size_t key_size, void* key, size_t data_size, void* data, int copy) {
    tree_entry_t* tree_entry = (tree_entry_t*) malloc(sizeof(tree_entry_t));

    if (copy) {
        tree_entry->key = malloc(key_size);
        printf("[XX] created KEY at %p\n", tree_entry->key);
        memcpy(tree_entry->key, key, key_size);
        tree_entry->data = malloc(data_size);
        memcpy(tree_entry->data, data, data_size);
    } else {
        tree_entry->key = key;
        tree_entry->data = data;
    }

    tree_entry->key_size = key_size;
    tree_entry->data_size = data_size;
    tree_entry->parent = NULL;
    tree_entry->left = NULL;
    tree_entry->right = NULL;

    return tree_entry;
}

void tree_entry_destroy(tree_entry_t* tree_entry, int destroy_children) {
    if (tree_entry == NULL) {
        return;
    }

    if (destroy_children) {
        tree_entry_destroy(tree_entry->left, 1);
        tree_entry_destroy(tree_entry->right, 1);
    }

    free(tree_entry->key);
    free(tree_entry->data);
    free(tree_entry);
}

tree_t* tree_create(int (*cmp_func)(size_t key_a_size, void* key_a, size_t key_b_size, void* key_b)) {
    tree_t* tree = (tree_t*) malloc(sizeof(tree_t));
    tree->root = NULL;
    tree->cmp_func = cmp_func;
    return tree;
}

void tree_destroy(tree_t* tree) {
    if (tree == NULL) {
        return;
    }
    tree_entry_destroy(tree->root, 1);
    free(tree);
}

// copies
int tree_add(tree_t* tree, size_t key_size, void* key, size_t data_size, void* data, int copy) {
    tree_entry_t* current;
    tree_entry_t* parent;
    int c;

    if (tree->root == NULL) {
        tree->root = tree_entry_create(key_size, key, data_size, data, copy);
        return 1;
    }
    current = tree->root;
    while (1) {
        c = tree->cmp_func(key_size, key, current->key_size, current->key);
        //printf("[XX] comparing new %s to existing %s: %d\n", key, current->key, c);
        if (c == 0) {
            return 0;
        } else if (c < 0) {
            parent = current;
            current = current->left;
            if (current == NULL) {
                parent->left = tree_entry_create(key_size, key, data_size, data, copy);
                parent->left->parent = parent;
                current = parent->left;
                while (current->parent != NULL) {
                    if (current->parent->left == current) {
                        current->parent->left = tree_entry_balance(current);
                    } else {
                        current->parent->right = tree_entry_balance(current);
                    }
                    current = current->parent;
                }
                tree->root = tree_entry_balance(tree->root);
                return -1;
            }
        } else {
            parent = current;
            current = current->right;
            if (current == NULL) {
                parent->right = tree_entry_create(key_size, key, data_size, data, copy);
                parent->right->parent = parent;
                current = parent->right;
                while (current->parent != NULL) {
                    if (current->parent->left == current) {
                        current->parent->left = tree_entry_balance(current);
                    } else {
                        current->parent->right = tree_entry_balance(current);
                    }
                    current = current->parent;
                }
                tree->root = tree_entry_balance(tree->root);
                return 1;
            }
        }
    }
}

tree_entry_t* tree_find(tree_t* tree, size_t key_size, void* key) {
    tree_entry_t* cur = tree_first(tree);
    int c;
    printf("[XX] looking for key of size %lu\n", key_size);
    while (cur != NULL) {
        c = tree->cmp_func(key_size, key, cur->key_size, cur->key);
        printf("[XX] try %p (cmp: %d)\n", cur, c);
        if (c == 0) {
            printf("[XX] found %p\n", cur);
            return cur;
        } else if (c < 0) {
            if (cur->left) {
                cur = cur->left;
            } else {
                return NULL;
            }
        } else {
            if (cur->right) {
                cur = cur->right;
            } else {
                return NULL;
            }
        }
    }
    return NULL;
}

void tree_remove(tree_t* tree, size_t key_size, void* key) {
    tree_entry_t* tmp;
    tree_entry_t* el = tree_find(tree, key_size, key);
    printf("[XX] found el for %s at %p\n", key, el);
    if (el == NULL) {
        return;
    }
    if (el->parent != NULL) {
        printf("[XX]    par: %s\n", el->parent->key);
    } else {
        printf("[XX]    par: <nul>\n");
    }
    if (el->left != NULL) {
        printf("[XX]    lft: %s\n", el->left->key);
    } else {
        printf("[XX]    lft: <nul>\n");
    }
    if (el->right != NULL) {
        printf("[XX]    rgt: %s\n", el->right->key);
    } else {
        printf("[XX]    rgt: <nul>\n");
    }
    if (el == tree->root) {
        // dunno
        printf("[XX] oh ai\n");
        if (el->left != NULL) {
            tmp = tree_entry_last(el->left);
            tmp->right = el->right;
            if (tmp->right != NULL) {
                tmp->right->parent = tmp;
            }
            tree->root = el->left;
            tree->root->parent = NULL;
        } else if (el->right != NULL) {
            tmp = tree_entry_first(el->right);
            tmp->left = el->left;
            if (tmp->left != NULL) {
                tmp->left->parent = tmp;
            }
            tree->root = el->right;
            tree->root->parent = NULL;
        } else {
            tree->root = NULL;
        }
        tree_entry_destroy(el, 0);
        return;
    }
    int is_left = (el->parent->left == el);
    printf("[XX] is_left? %d\n", is_left);
    if (el->left == NULL && el->right == NULL) {
        if (is_left) {
            el->parent->left = NULL;
        } else {
            el->parent->right = NULL;
        }
        tree_entry_destroy(el, 0);
    } else if (el->right == NULL) {
        // left not null
        if (is_left) {
            el->parent->left = el->left;
            el->left->parent = el->parent;
        } else {
            el->parent->right = el->left;
            el->left->parent = el->parent;
        }
        tree_entry_destroy(el, 0);
    } else if (el->left == NULL) {
        // right not null
        if (is_left) {
            el->parent->right = el->right;
            el->right->parent = el->parent;
        } else {
            el->parent->right = el->right;
            el->right->parent = el->parent;
        }
        tree_entry_destroy(el, 0);
    } else {
        // neither are null
        if (is_left) {
            // todo
            tmp = tree_entry_first(el->right);
            tmp->left = el->left;
            el->parent->left = el->right;
            el->right->parent = el->parent;
        } else {
            tmp = tree_entry_last(el->left);
            tmp->right = el->right;
            el->parent->right = el->left;
            el->right->parent = el->parent;
        }
        tree_entry_destroy(el, 0);
    }
}

tree_entry_t* tree_first(tree_t* tree) {
    tree_entry_t* current;
    if (tree->root == NULL) {
        return NULL;
    }
    current = tree->root;
    while (current->left != NULL) {
        current = current->left;
    }
    return current;
}

tree_entry_t* tree_entry_first(tree_entry_t* current) {
    while (current->left != NULL) {
        current = current->left;
    }
    return current;
}

tree_entry_t* tree_entry_last(tree_entry_t* current) {
    while (current->right != NULL) {
        current = current->right;
    }
    return current;
}

tree_entry_t* tree_next(tree_entry_t* current) {
/*
    if (current == NULL) {
        printf("[XX] tree_next() called. current node: <null>\n");
    } else {
        printf("[XX] tree_next() called. current node: %p (%s)\n", current, current->key);
        printf("[XX] parent: %p\n", current->parent);
        printf("[XX] left: %p\n", current->left);
        printf("[XX] right: %p\n", current->right);
    }
*/
//    printf("[XX] tree_next on %p\n", current);
    if (current->right != NULL) {
//        printf("[Xx] have a right, returning leftmost of %p: %p\n", current->right, tree_entry_first(current->right));
        return tree_entry_first(current->right);
    } else {
//        printf("[XX] no right, finding first 'right' parent\n");
        // find the first parent where this is a left branch of
        while (current != NULL) {
            if (current->parent && current->parent->left == current) {
//                printf("[XX] returning 'right' parent %p\n", current->parent);
                return current->parent;
            }
            current = current->parent;
        }
        return NULL;
    }
}

int tree_entry_depth(tree_entry_t* current) {
    int left = 0;
    int right = 0;
    if (current == NULL) {
        return 0;
    }
    if (current->left) {
        left = tree_entry_depth(current->left);
    }
    if (current->right) {
        right = tree_entry_depth(current->right);
    }
    if (left > right) {
        return 1 + left;
    } else {
        return 1 + right;
    }
}

tree_entry_t* rotate_right(tree_entry_t* q) {
    tree_entry_t* p = q->left;
    if (p != NULL) {
        printf("[XX] p is %s\n", p->key);
        q->left = p->right;
        if (q->left != NULL) {
            p->right->parent = q;
        }
        p->right = q;
    } else {
        q->left = NULL;
    }
    p->parent = q->parent;
    q->parent = p;
    return p;
}

tree_entry_t* rotate_left(tree_entry_t* p) {
    tree_entry_t* q = p->right;
    if (q != NULL) {
        printf("[XX] q is %s\n", q->key);
        p->right = q->left;
        if (q->left != NULL) {
            q->left->parent = p;
        }
        q->left = p;
    } else {
        p->right = NULL;
    }
    q->parent = p->parent;
    p->parent = q;
    return q;
}

tree_entry_t* tree_entry_balance(tree_entry_t* current) {
    int left = tree_entry_depth(current->left);
    int right = tree_entry_depth(current->right);

    tree_entry_t* p;
    tree_entry_t* q;

    if (left > right + 2) {
        printf("[XX] unbalanced left\n");
        return rotate_right(current);
    } else if (right > left + 1) {
        printf("[XX] unbalanced right\n");
        return rotate_left(current);
    } else {
        return current;
    }
}


