#include <stdlib.h>
#include <string.h>
#include "private.h"

struct liftoff_layer *liftoff_layer_create(struct liftoff_output *output)
{
	struct liftoff_layer *layer;

	layer = calloc(1, sizeof(*layer));
	if (layer == NULL) {
		liftoff_log_errno(LIFTOFF_ERROR, "calloc");
		return NULL;
	}
	layer->output = output;
	liftoff_list_insert(output->layers.prev, &layer->link);
	output->layers_changed = true;
	return layer;
}

void liftoff_layer_destroy(struct liftoff_layer *layer)
{
	if (layer == NULL) {
		return;
	}

	layer->output->layers_changed = true;
	if (layer->plane != NULL) {
		layer->plane->layer = NULL;
	}
	if (layer->output->composition_layer == layer) {
		layer->output->composition_layer = NULL;
	}
	free(layer->props);
	liftoff_list_remove(&layer->link);
	free(layer);
}

struct liftoff_layer_property *layer_get_property(struct liftoff_layer *layer,
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

void liftoff_layer_set_property(struct liftoff_layer *layer, const char *name,
				uint64_t value)
{
	struct liftoff_layer_property *props;
	struct liftoff_layer_property *prop;

	/* TODO: better error handling */
	if (strcmp(name, "CRTC_ID") == 0) {
		liftoff_log(LIFTOFF_ERROR,
			    "refusing to set a layer's CRTC_ID");
		return;
	}

	prop = layer_get_property(layer, name);
	if (prop == NULL) {
		props = realloc(layer->props, (layer->props_len + 1)
				* sizeof(struct liftoff_layer_property));
		if (props == NULL) {
			liftoff_log_errno(LIFTOFF_ERROR, "realloc");
			return;
		}
		layer->props = props;
		layer->props_len++;

		prop = &layer->props[layer->props_len - 1];
		memset(prop, 0, sizeof(*prop));
		strncpy(prop->name, name, sizeof(prop->name) - 1);

		layer->changed = true;
	}

	prop->value = value;

	if (strcmp(name, "FB_ID") == 0 && layer->force_composition) {
		layer->force_composition = false;
		layer->changed = true;
	}
}

void liftoff_layer_set_fb_composited(struct liftoff_layer *layer)
{
	if (layer->force_composition) {
		return;
	}

	liftoff_layer_set_property(layer, "FB_ID", 0);

	layer->force_composition = true;
	layer->changed = true;
}

struct liftoff_plane *liftoff_layer_get_plane(struct liftoff_layer *layer)
{
	return layer->plane;
}

bool liftoff_layer_needs_composition(struct liftoff_layer *layer)
{
	if (!layer_is_visible(layer)) {
		return false;
	}
	return layer->plane == NULL;
}

void layer_get_rect(struct liftoff_layer *layer, struct liftoff_rect *rect)
{
	struct liftoff_layer_property *x_prop, *y_prop, *w_prop, *h_prop;

	x_prop = layer_get_property(layer, "CRTC_X");
	y_prop = layer_get_property(layer, "CRTC_Y");
	w_prop = layer_get_property(layer, "CRTC_W");
	h_prop = layer_get_property(layer, "CRTC_H");

	rect->x = x_prop != NULL ? x_prop->value : 0;
	rect->y = y_prop != NULL ? y_prop->value : 0;
	rect->width = w_prop != NULL ? w_prop->value : 0;
	rect->height = h_prop != NULL ? h_prop->value : 0;
}

bool layer_intersects(struct liftoff_layer *a, struct liftoff_layer *b)
{
	struct liftoff_rect ra, rb;

	layer_get_rect(a, &ra);
	layer_get_rect(b, &rb);

	return ra.x < rb.x + rb.width && ra.y < rb.y + rb.height &&
	       ra.x + ra.width > rb.x && ra.y + ra.height > rb.y;
}

void layer_mark_clean(struct liftoff_layer *layer)
{
	size_t i;

	layer->changed = false;

	for (i = 0; i < layer->props_len; i++) {
		layer->props[i].prev_value = layer->props[i].value;
	}
}

static void log_priority(struct liftoff_layer *layer)
{
	if (layer->current_priority == layer->pending_priority) {
		return;
	}

	liftoff_log(LIFTOFF_DEBUG, "Layer %p priority change: %d -> %d",
		    (void *)layer, layer->current_priority,
		    layer->pending_priority);
}

void layer_update_priority(struct liftoff_layer *layer, bool make_current) {
	struct liftoff_layer_property *prop;

	/* TODO: also bump priority when updating other properties */
	prop = layer_get_property(layer, "FB_ID");
	if (prop != NULL && prop->prev_value != prop->value) {
		layer->pending_priority++;
	}

	if (make_current) {
		log_priority(layer);
		layer->current_priority = layer->pending_priority;
		layer->pending_priority = 0;
	}
}

bool layer_has_fb(struct liftoff_layer *layer) {
	struct liftoff_layer_property *fb_id_prop;

	fb_id_prop = layer_get_property(layer, "FB_ID");
	return fb_id_prop != NULL && fb_id_prop->value != 0;
}

bool layer_is_visible(struct liftoff_layer *layer)
{
	struct liftoff_layer_property *alpha_prop;

	alpha_prop = layer_get_property(layer, "alpha");
	if (alpha_prop != NULL && alpha_prop->value == 0) {
		return false; /* fully transparent */
	}

	if (layer->force_composition) {
		return true;
	} else {
		return layer_has_fb(layer);
	}
}
