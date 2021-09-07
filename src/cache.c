#define _CRT_SECURE_NO_WARNINGS

#include <string.h>
#include <math.h>

#include "cache.h"
#include "slab.h"
#include "buddy.h"
#include "myDefs.h"


void kmem_cache_s_init(struct kmem_cache_s* handle, const char* name, size_t object_size, void (*ctor)(void*), void (*dtor)(void*)) {
	strncpy(handle->name, name, 20);
	handle->object_size = object_size;
	handle->slab_size = 1ULL << (long long unsigned)ceil(log2(ceil(((double)(sizeof(struct kmem_cache_s) + handle->object_size) / (double)BLOCK_SIZE))));
	handle->objects_in_slab = (handle->slab_size * BLOCK_SIZE - sizeof(struct Slab)) / handle->object_size;
	handle->slabs_free = NULL;
	handle->slabs_full = NULL;
	handle->slabs_partial = NULL;
	handle->ctor = ctor;
	handle->dtor = dtor;
	handle->shrink_info = SHRINKABLE;
	handle->error_code = 0;
	handle->next = NULL;
	handle->tmp_chache_offset = 0;
	int g = sizeof(struct Slab);
	handle->max_chache_offset = (size_t)floor((double)(handle->slab_size * BLOCK_SIZE - sizeof(struct Slab) - handle->objects_in_slab * handle->object_size) / (double)CACHE_L1_LINE_SIZE);
	InitializeCriticalSectionAndSpinCount(&handle->critical_section, 0x00000400);
}

int remove_object_from_slab(struct Slab* slab, size_t objects_in_slab, size_t object_size, const void* objp, void (*dtor)(void*)) {
	if (!(slab->start_address <= objp && (char*)objp < (char*)slab->start_address + objects_in_slab * object_size))
		return ERROR_INVALID_ADDRESS;

	if (((char*)slab->start_address - (char*)objp) % object_size != 0) {
		return ERROR_UNALIGNED_ADDRESS;
	}

	struct SlabNode* tmp = slab->head;
	while (tmp != NULL) {
		if (((void*)tmp <= objp && (char*)objp < (char*)(tmp)+object_size))
			return ERROR_OBJECT_FREE;
		tmp = tmp->next;
	}
	if (dtor != NULL)
		dtor((void*)objp);

	struct SlabNode* slot = (struct SlabNode*)objp;
	slot->next = slab->head;
	slab->head = slot;
	slab->number_of_free_objects++;
	return 0;
}

int free_object_from_cache(struct kmem_cache_s* cachep, const void* objp) {
	struct Slab* prev = NULL;
	struct Slab* tmp = cachep->slabs_full;
	while (tmp != NULL) {
		int status = remove_object_from_slab(tmp, cachep->objects_in_slab, cachep->object_size, objp, cachep->dtor);
		if (status == ERROR_OBJECT_FREE || status == ERROR_UNALIGNED_ADDRESS)
			return status;
		if (status == 0) {
			if (prev == NULL)
				cachep->slabs_full = tmp->next;
			else
				prev->next = tmp->next;
			if (tmp->number_of_free_objects != cachep->objects_in_slab) {
				tmp->next = cachep->slabs_partial;
				cachep->slabs_partial = tmp;
			}
			else {
				tmp->next = cachep->slabs_free;
				cachep->slabs_free = tmp;
			}
			return 0;
		}
		tmp = tmp->next;
	}

	prev = NULL;
	tmp = cachep->slabs_partial;
	while (tmp != NULL) {
		int status = remove_object_from_slab(tmp, cachep->objects_in_slab, cachep->object_size, objp, cachep->dtor);
		if (status == ERROR_OBJECT_FREE || status == ERROR_UNALIGNED_ADDRESS) {
			return status;
		}
		if (status == 0) {
			if (tmp->number_of_free_objects == cachep->objects_in_slab) {
				if (prev == NULL)
					cachep->slabs_partial = tmp->next;
				else
					prev->next = tmp->next;
				tmp->next = cachep->slabs_free;
				cachep->slabs_free = tmp;
				return 0;
			}
		}
		tmp = tmp->next;
	}
	return ERROR_INVALID_ADDRESS;
}