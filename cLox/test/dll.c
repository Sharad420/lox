#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Node {
    char *data;
    struct Node *prev;
    struct Node *next;
}Node;

typedef struct {
    Node *head;
    Node *tail;
    int size;
}DLList;


// Helper functions

static Node *make_node(const char *s) {
    Node *n = malloc(sizeof(Node));
    if (!n) {
        perror("malloc");
        exit(1);
    }

    n->data = strdup(s); /*Heap allocated string, now ownership of memory is with list, so it is outside the lifetime of stack frame. Have to manually free it too.*/
    if (!n->data) {
        perror("strdup");
        exit(1);
    }
    n->prev = n->next = NULL;
    return n;
}

static void free_node(Node *n) {
    free(n->data);
    free(n);
}

/* API */

DLList *dllist_new(void) {
    DLList *l = calloc(1, sizeof(DLList));
    if (!l) {
        perror("calloc");
        exit(1);
    }
    return l;
}

/*Insert at tail */
void dllist_insert(DLList *l, const char *s) {
    Node *n = make_node(s);
    if (!l->tail) {
        l->head = l->tail = n;
    } else {
        n->prev = l->tail;
        l->tail->next = n;
        l->tail = n;
    }
    l->size++;
}

/*Insert at head*/
void dllist_insert_front(DLList *l, const char *s) {
    Node *n = make_node(s);
    if (!l->head) {
        l->tail = l->head = n;
    } else {
        n->next = l->head;
        l->head->prev = n;
        l->head = n;
    }
    l->size++;
} 

Node *dllist_find(DLList* l, const char *s) {
    for (Node *cur = l->head; cur; cur = cur->next) {
        if (strcmp(cur->data, s) == 0) {
            return cur;
        }
    }
    return NULL;
} 

int dllist_delete(DLList *l, const char *s) {
    Node *n = dllist_find(l, s);
    if (!n) return 0;

    if (n->prev) {
        n->prev->next = n->next;
    } else {
        l->head = n->next;
    }

    if (n->next) {
        n->next->prev = n->prev;
    } else {
        l->tail = n->prev;
    }

    free_node(n);
    l->size--;
    return 1;
}

void dllist_free(DLList *l) {
    Node *cur = l->head;
    while (cur) {
        Node *next = cur->next;
        free_node(cur);
        cur = next;
    }

    free(l);
}


void dlist_print_fwd(const DLList *l) {
    printf("fwd [%d]: ", l->size);
    for (Node *c = l->head; c; c = c->next)
        printf("%s%s", c->data, c->next ? " <-> " : "");
    printf("\n");
}

void dlist_print_bwd(const DLList *l) {
    printf("bwd [%d]: ", l->size);
    for (Node *c = l->tail; c; c = c->prev)
        printf("%s%s", c->data, c->prev ? " <-> " : "");
    printf("\n");
}

int main(void) {
    DLList *l = dllist_new();

    dllist_insert(l, "A");
    dllist_insert(l, "B");
    dllist_insert_front(l, "C");

    dlist_print_fwd(l);
    dlist_print_bwd(l);

    dllist_delete(l, "B");
    dlist_print_fwd(l);

    dllist_free(l);
    return 0;
}