#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "trie.h"

typedef struct trie_node
{
    char value;
    bool end;
    struct trie_node* children;
    size_t children_len;
} trie_node_t;

struct trie
{
    trie_node_t* root_node;
    trie_node_t* current_node;
    trie_node_t** node_stack;
    time_info_t* time_stack;
    size_t stack_size;
    uint16_t index;
};

void trie_node_del(trie_node_t* node)
{
    for (size_t index = 0; index < node->children_len; index++)
        trie_node_del(&node->children[index]);
    m_del(trie_node_t, node->children, node->children_len);
}

static int trie_node_add(trie_node_t* node, const char* word, size_t depth)
{
    if (*word == '\0')
    {
        node->end = true;
        return depth;
    }

    for (size_t index = 0; index < node->children_len; index++)
        if (node->children[index].value == *word)
            return trie_node_add(&node->children[index], ++word, depth + 1);

    trie_node_t new_node =
    {
        .value = *word,
        .end = false,
        .children = NULL,
        .children_len = 0
    };

    trie_node_add(&new_node, ++word, depth + 1);

    node->children = m_renew(trie_node_t, node->children, node->children_len, ++node->children_len);

    if (node->children == NULL)
        return -1;

    node->children[node->children_len - 1] = new_node;

    return depth + 1;
}

static void trie_node_print(const trie_node_t* node, size_t depth)
{
    for (size_t index = 0; index < depth; index++)
        printf("\t");
    if (node->end)
        printf("[%c]\n", node->value);
    else
        printf("%c\n", node->value);
    for (size_t index = 0; index < node->children_len; index++)
        trie_node_print(&node->children[index], depth + 1);
}

trie_t* trie_new()
{
    trie_t* trie = m_new(trie_t, 1);
    memset(trie, 0, sizeof(trie_t));

    if (trie != NULL)
    {
        trie->root_node = m_new(trie_node_t, 1);
        trie->current_node = trie->root_node;

        memset(trie->root_node, 0, sizeof(trie_node_t));
    }
    return trie;
}

void trie_del(trie_t* trie)
{
    trie_node_del(trie->root_node);
    m_del(trie_node_t, trie->root_node, 1);

    if (trie->node_stack != NULL)
        m_del(trie_node_t, trie->node_stack, trie->stack_size);

    if (trie->time_stack != NULL)
        m_del(time_info_t, trie->time_stack, trie->stack_size - 1);

    m_del(trie_t, trie, 1);
}

int trie_add(trie_t* trie, const char* word)
{
    int depth = trie_node_add(trie->root_node, word, 0);
    if (depth < 0)
        return -1;
    
    if ((depth + 1) > trie->stack_size)
    {
        trie->time_stack = m_renew(time_info_t, trie->time_stack, (trie->stack_size == 0) ? (0) : (trie->stack_size - 1), depth);
        if (trie->time_stack == NULL)
            return -1;

        trie->node_stack = m_renew(trie_node_t*, trie->node_stack, trie->stack_size, depth + 1);
        if (trie->node_stack == NULL)
            return -1;

        trie->stack_size = depth + 1;
    }
    
    return 0;
}

void trie_reset(trie_t* trie)
{
    trie->current_node = trie->root_node;
    trie->index = 0;
}

bool trie_accept(trie_t* trie, char value, time_info_t* time)
{
    trie->time_stack[trie->index] = *time;

    for (size_t index = 0; index < trie->current_node->children_len; index++)
    {
        if (trie->current_node->children[index].value == value)
        {
            trie->node_stack[trie->index++] = trie->current_node;
            trie->current_node = &trie->current_node->children[index];
            
            bool found = trie->current_node->end && trie->current_node->children_len == 0;
            if (found)
            {
                *time = trie->time_stack[trie->index - 1];
                time->index++;
            }

            return found;
        }
    }

    while (!trie->current_node->end && trie->index > 0)
        trie->current_node = trie->node_stack[--trie->index];

    bool found = trie->current_node->end;
    if (found)
    {
        *time = trie->time_stack[trie->index - 1];
        time->index++;
    }

    return found;
}

void trie_print(const trie_t* trie)
{
    trie_node_print(trie->root_node, 0);
}
