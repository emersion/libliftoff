#include <assert.h>
#include <unistd.h>
#include <libliftoff.h>
#include <stdio.h>
#include <string.h>
#include "libdrm_mock.h"

/* Number of page-flips before the plane allocation has stabilized */
#define STABILIZE_PAGEFLIP_COUNT 600 /* 10s at 60FPS */

static struct liftoff_layer *add_layer(struct liftoff_output *output,
				       int x, int y, int width, int height)
{
	uint32_t fb_id;
	struct liftoff_layer *layer;

	layer = liftoff_layer_create(output);
	fb_id = liftoff_mock_drm_create_fb(layer);
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

int main(int argc, char *argv[]) {
	struct liftoff_mock_plane *mock_plane;
	int drm_fd;
	struct liftoff_display *display;
	struct liftoff_output *output;
	struct liftoff_layer *layers[2], *layer;
	uint32_t fbs[2];
	drmModeAtomicReq *req;
	bool ok;

	liftoff_log_init(LIFTOFF_SILENT, NULL);

	mock_plane = liftoff_mock_drm_create_plane(DRM_PLANE_TYPE_PRIMARY);
	/* Plane incompatible with all layers */
	liftoff_mock_drm_create_plane(DRM_PLANE_TYPE_CURSOR);

	drm_fd = liftoff_mock_drm_open();
	display = liftoff_display_create(drm_fd);
	assert(display != NULL);

	output = liftoff_output_create(display, liftoff_mock_drm_crtc_id);
	layers[0] = add_layer(output, 0, 0, 1920, 1080);
	layers[1] = add_layer(output, 0, 0, 1920, 1080);

	/* All layers are compatible with the primary plane */
	liftoff_mock_plane_add_compatible_layer(mock_plane, layers[0]);
	liftoff_mock_plane_add_compatible_layer(mock_plane, layers[1]);

	for (size_t i = 0; i < sizeof(layers) / sizeof(layers[0]); i++) {
		/* We will continuously update layers[i]. After some time we
		 * want to see it get a plane. */
		layer = layers[i];
		fprintf(stderr, "Testing layer %zu\n", i);

		fbs[0] = liftoff_mock_drm_create_fb(layer);
		fbs[1] = liftoff_mock_drm_create_fb(layer);

		req = drmModeAtomicAlloc();
		for (int j = 0; j < STABILIZE_PAGEFLIP_COUNT; j++) {
			drmModeAtomicSetCursor(req, 0);

			liftoff_layer_set_property(layer, "FB_ID", fbs[j % 2]);

			ok = liftoff_display_apply(display, req);
			assert(ok);
		}

		assert(liftoff_mock_plane_get_layer(mock_plane, req) == layer);

		drmModeAtomicFree(req);
	}

	liftoff_display_destroy(display);
	close(drm_fd);

	return 0;
}
