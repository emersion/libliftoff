#ifndef PRIVATE_H
#define PRIVATE_H

#include <libliftoff.h>
#include "list.h"
#include "log.h"

struct liftoff_display {
	int drm_fd;

	struct liftoff_list planes; /* liftoff_plane.link */
	struct liftoff_list outputs; /* liftoff_output.link */

	uint32_t *crtcs;
	size_t crtcs_len;
};

struct liftoff_output {
	struct liftoff_display *display;
	uint32_t crtc_id;
	size_t crtc_index;
	struct liftoff_list link; /* liftoff_display.outputs */

	struct liftoff_layer *composition_layer;

	struct liftoff_list layers; /* liftoff_layer.link */
};

struct liftoff_layer {
	struct liftoff_output *output;
	struct liftoff_list link; /* liftoff_output.layers */

	struct liftoff_layer_property *props;
	size_t props_len;

	struct liftoff_plane *plane;
};

struct liftoff_layer_property {
	char name[DRM_PROP_NAME_LEN];
	uint64_t value;
	bool changed;
};

struct liftoff_plane {
	uint32_t id;
	uint32_t possible_crtcs;
	uint32_t type;
	int zpos; /* greater values mean closer to the eye */
	/* TODO: formats */
	struct liftoff_list link; /* liftoff_display.planes */

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

struct liftoff_layer_property *layer_get_property(struct liftoff_layer *layer,
						  const char *name);
void layer_get_rect(struct liftoff_layer *layer, struct liftoff_rect *rect);
bool layer_intersects(struct liftoff_layer *a, struct liftoff_layer *b);
void layer_mark_clean(struct liftoff_layer *layer);

struct liftoff_plane *plane_create(struct liftoff_display *display, uint32_t id);
void plane_destroy(struct liftoff_plane *plane);
struct liftoff_plane_property *plane_get_property(struct liftoff_plane *plane,
						  const char *name);
bool plane_apply(struct liftoff_plane *plane, struct liftoff_layer *layer,
		 drmModeAtomicReq *req, bool *compatible);

#endif
