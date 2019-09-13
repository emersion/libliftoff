#include <assert.h>
#include <unistd.h>
#include <libliftoff.h>
#include "libdrm_mock.h"

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
	struct liftoff_layer *layer;
	drmModeAtomicReq *req;
	bool ok;

	mock_plane = liftoff_mock_drm_create_plane(DRM_PLANE_TYPE_PRIMARY);

	drm_fd = liftoff_mock_drm_open();
	display = liftoff_display_create(drm_fd);
	assert(display != NULL);

	output = liftoff_output_create(display, liftoff_mock_drm_crtc_id);
	layer = add_layer(output, 0, 0, 1920, 1080);

	req = drmModeAtomicAlloc();
	ok = liftoff_display_apply(display, req);
	assert(ok);
	assert(liftoff_mock_plane_get_layer(mock_plane, req) == NULL);

	liftoff_mock_plane_add_compatible_layer(mock_plane, layer);

	drmModeAtomicSetCursor(req, 0);
	ok = liftoff_display_apply(display, req);
	assert(ok);
	assert(liftoff_mock_plane_get_layer(mock_plane, req) == layer);
	drmModeAtomicFree(req);

	liftoff_display_destroy(display);
	close(drm_fd);
	return 0;
}
