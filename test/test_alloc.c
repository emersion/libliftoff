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

struct test_plane {
	int type;
};

/* This structure describes a layer in a test case. The first block of fields
 * describe the layer properties: geometry, vertical ordering, etc. The `compat`
 * field describes which hardware planes the layer is compatible with. The
 * `result` field describes the expected hardware plane returned by libliftoff.
 */
struct test_layer {
	int x, y, width, height;
	int zpos; /* zero means unset */
	bool composition;
	bool force_composited;

	struct test_plane *compat[64];
	struct test_plane *result;
};

struct test_case {
	const char *name;
	struct test_layer layers[64];
};

/* This array describes the hardware we're going to perform the tests with. Our
 * hardware has one primary plane at the bottom position, two overlay planes
 * at the middle position (with undefined ordering between themselves), and one
 * cursor plane at the top.
 */
static struct test_plane test_setup[] = {
	{ .type = DRM_PLANE_TYPE_PRIMARY }, /* zpos = 0 */
	{ .type = DRM_PLANE_TYPE_CURSOR }, /* zpos = 2 */
	{ .type = DRM_PLANE_TYPE_OVERLAY }, /* zpos = 1 */
	{ .type = DRM_PLANE_TYPE_OVERLAY }, /* zpos = 1 */
};

static const size_t test_setup_len = sizeof(test_setup) / sizeof(test_setup[0]);

#define PRIMARY_PLANE &test_setup[0]
#define CURSOR_PLANE &test_setup[1]
#define OVERLAY_PLANE &test_setup[2]

/* non-primary planes */
#define FIRST_2_SECONDARY_PLANES { &test_setup[1], &test_setup[2] }
#define FIRST_3_SECONDARY_PLANES { &test_setup[1], &test_setup[2], \
				   &test_setup[3] }

static struct test_case tests[] = {
	{
		.name = "empty",
	},
	{
		.name = "simple-1x-fail",
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.compat = { NULL },
				.result = NULL,
			},
		},
	},
	{
		.name = "simple-1x",
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
		},
	},
	{
		.name = "simple-3x",
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.compat = { CURSOR_PLANE },
				.result = CURSOR_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.compat = { OVERLAY_PLANE },
				.result = OVERLAY_PLANE,
			},
		},
	},
	{
		.name = "zpos-3x",
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 2,
				.compat = FIRST_2_SECONDARY_PLANES,
				.result = OVERLAY_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 3,
				.compat = FIRST_2_SECONDARY_PLANES,
				.result = CURSOR_PLANE,
			},
		},
	},
	{
		.name = "zpos-3x-intersect-fail",
		/* Layer 1 is over layer 2 but falls back to composition. Since
		 * they intersect, layer 2 needs to be composited too. */
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 3,
				.compat = { NULL },
				.result = NULL,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 2,
				.compat = FIRST_2_SECONDARY_PLANES,
				.result = NULL,
			},
		},
	},
	{
		.name = "zpos-3x-intersect-partial",
		/* Layer 1 is only compatible with the cursor plane. Layer 2 is
		 * only compatible with the overlay plane. Layer 2 is over layer
		 * 1, but the cursor plane is over the overlay plane. There is a
		 * zpos conflict, only one of these two layers can be mapped to
		 * a plane. */
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 2,
				.compat = { CURSOR_PLANE },
				.result = NULL,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 3,
				.compat = { OVERLAY_PLANE },
				.result = OVERLAY_PLANE,
			},
		},
	},
	{
		.name = "zpos-3x-disjoint-partial",
		/* Layer 1 is over layer 2 and falls back to composition. Since
		 * they don't intersect, layer 2 can be mapped to a plane. */
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 3,
				.compat = { NULL },
				.result = NULL,
			},
			{
				.x = 100,
				.y = 100,
				.width = 100,
				.height = 100,
				.zpos = 2,
				.compat = { CURSOR_PLANE },
				.result = CURSOR_PLANE,
			},
		},
	},
	{
		.name = "zpos-3x-disjoint",
		/* Layer 1 is only compatible with the cursor plane. Layer 2 is
		 * only compatible with the overlay plane. Layer 2 is over layer
		 * 1, but the cursor plane is over the overlay plane. There is a
		 * zpos conflict, however since these two layers don't
		 * intersect, we can still map them to planes. */
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 2,
				.compat = { CURSOR_PLANE },
				.result = CURSOR_PLANE,
			},
			{
				.x = 100,
				.y = 100,
				.width = 100,
				.height = 100,
				.zpos = 3,
				.compat = { OVERLAY_PLANE },
				.result = OVERLAY_PLANE,
			},
		},
	},
	{
		.name = "zpos-4x-intersect-partial",
		/* We have 4 layers and 4 planes. However since they all
		 * intersect and the ordering between both overlay planes is
		 * undefined, we can only use 3 planes. */
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 4,
				.compat = FIRST_3_SECONDARY_PLANES,
				.result = CURSOR_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 2,
				.compat = FIRST_3_SECONDARY_PLANES,
				.result = NULL,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 3,
				.compat = FIRST_3_SECONDARY_PLANES,
				.result = &test_setup[3],
			},
		},
	},
	{
		.name = "zpos-4x-disjoint",
		/* Ordering between the two overlay planes isn't defined,
		 * however layers 2 and 3 don't intersect so they can be mapped
		 * to these planes nonetheless. */
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 4,
				.compat = FIRST_3_SECONDARY_PLANES,
				.result = CURSOR_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 2,
				.compat = { &test_setup[3] },
				.result = &test_setup[3],
			},
			{
				.x = 100,
				.y = 100,
				.width = 100,
				.height = 100,
				.zpos = 3,
				.compat = { OVERLAY_PLANE },
				.result = OVERLAY_PLANE,
			},
		},
	},
	{
		.name = "zpos-4x-disjoint-alt",
		/* Same as zpos-4x-disjoint, but with the last two layers'
		 * plane swapped. */
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 4,
				.compat = FIRST_3_SECONDARY_PLANES,
				.result = CURSOR_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 2,
				.compat = { OVERLAY_PLANE },
				.result = OVERLAY_PLANE,
			},
			{
				.x = 100,
				.y = 100,
				.width = 100,
				.height = 100,
				.zpos = 3,
				.compat = { &test_setup[3] },
				.result = &test_setup[3],
			},
		},
	},
	{
		.name = "zpos-4x-domino-fail",
		/* A layer on top falls back to composition. There is a layer at
		 * zpos=2 which doesn't overlap and could be mapped to a plane,
		 * however another layer at zpos=3 overlaps both and prevents
		 * all layers from being mapped to a plane. */
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 4,
				.compat = { NULL },
				.result = NULL,
			},
			{
				.x = 100,
				.y = 100,
				.width = 100,
				.height = 100,
				.zpos = 2,
				.compat = FIRST_3_SECONDARY_PLANES,
				.result = NULL,
			},
			{
				.x = 50,
				.y = 50,
				.width = 100,
				.height = 100,
				.zpos = 3,
				.compat = FIRST_3_SECONDARY_PLANES,
				.result = NULL,
			},
		},
	},
	{
		.name = "zpos-4x-domino-partial",
		/* A layer on top falls back to composition. A layer at zpos=2
		 * falls back to composition too because it's underneath. A
		 * layer at zpos=3 doesn't intersect with the one at zpos=4 and
		 * is over the one at zpos=2 so it can be mapped to a plane. */
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 4,
				.compat = { NULL },
				.result = NULL,
			},
			{
				.x = 100,
				.y = 100,
				.width = 100,
				.height = 100,
				.zpos = 3,
				.compat = FIRST_3_SECONDARY_PLANES,
				.result = CURSOR_PLANE,
			},
			{
				.x = 50,
				.y = 50,
				.width = 100,
				.height = 100,
				.zpos = 2,
				.compat = FIRST_3_SECONDARY_PLANES,
				.result = NULL,
			},
		},
	},
	{
		.name = "composition-3x",
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.composition = true,
				.compat = { PRIMARY_PLANE },
				.result = NULL,
			},
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 2,
				.compat = FIRST_2_SECONDARY_PLANES,
				.result = OVERLAY_PLANE,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 3,
				.compat = FIRST_2_SECONDARY_PLANES,
				.result = CURSOR_PLANE,
			},
		},
	},
	{
		.name = "composition-3x-fail",
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.composition = true,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.compat = { PRIMARY_PLANE },
				.result = NULL,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 2,
				.compat = FIRST_2_SECONDARY_PLANES,
				.result = NULL,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 3,
				.compat = { 0 },
				.result = NULL,
			},
		},
	},
	{
		.name = "composition-3x-partial",
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.composition = true,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.compat = { PRIMARY_PLANE },
				.result = NULL,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 2,
				.compat = { 0 },
				.result = NULL,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 3,
				.compat = { CURSOR_PLANE },
				.result = CURSOR_PLANE,
			},
		},
	},
	{
		.name = "composition-3x-force",
		/* Layers at zpos=1 and zpos=2 could be put on a plane, but
		 * FB composition is forced on the zpos=2 one. As a result, only
		 * the layer at zpos=3 can be put into a plane. */
		.layers = {
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.composition = true,
				.compat = { PRIMARY_PLANE },
				.result = PRIMARY_PLANE,
			},
			{
				.width = 1920,
				.height = 1080,
				.zpos = 1,
				.compat = { PRIMARY_PLANE },
				.result = NULL,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 2,
				.force_composited = true,
				.compat = FIRST_2_SECONDARY_PLANES,
				.result = NULL,
			},
			{
				.width = 100,
				.height = 100,
				.zpos = 3,
				.compat = { CURSOR_PLANE },
				.result = CURSOR_PLANE,
			},
		},
	},
};

static bool test_needs_composition(struct test_layer *test_layers)
{
	size_t i;

	for (i = 0; test_layers[i].width > 0; i++) {
		if (test_layers[i].result == NULL) {
			return true;
		}
	}

	return false;
}

static void run_test(struct test_layer *test_layers)
{
	size_t i, j;
	ssize_t plane_index_got, plane_index_want;
	struct liftoff_mock_plane *mock_planes[64];
	struct liftoff_mock_plane *mock_plane;
	struct test_layer *test_layer;
	int drm_fd;
	struct liftoff_device *device;
	struct liftoff_output *output;
	struct liftoff_layer *layers[64];
	struct liftoff_plane *plane;
	drmModeAtomicReq *req;
	bool ok;
	int ret;
	uint32_t plane_id;

	for (i = 0; i < test_setup_len; i++) {
		mock_planes[i] = liftoff_mock_drm_create_plane(test_setup[i].type);
	}

	drm_fd = liftoff_mock_drm_open();
	device = liftoff_device_create(drm_fd);
	assert(device != NULL);

	liftoff_device_register_all_planes(device);

	output = liftoff_output_create(device, liftoff_mock_drm_crtc_id);
	for (i = 0; test_layers[i].width > 0; i++) {
		test_layer = &test_layers[i];
		layers[i] = add_layer(output, test_layer->x, test_layer->y,
				      test_layer->width, test_layer->height);
		if (test_layer->zpos != 0) {
			liftoff_layer_set_property(layers[i], "zpos",
						   test_layer->zpos);
		}
		if (test_layer->composition) {
			liftoff_output_set_composition_layer(output, layers[i]);
		}
		if (test_layer->force_composited) {
			liftoff_layer_set_fb_composited(layers[i]);
		}
		for (j = 0; test_layer->compat[j] != NULL; j++) {
			mock_plane = mock_planes[test_layer->compat[j] -
						 test_setup];
			liftoff_mock_plane_add_compatible_layer(mock_plane,
								layers[i]);
		}
	}

	req = drmModeAtomicAlloc();
	ok = liftoff_output_apply(output, req, 0);
	assert(ok);
	ret = drmModeAtomicCommit(drm_fd, req, 0, NULL);
	assert(ret == 0);
	drmModeAtomicFree(req);

	for (i = 0; test_layers[i].width > 0; i++) {
		plane = liftoff_layer_get_plane(layers[i]);
		mock_plane = NULL;
		if (plane != NULL) {
			plane_id = liftoff_plane_get_id(plane);
			mock_plane = liftoff_mock_drm_get_plane(plane_id);
		}
		plane_index_got = -1;
		for (j = 0; j < test_setup_len; j++) {
			if (mock_planes[j] == mock_plane) {
				plane_index_got = j;
				break;
			}
		}
		assert(mock_plane == NULL || plane_index_got >= 0);

		fprintf(stderr, "layer %zu got assigned to plane %d\n",
			i, (int)plane_index_got);

		plane_index_want = -1;
		if (test_layers[i].result != NULL) {
			plane_index_want = test_layers[i].result - test_setup;
		}

		if (plane_index_got != plane_index_want) {
			fprintf(stderr, "  ERROR: want plane %d\n",
				(int)plane_index_want);
			ok = false;
		}
	}
	assert(ok);

	assert(test_needs_composition(test_layers) ==
	       liftoff_output_needs_composition(output));

	liftoff_output_destroy(output);
	liftoff_device_destroy(device);
	close(drm_fd);
}

static void test_basic(void)
{
	struct liftoff_mock_plane *mock_plane;
	int drm_fd;
	struct liftoff_device *device;
	struct liftoff_output *output;
	struct liftoff_layer *layer;
	drmModeAtomicReq *req;
	bool ok;
	int ret;

	mock_plane = liftoff_mock_drm_create_plane(DRM_PLANE_TYPE_PRIMARY);

	drm_fd = liftoff_mock_drm_open();
	device = liftoff_device_create(drm_fd);
	assert(device != NULL);

	liftoff_device_register_all_planes(device);

	output = liftoff_output_create(device, liftoff_mock_drm_crtc_id);
	layer = add_layer(output, 0, 0, 1920, 1080);

	liftoff_mock_plane_add_compatible_layer(mock_plane, layer);

	req = drmModeAtomicAlloc();
	ok = liftoff_output_apply(output, req, 0);
	assert(ok);
	ret = drmModeAtomicCommit(drm_fd, req, 0, NULL);
	assert(ret == 0);
	assert(liftoff_mock_plane_get_layer(mock_plane) == layer);
	drmModeAtomicFree(req);

	liftoff_device_destroy(device);
	close(drm_fd);
}

/* Checks that the library doesn't allocate a plane for a layer without a
 * non-zero FB_ID set. */
static void test_no_fb_fail(bool zero_fb_id)
{
	struct liftoff_mock_plane *mock_plane;
	int drm_fd;
	struct liftoff_device *device;
	struct liftoff_output *output;
	struct liftoff_layer *layer;
	drmModeAtomicReq *req;
	bool ok;
	int ret;

	mock_plane = liftoff_mock_drm_create_plane(DRM_PLANE_TYPE_PRIMARY);

	drm_fd = liftoff_mock_drm_open();
	device = liftoff_device_create(drm_fd);
	assert(device != NULL);

	liftoff_device_register_all_planes(device);

	output = liftoff_output_create(device, liftoff_mock_drm_crtc_id);
	layer = liftoff_layer_create(output);
	if (zero_fb_id) {
		liftoff_layer_set_property(layer, "FB_ID", 0);
	}

	liftoff_mock_plane_add_compatible_layer(mock_plane, layer);

	req = drmModeAtomicAlloc();
	ok = liftoff_output_apply(output, req, 0);
	assert(ok);
	ret = drmModeAtomicCommit(drm_fd, req, 0, NULL);
	assert(ret == 0);
	assert(liftoff_mock_plane_get_layer(mock_plane) == NULL);
	drmModeAtomicFree(req);

	liftoff_device_destroy(device);
	close(drm_fd);
}

/* Checks that the library doesn't fallback to composition when a layer doesn't
 * have a FB. */
static void test_composition_zero_fb(void)
{
	struct liftoff_mock_plane *mock_plane;
	int drm_fd;
	struct liftoff_device *device;
	struct liftoff_output *output;
	struct liftoff_layer *composition_layer, *layer_with_fb,
			     *layer_without_fb;
	drmModeAtomicReq *req;
	bool ok;
	int ret;

	mock_plane = liftoff_mock_drm_create_plane(DRM_PLANE_TYPE_PRIMARY);

	drm_fd = liftoff_mock_drm_open();
	device = liftoff_device_create(drm_fd);
	assert(device != NULL);

	liftoff_device_register_all_planes(device);

	output = liftoff_output_create(device, liftoff_mock_drm_crtc_id);
	composition_layer = add_layer(output, 0, 0, 1920, 1080);
	layer_with_fb = add_layer(output, 0, 0, 1920, 1080);
	layer_without_fb = liftoff_layer_create(output);
	(void)layer_with_fb;

	liftoff_output_set_composition_layer(output, composition_layer);

	liftoff_mock_plane_add_compatible_layer(mock_plane, composition_layer);
	liftoff_mock_plane_add_compatible_layer(mock_plane, layer_without_fb);
	liftoff_mock_plane_add_compatible_layer(mock_plane, layer_with_fb);

	req = drmModeAtomicAlloc();
	ok = liftoff_output_apply(output, req, 0);
	assert(ok);
	ret = drmModeAtomicCommit(drm_fd, req, 0, NULL);
	assert(ret == 0);
	assert(liftoff_mock_plane_get_layer(mock_plane) == layer_with_fb);
	drmModeAtomicFree(req);

	liftoff_device_destroy(device);
	close(drm_fd);
}

int main(int argc, char *argv[]) {
	const char *test_name;
	size_t i;

	liftoff_log_set_priority(LIFTOFF_DEBUG);

	if (argc != 2) {
		fprintf(stderr, "usage: %s <test-name>\n", argv[0]);
		return 1;
	}
	test_name = argv[1];

	if (strcmp(test_name, "basic") == 0) {
		test_basic();
		return 0;
	} else if (strcmp(test_name, "no-props-fail") == 0) {
		test_no_fb_fail(false);
		return 0;
	} else if (strcmp(test_name, "zero-fb-id-fail") == 0) {
		test_no_fb_fail(true);
		return 0;
	} else if (strcmp(test_name, "composition-zero-fb-id") == 0) {
		test_composition_zero_fb();
		return 0;
	}

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		if (strcmp(tests[i].name, test_name) == 0) {
			run_test(tests[i].layers);
			return 0;
		}
	}

	fprintf(stderr, "no such test: %s\n", test_name);
	return 1;
}
