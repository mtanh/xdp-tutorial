#ifndef __GLUE_DLL__
#define __GLUE_DLL__

#include <stdbool.h>

// API for the glue way double link.

typedef struct glue_
{
    struct glue_ *left;
    struct glue_ *right;
} glue_t;

void init_glue(glue_t *gl);

// Check if the list is empty base on the node is holding the glue `gl`.
// Return:
// - false: if the list is not empty.
// - true: if the list is empty.
bool is_empty(glue_t *gl);

// Count the number of node follows the node is holding the glue `gl`.
unsigned int count(glue_t *gl);

// Add glue `glNew` into the right side of glue `glBase`.
void list_add_next(glue_t *glBase, glue_t *glNew);

// Add glue `glNew` into the left side of glue `glBase`.
void list_add_before(glue_t *glBase, glue_t *glNew);

// Add glue `glNew` into the most right side of list from glue `glBase`.
void list_add_last(glue_t *glBase, glue_t *glNew);

// Return the node at the right side of node is holding glue `gl`.
// The returned node will be removed from the list.
glue_t *list_dequeue(glue_t *gl);

// Return the right after node of the node is holding the glue `gl`.
glue_t *list_get_first(glue_t *gl);

// Extract struct's user data by substracting glue member from the struct.
void *get_user_data_from_offset(glue_t *gl, unsigned long offset);

// Remove the node is holding the glue `gl` from the list.
// There are three situations when removing the node from the list:
// - Node is going to be removed is the base node of the list.
// - Node is going to be removed is the last node of the list.
// - Node is going to be removed is the middle node.
void list_remove(glue_t *gl);

// Remove all nodes from the node is holding glue `gl`.
void list_delete_all(glue_t *gl);

// Add node is holding glue `glNew` node into the list base on its priority.
// The node's priority is calculated by function `comp_fn`.
// `comp_fn` returns:
// - 0: if the nodes are equal.
// - -1: if the first node has slower priority than the second one.
// - 1: if the first node has higher priority than the second one.
void list_priority_insert(glue_t *glBase,
                          glue_t *glNew,
                          int (*comp_fn)(void *, void *),
                          unsigned long offset);

#define GLSTRUCT_TO_STRUCT(fn_name, structure_name, field_name)                                 \
    static inline structure_name *fn_name(glue_t *gl)                                           \
    {                                                                                           \
        return (structure_name *)((char *)(gl) - (char *)&(((structure_name *)0)->field_name)); \
    }

#endif /* __GLUE_DLL__ */
