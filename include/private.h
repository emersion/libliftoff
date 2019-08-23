#ifndef PRIVATE_H
#define PRIVATE_H

#include "libhwc.h"
#include "list.h"

struct hwc_display {
	int drm_fd;

	struct hwc_plane *planes;
	size_t planes_len;

	struct hwc_list outputs; /* hwc_output.link */
};

struct hwc_output {
	struct hwc_display *display;
	uint32_t crtc_id;
	struct hwc_list link; /* hwc_display.outputs */

	struct hwc_list layers; /* hwc_layer.link */
};

struct hwc_layer {
	struct hwc_output *output;
	struct hwc_list link; /* hwc_output.layers */

	struct hwc_layer_property *props;
	size_t props_len;

	struct hwc_plane *plane;
};

struct hwc_layer_property {
	char name[DRM_PROP_NAME_LEN];
	uint64_t value;
};

struct hwc_plane {
	uint32_t id;
	uint32_t possible_crtcs;
	/* TODO: formats */

	struct hwc_plane_property *props;
	size_t props_len;

	struct hwc_layer *layer;
};

struct hwc_plane_property {
	char name[DRM_PROP_NAME_LEN];
	uint32_t id;
};

#endif
