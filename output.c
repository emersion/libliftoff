#include <assert.h>
#include <inttypes.h>
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
	if (output == NULL) {
		return;
	}

	liftoff_list_remove(&output->link);
	free(output);
}

void liftoff_output_set_composition_layer(struct liftoff_output *output,
					  struct liftoff_layer *layer)
{
	assert(layer->output == output);
	if (layer != output->composition_layer) {
		output->layers_changed = true;
	}
	output->composition_layer = layer;
}

void output_log_layers(struct liftoff_output *output) {
	struct liftoff_layer *layer;
	size_t i;

	if (!log_has(LIFTOFF_DEBUG)) {
		return;
	}

	liftoff_log(LIFTOFF_DEBUG, "Layers on CRTC %"PRIu32":", output->crtc_id);
	liftoff_list_for_each(layer, &output->layers, link) {
		liftoff_log(LIFTOFF_DEBUG, "  Layer %p:", (void *)layer);
		for (i = 0; i < layer->props_len; i++) {
			liftoff_log(LIFTOFF_DEBUG, "    %s = %"PRIu64,
				    layer->props[i].name,
				    layer->props[i].value);
		}
	}
}
