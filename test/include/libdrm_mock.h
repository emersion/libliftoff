#ifndef LIFTOFF_LIBDRM_MOCK_H
#define LIFTOFF_LIBDRM_MOCK_H

#include <stddef.h>
#include <stdint.h>
#include <xf86drmMode.h>

extern uint32_t liftoff_mock_drm_crtc_id;

struct liftoff_layer;

int liftoff_mock_drm_open(void);
uint32_t liftoff_mock_drm_create_fb(struct liftoff_layer *layer);
struct liftoff_mock_plane *liftoff_mock_drm_create_plane(int type);
struct liftoff_mock_plane *liftoff_mock_drm_get_plane(uint32_t id);
void liftoff_mock_plane_add_compatible_layer(struct liftoff_mock_plane *plane,
					     struct liftoff_layer *layer);
struct liftoff_layer *liftoff_mock_plane_get_layer(struct liftoff_mock_plane *plane,
						   drmModeAtomicReq *req);

#endif
