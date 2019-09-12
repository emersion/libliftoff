# libliftoff

Lightweight hardware composer library for libdrm.

libliftoff eases the use of KMS planes from userspace without standing in your
way.  Users create "virtual planes" called layers, set KMS properties on them,
and libliftoff will allocate planes for these layers if possible.

## Building

Depends on libdrm. Requires universal planes and atomic.

    meson build/
    ninja -C build/

## License

MIT
