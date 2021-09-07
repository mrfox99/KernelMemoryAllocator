#include <math.h>

#include "buddy.h"
#include "cache.h"
#include "slab.h"

void buddy_init(void* address, size_t blocks) {
	InitializeCriticalSectionAndSpinCount(&meta_buddy.critical_section, 0x00000400);
	meta_buddy.start = address;
	meta_buddy.array = buddy_array;
	int n = (int)ceil(log2((double)blocks));
	meta_buddy.array_size = n;

	for(int i = meta_buddy.array_size - 1; i >= 0;i--){
		if ((1ULL << i) & blocks) {
			struct BuddyNode* tmp = (struct BuddyNode*)address;
			tmp->next = NULL;
			meta_buddy.array[i] = tmp;
			address = (char*)address + (unsigned long long)BLOCK_SIZE * (1ULL << i);
			blocks -= (1ULL << i);
		}
		else {
			meta_buddy.array[i] = NULL;
		}
	}
}

void* buddy_alloc(size_t size_in_blocks) {
	EnterCriticalSection(&meta_buddy.critical_section);
	if (size_in_blocks == 0) {
		LeaveCriticalSection(&meta_buddy.critical_section);
		return NULL;
	}
	int index = (int)ceil(log2((double)size_in_blocks));
	struct BuddyNode** array = meta_buddy.array;
	int array_size = meta_buddy.array_size;

	if (index >= array_size) {
		LeaveCriticalSection(&meta_buddy.critical_section);
		return NULL;
	}

	if (array[index] != NULL) {
		void* tmp = array[index];
		array[index] = array[index]->next;
		LeaveCriticalSection(&meta_buddy.critical_section);
		return tmp;
	}

	int i;
	for (i = index + 1; i < array_size; i++) {
		if (array[i] != NULL)
			break;
	}
	if (i == array_size) {
		LeaveCriticalSection(&meta_buddy.critical_section);
		return NULL;
	}

	struct BuddyNode* address = array[i];
	array[i] = array[i]->next;

	for (int j = i - 1; j >= index; j--) {
		struct BuddyNode* tmp = (struct BuddyNode*)address;
		address = (struct BuddyNode*)((char*)address + BLOCK_SIZE * (1ULL << j));
		tmp->next = NULL;
		array[j] = tmp;
	}
	LeaveCriticalSection(&meta_buddy.critical_section);
	return address;
}

void buddy_free(void* address, size_t size_in_blocks) {
	EnterCriticalSection(&meta_buddy.critical_section);
	if (size_in_blocks == 0) {
		LeaveCriticalSection(&meta_buddy.critical_section);
		return;
	}
	int index = (int)ceil(log2((double)size_in_blocks));

	struct BuddyNode** array = meta_buddy.array;
	int array_size = meta_buddy.array_size;

	for (size_t i = index; i < array_size; i++) {
		struct BuddyNode* prev = NULL;
		struct BuddyNode* tmp = array[i];
		while (tmp != NULL) {
			if (((char*)address - (char*)tmp) == BLOCK_SIZE * (1ULL << i)
				&& (((char*)tmp - (char*)meta_buddy.start) / (BLOCK_SIZE * (1ULL << i))) % 2 == 0
				&& (((char*)address - (char*)meta_buddy.start) / (BLOCK_SIZE * (1ULL << i))) % 2 == 1) {
				if (prev != NULL)
					prev->next = tmp->next;
				else
					array[i] = array[i]->next;
				address = tmp;
				break;
			}
			if (((char*)tmp - (char*)address) == BLOCK_SIZE * (1ULL << i)
				&& (((char*)tmp - (char*)meta_buddy.start) / (BLOCK_SIZE * (1ULL << i))) % 2 == 1
				&& (((char*)address - (char*)meta_buddy.start) / (BLOCK_SIZE * (1ULL << i))) % 2 == 0) {
				if (prev != NULL)
					prev->next = tmp->next;
				else
					array[i] = array[i]->next;
				break;
			}
			prev = tmp;
			tmp = tmp->next;
		}

		if (tmp == NULL) {
			struct BuddyNode* free_address = (struct BuddyNode*)address;
			free_address->next = array[i];
			array[i] = free_address;
			break;
		}
	}
	LeaveCriticalSection(&meta_buddy.critical_section);
}
