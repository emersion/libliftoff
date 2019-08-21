#ifndef LIST_H
#define LIST_H

#include <stdbool.h>
#include <stddef.h>

struct hwc_list {
	struct hwc_list *prev;
	struct hwc_list *next;
};

void hwc_list_init(struct hwc_list *list);
void hwc_list_insert(struct hwc_list *list, struct hwc_list *elm);
void hwc_list_remove(struct hwc_list *elm);
size_t hwc_list_length(const struct hwc_list *list);
bool hwc_list_empty(const struct hwc_list *list);

#define hwc_container_of(ptr, sample, member)				\
	(__typeof__(sample))((char *)(ptr) -				\
			     offsetof(__typeof__(*sample), member))

#define hwc_list_for_each(pos, head, member)				\
	for (pos = hwc_container_of((head)->next, pos, member);		\
	     &pos->member != (head);					\
	     pos = hwc_container_of(pos->member.next, pos, member))

#define hwc_list_for_each_safe(pos, tmp, head, member)			\
	for (pos = hwc_container_of((head)->next, pos, member),		\
	     tmp = hwc_container_of(pos->member.next, tmp, member);	\
	     &pos->member != (head);					\
	     pos = tmp,							\
	     tmp = hwc_container_of(pos->member.next, tmp, member))

#endif
