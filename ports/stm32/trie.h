#ifndef TRIE_H
#define TRIE_H

typedef struct time_info
{
    uint16_t length;
    mp_uint_t time;
} time_info_t;

typedef struct trie trie_t;

trie_t* trie_new(void);
void trie_del(trie_t* trie);
void trie_reset(trie_t* trie);
int trie_add(trie_t* trie, const char* word);
bool trie_accept(trie_t* trie, char value, time_info_t* time);
void trie_print(const trie_t* trie);

#endif
