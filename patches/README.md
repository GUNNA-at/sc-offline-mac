# MoltenVK patches

CrossOver 26.3 ships a MoltenVK that reports Vulkan 1.2; the game needs 1.3. The kit therefore uses MoltenVK 1.4.2 built from
source, with the patches in this folder, installed as `libMoltenVK.real.dylib` behind the shim in [`../mvkshim`](../mvkshim).

| Patch | What it changes | Measured |
|:--|:--|:--|
| `moltenvk-1.4.2-01-shared-tess-scratch.patch` | Tessellation scratch buffers come from one allocator per device instead of one per command pool, and a pool hands out the buffer returned most recently instead of the oldest | 507 buffers (1.9 GB) instead of about 3,700 (6.4 GB) with a planet in view; wired memory 9.1 GB instead of 11.7 |
| `moltenvk-1.4.2-02-tess-prepass-async-submit.patch` | Two switches, both off unless turned on. `tess-prepass`: the compute stages of a pass's tessellated draws go to a command buffer queued ahead of the pass. `async-submit`: queue submissions are encoded off the calling thread | 22 render-pass restarts per frame became 2.5; the frame rate did not change with either switch. Kept as an experiment |

The switches of patch 02 are flipped while the game runs with `../tools/live.sh tess-prepass on` (files in `/private/tmp/mvk-live/`), or set for a whole run with `MVK_SC_TESS_PREPASS=1` / `MVK_SC_ASYNC_SUBMIT=1` in the bottle's environment. MoltenVK then prints a `[mvk-live]` line every 5 seconds to the game's stderr log.

```bash
./install-moltenvk.sh                 # clone v1.4.2 if needed, apply the patches, build, install
./install-moltenvk.sh --install-only  # install what is already built
../mvkshim/build-and-install.sh       # the shim in front of it
```

Each patch is a plain `git diff`: 01 against the `v1.4.2` tag, 02 on top of 01. MoltenVK is Apache-2.0; this repository as a whole is GPL-3.0.
To go back to the unpatched library, build `v1.4.2` without the patches and run `./install-moltenvk.sh --install-only`.
