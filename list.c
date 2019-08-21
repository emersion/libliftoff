#include "list.h"

void hwc_list_init(struct hwc_list *list)
{
	list->prev = list;
	list->next = list;
}

void hwc_list_insert(struct hwc_list *list, struct hwc_list *elm)
{
	elm->prev = list;
	elm->next = list->next;
	list->next = elm;
	elm->next->prev = elm;
}

void hwc_list_remove(struct hwc_list *elm)
{
	elm->prev->next = elm->next;
	elm->next->prev = elm->prev;
	elm->next = NULL;
	elm->prev = NULL;
}

size_t hwc_list_length(const struct hwc_list *list)
{
	struct hwc_list *e;
	size_t count;

	count = 0;
	e = list->next;
	while (e != list) {
		e = e->next;
		count++;
	}

	return count;
}

bool hwc_list_empty(const struct hwc_list *list)
{
	return list->next == list;
}
