/* salvo-libc: heap allocation.
 *
 * First-fit free list over an sbrk arena, 16-byte alignment throughout.
 * The header union forces max_align_t alignment so payloads satisfy the
 * strictest fundamental alignment on all supported LP64 ABIs. v0.1 is
 * single-threaded; a lock belongs to the threading drop. */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef union salvo_block salvo_block;
union salvo_block
{
  struct
  {
    size_t size; /* payload bytes, excluding this header */
    int free;
    salvo_block *next;
  } meta;
  max_align_t _align;
};

#define SALVO_ALIGN 16

static salvo_block *salvo_heap_head;

static size_t
salvo_align_up (size_t n)
{
  return (n + (SALVO_ALIGN - 1)) & ~(size_t)(SALVO_ALIGN - 1);
}

static unsigned char *
salvo_block_end (salvo_block *block)
{
  return (unsigned char *)(block + 1) + block->meta.size;
}

static salvo_block *
salvo_heap_grow (size_t payload)
{
  size_t total = sizeof (salvo_block) + payload;
  salvo_block *block;

  if (total < payload)
    { /* addition wrapped */
      errno = ENOMEM;
      return NULL;
    }
  block = (salvo_block *)sbrk ((ptrdiff_t)total);
  if (block == (void *)-1)
    return NULL; /* sbrk already set errno */
  block->meta.size = payload;
  block->meta.free = 0;
  block->meta.next = NULL;
  return block;
}

void *
malloc (size_t size)
{
  salvo_block **link = &salvo_heap_head;
  salvo_block *block;

  if (size == 0 || size > SALVO_MALLOC_MAX)
    return NULL;
  size = salvo_align_up (size);
  for (block = salvo_heap_head; block != NULL; block = block->meta.next)
    {
      if (block->meta.free && block->meta.size >= size)
        {
          /* Split when the remainder fits a header plus one minimum
           * payload; otherwise hand out the whole block. */
          if (block->meta.size >= size + sizeof (salvo_block) + SALVO_ALIGN)
            {
              salvo_block *rest
                  = (salvo_block *)((unsigned char *)(block + 1) + size);
              rest->meta.size = block->meta.size - size - sizeof (salvo_block);
              rest->meta.free = 1;
              rest->meta.next = block->meta.next;
              block->meta.next = rest;
              block->meta.size = size;
            }
          block->meta.free = 0;
          return (void *)(block + 1);
        }
      link = &block->meta.next;
    }
  block = salvo_heap_grow (size);
  if (block == NULL)
    return NULL;
  *link = block;
  return (void *)(block + 1);
}

void
free (void *ptr)
{
  salvo_block *block;

  if (ptr == NULL)
    return;
  block = ((salvo_block *)ptr) - 1;
  block->meta.free = 1;
  /* Coalesce forward-adjacent free runs; the arena grows strictly by
   * sbrk, so physically adjacent blocks are list-adjacent. Stay on the
   * merged block so chains of three or more collapse in one call. */
  {
    salvo_block *b = salvo_heap_head;
    while (b != NULL && b->meta.next != NULL)
      {
        if (b->meta.free && b->meta.next->meta.free
            && salvo_block_end (b) == (unsigned char *)b->meta.next)
          {
            b->meta.size += sizeof (salvo_block) + b->meta.next->meta.size;
            b->meta.next = b->meta.next->meta.next;
          }
        else
          {
            b = b->meta.next;
          }
      }
  }
}

void *
calloc (size_t nmemb, size_t size)
{
  size_t total;
  void *ptr;

  if (nmemb != 0 && size > (size_t)-1 / nmemb)
    {
      errno = ENOMEM;
      return NULL;
    }
  total = nmemb * size;
  ptr = malloc (total);
  if (ptr != NULL)
    memset (ptr, 0, salvo_align_up (total));
  return ptr;
}

void *
realloc (void *ptr, size_t size)
{
  salvo_block *block;
  void *fresh;
  size_t keep;

  if (ptr == NULL)
    return malloc (size);
  if (size == 0)
    {
      free (ptr);
      return NULL;
    }
  block = ((salvo_block *)ptr) - 1;
  if (salvo_align_up (size) <= block->meta.size)
    return ptr;
  fresh = malloc (size);
  if (fresh == NULL)
    return NULL;
  keep = block->meta.size < size ? block->meta.size : size;
  memcpy (fresh, ptr, keep);
  free (ptr);
  return fresh;
}
