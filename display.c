#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "private.h"

static bool plane_init(struct hwc_plane *plane, struct hwc_display *display,
		       int32_t id)
{
	drmModePlane *drm_plane;
	drmModeObjectProperties *drm_props;
	uint32_t i;
	drmModePropertyRes *drm_prop;
	struct hwc_plane_property *prop;

	drm_plane = drmModeGetPlane(display->drm_fd, id);
	if (drm_plane == NULL) {
		return false;
	}
	plane->id = drm_plane->plane_id;
	plane->possible_crtcs = drm_plane->possible_crtcs;
	drmModeFreePlane(drm_plane);

	drm_props = drmModeObjectGetProperties(display->drm_fd, id,
					       DRM_MODE_OBJECT_PLANE);
	if (drm_props == NULL) {
		return false;
	}
	plane->props = calloc(drm_props->count_props,
			      sizeof(struct hwc_plane_property));
	if (plane->props == NULL) {
		drmModeFreeObjectProperties(drm_props);
		return false;
	}
	for (i = 0; i < drm_props->count_props; i++) {
		drm_prop = drmModeGetProperty(display->drm_fd,
					      drm_props->props[i]);
		if (drm_prop == NULL) {
			drmModeFreeObjectProperties(drm_props);
			return false;
		}
		prop = &plane->props[i];
		memcpy(prop->name, drm_prop->name, sizeof(prop->name));
		prop->id = drm_prop->prop_id;
		drmModeFreeProperty(drm_prop);
		plane->props_len++;
	}
	drmModeFreeObjectProperties(drm_props);

	return true;
}

static void plane_finish(struct hwc_plane *plane)
{
	free(plane->props);
}

struct hwc_display *hwc_display_create(int drm_fd)
{
	struct hwc_display *display;
	drmModePlaneRes *drm_plane_res;
	uint32_t i;

	display = calloc(1, sizeof(*display));
	if (display == NULL) {
		return NULL;
	}
	display->drm_fd = dup(drm_fd);
	if (display->drm_fd < 0) {
		hwc_display_destroy(display);
		return NULL;
	}

	hwc_list_init(&display->outputs);

	/* TODO: allow users to choose which layers to hand over */
	drm_plane_res = drmModeGetPlaneResources(drm_fd);
	if (drm_plane_res == NULL) {
		hwc_display_destroy(display);
		return NULL;
	}

	display->planes = calloc(drm_plane_res->count_planes,
				 sizeof(struct hwc_plane));
	if (display->planes == NULL) {
		hwc_display_destroy(display);
		return NULL;
	}
	for (i = 0; i < drm_plane_res->count_planes; i++) {
		if (!plane_init(&display->planes[i], display,
				drm_plane_res->planes[i])) {
			hwc_display_destroy(display);
			return NULL;
		}
		display->planes_len++;
	}
	drmModeFreePlaneResources(drm_plane_res);

	return display;
}

void hwc_display_destroy(struct hwc_display *display)
{
	size_t i;

	close(display->drm_fd);
	for (i = 0; i < display->planes_len; i++) {
		plane_finish(&display->planes[i]);
	}
	free(display->planes);
	free(display);
}

struct hwc_plane_property *plane_get_property(struct hwc_plane *plane,
					      const char *name)
{
	size_t i;

	for (i = 0; i < plane->props_len; i++) {
		if (strcmp(plane->props[i].name, name) == 0) {
			return &plane->props[i];
		}
	}
	return NULL;
}

bool layer_apply(struct hwc_layer *layer, struct hwc_plane *plane,
		 drmModeAtomicReq *req)
{
	size_t i;
	int ret;
	struct hwc_layer_property *layer_prop;
	struct hwc_plane_property *plane_prop;

	/* TODO: disable planes that aren't used */

	fprintf(stderr, "  Setting CRTC_ID = %"PRIu32"\n",
		layer->output->crtc_id);
	plane_prop = plane_get_property(plane, "CRTC_ID");
	ret = drmModeAtomicAddProperty(req, plane->id, plane_prop->id,
				       layer->output->crtc_id);
	if (ret < 0) {
		perror("drmModeAtomicAddProperty");
		return false;
	}

	for (i = 0; i < layer->props_len; i++) {
		layer_prop = &layer->props[i];
		plane_prop = plane_get_property(plane, layer_prop->name);
		if (plane_prop == NULL) {
			fprintf(stderr, "failed to find property %s\n",
				layer_prop->name);
			return false;
		}

		fprintf(stderr, "  Setting %s = %"PRIu64"\n", layer_prop->name,
			layer_prop->value);
		ret = drmModeAtomicAddProperty(req, plane->id,
					       plane_prop->id,
					       layer_prop->value);
		if (ret < 0) {
			perror("drmModeAtomicAddProperty");
			return false;
		}
	}

	return true;
}

bool layer_choose_plane(struct hwc_layer *layer, drmModeAtomicReq *req)
{
	struct hwc_display *display;
	int cursor;
	struct hwc_plane *plane;
	size_t i;
	int ret;

	display = layer->output->display;
	cursor = drmModeAtomicGetCursor(req);

	for (i = 0; i < display->planes_len; i++) {
		plane = &display->planes[i];

		fprintf(stderr, "Trying to apply layer %p with plane %d...\n",
			(void *)layer, plane->id);
		if (!layer_apply(layer, plane, req)) {
			return false;
		}

		ret = drmModeAtomicCommit(display->drm_fd, req,
					  DRM_MODE_ATOMIC_TEST_ONLY, NULL);
		if (ret == 0) {
			fprintf(stderr, "Success\n");
			layer->plane = plane;
			return true;
		} else if (-ret != EINVAL && -ret != ERANGE) {
			perror("drmModeAtomicCommit");
			return false;
		}

		fprintf(stderr, "Failure\n");
		drmModeAtomicSetCursor(req, cursor);
	}

	return false;
}

bool hwc_display_apply(struct hwc_display *display, drmModeAtomicReq *req)
{
	int cursor;
	struct hwc_output *output;
	struct hwc_layer *layer;

	cursor = drmModeAtomicGetCursor(req);

	hwc_list_for_each(output, &display->outputs, link) {
		hwc_list_for_each(layer, &output->layers, link) {
			if (!layer_choose_plane(layer, req)) {
				fprintf(stderr,
					"Failed to find plane for layer %p\n",
					(void *)layer);
				drmModeAtomicSetCursor(req, cursor);
				return false;
			}
		}
	}

	return true;
}
