#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "log.h"
#include "private.h"

struct liftoff_display *liftoff_display_create(int drm_fd)
{
	struct liftoff_display *display;
	drmModeRes *drm_res;
	drmModePlaneRes *drm_plane_res;
	uint32_t i;

	display = calloc(1, sizeof(*display));
	if (display == NULL) {
		liftoff_log_errno(LIFTOFF_ERROR, "calloc");
		return NULL;
	}

	liftoff_list_init(&display->planes);
	liftoff_list_init(&display->outputs);

	display->drm_fd = dup(drm_fd);
	if (display->drm_fd < 0) {
		liftoff_log_errno(LIFTOFF_ERROR, "dup");
		liftoff_display_destroy(display);
		return NULL;
	}

	drm_res = drmModeGetResources(drm_fd);
	if (drm_res == NULL) {
		liftoff_log_errno(LIFTOFF_ERROR, "drmModeGetResources");
		liftoff_display_destroy(display);
		return NULL;
	}

	display->crtcs = malloc(drm_res->count_crtcs * sizeof(uint32_t));
	if (display->crtcs == NULL) {
		liftoff_log_errno(LIFTOFF_ERROR, "malloc");
		drmModeFreeResources(drm_res);
		liftoff_display_destroy(display);
		return NULL;
	}
	display->crtcs_len = drm_res->count_crtcs;
	memcpy(display->crtcs, drm_res->crtcs,
	       drm_res->count_crtcs * sizeof(uint32_t));

	drmModeFreeResources(drm_res);

	/* TODO: allow users to choose which layers to hand over */
	drm_plane_res = drmModeGetPlaneResources(drm_fd);
	if (drm_plane_res == NULL) {
		liftoff_log_errno(LIFTOFF_ERROR, "drmModeGetPlaneResources");
		liftoff_display_destroy(display);
		return NULL;
	}

	for (i = 0; i < drm_plane_res->count_planes; i++) {
		if (plane_create(display, drm_plane_res->planes[i]) == NULL) {
			liftoff_display_destroy(display);
			return NULL;
		}
	}
	drmModeFreePlaneResources(drm_plane_res);

	return display;
}

void liftoff_display_destroy(struct liftoff_display *display)
{
	struct liftoff_plane *plane, *tmp;

	close(display->drm_fd);
	liftoff_list_for_each_safe(plane, tmp, &display->planes, link) {
		plane_destroy(plane);
	}
	free(display->crtcs);
	free(display);
}
