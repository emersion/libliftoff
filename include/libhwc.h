#ifndef HWC_H
#define HWC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <xf86drmMode.h>

struct hwc_display *hwc_display_create(int drm_fd);
void hwc_display_destroy(struct hwc_display *display);
bool hwc_display_apply(struct hwc_display *display, drmModeAtomicReq *req);

struct hwc_output *hwc_output_create(struct hwc_display *display,
				     uint32_t crtc_id);
void hwc_output_destroy(struct hwc_output *output);

struct hwc_layer *hwc_layer_create(struct hwc_output *output);
void hwc_layer_destroy(struct hwc_layer *layer);
void hwc_layer_set_property(struct hwc_layer *layer, const char *name,
			    uint64_t value);
uint32_t hwc_layer_get_plane_id(struct hwc_layer *layer);

#endif
