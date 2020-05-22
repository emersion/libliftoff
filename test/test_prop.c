#include <assert.h>
#include <unistd.h>
#include <libliftoff.h>
#include <stdio.h>
#include <string.h>
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
	const char *test_name;
	struct liftoff_mock_plane *mock_plane_with_prop,
				  *mock_plane_without_prop;
	int drm_fd;
	struct liftoff_device *device;
	struct liftoff_output *output;
	struct liftoff_layer *layer;
	drmModeAtomicReq *req;
	bool ok;

	liftoff_log_init(LIFTOFF_DEBUG, NULL);

	if (argc != 2) {
		fprintf(stderr, "usage: %s <test-name>\n", argv[0]);
		return 1;
	}
	test_name = argv[1];

	mock_plane_without_prop = liftoff_mock_drm_create_plane(DRM_PLANE_TYPE_OVERLAY);
	mock_plane_with_prop = liftoff_mock_drm_create_plane(DRM_PLANE_TYPE_OVERLAY);

	/* Value that requires the prop for the plane allocation to succeed */
	uint64_t require_prop_value;
	/* Value that doesn't require the prop to be present */
	uint64_t default_value;
	if (strcmp(test_name, "alpha") == 0) {
		require_prop_value = (uint16_t)(0.5 * 0xFFFF);
		default_value = 0xFFFF; /* opaque */
	} else {
		fprintf(stderr, "no such test: %s\n", test_name);
		return 1;
	}

	/* We need to setup mock plane properties before creating the liftoff
	 * device */
	drmModePropertyRes prop = {0};
	strncpy(prop.name, test_name, sizeof(prop.name) - 1);
	liftoff_mock_plane_add_property(mock_plane_with_prop, &prop);

	drm_fd = liftoff_mock_drm_open();
	device = liftoff_device_create(drm_fd);
	assert(device != NULL);

	output = liftoff_output_create(device, liftoff_mock_drm_crtc_id);
	layer = add_layer(output, 0, 0, 1920, 1080);

	liftoff_mock_plane_add_compatible_layer(mock_plane_without_prop, layer);

	/* First test that the layer doesn't get assigned to the plane without
	 * the prop when using a non-default value */

	req = drmModeAtomicAlloc();

	liftoff_layer_set_property(layer, prop.name, require_prop_value);

	ok = liftoff_output_apply(output, req);
	assert(ok);
	assert(liftoff_layer_get_plane_id(layer) == 0);
	drmModeAtomicFree(req);

	/* The layer should get assigned to the plane without the prop when
	 * using the default value */

	req = drmModeAtomicAlloc();

	liftoff_layer_set_property(layer, prop.name, default_value);

	ok = liftoff_output_apply(output, req);
	assert(ok);
	assert(liftoff_layer_get_plane_id(layer) != 0);
	drmModeAtomicFree(req);

	/* The layer should get assigned to the plane with the prop when using
	 * a non-default value */

	liftoff_mock_plane_add_compatible_layer(mock_plane_with_prop, layer);

	req = drmModeAtomicAlloc();

	liftoff_layer_set_property(layer, prop.name, require_prop_value);

	ok = liftoff_output_apply(output, req);
	assert(ok);
	assert(liftoff_layer_get_plane_id(layer) != 0);
	drmModeAtomicFree(req);

	liftoff_device_destroy(device);
	close(drm_fd);

	return 0;
}
