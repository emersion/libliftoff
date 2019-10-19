# libliftoff

[![builds.sr.ht status](https://builds.sr.ht/~emersion/libliftoff.svg)](https://builds.sr.ht/~emersion/libliftoff)

Lightweight hardware composer library for libdrm.

libliftoff eases the use of KMS planes from userspace without standing in your
way.  Users create "virtual planes" called layers, set KMS properties on them,
and libliftoff will allocate planes for these layers if possible.

See the [blog post introducing the project][intro-post] for more context.

## Building

Depends on libdrm. Requires universal planes and atomic.

    meson build/
    ninja -C build/

## Usage

See [`liftoff.h`][liftoff.h]. Here's the general idea:

```c
struct liftoff_device *device;
struct liftoff_output *output;
struct liftoff_layer *layer;
drmModeAtomicReq *req;
int ret;

device = liftoff_device_create(drm_fd);
output = liftoff_output_create(device, crtc_id);

layer = liftoff_layer_create(output);
liftoff_layer_set_property(layer, "FB_ID", fb_id);
/* Probably setup more properties and more layers */

req = drmModeAtomicAlloc();
if (!liftoff_output_apply(output, req)) {
	perror("liftoff_output_apply");
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
[intro-post]: https://emersion.fr/blog/2019/xdc2019-wrap-up/#libliftoff
