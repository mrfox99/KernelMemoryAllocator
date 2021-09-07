#ifndef _CACHE_H_
#define _CACHE_H_

#include <Windows.h>

#include "myDefs.h"


struct SlabNode {
	struct SlabNode* next;
};
struct Slab {
	void* start_address;
	struct Slab* next;
	struct SlabNode* head;
	size_t number_of_free_objects;
};

struct kmem_cache_s {
	char name[NAME_SIZE];
	size_t object_size;
	size_t slab_size; //in blocks
	size_t objects_in_slab;
	struct Slab* slabs_free;
	struct Slab* slabs_full;
	struct Slab* slabs_partial;
	void (*ctor)(void*);
	void (*dtor)(void*);
	int error_code;
	int shrink_info;
	struct kmem_cache_s* next;
	size_t tmp_chache_offset;
	size_t max_chache_offset;
	CRITICAL_SECTION critical_section;
};

void kmem_cache_s_init(struct kmem_cache_s* handle, const char* name, size_t size, void (*ctor)(void*), void (*dtor)(void*));

// int remove_object_from_slab(struct Slab* slab, size_t objects_in_slab, size_t object_size, const void* objp);

int free_object_from_cache(struct kmem_cache_s* cachep,const void* objp);
#endif 
