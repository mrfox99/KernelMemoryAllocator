#ifndef _BUDDY_H_
#define _BUDDY_H_

#include <stddef.h>
#include <Windows.h>

#include"myDefs.h"

struct BuddyNode {
	struct BuddyNode* next;
};
struct Buddy {
	void* start;
	struct BuddyNode** array;
	int array_size;
	CRITICAL_SECTION critical_section;
};

void buddy_init(void* address, size_t blocks);
void* buddy_alloc(size_t size_in_blocks);
void buddy_free(void* address, size_t size);
//void buddy_print();
#endif