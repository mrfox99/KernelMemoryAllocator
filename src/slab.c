#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <Windows.h>

#include "slab.h"
#include "buddy.h"
#include "myDefs.h"
#include "cache.h"

void kmem_init(void* space, int block_num) {
	start_address = space;
	blocks = block_num;

	size_t space_needed = sizeof(struct Buddy)
		+ sizeof(struct kmem_cache_s) //meta cache
		+ sizeof(struct kmem_cache_s*) //cache head
		+ sizeof(struct kmem_cache_s) * (MAX_BUFFER_SIZE - MIN_BUFFER_SIZE + 1) //buffers
		+ block_num * sizeof(struct BuddyNode*); //array of BuddyNode*, upper bound for array size
						
	size_t blocks_needed = space_needed / BLOCK_SIZE + 1; //aproximation
	if (block_num <= blocks_needed) {
		printf("Not enough space for meta data");
		exit(1);
	}
	void* address = ((char*)start_address + blocks_needed * BLOCK_SIZE);
	buddy_init(address, block_num - blocks_needed);
	kmem_cache_s_init(&meta_cache, "Meta cache", sizeof(struct kmem_cache_s), NULL, NULL);
	cache_head = NULL;
	for (int i = 0; i < MAX_BUFFER_SIZE - MIN_BUFFER_SIZE + 1; i++) {
		char name[NAME_SIZE];
		sprintf(name, "size-%d", (int)pow(2, 5 + i));
		kmem_cache_s_init(buffer_array + i, name, 1ULL << (MIN_BUFFER_SIZE + i), NULL, NULL);
	}
}

kmem_cache_t* kmem_cache_create(const char* name, size_t size, void (*ctor)(void*), void (*dtor)(void*)) {// provera za psotjece ime
	EnterCriticalSection(&meta_cache.critical_section);

	if (size == 0 || strlen(name) > 20) {
		LeaveCriticalSection(&meta_cache.critical_section);
		return NULL;
	}

	for (int i = 0; i < MAX_BUFFER_SIZE - MIN_BUFFER_SIZE + 1; i++) {
		char* n = buffer_array[i].name;
		if (!strcmp(buffer_array[i].name, name)) {
			return NULL;
		}
	}

	struct kmem_cache_s* tmp = cache_head;
	while (tmp != NULL) {
		if (!strcmp(tmp->name, name)) {
			return NULL;
		}
		tmp = tmp->next;
	}

	if (size < sizeof(struct SlabNode))
		size = sizeof(struct SlabNode);

	struct kmem_cache_s* handle = (struct kmem_cache_s*)kmem_cache_alloc(&meta_cache);
	
	if (handle == NULL) {
		LeaveCriticalSection(&meta_cache.critical_section);
		return NULL;
	}

	kmem_cache_s_init(handle, name, size, ctor, dtor);

	handle->next = cache_head;
	cache_head = handle;

	LeaveCriticalSection(&meta_cache.critical_section);
	return handle;
}

int kmem_cache_shrink(kmem_cache_t* cachep) {
	EnterCriticalSection(&cachep->critical_section);

	int n = 0;
	if (cachep->shrink_info == SHRINKABLE && cachep->slabs_free != NULL) {
		struct Slab* tmp = cachep->slabs_free;
		while (tmp != NULL) {
			struct Slab* prev = tmp;
			tmp = tmp->next;
			buddy_free(prev->start_address, cachep->slab_size);
			n++;
		}
		cachep->slabs_free = NULL;
	}
	cachep->shrink_info = SHRINKABLE;
	LeaveCriticalSection(&cachep->critical_section);
	return n;
}

void* kmem_cache_alloc(kmem_cache_t* cachep) {
	EnterCriticalSection(&cachep->critical_section);

	if (cachep->slabs_partial != NULL) {
		void* ret = cachep->slabs_partial->head;
		cachep->slabs_partial->head = cachep->slabs_partial->head->next;
		cachep->slabs_partial->number_of_free_objects--;
		if (cachep->slabs_partial->number_of_free_objects == 0) {
			struct Slab* tmp = cachep->slabs_partial;
			cachep->slabs_partial = cachep->slabs_partial->next;
			tmp->next = cachep->slabs_full;
			cachep->slabs_full = tmp;
		}
		cachep->error_code = 0;
		if (cachep->ctor != NULL)
			cachep->ctor(ret);

		LeaveCriticalSection(&cachep->critical_section);
		return ret;
	}
	if (cachep->slabs_free != NULL) {
		void* ret = cachep->slabs_free->head;
		cachep->slabs_free->head = cachep->slabs_free->head->next;
		cachep->slabs_free->number_of_free_objects--;
		if (cachep->slabs_free->number_of_free_objects != 0) {
			struct Slab* tmp = cachep->slabs_free;
			cachep->slabs_free = cachep->slabs_free->next;
			tmp->next = cachep->slabs_partial;
			cachep->slabs_partial = tmp;
		}
		else {
			struct Slab* tmp = cachep->slabs_free;
			cachep->slabs_free = cachep->slabs_free->next;
			tmp->next = cachep->slabs_full;
			cachep->slabs_full = tmp;
		}
		cachep->error_code = 0;
		if (cachep->ctor != NULL)
			cachep->ctor(ret);

		LeaveCriticalSection(&cachep->critical_section);
		return ret;
	}

	
	void* address = (struct Slab*)buddy_alloc(cachep->slab_size);

	if (address == NULL) {
		cachep->error_code = ERROR_INSUFICIENT_MEMORY;
		LeaveCriticalSection(&cachep->critical_section);
		return NULL;
	}

	//for (int i = 0; i < cachep->slab_size * BLOCK_SIZE; i++) {
	//	((char*)address)[i] = 6;
	//}

	struct Slab* slab = (struct Slab*)((char*)address + cachep->slab_size * BLOCK_SIZE - sizeof(struct Slab));
	slab->next = NULL;
	
	slab->start_address = (char*)address;
	slab->head = (struct SlabNode*)((char*)slab->start_address + cachep->tmp_chache_offset * CACHE_L1_LINE_SIZE); //L1
	if(cachep->max_chache_offset)
		cachep->tmp_chache_offset = (cachep->tmp_chache_offset + 1) % cachep->max_chache_offset;
	slab->number_of_free_objects = cachep->objects_in_slab;

	size_t number_of_objects = cachep->objects_in_slab;
	size_t object_size = cachep->object_size;
	struct SlabNode* tmp = slab->head;

	for (int i = 0; i < number_of_objects - 1; i++) {
		tmp->next = (struct SlabNode*)((char*)tmp + object_size);
		tmp = tmp->next;
	}
	tmp->next = NULL;

	struct SlabNode* ret = slab->head;
	slab->head = slab->head->next;
	slab->number_of_free_objects--;
	if (slab->number_of_free_objects == 0) {
		slab->next = cachep->slabs_full;
		cachep->slabs_full = slab;
	}
	else {
		slab->next = cachep->slabs_partial;
		cachep->slabs_partial = slab;
	}
	cachep->error_code = 0;

	/*if (cachep->shrink_info == MADE)
		cachep->shrink_info = INCREASED;

	if (cachep->shrink_info == SHRINKED)
		cachep->shrink_info = SHRINKED_AND_INCREASED;*/

	cachep->shrink_info = UNSHRINKABLE;

	if (cachep->ctor != NULL)
		cachep->ctor(ret);

	LeaveCriticalSection(&cachep->critical_section);
	return ret;
}

void kmem_cache_free(kmem_cache_t* cachep, void* objp) {
	EnterCriticalSection(&cachep->critical_section);

	cachep->error_code = free_object_from_cache(cachep, objp);

	LeaveCriticalSection(&cachep->critical_section);
	return;
}

void* kmalloc(size_t size) {
	size_t power = (size_t)ceil(log2((double)size));
	if (power > MAX_BUFFER_SIZE) {
		return NULL;
	}

	if (power < MIN_BUFFER_SIZE)
		power = MIN_BUFFER_SIZE;

	return kmem_cache_alloc(buffer_array + power - MIN_BUFFER_SIZE);
}

void kfree(const void* objp) {
	for (int i = 0; i < MAX_BUFFER_SIZE - MIN_BUFFER_SIZE + 1; i++) {
		EnterCriticalSection(&buffer_array[i].critical_section);
		int status = free_object_from_cache(buffer_array + i, objp);
		LeaveCriticalSection(&buffer_array[i].critical_section);
		if (status == 0 || status == ERROR_OBJECT_FREE || status == ERROR_UNALIGNED_ADDRESS) {
			buffer_array[i].error_code = status;
			return;
		}	
	}
	return;
}

void kmem_cache_destroy(kmem_cache_t* cachep) {
	EnterCriticalSection(&meta_cache.critical_section);

	struct kmem_cache_s* prev = NULL;
	struct kmem_cache_s* curr = cache_head;
	while (curr != NULL) {
		if (curr == cachep)
			break;
		prev = curr;
		curr = curr->next;
	}
	if (curr == NULL) {
		LeaveCriticalSection(&meta_cache.critical_section);
		return;
	}

	if (prev != NULL)
		prev->next = curr->next;
	else 
		cache_head = curr->next;// trebalo bi cache_head = curr->next; cachep = curr->next
	struct Slab* tmp = cachep->slabs_full;
	while (tmp) {
		struct Slab* prev = tmp;
		tmp = tmp->next;
		buddy_free(prev->start_address, cachep->slab_size);
	}
	tmp = cachep->slabs_partial;
	while (tmp) {
		struct Slab* prev = tmp;
		tmp = tmp->next;
		buddy_free(prev->start_address, cachep->slab_size);
	}
	tmp = cachep->slabs_free;
	while (tmp) {
		struct Slab* prev = tmp;
		tmp = tmp->next;
		buddy_free(prev->start_address, cachep->slab_size);
	}
	kmem_cache_free(&meta_cache, cachep);

	LeaveCriticalSection(&meta_cache.critical_section);
}

void kmem_cache_info(kmem_cache_t* cachep) {
	EnterCriticalSection(&cachep->critical_section);

	printf("Name: %s\n", cachep->name);
	printf("Object size: %zu\n", cachep->object_size);
	printf("Slab size in blocks: %zu\n", cachep->slab_size);
	printf("Maximum number of objects in slab: %zu\n", cachep->objects_in_slab);
	printf("Has constructor: %s\n", cachep->ctor ? "yes" : "no");
	printf("Has destructor: %s\n", cachep->dtor ? "yes" : "no");

	LeaveCriticalSection(&cachep->critical_section);
}

int kmem_cache_error(kmem_cache_t* cachep) {
	EnterCriticalSection(&cachep->critical_section);

	switch (cachep->error_code){
	case ERROR_INSUFICIENT_MEMORY:
		printf("Not enough memory for allocation\n");
		break;
	case ERROR_INVALID_ADDRESS:
		printf("Invalid address for deallocation\n");
		break;
	case ERROR_UNALIGNED_ADDRESS:
		printf("Adress is not aligned\n");
		break;
	case ERROR_OBJECT_FREE:
		printf("Object is already free\n");
		break;
	}

	LeaveCriticalSection(&cachep->critical_section);

	return cachep->error_code;	
}