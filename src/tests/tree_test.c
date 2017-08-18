
#include "tree.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>

int
int_cmp(size_t sa, const void* a, size_t sb, const void* b) {
    int* ia = (int*) a;
    int* ib = (int*) b;
    if (*ia > *ib) {
        return 1;
    } else if (*ia < *ib) {
        return -1;
    } else {
        return 0;
    }
}

void
do_int_add(tree_t* tree, int key_and_value) {
    tree_add(tree, sizeof(key_and_value), &key_and_value, sizeof(key_and_value), &key_and_value, 1);
}

void
do_int_remove(tree_t* tree, int key_and_value) {
    tree_remove(tree, sizeof(key_and_value), &key_and_value);
}

void
int_print(size_t s, void* v) {
    (void)s;
    int* i = (int*)v;
    printf("%d", *i);
}

void
do_int_print(tree_t* tree) {
    tree_print(tree, int_print);
}

void
test_empty() {
    tree_t* tree = tree_create(int_cmp);

    assert(tree_empty(tree) == 1);

    do_int_add(tree, 1);
    assert(tree_empty(tree) == 0);

    tree_destroy(tree);
}

void
test_add_nocopy() {
    tree_t* tree = tree_create(int_cmp);

    int ref = 1;
    int size = sizeof(ref);

    int* a = malloc(size);
    int* b = malloc(size);
    int* c = malloc(size);
    int* d = malloc(size);
    *a = 123;
    *b = 66;
    *c = 87;
    *d = 999;

    tree_add(tree, size, a, size, b, 0);
    tree_add(tree, size, c, size, d, 0);

    tree_destroy(tree);

}

void
test_add_single() {
    tree_t* tree = tree_create(int_cmp);

    assert(tree_size(tree) == 0);
    do_int_add(tree, 1);
    assert(tree_size(tree) == 1);
    do_int_add(tree, 1);
    assert(tree_size(tree) == 1);
    do_int_add(tree, 1);
    assert(tree_size(tree) == 1);
    do_int_add(tree, 1);
    assert(tree_size(tree) == 1);
    do_int_add(tree, 1);
    assert(tree_size(tree) == 1);
    do_int_add(tree, 1);
    assert(tree_size(tree) == 1);
    do_int_add(tree, 2);
    assert(tree_size(tree) == 2);
    do_int_add(tree, 1);
    assert(tree_size(tree) == 2);
    do_int_add(tree, 2);
    assert(tree_size(tree) == 2);

    tree_destroy(tree);
}

void
test_add_1() {
    tree_t* tree = tree_create(int_cmp);

    do_int_add(tree, 1);
    do_int_add(tree, 2);
    do_int_add(tree, 3);
    do_int_add(tree, 4);
    do_int_add(tree, 5);
    do_int_add(tree, 6);

    assert(tree_size(tree) == 6);
    assert(tree_entry_depth(tree->root) == 3);

    do_int_add(tree, 7);
    assert(tree_size(tree) == 7);
    assert(tree_entry_depth(tree->root) == 3);

    do_int_add(tree, 8);
    assert(tree_entry_depth(tree->root) == 4);

    do_int_print(tree);
    tree_destroy(tree);
}

void
test_add_2() {
    tree_t* tree = tree_create(int_cmp);

    do_int_add(tree, 8);
    do_int_add(tree, 7);
    do_int_add(tree, 6);
    do_int_add(tree, 5);
    do_int_add(tree, 4);
    do_int_add(tree, 3);

    assert(tree_size(tree) == 6);
    assert(tree_entry_depth(tree->root) == 3);

    do_int_add(tree, 2);
    assert(tree_size(tree) == 7);
    assert(tree_entry_depth(tree->root) == 3);

    do_int_add(tree, 1);
    assert(tree_entry_depth(tree->root) == 4);

    tree_destroy(tree);
}

void
test_add_3() {
    tree_t* tree = tree_create(int_cmp);

    do_int_add(tree, 4);
    do_int_print(tree);
    do_int_add(tree, 8);
    do_int_print(tree);
    do_int_add(tree, 5);
    do_int_print(tree);
    do_int_add(tree, 6);
    do_int_print(tree);
    do_int_add(tree, 7);
    do_int_print(tree);
    do_int_add(tree, 1);
    do_int_print(tree);
    do_int_add(tree, 2);
    do_int_print(tree);
    do_int_add(tree, 3);
    do_int_print(tree);

    assert(tree_entry_depth(tree->root) == 4);
    tree_destroy(tree);
}

void
test_add_4() {
    tree_t* tree = tree_create(int_cmp);

    do_int_add(tree, 0);
    do_int_add(tree, -1);
    do_int_add(tree, 1);
    do_int_add(tree, -2);
    do_int_add(tree, 20);
    do_int_add(tree, 10);
    do_int_add(tree, 11);
    do_int_add(tree, 12);
    do_int_add(tree, 13);
    do_int_print(tree);
    assert(tree_entry_depth(tree->root) == 4);

    tree_destroy(tree);
}

static inline void find_existing(tree_t* tree, int val) {
    tree_entry_t* entry;
    int *fval;

    entry = tree_find(tree, sizeof(val), &val);
    assert(entry != NULL);
    fval = (int*)entry->key;
    assert(val == *fval);
}

static inline void find_nonexisting(tree_t* tree, int val) {
    tree_entry_t* entry;
    entry = tree_find(tree, sizeof(val), &val);
    assert(entry == NULL);
}

void
test_find() {
    tree_t* tree = tree_create(int_cmp);

    find_nonexisting(tree, 1);

    do_int_add(tree, 1);
    do_int_add(tree, 2);
    do_int_add(tree, 3);
    do_int_add(tree, 4);
    do_int_add(tree, 5);
    do_int_add(tree, 6);

    find_existing(tree, 1);
    find_existing(tree, 2);
    find_existing(tree, 3);
    find_existing(tree, 4);
    find_existing(tree, 5);
    find_existing(tree, 6);

    find_nonexisting(tree, 7);
    find_nonexisting(tree, 8);
    find_nonexisting(tree, 9);
    find_nonexisting(tree, -1);
    find_nonexisting(tree, 123);

    tree_destroy(tree);
}

void
test_remove_find() {
    tree_t* tree = tree_create(int_cmp);

    do_int_add(tree, 1);
    do_int_add(tree, 2);
    do_int_add(tree, 3);
    do_int_add(tree, 4);
    do_int_add(tree, 5);
    do_int_add(tree, 6);

    find_existing(tree, 3);
    do_int_remove(tree, 3);
    find_nonexisting(tree, 3);
    do_int_add(tree, 3);
    find_existing(tree, 3);
    do_int_remove(tree, 3);
    find_nonexisting(tree, 3);

    assert(tree_size(tree) == 5);
    do_int_remove(tree, 3);
    find_nonexisting(tree, 3);
    assert(tree_size(tree) == 5);

    tree_destroy(tree);
}

void
test_remove_1() {
    tree_t* tree = tree_create(int_cmp);

    do_int_add(tree, 1);
    do_int_add(tree, 2);
    do_int_add(tree, 3);
    do_int_add(tree, 4);
    do_int_add(tree, 5);
    do_int_add(tree, 6);

    assert(tree_empty(tree) == 0);
    assert(tree_size(tree) == 6);
    do_int_remove(tree, 1);
    assert(tree_size(tree) == 5);
    do_int_remove(tree, 2);
    assert(tree_size(tree) == 4);
    do_int_remove(tree, 3);
    assert(tree_size(tree) == 3);
    do_int_remove(tree, 4);
    assert(tree_size(tree) == 2);
    do_int_remove(tree, 5);
    assert(tree_size(tree) == 1);
    do_int_remove(tree, 6);
    assert(tree_size(tree) == 0);
    assert(tree_empty(tree) == 1);

    tree_destroy(tree);
}

void
test_remove_2() {
    tree_t* tree = tree_create(int_cmp);

    do_int_add(tree, 6);
    do_int_add(tree, 5);
    do_int_add(tree, 4);
    do_int_add(tree, 3);
    do_int_add(tree, 2);
    do_int_add(tree, 1);

    do_int_print(tree);
    assert(tree_empty(tree) == 0);
    assert(tree_size(tree) == 6);
    do_int_remove(tree, 1);
    assert(tree_size(tree) == 5);
    do_int_remove(tree, 2);
    assert(tree_size(tree) == 4);
    do_int_remove(tree, 3);
    assert(tree_size(tree) == 3);
    do_int_remove(tree, 4);
    assert(tree_size(tree) == 2);
    do_int_remove(tree, 5);
    assert(tree_size(tree) == 1);
    do_int_remove(tree, 6);
    assert(tree_size(tree) == 0);
    assert(tree_empty(tree) == 1);

    tree_destroy(tree);
}

void
test_remove_3() {
    tree_t* tree = tree_create(int_cmp);

    do_int_add(tree, 6);
    do_int_add(tree, 5);
    do_int_add(tree, 4);
    do_int_add(tree, 3);
    do_int_add(tree, 2);
    do_int_add(tree, 1);

    do_int_print(tree);
    do_int_remove(tree, 3);
    do_int_remove(tree, 2);
    do_int_remove(tree, 1);
    do_int_remove(tree, 5);
    do_int_remove(tree, 4);
    do_int_remove(tree, 6);
    assert(tree_empty(tree) == 1);

    do_int_add(tree, 1);
    do_int_add(tree, 2);
    do_int_add(tree, 3);
    do_int_add(tree, 4);
    do_int_add(tree, 5);
    do_int_add(tree, 6);

    do_int_remove(tree, 3);
    do_int_remove(tree, 2);
    do_int_remove(tree, 1);
    do_int_remove(tree, 5);
    do_int_remove(tree, 4);
    do_int_remove(tree, 6);
    assert(tree_empty(tree) == 1);

    tree_destroy(tree);
}

void
test_remove_4() {
    tree_t* tree = tree_create(int_cmp);

    do_int_add(tree, 3);
    do_int_add(tree, 2);
    do_int_add(tree, 1);
    assert(tree_size(tree) == 3);

    do_int_remove(tree, 2);
    do_int_remove(tree, 3);
    assert(tree_size(tree) == 1);

    do_int_add(tree, 2);
    do_int_add(tree, 3);
    do_int_add(tree, 4);
    do_int_add(tree, 5);
    do_int_add(tree, 6);
    do_int_add(tree, 7);
    do_int_add(tree, 8);
    assert(tree_size(tree) == 8);

    do_int_remove(tree, 3);
    do_int_remove(tree, 2);
    assert(tree_size(tree) == 6);

    do_int_add(tree, 2);
    do_int_add(tree, 3);
    assert(tree_size(tree) == 8);
    do_int_remove(tree, 2);
    do_int_remove(tree, 3);
    assert(tree_size(tree) == 6);

    do_int_remove(tree, 6);
    do_int_remove(tree, 7);
    assert(tree_size(tree) == 4);

    do_int_add(tree, 6);
    do_int_add(tree, 7);
    assert(tree_size(tree) == 6);
    do_int_remove(tree, 7);
    do_int_remove(tree, 6);

    assert(tree_size(tree) == 4);
    tree_destroy(tree);
}

// fill a tree with 200 values, and randomly
// remove values from it.
// repeat it 20 times
void
test_remove_5() {
    int test_size = 200;
    tree_t* tree;
    tree_entry_t* to_remove;
    int i;
    int rand_i;
    time_t t = time(NULL);
    int count;

    for (count = 0; count < 20; count++) {
        tree = tree_create(int_cmp);
        for (i = 0; i < test_size; i++) {
            do_int_add(tree, i);
        }
        assert(tree_size(tree) == test_size);

        srand(t);
        // until empty, remove a random xth element
        for (i = test_size; i > 0; i--) {
            assert(tree_size(tree) == i);
            rand_i = rand() % i;
            to_remove = tree_first(tree);
            while(rand_i > 1) {
                to_remove = tree_next(to_remove);
                rand_i--;
            }
            tree_remove_entry(tree, to_remove);
        }

        tree_destroy(tree);
    }
}

static inline void check_first(tree_t* tree, int val) {
    tree_entry_t* entry;
    int *fval;

    entry = tree_first(tree);
    assert(entry != NULL);
    fval = (int*)entry->key;
    assert(val == *fval);

}

void
test_first() {
    tree_t* tree = tree_create(int_cmp);

    do_int_add(tree, 6);
    check_first(tree, 6);

    do_int_add(tree, 5);
    check_first(tree, 5);

    do_int_add(tree, 4);
    check_first(tree, 4);

    do_int_add(tree, 3);
    check_first(tree, 3);

    do_int_add(tree, 2);
    check_first(tree, 2);

    do_int_add(tree, 1);
    check_first(tree, 1);

    do_int_add(tree, 10);
    check_first(tree, 1);

    do_int_add(tree, 11);
    check_first(tree, 1);

    tree_destroy(tree);
}

int main(int argc, char** argv) {
    test_empty();
    test_add_single();
    test_add_nocopy();
    test_add_1();
    test_add_2();
    test_add_3();
    test_add_4();
    test_find();
    test_first();
    test_remove_find();
    test_remove_1();
    test_remove_2();
    test_remove_3();
    test_remove_4();
    test_remove_5();
    return 0;
}
