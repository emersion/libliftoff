#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "private.h"

struct liftoff_output *liftoff_output_create(struct liftoff_device *device,
					     uint32_t crtc_id)
{
	struct liftoff_output *output;
	ssize_t crtc_index;
	size_t i;

	crtc_index = -1;
	for (i = 0; i < device->crtcs_len; i++) {
		if (device->crtcs[i] == crtc_id) {
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
	output->device = device;
	output->crtc_id = crtc_id;
	output->crtc_index = crtc_index;
	liftoff_list_init(&output->layers);
	liftoff_list_insert(&device->outputs, &output->link);
	return output;
}

void liftoff_output_destroy(struct liftoff_output *output)
{
	liftoff_list_remove(&output->link);
	free(output);
}

void liftoff_output_set_composition_layer(struct liftoff_output *output,
					  struct liftoff_layer *layer)
{
	assert(layer->output == output);
	output->composition_layer = layer;
}
