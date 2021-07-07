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

void output_log_planes(struct liftoff_output *output)
{
	struct liftoff_device *device;
	struct liftoff_plane *plane;
	drmModeObjectProperties *drm_props;
	drmModePropertyRes *drm_prop;
	size_t i;
	int per_line, max_per_line;

	device = output->device;

	if (!log_has(LIFTOFF_DEBUG)) {
		return;
	}

	liftoff_log(LIFTOFF_DEBUG, "Planes on CRTC %"PRIu32":", output->crtc_id);

	liftoff_list_for_each(plane, &device->planes, link) {
		bool active = false;

		if ((plane->possible_crtcs & (1 << output->crtc_index)) == 0) {
			continue;
		}


		drm_props = drmModeObjectGetProperties(device->drm_fd,
			plane->id, DRM_MODE_OBJECT_PLANE);
		if (drm_props == NULL) {
			liftoff_log_errno(LIFTOFF_ERROR, "drmModeObjectGetProperties");
			continue;
		}

		for (i = 0; i < drm_props->count_props; i++) {
			drm_prop = drmModeGetProperty(device->drm_fd, drm_props->props[i]);
			if (drm_prop == NULL) {
				liftoff_log_errno(LIFTOFF_ERROR, "drmModeObjectGetProperties");
				continue;
			}

			if (strcmp(drm_prop->name, "CRTC_ID") == 0
					&& drm_props->prop_values[i] != 0) {
				active = true;
				break;
			}
		}

		struct liftoff_log_buffer log_buf = {0};
		liftoff_log_buffer_append(&log_buf, "  Plane %"PRIu32 "%s", plane->id,
			active ? ":" : " (inactive):");

		max_per_line = active ? 1 : 4;
		per_line = max_per_line - 1;
		for (i = 0; i < drm_props->count_props; i++) {
			uint64_t value = drm_props->prop_values[i];
			char *name;

			if (++per_line == max_per_line) {
				liftoff_log_buffer_append(&log_buf, "\n   ");
				per_line = 0;
			}

			drm_prop = drmModeGetProperty(device->drm_fd,
				drm_props->props[i]);
			if (drm_prop == NULL) {
				liftoff_log_buffer_append(&log_buf, "ERR!");
				continue;
			}

			name = drm_prop->name;

			if (strcmp(name, "type") == 0) {
				liftoff_log_buffer_append(&log_buf, " %s: %s", name,
				value == DRM_PLANE_TYPE_PRIMARY ? "primary" :
				value == DRM_PLANE_TYPE_CURSOR ? "cursor" : "overlay");
				continue;
			}

			if (strcmp(name, "CRTC_X") == 0 || strcmp(name, "CRTC_Y") == 0
					|| strcmp(name, "IN_FENCE_FD") == 0) {
				liftoff_log_buffer_append(&log_buf, " %s: %"PRIi32, name,
					(int32_t)value);
				continue;
			}

			if (strcmp(name, "SRC_W") == 0 || strcmp(name, "SRC_H") == 0) {
				value = value >> 16;
			}
			liftoff_log_buffer_append(&log_buf, " %s: %"PRIu64, name, value);
		}
		liftoff_log_buffer_flush(&log_buf, LIFTOFF_DEBUG);
	}
}

void output_log_layers(struct liftoff_output *output) {
	struct liftoff_layer *layer;
	size_t i;
	bool is_composition_layer;

	if (!log_has(LIFTOFF_DEBUG)) {
		return;
	}

	liftoff_log(LIFTOFF_DEBUG, "Available layers:");
	liftoff_list_for_each(layer, &output->layers, link) {
		if (layer->force_composition) {
			liftoff_log(LIFTOFF_DEBUG, "  Layer %p "
				    "(forced composition):", (void *)layer);
		} else {
			if (!layer_has_fb(layer)) {
				continue;
			}
			is_composition_layer = output->composition_layer == layer;
			liftoff_log(LIFTOFF_DEBUG, "  Layer %p%s:",
				    (void *)layer, is_composition_layer ?
						   " (composition layer)" : "");
		}

		for (i = 0; i < layer->props_len; i++) {
			char *name = layer->props[i].name;
			uint64_t value = layer->props[i].value;

			if (strcmp(name, "CRTC_X") == 0 ||
			    strcmp(name, "CRTC_Y") == 0) {
				liftoff_log(LIFTOFF_DEBUG, "    %s = %"PRIi32,
					    name, (int32_t)value);
			} else if (strcmp(name, "SRC_W") == 0 ||
				   strcmp(name, "SRC_H") == 0) {
				liftoff_log(LIFTOFF_DEBUG, "    %s = %"PRIu64,
					    name, value >> 16);
			} else {
				liftoff_log(LIFTOFF_DEBUG, "    %s = %"PRIu64,
					    name, value);
			}
		}
	}
}
