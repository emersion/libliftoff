#include <stdlib.h>
#include <string.h>
#include "private.h"

struct hwc_output *hwc_output_create(struct hwc_display *display,
				     uint32_t crtc_id)
{
	struct hwc_output *output;

	output = calloc(1, sizeof(*output));
	if (output == NULL) {
		return NULL;
	}
	output->display = display;
	output->crtc_id = crtc_id;
	hwc_list_init(&output->layers);
	hwc_list_insert(&display->outputs, &output->link);
	return output;
}

void hwc_output_destroy(struct hwc_output *output)
{
	hwc_list_remove(&output->link);
	free(output);
}
