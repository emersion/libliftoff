#ifndef EXAMPLE_COMMON_H
#define EXAMPLE_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <xf86drmMode.h>

drmModeConnector *pick_connector(int drm_fd, drmModeRes *drm_res);
drmModeCrtc *pick_crtc(int drm_fd, drmModeRes *drm_res,
		       drmModeConnector *connector);
void disable_all_crtcs_except(int drm_fd, drmModeRes *drm_res, uint32_t crtc_id);
uint32_t create_argb_fb(int drm_fd, uint32_t width, uint32_t height,
			uint32_t color, bool with_alpha);

#endif
