#include <stdio.h>
#include <string.h>

#include "../common/glue_dll.h"

typedef struct data_
{
    int val1;
    int val2;
    glue_t glue;
} data_t;

int comp_val1(void *d1, void *d2)
{
    data_t *p1 = (data_t *)d1;
    if (p1 == NULL)
    {
        return -1;
    }

    data_t *p2 = (data_t *)d2;
    if (p1 == NULL)
    {
        return 1;
    }

    if (p1->val1 == p2->val1)
    {
        return 0;
    }

    if (p1->val1 < p2->val1)
    {
        return -1;
    }

    return 1;
}

#define offset(struct_name, fld_name) \
    (unsigned long)&(((struct_name *)0)->fld_name)

GLSTRUCT_TO_STRUCT(gldata_to_data, data_t, glue)

int main()
{
    {
        data_t data[5];
        memset(data, 0, sizeof(data_t) * 5);
        data[0].val1 = 1;
        data[0].val2 = 2;

        data[1].val1 = 3;
        data[1].val2 = 4;

        data[2].val1 = 5;
        data[2].val2 = 6;

        data[3].val1 = 7;
        data[3].val2 = 8;

        data[4].val1 = 9;
        data[4].val2 = 10;

        glue_t base_glthread;
        init_glue(&base_glthread);

        list_priority_insert(&base_glthread, &data[4].glue, comp_val1, offset(data_t, glue));
        list_priority_insert(&base_glthread, &data[3].glue, comp_val1, offset(data_t, glue));
        list_priority_insert(&base_glthread, &data[2].glue, comp_val1, offset(data_t, glue));
        list_priority_insert(&base_glthread, &data[1].glue, comp_val1, offset(data_t, glue));
        list_priority_insert(&base_glthread, &data[0].glue, comp_val1, offset(data_t, glue));

        glue_t *curr = list_get_first(&base_glthread);
        glue_t *next = NULL;
        for (; curr != NULL; curr = next)
        {
            next = curr->right;

            data_t *p = gldata_to_data(curr);
            printf("val1 = %d\n", p->val1);
        }
    }
    
    return 0; 
}