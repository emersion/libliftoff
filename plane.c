#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "private.h"

static int
guess_plane_zpos_from_type(struct liftoff_device *device, uint32_t plane_id,
			   uint32_t type)
{
	struct liftoff_plane *primary;

	/* From far to close to the eye: primary, overlay, cursor. Unless
	 * the overlay ID < primary ID. */
	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
		return 0;
	case DRM_PLANE_TYPE_CURSOR:
		return 2;
	case DRM_PLANE_TYPE_OVERLAY:
		if (liftoff_list_empty(&device->planes)) {
			return 0; /* No primary plane, shouldn't happen */
		}
		primary = liftoff_container_of(device->planes.next,
					       primary, link);
		if (plane_id < primary->id) {
			return -1;
		} else {
			return 1;
		}
	}
	return 0;
}

struct liftoff_plane *
liftoff_plane_create(struct liftoff_device *device, uint32_t id)
{
	struct liftoff_plane *plane, *cur;
	drmModePlane *drm_plane;
	drmModeObjectProperties *drm_props;
	uint32_t i;
	drmModePropertyRes *drm_prop;
	struct liftoff_plane_property *prop;
	uint64_t value;
	bool has_type = false, has_zpos = false;
	ssize_t core_prop_idx;

	liftoff_list_for_each(plane, &device->planes, link) {
		if (plane->id == id) {
			liftoff_log(LIFTOFF_ERROR, "tried to register plane "
				    "%"PRIu32" twice\n", id);
			errno = EEXIST;
			return NULL;
		}
	}

	plane = calloc(1, sizeof(*plane));
	if (plane == NULL) {
		liftoff_log_errno(LIFTOFF_ERROR, "calloc");
		return NULL;
	}

	drm_plane = drmModeGetPlane(device->drm_fd, id);
	if (drm_plane == NULL) {
		liftoff_log_errno(LIFTOFF_ERROR, "drmModeGetPlane");
		return NULL;
	}
	plane->id = drm_plane->plane_id;
	plane->possible_crtcs = drm_plane->possible_crtcs;
	drmModeFreePlane(drm_plane);

	drm_props = drmModeObjectGetProperties(device->drm_fd, id,
					       DRM_MODE_OBJECT_PLANE);
	if (drm_props == NULL) {
		liftoff_log_errno(LIFTOFF_ERROR, "drmModeObjectGetProperties");
		return NULL;
	}
	plane->props = calloc(drm_props->count_props,
			      sizeof(struct liftoff_plane_property));
	if (plane->props == NULL) {
		liftoff_log_errno(LIFTOFF_ERROR, "calloc");
		drmModeFreeObjectProperties(drm_props);
		return NULL;
	}
	for (i = 0; i < drm_props->count_props; i++) {
		drm_prop = drmModeGetProperty(device->drm_fd,
					      drm_props->props[i]);
		if (drm_prop == NULL) {
			liftoff_log_errno(LIFTOFF_ERROR, "drmModeGetProperty");
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

		core_prop_idx = core_property_index(prop->name);
		if (core_prop_idx >= 0) {
			plane->core_props[core_prop_idx] = prop;
		}
	}
	drmModeFreeObjectProperties(drm_props);

	if (!has_type) {
		liftoff_log(LIFTOFF_ERROR,
			    "plane %"PRIu32" is missing the 'type' property",
			    plane->id);
		free(plane);
		errno = EINVAL;
		return NULL;
	} else if (!has_zpos) {
		plane->zpos = guess_plane_zpos_from_type(device, plane->id,
							 plane->type);
	}

	/* During plane allocation, we will use the plane list order to fill
	 * planes with FBs. Primary planes need to be filled first, then planes
	 * far from the primary planes, then planes closer and closer to the
	 * primary plane. */
	if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
		liftoff_list_insert(&device->planes, &plane->link);
	} else {
		liftoff_list_for_each(cur, &device->planes, link) {
			if (cur->type != DRM_PLANE_TYPE_PRIMARY &&
			    plane->zpos >= cur->zpos) {
				liftoff_list_insert(cur->link.prev, &plane->link);
				break;
			}
		}

		if (plane->link.next == NULL) { /* not inserted */
			liftoff_list_insert(device->planes.prev, &plane->link);
		}
	}

	return plane;
}

void
liftoff_plane_destroy(struct liftoff_plane *plane)
{
	if (plane->layer != NULL) {
		plane->layer->plane = NULL;
	}
	liftoff_list_remove(&plane->link);
	free(plane->props);
	free(plane);
}

uint32_t
liftoff_plane_get_id(struct liftoff_plane *plane)
{
	return plane->id;
}

static struct liftoff_plane_property *
plane_get_property(struct liftoff_plane *plane,
		   const struct liftoff_layer_property *layer_prop)
{
	size_t i;

	if (layer_prop->core_index >= 0)
		return plane->core_props[layer_prop->core_index];

	for (i = 0; i < plane->props_len; i++) {
		if (strcmp(plane->props[i].name, layer_prop->name) == 0) {
			return &plane->props[i];
		}
	}
	return NULL;
}

static int
plane_set_prop(struct liftoff_plane *plane, drmModeAtomicReq *req,
	       struct liftoff_plane_property *prop, uint64_t value)
{
	int ret;

	ret = drmModeAtomicAddProperty(req, plane->id, prop->id, value);
	if (ret < 0) {
		liftoff_log(LIFTOFF_ERROR, "drmModeAtomicAddProperty: %s",
			    strerror(-ret));
		return ret;
	}

	return 0;
}

static int
set_plane_core_prop(struct liftoff_plane *plane, drmModeAtomicReq *req,
		    enum liftoff_core_property core_prop, uint64_t value)
{
	struct liftoff_plane_property *prop;

	prop = plane->core_props[core_prop];
	if (prop == NULL) {
		liftoff_log(LIFTOFF_DEBUG,
			    "plane %"PRIu32" is missing core property %d",
			    plane->id, core_prop);
		return -EINVAL;
	}

	return plane_set_prop(plane, req, prop, value);
}

int
plane_apply(struct liftoff_plane *plane, struct liftoff_layer *layer,
	    drmModeAtomicReq *req)
{
	int cursor, ret;
	size_t i;
	struct liftoff_layer_property *layer_prop;
	struct liftoff_plane_property *plane_prop;

	cursor = drmModeAtomicGetCursor(req);

	if (layer == NULL) {
		ret = set_plane_core_prop(plane, req, LIFTOFF_PROP_FB_ID, 0);
		if (ret != 0) {
			return ret;
		}
		return set_plane_core_prop(plane, req, LIFTOFF_PROP_CRTC_ID, 0);
	}

	ret = set_plane_core_prop(plane, req, LIFTOFF_PROP_CRTC_ID,
				  layer->output->crtc_id);
	if (ret != 0) {
		return ret;
	}

	for (i = 0; i < layer->props_len; i++) {
		layer_prop = &layer->props[i];
		if (layer_prop->core_index == LIFTOFF_PROP_ZPOS) {
			/* We don't yet support setting the zpos property. We
			 * only use it (read-only) during plane allocation. */
			continue;
		}

		plane_prop = plane_get_property(plane, layer_prop);
		if (plane_prop == NULL) {
			if (layer_prop->core_index == LIFTOFF_PROP_ALPHA &&
			    layer_prop->value == 0xFFFF) {
				continue; /* Layer is completely opaque */
			}
			if (layer_prop->core_index == LIFTOFF_PROP_ROTATION &&
			    layer_prop->value == DRM_MODE_ROTATE_0) {
				continue; /* Layer isn't rotated */
			}
			drmModeAtomicSetCursor(req, cursor);
			return -EINVAL;
		}

		ret = plane_set_prop(plane, req, plane_prop, layer_prop->value);
		if (ret != 0) {
			drmModeAtomicSetCursor(req, cursor);
			return ret;
		}
	}

	return 0;
}
