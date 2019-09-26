# libliftoff

Lightweight hardware composer library for libdrm.

libliftoff eases the use of KMS planes from userspace without standing in your
way.  Users create "virtual planes" called layers, set KMS properties on them,
and libliftoff will allocate planes for these layers if possible.

## Building

Depends on libdrm. Requires universal planes and atomic.

    meson build/
    ninja -C build/

## Usage

See [`liftoff.h`][liftoff.h]. Here's the general idea:

```c
struct liftoff_display *display;
struct liftoff_output *output;
struct liftoff_layer *layer;
drmModeAtomicReq *req;
int ret;

display = liftoff_display_create(drm_fd);
output = liftoff_output_create(display, crtc_id);

layer = liftoff_layer_create(output);
liftoff_layer_set_property(layer, "FB_ID", fb_id);
/* Probably setup more properties and more layers */

req = drmModeAtomicAlloc();
if (!liftoff_display_apply(display, req)) {
	perror("liftoff_display_apply");
	exit(1);
}

ret = drmModeAtomicCommit(drm_fd, req, DRM_MODE_ATOMIC_NONBLOCK, NULL);
if (ret < 0) {
	perror("drmModeAtomicCommit");
	exit(1);
}
drmModeAtomicFree(req);
```

## License

MIT

[liftoff.h]: https://github.com/emersion/libliftoff/blob/master/include/libliftoff.h
