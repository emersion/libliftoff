#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "private.h"

struct hwc_layer *hwc_layer_create(struct hwc_output *output)
{
	struct hwc_layer *layer;

	layer = calloc(1, sizeof(*layer));
	if (layer == NULL) {
		return NULL;
	}
	layer->output = output;
	hwc_list_insert(output->layers.prev, &layer->link);
	return layer;
}

void hwc_layer_destroy(struct hwc_layer *layer)
{
	free(layer->props);
	hwc_list_remove(&layer->link);
	free(layer);
}

struct hwc_layer_property *layer_get_property(struct hwc_layer *layer,
					      const char *name)
{
	size_t i;

	for (i = 0; i < layer->props_len; i++) {
		if (strcmp(layer->props[i].name, name) == 0) {
			return &layer->props[i];
		}
	}
	return NULL;
}

void hwc_layer_set_property(struct hwc_layer *layer, const char *name,
			    uint64_t value)
{
	struct hwc_layer_property *props;
	struct hwc_layer_property *prop;

	/* TODO: better error handling */
	if (strcmp(name, "CRTC_ID") == 0) {
		fprintf(stderr, "refusing to set a layer's CRTC_ID\n");
		return;
	}

	prop = layer_get_property(layer, name);
	if (prop == NULL) {
		props = realloc(layer->props, (layer->props_len + 1)
				* sizeof(struct hwc_layer_property));
		if (props == NULL) {
			perror("realloc");
			return;
		}
		layer->props = props;
		layer->props_len++;

		prop = &layer->props[layer->props_len - 1];
		memset(prop, 0, sizeof(*prop));
		strncpy(prop->name, name, sizeof(prop->name) - 1);
	}

	prop->value = value;
}

uint32_t hwc_layer_get_plane_id(struct hwc_layer *layer)
{
	if (layer->plane == NULL) {
		return 0;
	}
	return layer->plane->id;
}
