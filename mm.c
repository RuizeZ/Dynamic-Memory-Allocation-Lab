/*-------------------------------------------------------------------
 *  UW CSE 351 Summer 2013 Lab 5 Starter code: 
 *        single doubly-linked free block list with LIFO policy
 *        with support for coalescing adjacent free blocks
 *
 * Terminology:
 * o We will implement an explicit free list allocator
 * o We use "next" and "previous" to refer to blocks as ordered in
 *   the free list.
 * o We use "following" and "preceding" to refer to adjacent blocks
 *   in memory.
 *-------------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "memlib.h"
#include "mm.h"

/* Macros for unscaled pointer arithmetic to keep other code cleaner.  
   Casting to a char* has the effect that pointer arithmetic happens at
   the byte granularity (i.e. POINTER_ADD(0x1, 1) would be 0x2).  (By
   default, incrementing a pointer in C has the effect of incrementing
   it by the size of the type to which it points (e.g. BlockInfo).)
   We cast the result to void* to force you to cast back to the 
   appropriate type and ensure you don't accidentally use the resulting
   pointer as a char* implicitly.  You are welcome to implement your
   own pointer arithmetic instead of using these macros.
*/
#define UNSCALED_POINTER_ADD(p,x) ((void*)((char*)(p) + (x)))
#define UNSCALED_POINTER_SUB(p,x) ((void*)((char*)(p) - (x)))


/******** FREE LIST IMPLEMENTATION ***********************************/


/* A BlockInfo contains information about a block, including the size
   and usage tags, as well as pointers to the next and previous blocks
   in the free list.  This is exactly the "explicit free list" structure
   illustrated in the lecture slides.
   
   Note that the next and prev pointers and the boundary tag are only
   needed when the block is free.  To achieve better utilization, mm_malloc
   should use the space for next and prev as part of the space it returns.

   +--------------+
   | sizeAndTags  |  <-  BlockInfo pointers in free list point here
   |  (header)    |
   +--------------+
   |     next     |  <-  Pointers returned by mm_malloc point here
   +--------------+
   |     prev     |
   +--------------+
   |  space and   |
   |   padding    |
   |     ...      |
   |     ...      |
   +--------------+
   | boundary tag |
   |  (footer)    |
   +--------------+
*/
struct BlockInfo {
  // Size of the block (in the high bits) and tags for whether the
  // block and its predecessor in memory are in use.  See the SIZE()
  // and TAG macros, below, for more details.
  size_t sizeAndTags;
  // Pointer to the next block in the free list.
  struct BlockInfo* next;
  // Pointer to the previous block in the free list.
  struct BlockInfo* prev;
};
typedef struct BlockInfo BlockInfo;


/* Pointer to the first BlockInfo in the free list, the list's head. 
   
   A pointer to the head of the free list in this implementation is
   always stored in the first word in the heap.  mem_heap_lo() returns
   a pointer to the first word in the heap, so we cast the result of
   mem_heap_lo() to a BlockInfo** (a pointer to a pointer to
   BlockInfo) and dereference this to get a pointer to the first
   BlockInfo in the free list. */
#define FREE_LIST_HEAD *((BlockInfo **)mem_heap_lo())

/* Size of a word on this architecture. In a x64 Machine, it is 8 bytes*/
#define WORD_SIZE sizeof(void*)

/* Minimum block size (to account for size header, next ptr, prev ptr,
   and boundary tag) */
#define MIN_BLOCK_SIZE (sizeof(BlockInfo) + WORD_SIZE)

/* Alignment of blocks returned by mm_malloc. */
#define ALIGNMENT 8

/* SIZE(blockInfo->sizeAndTags) extracts the size of a 'sizeAndTags' field.
   Also, calling SIZE(size) selects just the higher bits of 'size' to ensure
   that 'size' is properly aligned.  We align 'size' so we can use the low
   bits of the sizeAndTags field to tag a block as free/used, etc, like this:
   
      sizeAndTags:
      +-------------------------------------------+
      | 63 | 62 | 61 | 60 |  . . . .  | 2 | 1 | 0 |
      +-------------------------------------------+
        ^                                       ^
      high bit                               low bit

   Since ALIGNMENT == 8, we reserve the low 3 bits of sizeAndTags for tag
   bits, and we use bits 3-63 to store the size.

   Bit 0 (2^0 == 1): TAG_USED
   Bit 1 (2^1 == 2): TAG_PRECEDING_USED
*/
#define SIZE(x) ((x) & ~(ALIGNMENT - 1))

/* TAG_USED is the bit mask used in sizeAndTags to mark a block as used. */
#define TAG_USED 1 

/* TAG_PRECEDING_USED is the bit mask used in sizeAndTags to indicate
   that the block preceding it in memory is used. (used in turn for
   coalescing).  If the previous block is not used, we can learn the size
   of the previous block from its boundary tag */
#define TAG_PRECEDING_USED 2

/*show the info of the curent heap*/
int GLobalShow = 0;

/* Print the heap by iterating through it as an implicit free list. */
static void examine_heap() {
  BlockInfo *block;

  /* print to stderr so output isn't buffered and not output if we crash */
  fprintf(stderr, "FREE_LIST_HEAD: %p\n", (void *)FREE_LIST_HEAD);

  for (block = (BlockInfo *)UNSCALED_POINTER_ADD(mem_heap_lo(), WORD_SIZE); /* first block on heap */
      SIZE(block->sizeAndTags) != 0 && block < mem_heap_hi();
      block = (BlockInfo *)UNSCALED_POINTER_ADD(block, SIZE(block->sizeAndTags))) {

    /* print out common block attributes */
    fprintf(stderr, "%p: %ld %ld %ld\t",
    (void *)block,
    SIZE(block->sizeAndTags),
    block->sizeAndTags & TAG_PRECEDING_USED,
    block->sizeAndTags & TAG_USED);

    /* and allocated/free specific data */
    if (block->sizeAndTags & TAG_USED) {
      fprintf(stderr, "ALLOCATED\n");
    } else {
      fprintf(stderr, "FREE\tnext: %p, prev: %p\n",
      (void *)block->next,
      (void *)block->prev);
    }
  }
  printf("block: %p\n", block);
  printf("SIZE(block->sizeAndTags): %ld\n", SIZE(block->sizeAndTags));
  printf("mem_heap_hi(): %p\n", mem_heap_hi());
  fprintf(stderr, "END OF HEAP\n\n");
}


/* Find a free block of the requested size in the free list.  Returns
   NULL if no free block is large enough. */
static void * searchFreeList(size_t reqSize) {   
  BlockInfo* freeBlock;

  freeBlock = FREE_LIST_HEAD;
  while (freeBlock != NULL){
    if (SIZE(freeBlock->sizeAndTags) >= reqSize) {
      return freeBlock;
    } else {
      freeBlock = freeBlock->next;
    }
  }
  return NULL;
}
           
/* Insert freeBlock at the head of the list.  (LIFO) */
static void insertFreeBlock(BlockInfo* freeBlock) {
  //printf("in insertFreeBlock\n");
  // printf("freeBlock %p\n", freeBlock);
  // printf("FREE_LIST_HEAD: %p\n", FREE_LIST_HEAD);
  BlockInfo* oldHead = FREE_LIST_HEAD;
  // printf("oldHead: %p\n", oldHead);
  freeBlock->next = oldHead;
  if (oldHead != NULL) {
    // printf("oldHead next: %p\n", oldHead->next);
    //printf("oldHead not NULL\n");
    oldHead->prev = freeBlock;
  }
  freeBlock->prev = NULL;
  FREE_LIST_HEAD = freeBlock;
}      

/* Remove a free block from the free list. */
static void removeFreeBlock(BlockInfo* freeBlock) {
  BlockInfo *nextFree, *prevFree;
  
  nextFree = freeBlock->next;//nextFree = NULL
  prevFree = freeBlock->prev;//prevFree = null


  // If the next block is not null, patch its prev pointer.
  if (nextFree != NULL) {
    nextFree->prev = prevFree;
  }

  // If we're removing the head of the free list, set the head to be
  // the next block, otherwise patch the previous block's next pointer.
  if (freeBlock == FREE_LIST_HEAD) {
    FREE_LIST_HEAD = nextFree;
    // printf("second FREE_LIST_HEAD: %p\n", FREE_LIST_HEAD);
  } else {

    prevFree->next = nextFree;//0038->next = NULL
  }
}

/* Coalesce 'oldBlock' with any preceeding or following free blocks. */
static void coalesceFreeBlock(BlockInfo* oldBlock) {
  BlockInfo *blockCursor;
  BlockInfo *newBlock;
  BlockInfo *freeBlock;
  // size of old block
  size_t oldSize = SIZE(oldBlock->sizeAndTags);
  // printf("oldSize: %ld\n", oldSize);
  // running sum to be size of final coalesced block
  size_t newSize = oldSize;

  // Coalesce with any preceding free block
  blockCursor = oldBlock;
  while ((blockCursor->sizeAndTags & TAG_PRECEDING_USED)==0) { 
    // While the block preceding this one in memory (not the
    // prev. block in the free list) is free:
  //   if (GLobalShow)
  // {
  //   printf("Coalesce with any preceding free block\n");
  // }
    //
    // Get the size of the previous block from its boundary tag.
    size_t size = SIZE(*((size_t*)UNSCALED_POINTER_SUB(blockCursor, WORD_SIZE)));
    // Use this size to find the block info for that block.
    // printf("free size: %ld\n" ,size);
    freeBlock = (BlockInfo*)UNSCALED_POINTER_SUB(blockCursor, size);
    // printf("free freeBlock: %p\n" ,freeBlock);
    // Remove that block from free list.

    removeFreeBlock(freeBlock);

    // Count that block's size and update the current block pointer.
    newSize += size;
    blockCursor = freeBlock;

  }
  newBlock = blockCursor;
  
  // Coalesce with any following free block.
  // Start with the block following this one in memory
  blockCursor = (BlockInfo*)UNSCALED_POINTER_ADD(oldBlock, oldSize);
  // printf("blockCursor boundary: %p\n", blockCursor);
  // printf("blockCursor->sizeAndTags: %ld\n",blockCursor->sizeAndTags);
  while ((blockCursor->sizeAndTags & TAG_USED)==0) {
    // While the block is free:
  //   if (GLobalShow)
  // {
  //   printf("Coalesce with any following free block.\n"); 
  // }
    //
    size_t size = SIZE(blockCursor->sizeAndTags);
    // Remove it from the free list.
    removeFreeBlock(blockCursor);
    // Count its size and step to the following block.
    newSize += size;
    blockCursor = (BlockInfo*)UNSCALED_POINTER_ADD(blockCursor, size);
  }
  
  // If the block actually grew, remove the old entry from the free
  // list and add the new entry.
  if (newSize != oldSize) {
    // Remove the original block from the free list
    removeFreeBlock(oldBlock);

    // Save the new size in the block info and in the boundary tag
    // and tag it to show the preceding block is used (otherwise, it
    // would have become part of this one!).
    newBlock->sizeAndTags = newSize | TAG_PRECEDING_USED;
    ////printf("newSize: %ld\n", newSize);
    // The boundary tag of the preceding block is the word immediately
    // preceding block in memory where we left off advancing blockCursor.
    *(size_t*)UNSCALED_POINTER_SUB(blockCursor, WORD_SIZE) = newSize | TAG_PRECEDING_USED;  
    //printf("Size after coalesce: %ld\n", SIZE(newBlock->sizeAndTags));
    // Put the new block in the free list.
    // printf("Current block: %p\n", newBlock);
    // printf("          next block: %ld\n", *((size_t*)UNSCALED_POINTER_ADD(newBlock, newSize)));
    insertFreeBlock(newBlock);
    
  }
  return;
}

/* Get more heap space of size at least reqSize. */
static void requestMoreSpace(size_t reqSize) {
  size_t pagesize = mem_pagesize();
  size_t numPages = (reqSize + pagesize - 1) / pagesize;
  BlockInfo *newBlock;
  size_t totalSize = numPages * pagesize;
  size_t prevLastWordMask;
  //printf("totalSize: %ld\n", totalSize);
  void* mem_sbrk_result = mem_sbrk(totalSize);
  // printf("......................mem_sbrk_result: %p\n", mem_sbrk_result);
  if ((size_t)mem_sbrk_result == -1) {
    printf("ERROR: mem_sbrk failed in requestMoreSpace\n");
    exit(0);
  }
  newBlock = (BlockInfo*)UNSCALED_POINTER_SUB(mem_sbrk_result, WORD_SIZE);
  
  
  /* initialize header, inherit TAG_PRECEDING_USED status from the
     previously useless last word however, reset the fake TAG_USED
     bit */
  prevLastWordMask = newBlock->sizeAndTags & TAG_PRECEDING_USED;
  
  
  // if (GLobalShow)
  // {
  //   printf("prevLastWordMask: %ld\n", prevLastWordMask);
  //   printf("newBlock->sizeAndTags: %p\n", &(newBlock->sizeAndTags));
  // }
  newBlock->sizeAndTags = totalSize | prevLastWordMask;
  //examine_heap();   
  // Initialize boundary tag.
  ((BlockInfo*)UNSCALED_POINTER_ADD(newBlock, totalSize - WORD_SIZE))->sizeAndTags = 
    totalSize | prevLastWordMask;

  /* initialize "new" useless last word
     the previous block is free at this moment
     but this word is useless, so its use bit is set
     This trick lets us do the "normal" check even at the end of
     the heap and avoid a special check to see if the following
     block is the end of the heap... */
  *((size_t*)UNSCALED_POINTER_ADD(newBlock, totalSize)) = TAG_USED;
  // Add the new block to the free list and immediately coalesce newly
  // allocated memory space
  // printf("          next block: %ld\n", *((size_t*)UNSCALED_POINTER_ADD(newBlock, totalSize)));
  insertFreeBlock(newBlock);
  // printf("after insert\n");
  // examine_heap();
  coalesceFreeBlock(newBlock);
  // printf("after coalesceFreeBlock\n");
  // examine_heap();

}



/* Initialize the allocator. */
int mm_init () {
  // Head of the free list.
  BlockInfo *firstFreeBlock;

  // Initial heap size: WORD_SIZE byte heap-header (stores pointer to head
  // of free list), MIN_BLOCK_SIZE bytes of space, WORD_SIZE byte heap-footer.
  // initSize: 48
  // MIN_BLOCK_SIZE: 32
  // WORD_SIZE: 8

  size_t initSize = WORD_SIZE+MIN_BLOCK_SIZE+WORD_SIZE;
  size_t totalSize;
  void* mem_sbrk_result = mem_sbrk(initSize);
  //  //printf("mem_sbrk returned %p\n", mem_sbrk_result);
  if ((ssize_t)mem_sbrk_result == -1) {
    printf("ERROR: mem_sbrk failed in mm_init, returning %p\n", 
           mem_sbrk_result);
    exit(1);
  }

  firstFreeBlock = (BlockInfo*)UNSCALED_POINTER_ADD(mem_heap_lo(), WORD_SIZE);

  // Total usable size is full size minus heap-header and heap-footer words
  // NOTE: These are different than the "header" and "footer" of a block!
  // The heap-header is a pointer to the first free block in the free list.
  // The heap-footer is used to keep the data structures consistent (see
  // requestMoreSpace() for more info, but you should be able to ignore it).
  totalSize = initSize - WORD_SIZE - WORD_SIZE;

  // The heap starts with one free block, which we initialize now.
  firstFreeBlock->sizeAndTags = totalSize | TAG_PRECEDING_USED;
  firstFreeBlock->next = NULL;
  firstFreeBlock->prev = NULL;
  // boundary tag
  *((size_t*)UNSCALED_POINTER_ADD(firstFreeBlock, totalSize - WORD_SIZE)) = totalSize | TAG_PRECEDING_USED;
  
  // Tag "useless" word at end of heap as used.
  // This is the is the heap-footer.
  *((size_t*)UNSCALED_POINTER_SUB(mem_heap_hi(), WORD_SIZE - 1)) = TAG_USED;
  // printf("\nfirstFreeBlock next block: %ld\n", *((size_t*)UNSCALED_POINTER_ADD(firstFreeBlock, totalSize)));
  // set the head of the free list to this new free block.
  FREE_LIST_HEAD = firstFreeBlock;
  //examine_heap();
  return 0;
}


// TOP-LEVEL ALLOCATOR INTERFACE ------------------------------------


/* Allocate a block of size size and return a pointer to it. */
void* mm_malloc (size_t size) {
  size_t reqSize;
  BlockInfo * ptrFreeBlock = NULL;
  size_t precedingBlockUseTag;
  size_t oldSize;
  BlockInfo * newBlock = NULL;
  BlockInfo * followingBlock;
  // examine_heap();
  // Zero-size requests get NULL.
  if (size == 0) {
    return NULL;
  }

  // Add one word for the initial size header.
  // Note that we don't need to boundary tag when the block is used!
  size += WORD_SIZE;
  if (size <= MIN_BLOCK_SIZE) {
    // Make sure we allocate enough space for a blockInfo in case we
    // free this block (when we free this block, we'll need to use the
    // next pointer, the prev pointer, and the boundary tag).
    reqSize = MIN_BLOCK_SIZE;
  } else {
    // Round up for correct alignment
    reqSize = ALIGNMENT * ((size + ALIGNMENT - 1) / ALIGNMENT);
  }

  // Implement mm_malloc.  You can change or remove any of the above
  // code.  It is included as a suggestion of where to start.
  // You will want to replace this return statement...
  // printf("reqSize: %ld\n", reqSize);
  AfterRequestMoreSpace:
  if ((ptrFreeBlock = searchFreeList(reqSize)) != NULL)
  { 
    // printf("ptrFreeBlock: %p\n", ptrFreeBlock);
    // examine_heap();
    /*keep the info of the following block*/
    followingBlock = (BlockInfo*)UNSCALED_POINTER_ADD(ptrFreeBlock, SIZE(ptrFreeBlock->sizeAndTags));
    // printf("followingBlock: %p\n", followingBlock);
    oldSize = SIZE(ptrFreeBlock->sizeAndTags);
    // printf("oldSize: %ld\n", oldSize);
    if ((oldSize - reqSize) >= MIN_BLOCK_SIZE)
    {
      // printf("Separate block\n");
      //newBlock header
      newBlock = (BlockInfo*)UNSCALED_POINTER_ADD(ptrFreeBlock, reqSize);
      // printf("newBlock: %p\n",newBlock);
      newBlock->sizeAndTags = (oldSize - reqSize) | TAG_PRECEDING_USED;
      // examine_heap();
      //newBlock->prev = NULL;
      // printf("new size: %ld\n", oldSize - reqSize);

      //boundary tag
      *((size_t*)UNSCALED_POINTER_ADD(newBlock, SIZE(newBlock->sizeAndTags) - WORD_SIZE)) = newBlock->sizeAndTags;
      //examine_heap();

      // printf("insert newBlock: %p\n",newBlock);
      precedingBlockUseTag = (ptrFreeBlock->sizeAndTags) & TAG_PRECEDING_USED;
      ptrFreeBlock->sizeAndTags = reqSize | precedingBlockUseTag;
      insertFreeBlock(newBlock);
      // examine_heap();
      // printf("insert completed\n");
      
      //Save the status of PRECEDING block
      precedingBlockUseTag = (ptrFreeBlock->sizeAndTags) & TAG_PRECEDING_USED;
      //change size to current request, keep both tag
      ptrFreeBlock->sizeAndTags |= TAG_USED;
      
    }
    else
    {
      //no block is needed to add to list
      ptrFreeBlock->sizeAndTags |= TAG_USED;
      followingBlock->sizeAndTags |= TAG_PRECEDING_USED;
    }
    removeFreeBlock(ptrFreeBlock);
    // examine_heap();
    // printf("mm_malloc Compeleted\n");
    return &(ptrFreeBlock->next);
  }

  else
  {
    // printf("start requestMoreSpace\n");
    requestMoreSpace(reqSize);
    //printf("requestMoreSpace Compeleted\n");
    goto AfterRequestMoreSpace;
  }

  return NULL; 
}

/* Free the block referenced by ptr. */
void mm_free (void *ptr) {
  size_t payloadSize;
  BlockInfo * blockInfo;
  BlockInfo * followingBlock;
  // Implement mm_free.  You can change or remove the declaraions
  // above.  They are included as minor hints.
  /*keep the info of the current struct*/
  blockInfo = (BlockInfo*)UNSCALED_POINTER_SUB(ptr, WORD_SIZE);
  //printf("free blockInfo: %p\n", blockInfo);
  blockInfo->sizeAndTags ^= TAG_USED;
  blockInfo->prev = NULL;
  payloadSize = SIZE(blockInfo->sizeAndTags) - WORD_SIZE - WORD_SIZE;
  // set boundary tag
  *((size_t*)UNSCALED_POINTER_ADD(blockInfo, SIZE(blockInfo->sizeAndTags) - WORD_SIZE)) = blockInfo->sizeAndTags;
  /*keep the info of the following block*/
  followingBlock = (BlockInfo*)UNSCALED_POINTER_ADD(blockInfo, SIZE(blockInfo->sizeAndTags));
  //if the following block is not last byte
  if (&followingBlock->sizeAndTags != (size_t*)UNSCALED_POINTER_SUB(mem_heap_hi(), WORD_SIZE - 1))
  {
    //set prev use bit to 0
    followingBlock->sizeAndTags ^= TAG_PRECEDING_USED;
    // if the following block is in the list
    if ((followingBlock->sizeAndTags & TAG_USED) == 0)
    {
      //set boundary tag
      *((size_t*)UNSCALED_POINTER_ADD(followingBlock, SIZE(followingBlock->sizeAndTags) - WORD_SIZE)) = followingBlock->sizeAndTags;
    }
  }
  //if the following block is last byte
  else
  {
    followingBlock->sizeAndTags = TAG_USED;
  }
  insertFreeBlock(blockInfo);
  coalesceFreeBlock(blockInfo);
  // examine_heap();
  // printf("free completed\n");
}


// Implement a heap consistency checker as needed.
int mm_check() {
  return 0;
}

// Extra credit.
void* mm_realloc(void* ptr, size_t size) {
  // ... implementation here ...
  BlockInfo * reallocblockInfo;
  BlockInfo * nextblockInfo;
  BlockInfo * leftblockafterrealloc;
  BlockInfo * followingBlock;
  BlockInfo * new_block;
  size_t oldsize;
  size_t reqSize;
  size_t precedingBlockUseTag;
  size_t extrasize;
  size_t sizewithheader;

  //If ptr is NULL, then the call is equivalent to malloc(size), for all values of size
  if (ptr == NULL)
  {
    mm_malloc(size);
  }
  //if size is equal to zero, and ptr is not NULL, then the call is equivalent to free(ptr).
  else if (size == 0)
  {
    mm_free(ptr);
  }
  else
  {
    //printf("start realloc\n");
    sizewithheader = size + WORD_SIZE;
    // Note that we don't need to boundary tag when the block is used!
    if (sizewithheader <= MIN_BLOCK_SIZE) {
      // Make sure we allocate enough space for a blockInfo in case we
      // free this block (when we free this block, we'll need to use the
      // next pointer, the prev pointer, and the boundary tag).
      reqSize = MIN_BLOCK_SIZE;
    } else {
      // Round up for correct alignment
      reqSize = ALIGNMENT * ((sizewithheader + ALIGNMENT - 1) / ALIGNMENT);
    }
    //printf("reqSize: %ld\n", reqSize);
    reallocblockInfo = (BlockInfo*)UNSCALED_POINTER_SUB(ptr, WORD_SIZE);
    precedingBlockUseTag = reallocblockInfo->sizeAndTags & TAG_PRECEDING_USED;
    // printf("reallocblockInfo: %p\n", reallocblockInfo);
    // examine_heap();
    //printf("Payload of %p: %p -> %p\n", reallocblockInfo, &(reallocblockInfo->next), ((size_t*)UNSCALED_POINTER_ADD(reallocblockInfo, SIZE(reallocblockInfo->sizeAndTags) - 1)));
    //if reqSize > ptr->size
    if (reqSize > SIZE(reallocblockInfo->sizeAndTags))
    {
      extrasize = reqSize - SIZE(reallocblockInfo->sizeAndTags);
      nextblockInfo = (BlockInfo*)UNSCALED_POINTER_ADD(reallocblockInfo, SIZE(reallocblockInfo->sizeAndTags));
      //printf("nextblockInfo: %p\n", nextblockInfo);
      //if the next block is free and size is enough
      if ((nextblockInfo->sizeAndTags & TAG_USED) == 0 && SIZE(nextblockInfo->sizeAndTags) >= extrasize)
      {
        //printf("the next block is free and size is enough\n");
      
        //begin to realloc
        removeFreeBlock(nextblockInfo);

        //change the status of the next block

        //if next block size > extrasize, the size left after the realloc needs to be added back to the free list
        if ((SIZE(nextblockInfo->sizeAndTags) - extrasize) >= MIN_BLOCK_SIZE)
        {
          // printf("the size left after the realloc needs to be added back to the free list\n");
          // examine_heap();
          reallocblockInfo->sizeAndTags = reqSize | precedingBlockUseTag | TAG_USED;
          // printf("reallocblockInfo->sizeAndTags: %ld\n", SIZE(reallocblockInfo->sizeAndTags));
          // printf("reallocblockInfo->sizeAndTags: %ld\n", reallocblockInfo->sizeAndTags);
          //left block header
          leftblockafterrealloc = (BlockInfo*)UNSCALED_POINTER_ADD(reallocblockInfo, SIZE(reallocblockInfo->sizeAndTags));
          //printf("leftblockafterrealloc->sizeAndTags: %p\n", leftblockafterrealloc);
          leftblockafterrealloc->sizeAndTags = (SIZE(nextblockInfo->sizeAndTags) - extrasize) | TAG_PRECEDING_USED;
          //printf("leftblockafterrealloc->sizeAndTags: %ld\n", leftblockafterrealloc->sizeAndTags);
          //boundary tag
          *((size_t*)UNSCALED_POINTER_ADD(leftblockafterrealloc, SIZE(leftblockafterrealloc->sizeAndTags) - WORD_SIZE)) = leftblockafterrealloc->sizeAndTags;
          // printf("insert newBlock: %p\n",newBlock);
          insertFreeBlock(leftblockafterrealloc);
          //examine_heap();
          //printf("Payload of %p: %p -> %p\n", reallocblockInfo, &(reallocblockInfo->next), ((size_t*)UNSCALED_POINTER_ADD(reallocblockInfo, SIZE(reallocblockInfo->sizeAndTags) - 1)));
        }

        //if there is no memory left, change the status of the next block
        else
        {
          reallocblockInfo->sizeAndTags = (SIZE(reallocblockInfo->sizeAndTags) + SIZE(nextblockInfo->sizeAndTags)) | precedingBlockUseTag | TAG_USED;
          followingBlock = (BlockInfo*)UNSCALED_POINTER_ADD(reallocblockInfo, SIZE(reallocblockInfo->sizeAndTags));
          followingBlock->sizeAndTags |= TAG_PRECEDING_USED;
        }
        //printf("&(reallocblockInfo->next): %p\n", &(reallocblockInfo->next));
        return &(reallocblockInfo->next);
      }
      else
      {
        // printf("next block is not free or not enough\n");
        
        new_block = mm_malloc(size);
        memcpy(new_block, &(reallocblockInfo->next), SIZE(reallocblockInfo->sizeAndTags)-WORD_SIZE);
        mm_free(&(reallocblockInfo->next));
        // printf("Payload of %p: %p -> %p\n", new_block, &(new_block->next), ((size_t*)UNSCALED_POINTER_ADD(new_block, SIZE(new_block->sizeAndTags) - 1)));
        // examine_heap();
        return new_block;
      }
      
    }

    //if reqSize < ptr->size
    else if (reqSize < SIZE(reallocblockInfo->sizeAndTags))
    {
      oldsize = SIZE(reallocblockInfo->sizeAndTags);
      //if the block left is big enough to be in the list
      if (oldsize - reqSize >= MIN_BLOCK_SIZE)
      {
        reallocblockInfo->sizeAndTags = reqSize | precedingBlockUseTag | TAG_USED;
        //insert the block
        leftblockafterrealloc = (BlockInfo*)UNSCALED_POINTER_ADD(reallocblockInfo, SIZE(reallocblockInfo->sizeAndTags));
        leftblockafterrealloc->sizeAndTags = (oldsize - reqSize) | TAG_PRECEDING_USED;
        //boundary tag
        *((size_t*)UNSCALED_POINTER_ADD(leftblockafterrealloc, SIZE(leftblockafterrealloc->sizeAndTags) - WORD_SIZE)) = leftblockafterrealloc->sizeAndTags;
        insertFreeBlock(leftblockafterrealloc);
        followingBlock = (BlockInfo*)UNSCALED_POINTER_ADD(leftblockafterrealloc, SIZE(leftblockafterrealloc->sizeAndTags));
        followingBlock->sizeAndTags ^= TAG_PRECEDING_USED;
        if (followingBlock->sizeAndTags & TAG_USED == 0)
        {
          *((size_t*)UNSCALED_POINTER_ADD(followingBlock, SIZE(followingBlock->sizeAndTags) - WORD_SIZE)) = followingBlock->sizeAndTags;
        }
        
        coalesceFreeBlock(leftblockafterrealloc);
      }
      //if there is no memory left, change the status of the next block
      return &(reallocblockInfo->next);
    }
    else
    {
      return &(reallocblockInfo->next);
    }
    
    
  }
}
