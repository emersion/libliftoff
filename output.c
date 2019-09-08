#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "private.h"

struct hwc_output *hwc_output_create(struct hwc_display *display,
				     uint32_t crtc_id)
{
	struct hwc_output *output;
	ssize_t crtc_index;
	size_t i;

	crtc_index = -1;
	for (i = 0; i < display->crtcs_len; i++) {
		if (display->crtcs[i] == crtc_id) {
			crtc_index = i;
			break;
		}
	}
	if (crtc_index < 0) {
		return NULL;
	}

	output = calloc(1, sizeof(*output));
	if (output == NULL) {
		return NULL;
	}
	output->display = display;
	output->crtc_id = crtc_id;
	output->crtc_index = crtc_index;
	hwc_list_init(&output->layers);
	hwc_list_insert(&display->outputs, &output->link);
	return output;
}

void hwc_output_destroy(struct hwc_output *output)
{
	hwc_list_remove(&output->link);
	free(output);
}
