#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <libliftoff.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "common.h"

/* ARGB 8:8:8:8 */
static const uint32_t colors[] = {
	0xFFFF0000, /* red */
	0xFF00FF00, /* green */
	0xFF0000FF, /* blue */
	0xFFFFFF00, /* yellow */
};

static struct liftoff_layer *add_layer(int drm_fd, struct liftoff_output *output,
				       int x, int y, int width, int height,
				       bool with_alpha)
{
	static size_t color_idx = 0;
	uint32_t fb_id;
	struct liftoff_layer *layer;

	fb_id = create_argb_fb(drm_fd, width, height, colors[color_idx],
			       with_alpha);
	if (fb_id == 0) {
		fprintf(stderr, "failed to create framebuffer\n");
		return NULL;
	}
	printf("Created FB %d with size %dx%d\n", fb_id, width, height);
	color_idx = (color_idx + 1) % (sizeof(colors) / sizeof(colors[0]));

	layer = liftoff_layer_create(output);
	liftoff_layer_set_property(layer, "FB_ID", fb_id);
	liftoff_layer_set_property(layer, "CRTC_X", x);
	liftoff_layer_set_property(layer, "CRTC_Y", y);
	liftoff_layer_set_property(layer, "CRTC_W", width);
	liftoff_layer_set_property(layer, "CRTC_H", height);
	liftoff_layer_set_property(layer, "SRC_X", 0);
	liftoff_layer_set_property(layer, "SRC_Y", 0);
	liftoff_layer_set_property(layer, "SRC_W", width << 16);
	liftoff_layer_set_property(layer, "SRC_H", height << 16);

	return layer;
}

int main(int argc, char *argv[])
{
	int drm_fd;
	struct liftoff_display *display;
	drmModeRes *drm_res;
	drmModeCrtc *crtc;
	drmModeConnector *connector;
	struct liftoff_output *output;
	struct liftoff_layer *layers[4];
	drmModeAtomicReq *req;
	int ret;
	size_t i;

	drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (drm_fd < 0) {
		perror("open");
		return 1;
	}

	if (drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) < 0) {
		perror("drmSetClientCap(UNIVERSAL_PLANES)");
		return 1;
	}
	if (drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0) {
		perror("drmSetClientCap(ATOMIC)");
		return 1;
	}

	display = liftoff_display_create(drm_fd);
	if (display == NULL) {
		perror("liftoff_display_create");
		return 1;
	}

	drm_res = drmModeGetResources(drm_fd);
	connector = pick_connector(drm_fd, drm_res);
	crtc = pick_crtc(drm_fd, drm_res, connector);
	disable_all_crtcs_except(drm_fd, drm_res, crtc->crtc_id);
	output = liftoff_output_create(display, crtc->crtc_id);
	drmModeFreeResources(drm_res);

	if (connector == NULL) {
		fprintf(stderr, "no connector found\n");
		return 1;
	}
	if (crtc == NULL || !crtc->mode_valid) {
		fprintf(stderr, "no CRTC found\n");
		return 1;
	}

	printf("Using connector %d, CRTC %d\n", connector->connector_id,
	       crtc->crtc_id);

	layers[0] = add_layer(drm_fd, output, 0, 0, crtc->mode.hdisplay,
			      crtc->mode.vdisplay, false);
	layers[1] = add_layer(drm_fd, output, 50, 50, 256, 256, true);
	layers[2] = add_layer(drm_fd, output, 300, 300, 128, 128, false);
	layers[3] = add_layer(drm_fd, output, 400, 400, 128, 128, true);

	liftoff_layer_set_property(layers[0], "zpos", 0);
	liftoff_layer_set_property(layers[1], "zpos", 1);
	liftoff_layer_set_property(layers[2], "zpos", 2);
	liftoff_layer_set_property(layers[3], "zpos", 3);

	req = drmModeAtomicAlloc();
	if (!liftoff_display_apply(display, req)) {
		perror("liftoff_display_commit");
		return 1;
	}

	ret = drmModeAtomicCommit(drm_fd, req, DRM_MODE_ATOMIC_NONBLOCK, NULL);
	if (ret < 0) {
		perror("drmModeAtomicCommit");
		return false;
	}

	for (i = 0; i < sizeof(layers) / sizeof(layers[0]); i++) {
		printf("Layer %zu got assigned to plane %u\n", i,
		       liftoff_layer_get_plane_id(layers[i]));
	}

	sleep(1);

	drmModeAtomicFree(req);
	for (i = 0; i < sizeof(layers) / sizeof(layers[0]); i++) {
		liftoff_layer_destroy(layers[i]);
	}
	liftoff_output_destroy(output);
	drmModeFreeCrtc(crtc);
	drmModeFreeConnector(connector);
	liftoff_display_destroy(display);
	return 0;
}
