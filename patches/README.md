# MoltenVK patches

CrossOver 26.3 ships a MoltenVK that reports Vulkan 1.2; the game needs 1.3. The kit therefore uses MoltenVK 1.4.2 built from
source, with the patches in this folder, installed as `libMoltenVK.real.dylib` behind the shim in [`../mvkshim`](../mvkshim).

| Patch | What it changes | Measured |
|:--|:--|:--|
| `moltenvk-1.4.2-shared-tess-scratch.patch` | Tessellation scratch buffers come from one allocator per device instead of one per command pool, and a pool hands out the buffer returned most recently instead of the oldest | 507 buffers (1.9 GB) instead of about 3,700 (6.4 GB) with a planet in view; wired memory 9.1 GB instead of 11.7 |

```bash
./install-moltenvk.sh                 # clone v1.4.2 if needed, apply the patches, build, install
./install-moltenvk.sh --install-only  # install what is already built
../mvkshim/build-and-install.sh       # the shim in front of it
```

The patch is a plain `git diff` against the `v1.4.2` tag. MoltenVK is Apache-2.0; this repository as a whole is GPL-3.0.
To go back to the unpatched library, build `v1.4.2` without the patch and run `./install-moltenvk.sh --install-only`.
