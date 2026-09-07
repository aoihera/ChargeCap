# libultrahand lives here

This directory is intentionally empty in a fresh checkout.

libultrahand is ~1 MB of upstream source, so instead of committing a copy that
immediately goes stale (or adding a git submodule), run this once from the repo
root:

```sh
./scripts/vendor-libultrahand.sh
```

That drops `common/`, `libultra/`, `libtesla/` and `ultrahand.mk` in here.
**Commit the result** and the repo is fully self-contained: no submodules, no
build-time downloads.

Pin a specific upstream revision with:

```sh
LIBULTRAHAND_REF=v1.9.6 ./scripts/vendor-libultrahand.sh
```

CI runs the same script (and caches the result), so builds work either way.

Upstream: https://github.com/ppkantorski/libultrahand (GPLv2, `libultra` under CC-BY-4.0)
