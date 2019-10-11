#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "private.h"

static int guess_plane_zpos_from_type(struct liftoff_display *display,
				      uint32_t plane_id, uint32_t type)
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
		if (liftoff_list_empty(&display->planes)) {
			return 0; /* No primary plane, shouldn't happen */
		}
		primary = liftoff_container_of(display->planes.next,
					       primary, link);
		if (plane_id < primary->id) {
			return -1;
		} else {
			return 1;
		}
	}
	return 0;
}

struct liftoff_plane *plane_create(struct liftoff_display *display, uint32_t id)
{
	struct liftoff_plane *plane, *cur;
	drmModePlane *drm_plane;
	drmModeObjectProperties *drm_props;
	uint32_t i;
	drmModePropertyRes *drm_prop;
	struct liftoff_plane_property *prop;
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
			      sizeof(struct liftoff_plane_property));
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
		liftoff_log(LIFTOFF_ERROR,
			    "plane %"PRIu32" is missing the 'type' property",
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
		liftoff_list_insert(&display->planes, &plane->link);
	} else {
		liftoff_list_for_each(cur, &display->planes, link) {
			if (cur->type != DRM_PLANE_TYPE_PRIMARY &&
			    plane->zpos >= cur->zpos) {
				liftoff_list_insert(cur->link.prev, &plane->link);
				break;
			}
		}

		if (plane->link.next == NULL) { /* not inserted */
			liftoff_list_insert(display->planes.prev, &plane->link);
		}
	}

	return plane;
}

void plane_destroy(struct liftoff_plane *plane)
{
	liftoff_list_remove(&plane->link);
	free(plane->props);
	free(plane);
}
