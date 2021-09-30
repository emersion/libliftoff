#ifndef PRIVATE_H
#define PRIVATE_H

#include <libliftoff.h>
#include "list.h"
#include "log.h"

/* Layer priority is assigned depending on the number of updates during a
 * given number of page-flips */
#define LIFTOFF_PRIORITY_PERIOD 60

struct liftoff_device {
	int drm_fd;

	struct liftoff_list planes; /* liftoff_plane.link */
	struct liftoff_list outputs; /* liftoff_output.link */

	uint32_t *crtcs;
	size_t crtcs_len;

	int page_flip_counter;
	int test_commit_counter;
};

struct liftoff_output {
	struct liftoff_device *device;
	uint32_t crtc_id;
	size_t crtc_index;
	struct liftoff_list link; /* liftoff_device.outputs */

	struct liftoff_layer *composition_layer;

	struct liftoff_list layers; /* liftoff_layer.link */
	/* layer added or removed, or composition layer changed */
	bool layers_changed;

	int alloc_reused_counter;
};

struct liftoff_layer {
	struct liftoff_output *output;
	struct liftoff_list link; /* liftoff_output.layers */

	struct liftoff_layer_property *props;
	size_t props_len;

	bool force_composition; /* FB needs to be composited */

	struct liftoff_plane *plane;

	int current_priority, pending_priority;
	/* prop added or force_composition changed */
	bool changed;
};

struct liftoff_layer_property {
	char name[DRM_PROP_NAME_LEN];
	uint64_t value, prev_value;
};

struct liftoff_plane {
	uint32_t id;
	uint32_t possible_crtcs;
	uint32_t type;
	int zpos; /* greater values mean closer to the eye */
	/* TODO: formats */
	struct liftoff_list link; /* liftoff_device.planes */

	struct liftoff_plane_property *props;
	size_t props_len;

	struct liftoff_layer *layer;
};

struct liftoff_plane_property {
	char name[DRM_PROP_NAME_LEN];
	uint32_t id;
};

struct liftoff_rect {
	int x, y;
	int width, height;
};

int
device_test_commit(struct liftoff_device *device, drmModeAtomicReq *req,
		   uint32_t flags);

struct liftoff_layer_property *
layer_get_property(struct liftoff_layer *layer, const char *name);

void
layer_get_rect(struct liftoff_layer *layer, struct liftoff_rect *rect);

bool
layer_intersects(struct liftoff_layer *a, struct liftoff_layer *b);

void
layer_mark_clean(struct liftoff_layer *layer);

void
layer_update_priority(struct liftoff_layer *layer, bool make_current);

bool
layer_has_fb(struct liftoff_layer *layer);

bool
layer_is_visible(struct liftoff_layer *layer);

int
plane_apply(struct liftoff_plane *plane, struct liftoff_layer *layer,
	    drmModeAtomicReq *req);

void
output_log_layers(struct liftoff_output *output);

#endif
