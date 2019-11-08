/* Dynamic: create a few layers, setup a rendering loop for one of them. The
 * result is a rectangle updating its color while all other layers that make it
 * into a plane are static. */

#define _POSIX_C_SOURCE 200809L
#include <drm_fourcc.h>
#include <fcntl.h>
#include <libliftoff.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "common.h"

#define LAYERS_LEN 4

struct example_layer {
	float color[3];
	int dec;
	int x, y;

	struct dumb_fb fbs[2];
	size_t front_fb;

	struct liftoff_layer *layer;
};

struct pageflip_context {
	int drm_fd;
	drmModeConnectorPtr connector;
	drmModeCrtcPtr crtc;
};

static struct liftoff_display *display = NULL;
static struct example_layer layers[LAYERS_LEN] = {0};
static size_t active_layer_idx = 2;

static bool init_layer(int drm_fd, struct example_layer *layer,
		       struct liftoff_output *output, int width, int height,
		       bool with_alpha)
{
	static size_t color_idx = 0;
	static float color_value = 1.0;
	uint32_t format;

	format = with_alpha ? DRM_FORMAT_ARGB8888 : DRM_FORMAT_XRGB8888;
	if (!dumb_fb_init(&layer->fbs[0], drm_fd, format, width, height) ||
	    !dumb_fb_init(&layer->fbs[1], drm_fd, format, width, height)) {
		fprintf(stderr, "failed to create framebuffer\n");
		return false;
	}

	layer->layer = liftoff_layer_create(output);
	liftoff_layer_set_property(layer->layer, "CRTC_W", width);
	liftoff_layer_set_property(layer->layer, "CRTC_H", height);
	liftoff_layer_set_property(layer->layer, "SRC_X", 0);
	liftoff_layer_set_property(layer->layer, "SRC_Y", 0);
	liftoff_layer_set_property(layer->layer, "SRC_W", width << 16);
	liftoff_layer_set_property(layer->layer, "SRC_H", height << 16);

	layer->color[color_idx % 3] = color_value;
	color_idx++;
	if (color_idx % 3 == 0) {
		color_value -= 0.1;
	}

	return true;
}

static void draw_layer(int drm_fd, struct example_layer *layer)
{
	uint32_t color;
	struct dumb_fb *fb;

	layer->front_fb = (layer->front_fb + 1) % 2;
	fb = &layer->fbs[layer->front_fb];

	color = ((uint32_t)0xFF << 24) |
		((uint32_t)(layer->color[0] * 0xFF) << 16) |
		((uint32_t)(layer->color[1] * 0xFF) << 8) |
		(uint32_t)(layer->color[2] * 0xFF);

	dumb_fb_fill(fb, drm_fd, color);

	liftoff_layer_set_property(layer->layer, "FB_ID", fb->id);
	liftoff_layer_set_property(layer->layer, "CRTC_X", layer->x);
	liftoff_layer_set_property(layer->layer, "CRTC_Y", layer->y);
}

static bool draw(struct pageflip_context *context)
{
	struct example_layer *active_layer;
	drmModeAtomicReq *req;
	int ret, inc;
	size_t i;

	active_layer = &layers[active_layer_idx];

	inc = (active_layer->dec + 1) % 3;

	active_layer->color[inc] += 0.05;
	active_layer->color[active_layer->dec] -= 0.05;

	if (active_layer->color[active_layer->dec] < 0.0f) {
		active_layer->color[inc] = 1.0f;
		active_layer->color[active_layer->dec] = 0.0f;
		active_layer->dec = inc;
	}

	draw_layer(context->drm_fd, active_layer);

	req = drmModeAtomicAlloc();
	if (!liftoff_display_apply(display, req)) {
		perror("liftoff_display_commit");
		return false;
	}

	set_global_properties(context->drm_fd, req, context->connector, context->crtc, &context->connector->modes[0]);

	ret = drmModeAtomicCommit(context->drm_fd, req, DRM_MODE_ATOMIC_NONBLOCK |
				  DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_ALLOW_MODESET, context);
	if (ret < 0) {
		perror("drmModeAtomicCommit");
		return false;
	}

	drmModeAtomicFree(req);

	for (i = 0; i < sizeof(layers) / sizeof(layers[0]); i++) {
		printf("Layer %zu got assigned to plane %u\n", i,
		       liftoff_layer_get_plane_id(layers[i].layer));
	}

	return true;
}

static void page_flip_handler(int fd, unsigned seq, unsigned tv_sec,
			      unsigned tv_usec, unsigned crtc_id, void *data)
{
	struct pageflip_context *context = data;
	draw(context);
}

int main(int argc, char *argv[])
{
	struct pageflip_context context = {
		.drm_fd = -1,
		.connector = NULL,
		.crtc = NULL,
	};
	drmModeRes *drm_res;
	struct liftoff_output *output;
	size_t i;
	int ret;

	context.drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (context.drm_fd < 0) {
		perror("open");
		return 1;
	}

	if (drmSetClientCap(context.drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) < 0) {
		perror("drmSetClientCap(UNIVERSAL_PLANES)");
		return 1;
	}
	if (drmSetClientCap(context.drm_fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0) {
		perror("drmSetClientCap(ATOMIC)");
		return 1;
	}

	display = liftoff_display_create(context.drm_fd);
	if (display == NULL) {
		perror("liftoff_display_create");
		return 1;
	}

	drm_res = drmModeGetResources(context.drm_fd);
	context.connector = pick_connector(context.drm_fd, drm_res);
	context.crtc = pick_crtc(context.drm_fd, drm_res, context.connector);
	disable_all_crtcs_except(context.drm_fd, drm_res, context.crtc->crtc_id);
	output = liftoff_output_create(display, context.crtc->crtc_id);
	drmModeFreeResources(drm_res);

	if (context.connector == NULL) {
		fprintf(stderr, "no connector found\n");
		return 1;
	}
	if (context.crtc == NULL) {
		fprintf(stderr, "no CRTC found\n");
		return 1;
	}

	printf("Using connector %d, CRTC %d\n", context.connector->connector_id,
	       context.crtc->crtc_id);

	init_layer(context.drm_fd, &layers[0], output, context.connector->modes[0].hdisplay,
		   context.connector->modes[0].vdisplay, false);
	for (i = 1; i < LAYERS_LEN; i++) {
		init_layer(context.drm_fd, &layers[i], output, 100, 100, i % 2);
		layers[i].x = 100 * i;
		layers[i].y = 100 * i;
	}

	for (i = 0; i < LAYERS_LEN; i++) {
		liftoff_layer_set_property(layers[i].layer, "zpos", i);

		draw_layer(context.drm_fd, &layers[i]);
	}

	draw(&context);

	for (i = 0; i < 120; i++) {
		drmEventContext drm_event = {
			.version = 3,
			.page_flip_handler2 = page_flip_handler,
		};
		struct pollfd pfd = {
			.fd = context.drm_fd,
			.events = POLLIN,
		};

		ret = poll(&pfd, 1, 1000);
		if (ret != 1) {
			perror("poll");
			return 1;
		}

		drmHandleEvent(context.drm_fd, &drm_event);
	}

	for (i = 0; i < sizeof(layers) / sizeof(layers[0]); i++) {
		liftoff_layer_destroy(layers[i].layer);
	}
	liftoff_output_destroy(output);
	drmModeFreeCrtc(context.crtc);
	drmModeFreeConnector(context.connector);
	liftoff_display_destroy(display);
	return 0;
}
