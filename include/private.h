#ifndef PRIVATE_H
#define PRIVATE_H

#include "libhwc.h"
#include "list.h"

struct hwc_display {
	int drm_fd;

	struct hwc_list planes; /* hwc_plane.link */
	struct hwc_list outputs; /* hwc_output.link */

	uint32_t *crtcs;
	size_t crtcs_len;
};

struct hwc_output {
	struct hwc_display *display;
	uint32_t crtc_id;
	size_t crtc_index;
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
	struct hwc_list link; /* hwc_display.planes */

	struct hwc_plane_property *props;
	size_t props_len;

	struct hwc_layer *layer;
};

struct hwc_plane_property {
	char name[DRM_PROP_NAME_LEN];
	uint32_t id;
};

#endif
