#ifndef DICTFILE_H
#define DICTFILE_H

/* dict.bin made by tools/mkdict.py: sorted headwords in RAM, articles read on demand */
#define DICT_FILE "dict.bin"
int dict_open(const char *path);
void dict_close(void);
int dict_count(void);
const char *dict_head(int i);
/* first index whose headword is >= prefix */
int dict_lower_bound(const char *prefix);
int dict_find(const char *word);
/* copies the article into buf (NUL-terminated), returns its length */
int dict_read(int i, char *buf, int size);
/* the word's transcription ("to boot": boot's), the first line of its article
   ("/grɑːsp/"); "" and 0 if it has none or the dictionary isn't open */
int dict_ipa(const char *word, char *buf, int size);
/* finds an inflected word ("went", "books", "running"), the base form goes to base */
int dict_lookup(const char *word, char *base, int base_size);

#endif
