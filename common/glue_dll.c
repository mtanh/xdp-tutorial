#include "glue_dll.h"

#include <stdio.h>
#include <stdlib.h>

void init_glue(glue_t *gl)
{
    gl->left = NULL;
    gl->right = NULL;
}

bool is_empty(glue_t *gl)
{
    if (gl == NULL)
    {
        fprintf(stderr, "%s: invalid glue. Mark the list is empty.\n", __func__);
        return true;
    }

    return (gl->left == NULL && gl->right == NULL);
}

unsigned int count(glue_t *gl)
{
    unsigned int count = 0;
    glue_t *basePtr = list_get_first(gl);
    glue_t *nextPtr = NULL;

    for (; basePtr != NULL; basePtr = nextPtr)
    {
        nextPtr = basePtr->right;
        count++;
    }

    return count;
}

void list_add_next(glue_t *glBase, glue_t *glNew)
{
    if (glBase == NULL || glNew == NULL)
    {
        fprintf(stderr, "%s: invalid glue.\n", __func__);
        return;
    }

    if (glBase->right == NULL)
    {
        glBase->right = glNew;
        glNew->left = glBase;
        return;
    }

    glue_t *temp = glBase->right;
    glBase->right = glNew;
    glNew->left = glBase;
    glNew->right = temp;
    temp->left = glNew;
}

void list_add_before(glue_t *glBase, glue_t *glNew)
{
    if (glBase == NULL || glNew == NULL)
    {
        fprintf(stderr, "%s: invalid glue.\n", __func__);
        return;
    }

    if (glBase->left == NULL)
    {
        glNew->left = NULL;
        glNew->right = glBase;
        glBase->left = glNew;
        return;
    }

    glue_t *temp = glBase->left;
    temp->right = glNew;
    glNew->left = temp;
    glNew->right = glBase;
    glBase->left = glNew;
}

void list_add_last(glue_t *glBase, glue_t *glNew)
{
    if (glBase == NULL || glNew == NULL)
    {
        fprintf(stderr, "%s: invalid glue.\n", __func__);
        return;
    }

    glue_t *basePtr = list_get_first(glBase);
    glue_t *prevPtr = NULL;
    glue_t *nextPtr = NULL;

    for (; basePtr != NULL; basePtr = nextPtr)
    {
        nextPtr = basePtr->right;
        prevPtr = basePtr;
    }

    if (prevPtr)
    {
        list_add_next(prevPtr, glNew);
    }
    else
    {
        list_add_next(glBase, glNew);
    }
}

glue_t *list_dequeue(glue_t *gl)
{
    if (gl == NULL)
    {
        fprintf(stderr, "%s: invalid glue.\n", __func__);
        return NULL;
    }

    glue_t *temp;
    if (gl->right == NULL)
    {
        return NULL;
    }

    temp = gl->right;
    list_remove(temp);
    return temp;
}

glue_t *list_get_first(glue_t *gl)
{
    if (gl == NULL)
    {
        fprintf(stderr, "%s: invalid glue.\n", __func__);
        return NULL;
    }

    return gl->right;
}

void *get_user_data_from_offset(glue_t *gl, unsigned long offset)
{
    if (gl == NULL)
    {
        fprintf(stderr, "%s: invalid glue.\n", __func__);
        return NULL;
    }

    return (void *)((char *)(gl)-offset);
}

void list_remove(glue_t *gl)
{
    if (gl == NULL)
    {
        fprintf(stderr, "%s: invalid glue.\n", __func__);
        return;
    }

    // Node has no base.
    if (gl->left == NULL)
    {
        if (gl->right)
        {
            gl->right->left = NULL;
            gl->right = NULL;
            return;
        }

        return;
    }

    // Node is the last node in the list.
    if (gl->right == NULL)
    {
        gl->left->right = NULL;
        gl->left = NULL;
        return;
    }

    // Node is middle one.
    gl->left->right = gl->right;
    gl->right->left = gl->left;
    gl->left = 0;
    gl->right = 0;
}

void list_delete_all(glue_t *gl)
{
    if (gl == NULL)
    {
        fprintf(stderr, "%s: invalid glue.\n", __func__);
        return;
    }

    glue_t *basePtr = list_get_first(gl);
    glue_t *nextPtr = NULL;

    for (; basePtr != NULL; basePtr = nextPtr)
    {
        nextPtr = basePtr->right;
        list_remove(basePtr);
    }
}

void list_priority_insert(glue_t *glBase,
                          glue_t *glNew,
                          int (*comp_fn)(void *, void *),
                          unsigned long offset)
{
    if (glBase == NULL || glNew == NULL)
    {
        fprintf(stderr, "%s: invalid glue.\n", __func__);
        return;
    }

    glue_t *currPtr = NULL;
    glue_t *prevPtr = NULL;
    glue_t *nextPtr = NULL;

    init_glue(glNew);

    if (is_empty(glBase))
    {
        list_add_next(glBase, glNew);
        return;
    }

    // There is only one existing node in the list.
    if (glBase->right && glBase->right->right == NULL)
    {
        // If the node `glNew` has a higher priority than the existing node,
        // then add it at the right after the `glBase` node.
        if (comp_fn(get_user_data_from_offset(glNew, offset),
                    get_user_data_from_offset(glBase->right, offset)) > 0)
        {
            list_add_next(glBase, glNew);
        }
        else
        {
            list_add_next(glBase->right, glNew);
        }

        return;
    }

    currPtr = list_get_first(glBase);
    for (; currPtr != NULL; currPtr = nextPtr)
    {
        nextPtr = currPtr->right;

        if (comp_fn(get_user_data_from_offset(glNew, offset),
                    get_user_data_from_offset(currPtr, offset)) <= 0)
        {
            prevPtr = currPtr;
            continue;
        }

        if (prevPtr == NULL)
        {
            list_add_next(glBase, glNew);
        }
        else
        {
            list_add_next(prevPtr, glNew);
        }

        return;
    }

    // Add in the end.
    list_add_next(prevPtr, glNew);
}
