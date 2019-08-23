#ifndef HWC_H
#define HWC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <xf86drmMode.h>

/**
 * Initialize libhwc for a DRM node. The node is expected to have
 * DRM_CLIENT_CAP_UNIVERSAL_PLANES and DRM_CLIENT_CAP_ATOMIC enabled.
 */
struct hwc_display *hwc_display_create(int drm_fd);
void hwc_display_destroy(struct hwc_display *display);
/**
 * Build a layer to plane mapping and append the plane configuration to `req`.
 * Callers are expected to commit `req` afterwards and can read the layer to
 * plane mapping with `hwc_layer_get_plane_id`.
 */
bool hwc_display_apply(struct hwc_display *display, drmModeAtomicReq *req);

/**
 * Make the display manage a CRTC's planes. The returned output allows callers
 * to attach layers.
 */
struct hwc_output *hwc_output_create(struct hwc_display *display,
				     uint32_t crtc_id);
void hwc_output_destroy(struct hwc_output *output);

/**
 * Create a new layer on an output.
 */
struct hwc_layer *hwc_layer_create(struct hwc_output *output);
void hwc_layer_destroy(struct hwc_layer *layer);
/**
 * Set a property on the layer. Any plane property can be set. If none of the
 * planes support the property, the layer won't be mapped to any plane.
 */
void hwc_layer_set_property(struct hwc_layer *layer, const char *name,
			    uint64_t value);
/**
 * Retrieve the plane mapped to this layer. Zero is returned if no plane is
 * mapped.
 */
uint32_t hwc_layer_get_plane_id(struct hwc_layer *layer);

#endif
