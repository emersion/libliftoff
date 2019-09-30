#ifndef LIFTOFF_H
#define LIFTOFF_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <xf86drmMode.h>

struct liftoff_display;
struct liftoff_output;
struct liftoff_layer;

/**
 * Initialize libliftoff for a DRM node. The node is expected to have
 * DRM_CLIENT_CAP_UNIVERSAL_PLANES and DRM_CLIENT_CAP_ATOMIC enabled.
 */
struct liftoff_display *liftoff_display_create(int drm_fd);
void liftoff_display_destroy(struct liftoff_display *display);
/**
 * Build a layer to plane mapping and append the plane configuration to `req`.
 * Callers are expected to commit `req` afterwards and can read the layer to
 * plane mapping with `liftoff_layer_get_plane_id`.
 */
bool liftoff_display_apply(struct liftoff_display *display,
			   drmModeAtomicReq *req);

/**
 * Make the display manage a CRTC's planes. The returned output allows callers
 * to attach layers.
 */
struct liftoff_output *liftoff_output_create(struct liftoff_display *display,
					     uint32_t crtc_id);
void liftoff_output_destroy(struct liftoff_output *output);

/**
 * Create a new layer on an output.
 */
struct liftoff_layer *liftoff_layer_create(struct liftoff_output *output);
void liftoff_layer_destroy(struct liftoff_layer *layer);
/**
 * Set a property on the layer. Any plane property can be set. If none of the
 * planes support the property, the layer won't be mapped to any plane.
 */
void liftoff_layer_set_property(struct liftoff_layer *layer, const char *name,
				uint64_t value);
/**
 * Retrieve the plane mapped to this layer. Zero is returned if no plane is
 * mapped.
 */
uint32_t liftoff_layer_get_plane_id(struct liftoff_layer *layer);

#endif
