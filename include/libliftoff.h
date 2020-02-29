#ifndef LIFTOFF_H
#define LIFTOFF_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <xf86drmMode.h>

struct liftoff_device;
struct liftoff_output;
struct liftoff_layer;

/**
 * Initialize libliftoff for a DRM node. The node is expected to have
 * DRM_CLIENT_CAP_UNIVERSAL_PLANES and DRM_CLIENT_CAP_ATOMIC enabled.
 */
struct liftoff_device *liftoff_device_create(int drm_fd);
void liftoff_device_destroy(struct liftoff_device *device);
/**
 * Build a layer to plane mapping and append the plane configuration to `req`.
 * Callers are expected to commit `req` afterwards and can read the layer to
 * plane mapping with `liftoff_layer_get_plane_id`.
 */
bool liftoff_output_apply(struct liftoff_output *output, drmModeAtomicReq *req);

/**
 * Make the device manage a CRTC's planes. The returned output allows callers
 * to attach layers.
 */
struct liftoff_output *liftoff_output_create(struct liftoff_device *device,
					     uint32_t crtc_id);
void liftoff_output_destroy(struct liftoff_output *output);
/**
 * Indicate on which layer composition can take place. Users should be able to
 * blend layers that haven't been mapped to a plane to this layer. The
 * composition layer won't be used if all other layers have been mapped to a
 * plane. There is at most one composition layer per output.
 */
void liftoff_output_set_composition_layer(struct liftoff_output *output,
					  struct liftoff_layer *layer);

/**
 * Create a new layer on an output.
 */
struct liftoff_layer *liftoff_layer_create(struct liftoff_output *output);
void liftoff_layer_destroy(struct liftoff_layer *layer);
/**
 * Set a property on the layer. Any plane property can be set (except CRTC_ID).
 * If none of the planes support the property, the layer won't be mapped to any
 * plane.
 *
 * Setting a zero FB_ID disables the layer.
 */
void liftoff_layer_set_property(struct liftoff_layer *layer, const char *name,
				uint64_t value);
/**
 * Force composition on this layer. This unsets any previous FB_ID value. To
 * switch back to direct scan-out, set FB_ID again.
 *
 * This can be used when no KMS FB ID is available for this layer but it still
 * needs to be displayed (e.g. the buffer cannot be imported in KMS).
 */
void liftoff_layer_set_fb_composited(struct liftoff_layer *layer);
/**
 * Retrieve the plane mapped to this layer. Zero is returned if no plane is
 * mapped.
 */
uint32_t liftoff_layer_get_plane_id(struct liftoff_layer *layer);

enum liftoff_log_importance {
	LIFTOFF_SILENT,
	LIFTOFF_ERROR,
	LIFTOFF_DEBUG,
};

typedef void (*liftoff_log_func)(enum liftoff_log_importance importance,
				 const char *fmt, va_list args);

void liftoff_log_init(enum liftoff_log_importance verbosity,
		      liftoff_log_func callback);

#endif
