#ifndef _MYDEFS_H_
#define _MYDEFS_H_

#define MIN_BUFFER_SIZE 5//32
#define MAX_BUFFER_SIZE 17//131072

//#define MADE 0
//#define SHRINKED 1
//#define SHRINKED_AND_INCREASED 2
//#define INCREASED 3

#define SHRINKABLE 0
#define UNSHRINKABLE 3

#define NAME_SIZE 21

#define ERROR_INSUFICIENT_MEMORY -1
#define ERROR_INVALID_ADDRESS -2
#define ERROR_UNALIGNED_ADDRESS -3
#define ERROR_OBJECT_FREE -4

#define meta_buddy (*((struct Buddy*)((char*)start_address)))

#define meta_cache (*(struct kmem_cache_s*)((char*)&meta_buddy + sizeof(struct Buddy)))

#define cache_head (*((struct kmem_cache_s**)((char*)&meta_cache + sizeof(struct kmem_cache_s))))

#define buffer_array (((struct kmem_cache_s*)((char*)&cache_head + sizeof(struct kmem_cache_s*))))

#define buddy_array ((struct BuddyNode**)((char*)buffer_array + (MAX_BUFFER_SIZE - MIN_BUFFER_SIZE + 1) * sizeof(struct kmem_cache_s)))


extern void* start_address;
extern int blocks;

#endif