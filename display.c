#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "private.h"

static int guess_plane_zpos_from_type(struct hwc_display *display,
				      uint32_t plane_id, uint32_t type)
{
	struct hwc_plane *primary;

	/* From far to close to the eye: primary, overlay, cursor. Unless
	 * the overlay ID < primary ID. */
	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
		return 0;
	case DRM_PLANE_TYPE_CURSOR:
		return 2;
	case DRM_PLANE_TYPE_OVERLAY:
		if (hwc_list_empty(&display->planes)) {
			return 0; /* No primary plane, shouldn't happen */
		}
		primary = hwc_container_of(display->planes.next, primary, link);
		if (plane_id < primary->id) {
			return -1;
		} else {
			return 1;
		}
	}
	return 0;
}

static struct hwc_plane *plane_create(struct hwc_display *display, uint32_t id)
{
	struct hwc_plane *plane, *cur;
	drmModePlane *drm_plane;
	drmModeObjectProperties *drm_props;
	uint32_t i;
	drmModePropertyRes *drm_prop;
	struct hwc_plane_property *prop;
	uint64_t value;
	bool has_type = false, has_zpos = false;

	plane = calloc(1, sizeof(*plane));
	if (plane == NULL) {
		return NULL;
	}

	drm_plane = drmModeGetPlane(display->drm_fd, id);
	if (drm_plane == NULL) {
		return NULL;
	}
	plane->id = drm_plane->plane_id;
	plane->possible_crtcs = drm_plane->possible_crtcs;
	drmModeFreePlane(drm_plane);

	drm_props = drmModeObjectGetProperties(display->drm_fd, id,
					       DRM_MODE_OBJECT_PLANE);
	if (drm_props == NULL) {
		return NULL;
	}
	plane->props = calloc(drm_props->count_props,
			      sizeof(struct hwc_plane_property));
	if (plane->props == NULL) {
		drmModeFreeObjectProperties(drm_props);
		return NULL;
	}
	for (i = 0; i < drm_props->count_props; i++) {
		drm_prop = drmModeGetProperty(display->drm_fd,
					      drm_props->props[i]);
		if (drm_prop == NULL) {
			drmModeFreeObjectProperties(drm_props);
			return NULL;
		}
		prop = &plane->props[i];
		memcpy(prop->name, drm_prop->name, sizeof(prop->name));
		prop->id = drm_prop->prop_id;
		drmModeFreeProperty(drm_prop);
		plane->props_len++;

		value = drm_props->prop_values[i];
		if (strcmp(prop->name, "type") == 0) {
			plane->type = value;
			has_type = true;
		} else if (strcmp(prop->name, "zpos") == 0) {
			plane->zpos = value;
			has_zpos = true;
		}
	}
	drmModeFreeObjectProperties(drm_props);

	if (!has_type) {
		fprintf(stderr, "plane %d is missing the 'type' property\n",
			plane->id);
		free(plane);
		return NULL;
	} else if (!has_zpos) {
		plane->zpos = guess_plane_zpos_from_type(display, plane->id,
							 plane->type);
	}

	/* During plane allocation, we will use the plane list order to fill
	 * planes with FBs. Primary planes need to be filled first, then planes
	 * far from the primary planes, then planes closer and closer to the
	 * primary plane. */
	if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
		hwc_list_insert(&display->planes, &plane->link);
	} else {
		hwc_list_for_each(cur, &display->planes, link) {
			if (cur->type != DRM_PLANE_TYPE_PRIMARY &&
			    plane->zpos >= cur->zpos) {
				hwc_list_insert(cur->link.prev, &plane->link);
				break;
			}
		}

		if (plane->link.next == NULL) { /* not inserted */
			hwc_list_insert(display->planes.prev, &plane->link);
		}
	}

	return plane;
}

static void plane_destroy(struct hwc_plane *plane)
{
	hwc_list_remove(&plane->link);
	free(plane->props);
	free(plane);
}

struct hwc_display *hwc_display_create(int drm_fd)
{
	struct hwc_display *display;
	drmModeRes *drm_res;
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

	hwc_list_init(&display->planes);
	hwc_list_init(&display->outputs);

	drm_res = drmModeGetResources(drm_fd);
	if (drm_res == NULL) {
		hwc_display_destroy(display);
		return NULL;
	}

	display->crtcs = malloc(drm_res->count_crtcs * sizeof(uint32_t));
	if (display->crtcs == NULL) {
		drmModeFreeResources(drm_res);
		hwc_display_destroy(display);
		return NULL;
	}
	display->crtcs_len = drm_res->count_crtcs;
	memcpy(display->crtcs, drm_res->crtcs,
	       drm_res->count_crtcs * sizeof(uint32_t));

	drmModeFreeResources(drm_res);

	/* TODO: allow users to choose which layers to hand over */
	drm_plane_res = drmModeGetPlaneResources(drm_fd);
	if (drm_plane_res == NULL) {
		hwc_display_destroy(display);
		return NULL;
	}

	for (i = 0; i < drm_plane_res->count_planes; i++) {
		if (plane_create(display, drm_plane_res->planes[i]) == NULL) {
			hwc_display_destroy(display);
			return NULL;
		}
	}
	drmModeFreePlaneResources(drm_plane_res);

	return display;
}

void hwc_display_destroy(struct hwc_display *display)
{
	struct hwc_plane *plane, *tmp;

	close(display->drm_fd);
	hwc_list_for_each_safe(plane, tmp, &display->planes, link) {
		plane_destroy(plane);
	}
	free(display->crtcs);
	free(display);
}

static struct hwc_plane_property *plane_get_property(struct hwc_plane *plane,
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

static bool plane_set_prop(struct hwc_plane *plane, drmModeAtomicReq *req,
			   struct hwc_plane_property *prop, uint64_t value)
{
	int ret;

	fprintf(stderr, "  Setting %s = %"PRIu64"\n", prop->name, value);
	ret = drmModeAtomicAddProperty(req, plane->id, prop->id, value);
	if (ret < 0) {
		perror("drmModeAtomicAddProperty");
		return false;
	}

	return true;
}

static bool plane_apply(struct hwc_plane *plane, struct hwc_layer *layer,
			drmModeAtomicReq *req, bool *compatible)
{
	int cursor;
	size_t i;
	struct hwc_layer_property *layer_prop;
	struct hwc_plane_property *plane_prop;

	*compatible = true;
	cursor = drmModeAtomicGetCursor(req);

	if (layer == NULL) {
		plane_prop = plane_get_property(plane, "FB_ID");
		if (plane_prop == NULL) {
			fprintf(stderr, "plane is missing the FB_ID property\n");
			return false;
		}
		return plane_set_prop(plane, req, plane_prop, 0);
	}

	plane_prop = plane_get_property(plane, "CRTC_ID");
	if (plane_prop == NULL) {
		fprintf(stderr, "plane is missing the CRTC_ID property\n");
		return false;
	}
	if (!plane_set_prop(plane, req, plane_prop, layer->output->crtc_id)) {
		return false;
	}

	for (i = 0; i < layer->props_len; i++) {
		layer_prop = &layer->props[i];
		plane_prop = plane_get_property(plane, layer_prop->name);
		if (plane_prop == NULL) {
			*compatible = false;
			drmModeAtomicSetCursor(req, cursor);
			return true;
		}

		if (!plane_set_prop(plane, req, plane_prop, layer_prop->value)) {
			drmModeAtomicSetCursor(req, cursor);
			return false;
		}
	}

	return true;
}

struct plane_alloc {
	drmModeAtomicReq *req;
	size_t planes_len;
	struct hwc_layer **current;
	struct hwc_layer **best;
	int best_score;
};

bool output_choose_layers(struct hwc_output *output, struct plane_alloc *alloc,
			  struct hwc_list *cur, size_t plane_idx, int score)
{
	struct hwc_display *display;
	struct hwc_plane *plane;
	struct hwc_layer *layer;
	int cursor, ret;
	size_t remaining_planes, i;
	bool found, compatible;

	display = output->display;

	if (cur == &display->planes) { /* Allocation finished */
		if (score > alloc->best_score) {
			/* We found a better allocation */
			alloc->best_score = score;
			memcpy(alloc->best, alloc->current,
			       alloc->planes_len * sizeof(struct hwc_layer *));
		}
		return true;
	}
	plane = hwc_container_of(cur, plane, link);

	remaining_planes = alloc->planes_len - plane_idx;
	if (alloc->best_score >= (int)remaining_planes) {
		/* Even if we find a layer for all remaining planes, we won't
		 * find a better allocation. Give up. */
		return true;
	}

	cursor = drmModeAtomicGetCursor(alloc->req);

	if (plane->layer != NULL) {
		goto skip;
	}
	if ((plane->possible_crtcs & (1 << output->crtc_index)) == 0) {
		goto skip;
	}

	hwc_list_for_each(layer, &output->layers, link) {
		if (layer->plane != NULL) {
			continue;
		}

		found = false;
		for (i = 0; i < plane_idx; i++) {
			if (alloc->current[i] == layer) {
				found = true;
				break;
			}
		}
		if (found) {
			continue;
		}

		/* Try to use this layer for the current plane */
		alloc->current[plane_idx] = layer;
		fprintf(stderr, "Trying to apply layer %p with plane %d...\n",
			(void *)layer, plane->id);
		if (!plane_apply(plane, layer, alloc->req, &compatible)) {
			return false;
		}
		if (!compatible) {
			continue;
		}

		ret = drmModeAtomicCommit(display->drm_fd, alloc->req,
					  DRM_MODE_ATOMIC_TEST_ONLY, NULL);
		if (ret == 0) {
			fprintf(stderr, "Success\n");
			/* Continue with the next plane */
			if (!output_choose_layers(output, alloc, cur->next,
						  plane_idx + 1, score + 1)) {
				return false;
			}
		} else if (-ret != EINVAL && -ret != ERANGE) {
			perror("drmModeAtomicCommit");
			return false;
		}

		drmModeAtomicSetCursor(alloc->req, cursor);
	}

skip:
	/* Try not to use the current plane */
	alloc->current[plane_idx] = NULL;
	if (!output_choose_layers(output, alloc, cur->next,
				  plane_idx + 1, score)) {
		return false;
	}
	drmModeAtomicSetCursor(alloc->req, cursor);

	return true;
}

bool hwc_display_apply(struct hwc_display *display, drmModeAtomicReq *req)
{
	struct hwc_output *output;
	struct hwc_plane *plane;
	struct hwc_layer *layer;
	struct plane_alloc alloc;
	size_t i;
	bool compatible;

	/* Unset all existing plane and layer mappings.
	   TODO: incremental updates keeping old configuration if possible */
	hwc_list_for_each(plane, &display->planes, link) {
		if (plane->layer != NULL) {
			plane->layer->plane = NULL;
			plane->layer = NULL;
		}
	}

	/* Disable all planes. Do it before building mappings to make sure not
	   to hit bandwidth limits because too many planes are enabled. */
	hwc_list_for_each(plane, &display->planes, link) {
		if (plane->layer == NULL) {
			fprintf(stderr, "Disabling plane %d\n", plane->id);
			if (!plane_apply(plane, NULL, req, &compatible)) {
				return false;
			}
			assert(compatible);
		}
	}

	alloc.req = req;
	alloc.planes_len = hwc_list_length(&display->planes);
	alloc.current = malloc(alloc.planes_len * sizeof(*alloc.current));
	alloc.best = malloc(alloc.planes_len * sizeof(*alloc.best));
	if (alloc.current == NULL || alloc.best == NULL) {
		perror("malloc");
		return false;
	}

	/* TODO: maybe start by allocating the primary plane on each output to
	 * make sure we can display at least something without hitting bandwidth
	 * issues? Also: be fair when mapping planes to outputs, don't give all
	 * planes to a single output. Also: don't treat each output separately,
	 * allocate planes for all outputs at once. */
	hwc_list_for_each(output, &display->outputs, link) {
		/* For each plane, try to find a layer. Don't do it the other
		 * way around (ie. for each layer, try to find a plane) because
		 * some drivers want user-space to enable the primary plane
		 * before any other plane. */

		alloc.best_score = 0;
		memset(alloc.best, 0, alloc.planes_len * sizeof(*alloc.best));
		if (!output_choose_layers(output, &alloc, display->planes.next,
					  0, 0)) {
			return false;
		}

		fprintf(stderr, "Found plane allocation for output %p "
			"with score=%d\n", (void *)output, alloc.best_score);

		/* Apply the best allocation */
		i = 0;
		hwc_list_for_each(plane, &display->planes, link) {
			layer = alloc.best[i];
			i++;
			if (layer == NULL) {
				continue;
			}

			fprintf(stderr, "Assigning layer %p to plane %d\n",
				(void *)layer, plane->id);
			if (!plane_apply(plane, layer, req, &compatible)) {
				return false;
			}
			assert(compatible);

			assert(plane->layer == NULL);
			assert(layer->plane == NULL);
			plane->layer = layer;
			layer->plane = plane;
		}
	}

	free(alloc.current);
	free(alloc.best);

	return true;
}
