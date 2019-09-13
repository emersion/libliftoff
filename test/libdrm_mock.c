#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "libdrm_mock.h"

#define MAX_PLANES 64
#define MAX_LAYERS 64
#define MAX_PROPS 64

uint32_t liftoff_mock_drm_crtc_id = 0xCC000000;

struct liftoff_mock_plane {
	uint32_t id;
	int type;
	struct liftoff_layer *compatible_layers[MAX_LAYERS];
};

struct liftoff_mock_prop {
	uint32_t obj_id, prop_id;
	uint64_t value;
};

struct _drmModeAtomicReq {
	struct liftoff_mock_prop props[MAX_PROPS];
	int cursor;
};

static int mock_pipe[2] = {-1, -1};
static struct liftoff_mock_plane mock_planes[MAX_PLANES];
static struct liftoff_layer *mock_fbs[MAX_LAYERS];

enum plane_prop {
	PLANE_TYPE,
	PLANE_FB_ID,
	PLANE_CRTC_ID,
};

static const char *plane_props[] = {
	[PLANE_TYPE] = "type",
	[PLANE_FB_ID] = "FB_ID",
	[PLANE_CRTC_ID] = "CRTC_ID",
	"CRTC_X",
	"CRTC_Y",
	"CRTC_W",
	"CRTC_H",
	"SRC_X",
	"SRC_Y",
	"SRC_W",
	"SRC_H",
};

static void assert_drm_fd(int fd)
{
	int ret;
	struct stat stat_got, stat_want;

	ret = fstat(mock_pipe[0], &stat_want);
	assert(ret == 0);
	ret = fstat(fd, &stat_got);
	assert(ret == 0);

	assert(stat_got.st_dev == stat_want.st_dev &&
	       stat_got.st_ino == stat_want.st_ino);
}

int liftoff_mock_drm_open(void)
{
	int ret;

	assert(mock_pipe[0] < 0);
	ret = pipe(mock_pipe);
	assert(ret == 0);

	return mock_pipe[0];
}

struct liftoff_mock_plane *liftoff_mock_drm_create_plane(int type)
{
	struct liftoff_mock_plane *plane;
	size_t i;

	assert(mock_pipe[0] < 0);

	i = 0;
	plane = &mock_planes[0];
	while (plane->id != 0) {
		plane++;
		i++;
	}

	plane->id = 0xEE000000 + i;
	plane->type = type;
	return plane;
}

struct liftoff_mock_plane *liftoff_mock_drm_get_plane(uint32_t id)
{
	struct liftoff_mock_plane *plane;

	plane = &mock_planes[0];
	while (plane->id != 0) {
		if (plane->id == id) {
			return plane;
		}
		plane++;
	}

	assert(0);
}

void liftoff_mock_plane_add_compatible_layer(struct liftoff_mock_plane *plane,
					     struct liftoff_layer *layer)
{
	size_t i;

	for (i = 0; i < MAX_LAYERS; i++) {
		if (plane->compatible_layers[i] == NULL) {
			plane->compatible_layers[i] = layer;
			return;
		}
	}

	assert(0);
}

uint32_t liftoff_mock_drm_create_fb(struct liftoff_layer *layer)
{
	size_t i;

	i = 0;
	while (mock_fbs[i] != 0) {
		i++;
	}

	mock_fbs[i] = layer;

	return 0xFB000000 + i;
}

static bool mock_atomic_req_get_property(drmModeAtomicReq *req, uint32_t obj_id,
					 enum plane_prop prop, uint64_t *value)
{
	ssize_t i;
	uint32_t prop_id;

	prop_id = 0xB0000000 + prop;
	for (i = req->cursor - 1; i >= 0; i--) {
		if (req->props[i].obj_id == obj_id &&
		    req->props[i].prop_id == prop_id) {
			*value = req->props[i].value;
			return true;
		}
	}

	return false;
}

static struct liftoff_layer *mock_fb_get_layer(uint32_t fb_id)
{
	size_t i;

	if (fb_id == 0) {
		return NULL;
	}

	assert((fb_id & 0xFF000000) == 0xFB000000);

	i = fb_id & 0x00FFFFFF;
	assert(i < MAX_LAYERS);

	return mock_fbs[i];
}

struct liftoff_layer *liftoff_mock_plane_get_layer(struct liftoff_mock_plane *plane,
						   drmModeAtomicReq *req)
{
	uint64_t fb_id;

	if (!mock_atomic_req_get_property(req, plane->id, PLANE_FB_ID, &fb_id)) {
		return NULL;
	}

	return mock_fb_get_layer(fb_id);
}

int drmModeAtomicCommit(int fd, drmModeAtomicReq *req, uint32_t flags,
			void *user_data)
{
	size_t i, j;
	struct liftoff_mock_plane *plane;
	uint64_t fb_id, crtc_id;
	bool has_fb, has_crtc, found;
	struct liftoff_layer *layer;

	assert_drm_fd(fd);
	assert(flags == DRM_MODE_ATOMIC_TEST_ONLY);

	for (i = 0; i < MAX_PLANES; i++) {
		plane = &mock_planes[i];
		if (plane->id == 0) {
			break;
		}

		has_fb = mock_atomic_req_get_property(req, plane->id,
						      PLANE_FB_ID, &fb_id);
		has_crtc = mock_atomic_req_get_property(req, plane->id,
						        PLANE_CRTC_ID,
							&crtc_id);

		has_fb = has_fb && fb_id != 0;
		has_crtc = has_crtc && crtc_id != 0;

		if (has_fb != has_crtc) {
			fprintf(stderr, "plane %u: both FB_ID and CRTC_ID must "
				"be set or unset together\n", plane->id);
			return -EINVAL;
		}

		if (has_fb) {
			if (crtc_id != liftoff_mock_drm_crtc_id) {
				fprintf(stderr, "plane %u: invalid CRTC_ID\n",
					plane->id);
				return -EINVAL;
			}
			layer = mock_fb_get_layer(fb_id);
			if (layer == NULL) {
				fprintf(stderr, "plane %u: invalid FB_ID\n",
					plane->id);
				return -EINVAL;
			}
			found = false;
			for (j = 0; j < MAX_LAYERS; j++) {
				if (plane->compatible_layers[i] == layer) {
					found = true;
					break;
				}
			}
			if (!found) {
				fprintf(stderr, "plane %u: layer %p is not "
					"compatible\n", plane->id,
					(void *)layer);
				return -EINVAL;
			}
		}
	}

	return 0;
}

drmModeRes *drmModeGetResources(int fd)
{
	drmModeRes *res;

	assert_drm_fd(fd);

	res = calloc(1, sizeof(*res));
	res->count_crtcs = 1;
	res->crtcs = &liftoff_mock_drm_crtc_id;
	return res;
}

void drmModeFreeResources(drmModeRes *res)
{
	free(res);
}

drmModePlaneRes *drmModeGetPlaneResources(int fd)
{
	static uint32_t plane_ids[MAX_PLANES];
	drmModePlaneRes *res;
	size_t i;

	assert_drm_fd(fd);

	for (i = 0; i < MAX_PLANES; i++) {
		if (mock_planes[i].id == 0) {
			break;
		}
		plane_ids[i] = mock_planes[i].id;
	}

	res = calloc(1, sizeof(*res));
	res->count_planes = i;
	res->planes = plane_ids;
	return res;
}

void drmModeFreePlaneResources(drmModePlaneRes *res)
{
	free(res);
}

drmModePlane *drmModeGetPlane(int fd, uint32_t id)
{
	drmModePlane *plane;

	assert_drm_fd(fd);

	plane = calloc(1, sizeof(*plane));
	plane->plane_id = id;
	plane->possible_crtcs = 1 << 0;
	return plane;
}

void drmModeFreePlane(drmModePlane *plane) {
	free(plane);
}

drmModeObjectProperties *drmModeObjectGetProperties(int fd, uint32_t obj_id,
						    uint32_t obj_type)
{
	struct liftoff_mock_plane *plane;
	drmModeObjectProperties *props;
	size_t i;
	static uint32_t prop_ids[MAX_PROPS];

	assert_drm_fd(fd);
	assert(obj_type == DRM_MODE_OBJECT_PLANE);

	plane = NULL;
	for (i = 0; i < MAX_PLANES; i++) {
		if (mock_planes[i].id == obj_id) {
			plane = &mock_planes[i];
			break;
		}
	}
	assert(plane != NULL);

	props = calloc(1, sizeof(*props));
	props->count_props = sizeof(plane_props) / sizeof(plane_props[0]);
	props->props = prop_ids;
	for (i = 0; i < props->count_props; i++) {
		prop_ids[i] = 0xB0000000 + i;
	}
	props->prop_values = calloc(props->count_props, sizeof(uint64_t));
	props->prop_values[PLANE_TYPE] = plane->type;
	return props;
}

void drmModeFreeObjectProperties(drmModeObjectProperties *props) {
	free(props->prop_values);
	free(props);
}

drmModePropertyRes *drmModeGetProperty(int fd, uint32_t id)
{
	size_t i;
	drmModePropertyRes *prop;

	assert_drm_fd(fd);
	assert((id & 0xFF000000) == 0xB0000000);

	i = id & 0x00FFFFFF;
	assert(i < sizeof(plane_props) / sizeof(plane_props[0]));

	prop = calloc(1, sizeof(*prop));
	prop->prop_id = id;
	strncpy(prop->name, plane_props[i], sizeof(prop->name) - 1);
	return prop;
}

void drmModeFreeProperty(drmModePropertyRes *prop) {
	free(prop);
}

drmModeAtomicReq *drmModeAtomicAlloc(void)
{
	return calloc(1, sizeof(drmModeAtomicReq));
}

void drmModeAtomicFree(drmModeAtomicReq *req)
{
	free(req);
}

int drmModeAtomicAddProperty(drmModeAtomicReq *req, uint32_t obj_id,
			     uint32_t prop_id, uint64_t value)
{
	assert((size_t)req->cursor < sizeof(req->props) / sizeof(req->props[0]));
	req->props[req->cursor].obj_id = obj_id;
	req->props[req->cursor].prop_id = prop_id;
	req->props[req->cursor].value = value;
	req->cursor++;
	return 0;
}

int drmModeAtomicGetCursor(drmModeAtomicReq *req)
{
	return req->cursor;
}

void drmModeAtomicSetCursor(drmModeAtomicReq *req, int cursor)
{
	req->cursor = cursor;
}
