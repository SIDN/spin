
#include <assert.h>

#include "spin_log.h"
#include "tree.h"

tree_entry_t*
tree_entry_create(size_t key_size, void* key, size_t data_size, void* data, int copy) {
    tree_entry_t* tree_entry = (tree_entry_t*) malloc(sizeof(tree_entry_t));

    if (copy) {
        tree_entry->key = malloc(key_size);
        //spin_log(LOG_DEBUG, "[XX] created KEY at %p\n", tree_entry->key);
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

tree_t* tree_create(int (*cmp_func)(size_t key_a_size, const void* key_a, size_t key_b_size, const void* key_b)) {
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
        if (c == 0) {
            // found the exact node, overwrite its data
            if (copy) {
                if (current->data != NULL) {
                    free(current->data);
                }
                current->data = malloc(data_size);
                memcpy(current->data, data, data_size);
                current->data_size = data_size;
            } else {
                current->data = data;
                current->data_size = data_size;
            }
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

tree_entry_t* tree_find(tree_t* tree, size_t key_size, const void* key) {
    tree_entry_t* current;
    int c;

    current = tree->root;
    while (current != NULL) {
        c = tree->cmp_func(key_size, key, current->key_size, current->key);
        if (c == 0) {
            return current;
        } else if (c < 0) {
            if (current->left) {
                current = current->left;
            } else {
                return NULL;
            }
        } else {
            if (current->right) {
                current = current->right;
            } else {
                return NULL;
            }
        }
    }
    return NULL;
}

static inline void
elv(tree_entry_t* e) {

    if (e == NULL) {
        spin_log(LOG_DEBUG, "<0>");
    } else {
        int* v = (int*)e->key;
        spin_log(LOG_DEBUG, "%d", *v);
    }
}

static inline void
elp(tree_entry_t* e, const char* txt) {
    spin_log(LOG_DEBUG, "[XX] %s: ", txt);
    elv(e);
    spin_log(LOG_DEBUG, "\n");
    spin_log(LOG_DEBUG, "     parent: ");
    elv(e->parent);
    spin_log(LOG_DEBUG, "\n");
    spin_log(LOG_DEBUG, "       left: ");
    elv(e->left);
    spin_log(LOG_DEBUG, "\n");
    spin_log(LOG_DEBUG, "      right: ");
    elv(e->right);
    spin_log(LOG_DEBUG, "\n");
}

void tp(const char* msg, tree_entry_t* e) {
    int* v;
    v = e->key;
    if (e != NULL) {
        spin_log(LOG_DEBUG, "%s %d\n", msg, *v);
    } else {
        spin_log(LOG_DEBUG, "%s <nil>\n", msg);
    }
}

void tree_remove_entry(tree_t* tree, tree_entry_t* el) {
    tree_entry_t* tmp, *btmp;
    if (el == NULL) {
        return;
    }
    if (el == tree->root) {
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
                // note; this case should not be reached since
                // the above code makes it rotate left upon root
                // removal
                tmp->left->parent = tmp;
            }
            tree->root = el->right;
            tree->root->parent = NULL;
        } else {
            tree->root = NULL;
        }
        tree_entry_destroy(el, 0);
        if (tree->root != NULL) {
            tree->root = tree_entry_balance(tree->root);
        }
        return;
    } else {
        int is_left = (el->parent->left == el);
        btmp = el->parent;
        //spin_log(LOG_DEBUG, "[XX] is_left? %d\n", is_left);
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
                // note; this case should not be reached since
                // the above code makes it rotate left upon node
                el->parent->right = el->left;
                el->left->parent = el->parent;
            }
            tree_entry_destroy(el, 0);
        } else if (el->left == NULL) {
            // right not null
            if (is_left) {
                el->parent->left = el->right;
                el->right->parent = el->parent;
            } else {
                el->parent->right = el->right;
                el->right->parent = el->parent;
            }
            tree_entry_destroy(el, 0);
        } else {
            // neither are null;
            // replace element to remove with the smallest of its
            // right side
            tmp = tree_entry_first(el->right);
            tmp->left = el->left;
            tmp->left->parent = tmp;
            // a few extra steps if the smallest is not the direct
            // right child
            if (tmp != el->right) {
                tmp->parent->left = tmp->right;
                if (tmp->right != NULL) {
                    tmp->right->parent = tmp->parent;
                }
                tmp->right = el->right;
                tmp->right->parent = tmp;
            }
            tmp->parent = el->parent;
            if (is_left) {
                tmp->parent->left = tmp;
            } else {
                tmp->parent->right = tmp;
            }
            tree_entry_destroy(el, 0);
        }
        (void)btmp;
        while (btmp != NULL) {
            if (is_left && btmp->left != NULL) {
                btmp->left = tree_entry_balance(btmp->left);
            } else if (btmp->right != NULL) {
                btmp->right = tree_entry_balance(btmp->right);
            }
            btmp = btmp->parent;
        }
        tree->root = tree_entry_balance(tree->root);
    }
}

void tree_remove(tree_t* tree, size_t key_size, void* key) {
    tree_entry_t* el = tree_find(tree, key_size, key);
    return tree_remove_entry(tree, el);
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
        spin_log(LOG_DEBUG, "[XX] tree_next() called. current node: <null>\n");
    } else {
        spin_log(LOG_DEBUG, "[XX] tree_next() called. current node: %p (%s)\n", current, current->key);
        spin_log(LOG_DEBUG, "[XX] parent: %p\n", current->parent);
        spin_log(LOG_DEBUG, "[XX] left: %p\n", current->left);
        spin_log(LOG_DEBUG, "[XX] right: %p\n", current->right);
    }
*/
//    spin_log(LOG_DEBUG, "[XX] tree_next on %p\n", current);
    if (current->right != NULL) {
//        spin_log(LOG_DEBUG, "[Xx] have a right, returning leftmost of %p: %p\n", current->right, tree_entry_first(current->right));
        return tree_entry_first(current->right);
    } else {
//        spin_log(LOG_DEBUG, "[XX] no right, finding first 'right' parent\n");
        // find the first parent where this is a left branch of
        while (current != NULL) {
            if (current->parent && current->parent->left == current) {
//                spin_log(LOG_DEBUG, "[XX] returning 'right' parent %p\n", current->parent);
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
        int* val;
        val = current->key;
        if (current == current->right) {
            spin_log(LOG_DEBUG, "[XX] error, right of %d is %d\n", *val, *val);
            assert(0);
        }
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
    //int *xxp;
    if (p != NULL) {
        //xxp = (int*)p->key;
        //spin_log(LOG_DEBUG, "[XX] p is %d\n", *xxp);
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
    //int* xxq, *xxp;
    //xxp = (int*)p->key;
    //spin_log(LOG_DEBUG, "[XX] p is %d\n", *xxp);
    if (q != NULL) {
        //xxq = (int*)q->key;
        //spin_log(LOG_DEBUG, "[XX] q is %d\n", *xxq);
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
/*
    return current;
}
tree_entry_t* AAAAtree_entry_balance(tree_entry_t* current) {
*/
    int left = tree_entry_depth(current->left);
    int right = tree_entry_depth(current->right);

    if (left > right + 1) {
        //spin_log(LOG_DEBUG, "[XX] unbalanced left (l: %d r: %d)\n", left, right);
        left = tree_entry_depth(current->left->left);
        right = tree_entry_depth(current->left->right);
        if (right > left) {
            current->left = rotate_left(current->left);
        }
        return rotate_right(current);
    } else if (right > left + 1) {
        //spin_log(LOG_DEBUG, "[XX] unbalanced right (l: %d r: %d)\n", left, right);
        left = tree_entry_depth(current->right->left);
        right = tree_entry_depth(current->right->right);
        if (left > right) {
            current->right = rotate_right(current->right);
        }
        return rotate_left(current);
    } else {
        return current;
    }
}

int
tree_empty(tree_t* tree) {
    return (tree->root == NULL);
}

int
tree_size(tree_t* tree) {
    int result = 0;
    tree_entry_t* cur = tree_first(tree);
    while (cur != NULL) {
        result++;
        cur = tree_next(cur);
    }
    return result;
}

static inline int get_node_depth(tree_entry_t* entry) {
    int depth = 0;
    while(entry->parent != NULL) {
        entry = entry->parent;
        depth++;
    }
    return depth;
}

void
tree_entry_print(tree_entry_t* entry, void(*print_func)(size_t size, void*key)) {
    int depth = get_node_depth(entry);
    int i;
    for (i = 0; i < depth; i++) {
        spin_log(LOG_DEBUG, "   ");
    }
    print_func(entry->key_size, entry->key);
    //spin_log(LOG_DEBUG, " (d: %d", tree_entry_depth(entry));
    /*
    if (entry->parent != NULL) {
        spin_log(LOG_DEBUG, " p: ");
        print_func(entry->parent->key_size, entry->parent->key);
    }
    */
    //spin_log(LOG_DEBUG, ")");
    spin_log(LOG_DEBUG, "\n");
    if (entry->left != NULL) {
        tree_entry_print(tree_entry_last(entry->left), print_func);
    }
    if (entry->parent != NULL && entry->parent->right == entry) {
        tree_entry_print(entry->parent, print_func);
    }
}

void
tree_print(tree_t* tree, void(*print_func)(size_t size, void*key)) {
    spin_log(LOG_DEBUG, "[tree] size: %d\n", tree_size(tree));
    if (tree->root != NULL) {
        tree_entry_print(tree_entry_last(tree->root), print_func);
    }
    spin_log(LOG_DEBUG, "[end of tree]\n");
}

void
tree_clear(tree_t* tree) {
    tree_entry_destroy(tree->root, 1);
    tree->root = NULL;
}
