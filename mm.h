#include <stdio.h>

extern int mm_init (void);
extern void *mm_malloc (size_t size, int);
extern void mm_free (void *ptr, int);

// Extra credit
extern void* mm_realloc(void* ptr, size_t size);
