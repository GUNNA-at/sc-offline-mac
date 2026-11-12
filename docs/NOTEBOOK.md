# Lab notebook

The dated record behind the handbook: every run, every number, and the dead ends with the measurement that closed them. Newest entries are at the bottom. Paths called "scratchpad" were throw-away test programs and are not in the repository.

State as of 2026-11-08 evening
- CrossOver 26.3.0, bottle `StarCitizen` (Win11 64-bit, D3DMetal on, MSync on, DXVK off).
- Game installed: LIVE 4.10.2, build **4.10.197.2878**, at `C:\Program Files\Roberts Space Industries\StarCitizen\LIVE` (~146 GB). Install reported "All phases completed".
- Easy Anti-Cheat service is NOT installed in the bottle (it only gets installed by pressing Launch Game in the RSI Launcher — never do that).
- Offline mod sc-offline v0.7.7 staged at `C:\users\crossover\sc-offline\`, config pinned to the game path.
- Free disk ~52 GB. RAM 24 GB; a Linux virtual machine on this Mac holds 8 GB while it runs.

Scripts (this folder)
- `sc-launcher.sh`        RSI Launcher only (install/verify). Uses the SwiftShader flags that make it render. Never press Launch Game in it.
- `sc-offline-mac.sh`     The actual thing:
    `play`      start the game offline with the mod, inside the no-network sandbox
    `vanilla`   plain client, no mod, renderer smoke test only, same sandbox
    `nettest`   prove the sandbox blocks TCP + DNS for a Windows process
    `status` / `uninstall` / `dry` / `kill`

Before the first launch (once)
1. macOS update to 26.7.2 (Rosetta deadlock fix for M4 chips), reboot.
2. Stop any virtual machine (the one here frees 8 GB). Quit the browser and other heavy apps.
3. (No Wi-Fi change needed.) `play` and `vanilla` run inside a macOS `sandbox-exec` profile that denies all network
   to the game's process tree (TCP: Access denied, DNS: Host not found, verified from inside Windows). `nettest` proves it.

Renderer notes
- Engine default since 4.5 is Vulkan (→ MoltenVK under CrossOver). DX11 fallback goes through D3DMetal or DXVK.
- The launcher's `StarCitizen.exe -PSOCacheGen` step deadlocked inside D3DMetal's shader converter on this Mac, so D3DMetal is suspect. Order to try: Vulkan (default) → DX11+DXVK (bottle env WINED3DMETAL=0 WINEDXVK=1) → DX11+D3DMetal.
- Renderer switch lives in GraphicsSettings.json (created on first run): "GraphicsRenderer": 1 = Vulkan, 0 = DX11.

Logs
- Game:      `LIVE\Game.log`, `LIVE\logbackups\`
- Mod:       `C:\users\crossover\sc-offline\data\mod.log` and `data\launcher.log`
- Wine side: bottle folder `sc-offline-mac.log` / `sc-vanilla.log`

Restore / undo
- `sc-offline-mac.sh uninstall` removes the mod DLL and restores `default_1.xml`.
- CrossOver 25.0.1 is in the Trash if 26.3.0 ever needs reverting.

## Renderer saga (2026-11-08 evening)
- D3DMetal deadlocks on this macOS (26.2) at "Physics initialization" → bottle now runs with WINED3DMETAL=0, WINEDXVK=1.
- 4.10 demands Vulkan 1.3 and the DX11 fallback is gone. CrossOver's MoltenVK = Vulkan 1.2 → replaced by Khronos MoltenVK 1.4.2
  (`CrossOver/lib64/libMoltenVK.real.dylib`; CrossOver's original kept as `libMoltenVK.dylib.cx-26.3.0`).
- The engine's GPU filter rejects Apple's integrated device → `libMoltenVK.dylib` is now a tiny shim that presents it as a
  discrete AMD GPU (source in the scratchpad `mvkshim/`). Remove with `mvkshim/remove-shim.sh`.
- Dialog boxes are clicked away automatically by `dismiss.ps1` inside the bottle; a fatal error kills the game within seconds.
- The engine's Vulkan gates, in order, and how each is passed now:
  1. API ≥ 1.3            → MoltenVK 1.4.2 (`CrossOver/lib64/libMoltenVK.real.dylib`)
  2. "valid Discrete GPU" → host shim `lib64/libMoltenVK.dylib` reports a discrete AMD card (source `mvkshim/`, `build-and-install.sh`, `remove-shim.sh`)
  3. features geometryShader/depthBounds/pipelineStatisticsQuery → host shim advertises them, strips them at vkCreateDevice
  4. VK_EXT_depth_clip_enable → host shim advertises it
  5. VK_KHR_external_memory_win32 / _semaphore_win32 → Windows-side `system32\vulkan-1.dll` wrapper (source `vk1wrap/`), override `vulkan-1=native,builtin`
  Undo: `mvkshim/remove-shim.sh`; restore `system32/vulkan-1.dll.wine-fake` and `wine reg delete HKCU\Software\Wine\DllOverrides /v vulkan-1 /f`.

## 2026-11-08 evening: where the launch stands (verified)
- With `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0` (cxbottle.conf) the 18:13 run passed every pipeline compile, loaded the
  frontend config, and started "Loading level megamap". 61 s into the level load the engine's own watchdog
  (`CWatchdogThread`, cvars `sys_watchdog_hard_timeout_loading` / `sys_watchdog_enable_hard_timeout`) declared the main
  thread deadlocked and exited. The main thread was at 200% CPU, i.e. loading, not idle.
- Every other run today (18:00, 18:05, 18:08, 18:32, 18:43) died earlier, always on the same pipeline
  `WaterBoundary@WaterBoundary(RT0)`: MoltenVK reports "Shader library compile failed … XPC_ERROR_CONNECTION_INTERRUPTED".
  The same dumped MSL (`/private/tmp/sc-shaders/shader-fs-83ff31f81995ee34.metal`, 120 KB) compiles fine through the runtime
  Metal compiler in a test tool (scratchpad `mtlrt/rtcompile`), natively, under Rosetta (x86_64 build) and inside the same
  sandbox profile. No MTLCompilerService crash report, nothing in the unified log. So it is the in-game compile context
  (many concurrent compiles at that moment), not the shader, the sandbox or Rosetta.
- Knobs now in cxbottle.conf for the next runs: `MVK_CONFIG_SHOULD_MAXIMIZE_CONCURRENT_COMPILATION=1`,
  `MVK_CONFIG_FAST_MATH_ENABLED=0` (both config-level, remove if they make things worse).
- Watchdog, config-level attempts: `LIVE/user.cfg` with `sys_watchdog_enable_hard_timeout = 0` etc. (unverified: no run
  reached the level since), and `./sc-offline-mac.sh direct [+cvar value ...]` (mod `install`, then StarCitizen.exe started
  directly with `+sys_watchdog_enable_hard_timeout 0`; `uninstall` afterwards) — untested.
- `dismiss.ps1` now captures dialog text (WM_GETTEXT): the two early boxes are "unsupported Windows OS" and "AMD graphics
  driver out of date"; both are informational. The crash watcher waits for `error.dmp` (max 120 s) and screenshots before killing.
- `wdpatch/` holds an experimental xinput9_1_0.dll (not installed, not used); leave it alone.
- Runs 4 and 5 (fast-math off + concurrent compile on): WaterBoundary now builds; the game renders the Terra Gazette loading
  screen and runs its "Optimizing Shaders" PSO warm-up (235,138 pipeline states, ~4/s; progress persists in the profile's
  shaders/ folder: 788 after run 4, 818 after run 5). Both runs died on the same pipeline `WaterVolume@General(RT40000100104003)`
  with "Render pipeline compile failed … XPC_ERROR_CONNECTION_INTERRUPTED" (the compiler service drops the back-end compile of
  that VS+FS pair). The watchdog did not fire in either run. Screenshot: loading-screen-run5.png.
- Isolated (19:15): the drop is a deterministic crash of Apple's GPU back-end compiler (error domain AGXMetalG16X) on two specific
  MoltenVK-generated fragment shaders, `WaterBoundaryPS` (dump shader-fs-83ff31f81995ee34) and one `WaterPS` variant
  (shader-fs-d422123d4c000b8e). The scratchpad tool `mtlrt/psotest <fastmath> <opt> <fs.metal>` pairs a dumped fragment shader with
  a stub vertex function and builds a render pipeline state: the two fail in ~0.3 s with every fast-math/optimization combination,
  other WaterPS variants build fine. The same descriptor with an empty fragment function and color writes masked off builds in
  0.01 s, so a "draw nothing" fallback pipeline is viable for those variants. Engine switches found in the exe: `r.PSOCacheGen`
  (0 = no warm-up and no runtime PSO compilation), `r.PSOCacheGen.RunOnStart`, `r_vulkanPSOCache`, `r_vulkanPSOCacheLoad`.
- Runs 6-7 (19:20-19:36): `LIVE/user.cfg` IS executed (non-whitelisted cvars are logged as "Not Whitelisted … IGNORING";
  `sys_spec` and `r.PSOCacheGen.RunOnStart` are ignored, `r.PSOCacheGen`, the `sys_watchdog_*` and `sys_spec_*` lines pass
  silently). With `r.PSOCacheGen = 0` the loading screen no longer shows the "Optimizing Shaders" counter and the game loads
  the level for ~2 min (6,700-7,000 frames), then dies on a third compiler-crashing shader, `RParticleNoMat@RParticleLightAtlas`,
  identical at lowest quality. Conclusion: a handful of CIG shader variants crash Apple's AGX back-end compiler deterministically;
  the only way through is a "draw nothing" fallback pipeline for those (proven buildable with mtlrt/psotest), which has to live in
  the MoltenVK layer (host shim or a MoltenVK rebuild). Nothing else at config level changes the outcome.
- 19:40 shim gains a graphics-pipeline fallback (mvkshim/shim.c, `shim_cgp`): when vkCreateGraphicsPipelines fails it retries
  the pipeline with an empty fragment stage and color writes masked off; if that still fails (geometry stage: Metal has none),
  it keeps vertex + empty fragment only and switches rasterization off. Each case is logged in /private/tmp/mvkshim.log
  ("retried with an empty fragment stage … -> 0", "second tier …"). `MVKSHIM_NO_PIPELINE_FALLBACK=1` disables it. Effect: the
  few water/particle variants Apple's compiler rejects draw nothing instead of aborting the game. Run 8: 7 of 8 rescued,
  the 8th was a particle-ribbon pipeline with a geometry stage (second tier added after).

## 2026-11-08 19:58 — IN GAME
Run 9 (shim with the two-tier pipeline fallback, user.cfg with `r.PSOCacheGen = 0` + watchdog cvars): no fatal error. Every
rejected pipeline was rescued (first tier for the water/particle fragment shaders, second tier for two geometry-stage particle
ribbon pipelines). The game loaded the megamap, spawned the player in a hab bed ("Exit Bed: Press … to stand up"), the HUD and
the pause menu rendered (ingame-pause-menu-run9.png), and it ended with a clean "Quit via console command" at 19:58:18.
Timeline: launch 19:49:39 → loading screen ~19:53 → in game ~19:58 (about 8.5 min). To play: `./sc-offline-mac.sh play`
(network-sandboxed, dialogs auto-dismissed). Known gaps: the stubbed water/particle variants draw nothing; the two early
"unsupported Windows OS" / "AMD driver out of date" boxes are informational; performance in the world not yet measured.

## 2026-11-08 20:40 — playable, memory-bound
- Runs 10 and 11 (audio off via `./sc-offline-mac.sh mute`, `r_DisplayInfo = 1` in user.cfg): in game 8 min after launch both
  times, no crash, 7-8 min in the world until the test stopped them. Shim fallbacks: ~200 per run, 160 of them in one minute
  of the level load, only a handful in the world (the shim now skips compiles for fragment shaders it already knows are bad and
  sends geometry-stage pipelines straight to the rasterization-off tier; log lines are timestamped).
- In-world FPS read 0.9-2.6 (main thread 400-1100 ms) but that is the Mac swapping: system swap went 10.7 -> 24.5 GB during the
  run, the game's resident set fell to 50 MB and its CPU to 20% while it kept logging. At the time other work on the Mac held about 17 GB
  (compiler builds 7.4 GB, a Linux virtual machine 8 GB, other apps). The game needs roughly 12-16 GB of real RAM; measure FPS only with the VM
  stopped and builds finished. Cheaper boot to try: `boot_map = PU` in sc-offline.ini (Stanton only, Orison spawn) instead of
  PU_All (every system; 2.6 M replication-layer entities in the Pyro spawn).
- Launchers: `Star Citizen Offline.command` (double-click) or `./sc-offline-mac.sh play`; `mute`/`unmute` for audio; remove the
  `r_DisplayInfo` line from LIVE/user.cfg to hide the overlay.

## 2026-11-09 — why it runs at 3-6 FPS, measured
> Superseded the same day: the 29 GB footprint was really costing 35-50 GB of RAM. Cause and fix are in the next section.
Project moved to `~/Documents/StarCitizen Offline` (tools in `tools/`, per-run logs and FPS crops in `runs/<tag>/`).

| Run | Settings | Game footprint | In the world |
|---|---|---|---|
| user session 08:41 | Very High, PU_All (Pyro spawn) | not measured | 3 FPS, main thread 326 ms |
| m1 | Medium profile, `boot_map = PU` (Orison) | 29-30 GB, 21-22 GB compressed | 4 FPS, main 304 ms, render 49 ms |
| m2 | every SysSpec at Low, PU | 29 GB = 21 GB heap + 6 GB graphics | 5.7 FPS, main 177 ms, render 31 ms |

- The 24 GB Mac cannot hold it: free RAM 0.0-0.1 GB, 160-210k page decompressions per second, hot threads mostly in the kernel.
  Graphics quality changes the footprint by ~1 GB; the 21 GB is the game's own heap (the offline mod runs the whole star system's
  server-side state in this process). Loading screen: 60 FPS, so the renderer is not the limit.
- Ruled out: Low Power Mode, thermal throttling, Rosetta (1.5x), background QoS, the dialog dismisser.
- Never use `sample` on the game: it freezes the process long enough for the engine's GPU-hang detector to kill it
  ("GPU Crash Vulkan (async): AMD - Device Timeout"). Heavy swapping triggers the same crash.
- Settings changed for these tests (backups in `backups/`): `attributes.xml` SysSpec values (currently all 1; `attributes.xml.veryhigh`
  and `.medium` are the earlier states), `sc-offline.ini` `boot_map = PU` (was PU_All) and `start = Daymar` (was empty).
  `LIVE/user.cfg` also carries `net_context_establisher_seeding_error_seconds = 1800` (fixes "Seeding Service Error 66000").
- Shim source has a new, compiled-but-not-installed build (`mvkshim/libMoltenVK.shim.next.dylib`): remembers compiler-rejected
  fragment shaders across launches by SPIR-V hash (`~/Library/Caches/mvkshim/bad-fragment-shaders.txt`).

## 2026-11-09 midday — the real cause (4 KB commits on 16 KB pages) and the fix: winegrid
**Cause.** Under Rosetta the game has 4 KB pages, the Mac's physical pages are 16 KB. Wine reserves with an anonymous PROT_NONE
mmap and commits with mprotect. The kernel creates an anonymous mapping's memory object at the first page fault, sized to the map
entry that faults, and mprotect on an untouched reservation splits the entry first. So every separately committed piece gets its
own object, rounded up to 16 KB. The game commits its heap one 4 KB page at a time: `footprint -v` showed 1.45 M regions, 994k of
them a single 4 KB page, 1.44 M distinct objects; system-wide "stored in compressor" was 54 GB for a game whose own compressed
count was 21 GB. `winegrid/kt.c` reproduces it without Wine: 1.00 GB of data costs 4.00 GB of RAM (262144 map entries), and
1.00 GB (one entry) when the reservation is one named memory object.

**Fix.** `winegrid/` builds `libwinegrid.dylib`. `winegrid/install.sh` copies it to CrossOver's `lib64` and adds a weak dependency
on it to CrossOver's `lib/wine/x86_64-unix/ntdll.so` (ad-hoc re-signed, original kept as `ntdll.so.cx-26.3`), so it is in the
process before the first reservation. It redirects ntdll.so's `mmap` and `munmap`: anonymous PROT_NONE reservations become
mappings of named memory objects (one per 128 MB, VM tag 240 = "app-specific tag 1" in footprint); Wine's decommit is done in
place (zero what is resident, MADV_FREE whole 16 KB pages, PROT_NONE). Nothing happens unless the environment has `WINEGRID=1`:
the StarCitizen bottle sets `WINEGRID=1` and `WINEGRID_ONLY=StarCitizen.exe` in `cxbottle.conf`. Log: `/private/tmp/winegrid.log`.
`winegrid/remove.sh` restores ntdll.so and deletes the library. A CrossOver update replaces ntdll.so: run `install.sh` again.
Tests: `grid_test` (native, 60k random commit/decommit operations), `wgtest.exe` under Wine via `wt.sh on|off misc|stress|pattern`,
`wt-pattern.sh on|off 512` (512 MB of page-by-page commits: ~2.2 GB of RAM without the fix, ~0.85 GB with it incl. Wine itself).
The earlier `pagefix` in the MoltenVK shim is gone (`mvkshim/superseded-pagefix/`): it treated decommit churn, not the cause.

| Run | What | Game in the world (Orison, all Low, boot_map = PU) |
|---|---|---|
| m10 | old page fix, no debug callbacks | 5.8 FPS, main 185 ms, render 42 ms; 190-218k decompressions/s; compressor 54 GB stored |
| m11 | winegrid | in the world after 304 s; lying in the bed (window view): 11-12.7 FPS, main 75-82 ms, render 16-28 ms; 70-110k/s; 37 GB stored |
| m12 | winegrid + shim reports 4 GB VRAM | same memory (GPU 7.0 + 1.1 GB): no effect. Standing in the hab: 25-26 FPS, main 33 ms, render 16 ms, 7k/s |
| m13 | winegrid + GPU accounting + texture pool 2048 | user playing: hab ~30 FPS, city view 10-15, moon right after teleport 2-3 |

- With the fix the heap is 18.6 GB dirty in 77k regions and costs ~19 GB of RAM (16 KB waste left: ~0.1 GB). Total regions 1.5 M -> 110k.
- Busy views are still paging-bound: in the bed view the main thread is on the CPU ~50% of the time (user ~27 ms/frame, system
  ~17 ms), the rest is waiting. RAM split in the world: wired ~11.7 GB (GPU 8 + kernel), compressor pool 4-7 GB, file cache ~2 GB,
  resident anonymous memory of all processes ~4 GB.
- GPU memory by kind (shim log, `gpumem:` every 30 s): device memory 4.4-5.1 GB in ~300 allocations (twelve 256 MB private blocks
  and four shared ones dominate), textures ~14300 = 2.2-2.4 GB, render targets 204 = 0.8 GB, geometry buffers 4.7-5.0 GB,
  shader data 0.45 GB. The engine believes it has an 18 GB card ("Dedicated video memory: 18186 MB" from DXGI); its own
  budget display reads "VRAM(MB): 0 / 0".
- user.cfg whitelist additions: `r_TexturesStreamingPoolSize` and `r_ShadowsPoolSize` are accepted; `e_StreamCgfPoolSize` is
  refused (current value 600). The planet-terrain virtual-texture part of the Low spec group is ignored by the engine itself.
- The level-load "black screen" of the morning was a deadlock: MoltenVK calling the game's debug-utils messenger from 14 threads
  during "Rebuilding all pipelines". The shim now accepts the messenger registration without passing it on
  (`MVKSHIM_KEEP_DEBUG_MESSENGER=1` restores it).
- Hands-free test helpers: `tools/standup.sh` (gives the game focus, presses keys inside the bottle with `sendkey.exe`, hands focus
  back; default is a held Y, which leaves the spawn bed), `tools/activate`. The mod will not teleport a player lying in the bed.
- `tools/measure-run.sh <tag> <seconds>` STOPS THE GAME after <seconds> in the world. With the user at the Mac use 36000.
- Kernel facts worth keeping: `mincore` under Rosetta answers per 4 KB page from the 16 KB object page; `MADV_FREE` works on
  named objects in a 4 KB map only for whole 16 KB pages and drops compressed pages at once; `MADV_FREE_REUSABLE` is a no-op in a
  4 KB map; partial unmap of a shared object leaves its pages allocated until the object dies.

## 2026-11-09 afternoon — busy views: what was learned, what is queued for the next launch
- User report after playing run m13: hab/rooms 30 FPS, heavy views 10-15, right after an M-menu teleport to a moon 2-3 FPS
  (they died there without a ship). Target now: constant minimum 25 FPS.
- Capture w40 of that session: 30.0 FPS at exactly 33.3 ms per frame while the main thread needs 19.1 ms and the render thread
  2.6 ms. So calm scenes are capped, either by vertical sync on Wine's 60 Hz mode (frame rate snaps to 60/n = 30, 20, 15, 12, 10,
  the very values the user sees) or by `sys_maxIdleFps` (30 when the game window is not focused, which also caps every reading
  my harness takes while the user is in another app). Queued in `LIVE/user.cfg` for the next launch: `r_VSync = 0`,
  `sys_maxFps = 0`, `r_TexturesStreamingPoolSize = 1536`, `r_ShadowsPoolSize = 2048`. The shim now logs each swapchain
  ("swapchain: ... game asked for present mode ...") and `MVKSHIM_PRESENT_MODE=immediate|fifo|mailbox` overrides it.
  NOT YET VERIFIED in game.
- GPU memory, checked with `mvkshim` test programs (scratch `vkheap/`): MoltenVK 1.4.2 backs each GPU-only VkDeviceMemory block with
  a Metal heap and places both buffers and textures in it, so there is no double allocation, and separating buffers from textures
  (`MVKSHIM_SPLIT_MEMTYPES=1`, implemented in the shim) only adds blocks: 1490 MB instead of 1234 MB in the test. Leave it off.
  `MVK_CONFIG_USE_MTLHEAP=1/2` changes nothing (already on). The shim's new `gpumem blocks:` log line reports, per launch, how
  much of each kind is bound in which blocks and the live buffers by size.
- A file-backed heap (page cold heap data straight to an SSD file instead of the compressor) was considered and dropped for now:
  the disk has 25 GB free with 11-12 GB of swap in use, and uncompressed paging would need ~22 GB.
- Still to do when the user is not playing: a whitelist probe launch for view-distance / LOD cvars (`e_ViewDistRatio`,
  `e_LodRatio`, `e_LodMin`, `e_MaxViewDistance`, ...), which would cut both geometry memory and main-thread work in busy views.

## 2026-11-09 12:35-13:00 — run m14 (sync off) and the game's own preset definitions
- m14: `r_VSync = 0` and `sys_maxFps = 60` are accepted; the shim logs "game asked for present mode IMMEDIATE (no sync)".
  Whether calm scenes now exceed 30 FPS is NOT yet observed: for the whole run the player stood at a window onto the city at
  night (6-8 FPS, main thread 105-165 ms, render 30-35 ms, 100-140k decompressions/s, footprint 32-33 GB).
- The universe clock runs ~6x real time (15:47 at 11:36 local, 22:04 at 12:41), so the same view is day in one run and night in
  the next. Night adds lights and is heavier: FPS readings from different launches are not directly comparable.
- `tools/p4kfind.py <Data.p4k> <regex> [outdir]` lists/extracts archive entries (streams the 447 MB index; entries are plain
  zstd). The game's preset groups are in `tools/game-cvargroups/` (from `Engine\Config\CVarGroups\sys_spec_*.cfg`). Low sets:
  `e_ViewDistRatio=75`, `e_ViewDistRatioLights=50`, `e_LodRatio=5`, `e_ObjQuality=1`, `ca_AttachmentCullingRation=150`,
  `w_planetsideScatterLevelDist=750`, `r_TexturesStreamingPoolSize=1400` (+ MipBias 1), `r_ShadowsPoolSize=4096`,
  `e_ShadowsCastViewDistRatio=0.8`, `e_ObjShadowCastSpec=1`. The engine applies these groups through the same whitelist check as
  user.cfg (only the planet-terrain virtual-texture ones log IGNORING), so every other cvar in the groups should be settable
  from user.cfg. NOTE: my earlier test values raised the texture pool above Low (2048 in m13, 1536 in m14).
- Queued in `LIVE/user.cfg` for the next launch (NOT yet tested): a below-Low profile: `e_ViewDistRatio = 40`,
  `e_ViewDistRatioLights = 25`, `e_LodRatio = 3`, `ca_AttachmentCullingRation = 100`, `w_planetsideScatterLevelDist = 450`,
  `e_ShadowsCastViewDistRatio = 0.5`, `r_TexturesStreamingPoolSize = 1000`, `r_ShadowsPoolSize = 2048`.
- GPU block accounting (shim line `gpumem blocks:`), Orison: 19 GPU-only blocks holding buffers = 2711 MB of Metal heaps with
  only 645 MB of buffers and 831 MB of textures bound in them, i.e. ~1.2 GB of allocator slack (256 MB block granularity);
  240 texture-only allocations = 1120 MB; 4 host-visible blocks = 800 MB carrying 51 large VkBuffers that alias the same memory
  (which is why the earlier "geometry 4.7 GB" figure was wrong: real buffer memory is ~0.65 GB GPU-only + 0.8 GB shared).
- `tools/sendkey.exe` also moves the mouse (`mouse:dx,dy`). Giving the game focus from a script fails while the user is
  actively using another app, so the helpers only work when the Mac is otherwise idle.
- A second copy of `measure-run.sh` (from m13) was still alive after its game was stopped and would have killed whatever game
  was running at its own limit; killed it by hand. Stop the old harness whenever a run is replaced.

## 2026-11-09 13:00-13:10 — run m15 (below-Low "lean" profile) and switchable profiles
- m15, all below-Low values accepted (no IGNORING lines). In the bed view at night: 31-35 FPS, main thread 28-32 ms, render
  thread 3 ms, frame time = main-thread time (sync off works). Footprint 26-28 GB (was 30-33), paging 2-28k/s (was 100k+),
  swap 7.1 GB (was 11). GPU: device memory 3350 MB (was 4657), textures 1221 MB (was 2000; only 29 textures of 2048 left).
  Busy (city) view with this profile: NOT measured, the player stayed in bed (scripted Y did not register in this run).
- Owner's feedback: it should look good too, not only run. So: `profiles/` + `tools/profile.sh <name>` (also
  `./sc-offline-mac.sh profile <name>`), applied at the next launch: `pretty` (Shading/PostProcessing/ShadowScreenSpace/
  TextureFiltering/TextureDetail/clouds/fog/water groups at 3, Particles and ShadowMaps 2, texture pool 1800 with mip bias 0,
  view distance 55, lights 35, LOD ratio 4), `lean` (the m15 values), `low` (game's Low). `pretty` is selected, untested.
  The profile is written between `-- >>> profile:` markers in `LIVE/user.cfg` and mirrored into `attributes.xml`.
- user.cfg is processed after the attribute-driven preset groups (log order), so its values win; `sys_spec_<Group> = N` lines in
  user.cfg are how the pretty profile sets group levels (acceptance to be confirmed in the next launch's log).
- Output is still 1670x1080 scaled up by macOS. Sharper output would need Wine's Retina mode plus the game's upscaler; untested.

## 2026-11-09 13:10-14:05 — city views: tessellation scratch buffers in MoltenVK (found), patched MoltenVK (built, installed)
- User checked the window view with the lean profile: 8-15 FPS, "still unplayable"; wants 25-30 minimum in cities.
- When the city/planet is in view the game's footprint rises by ~5 GB (27 -> 32 GB) and none of it is Vulkan memory: device
  memory stays ~3.6 GB. `footprint -v` shows ~3,700 private GPU allocations of exactly 2, 4 and 8 MB, 20-40% dirty
  (8 MB x 2288 = 4.4 GB dirty, 4 MB x 961 = 1.6 GB, 2 MB x 428 = 0.3 GB), 1% dirty when the city has not been looked at.
- They are MoltenVK's scratch buffers for tessellated draws. Metal has no tessellation shaders, so MoltenVK runs the vertex and
  tess-control stages as compute passes into private temp buffers sized for the worst case (`count * 4 * 124` bytes), rounded
  to a power of two, taken from an allocator that every VkCommandPool owns separately and that reuses buffers oldest-first.
  The game requires `tessellationShader` (hiding it: "Required VkPhysicalDeviceFeatures not found: {tessellationShader}") and
  draws ~21-25 tessellated terrain/planet tiles per frame, 1 control point per patch, 4,096 patches each, when the planet is
  visible; it records from dozens of command pools, so each pool keeps its own ~75 buffers and all of them get rewritten every
  few seconds. Shim counters (`MVKSHIM_FRAME_STATS=1`, line `frames:` every 5 s): ~210-234 render passes, 2,300-4,000 draws,
  21-43 tessellated draws with 86k-110k patches per frame; 455 of 10,714 pipelines have tessellation stages.
  Each tessellated draw also ends and restarts the Metal render pass (compute cannot run inside one).
- Standalone Metal tests (`scratch tvb/`): plain render passes and many draws do NOT create such buffers, so it is not the tile
  parameter buffer. `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0` means MoltenVK creates no residency set (with argument buffers on,
  every resource would be pinned resident).
- Fix: MoltenVK v1.4.2 source in `moltenvk-src/MoltenVK` (git, patch = `git diff`), built with `./fetchDependencies --macos`
  then `make macos` (Xcode 26.6; about 2 minutes in total). Patch: (1) one private temp-buffer allocator per device
  (`MVKDevice::getSharedPrivateTempBufferAllocator`) instead of one per command pool, (2) buffer pools hand out the most
  recently returned buffer first (`MVKObjectPool::returnObjectToFront`). Installed as `lib64/libMoltenVK.real.dylib`; the
  Khronos build is kept as `libMoltenVK.real.dylib.khronos-1.4.2`. Expected: a few hundred MB of scratch buffers instead of 5-6 GB.
- `tools/measure-run.sh` now logs GPU load from `ioreg` (`gpu: busy %, tiler %, renderer %, parameter buffer MB`).
  Bottle env also has `MVK_CONFIG_PERFORMANCE_TRACKING=1`, `MVK_CONFIG_PERFORMANCE_LOGGING_FRAME_COUNT=300`,
  `MVKSHIM_FRAME_STATS=1` for now (diagnostics; remove when done).
- Scripted focus (`tools/standup.sh`) works only when the user is not actively using another app; Y did not register in m15.

## 2026-11-09 14:05-14:50 — patched MoltenVK measured; the compressor's 60% share; the squeeze (works)
- m18/m19 on the patched MoltenVK: tessellation scratch buffers 507 instead of ~3,700 (8 MB x 220, 4 MB x 184, 2 MB x 103 =
  1.9 GB instead of 6.4 GB), private GPU memory 5.0 GB instead of 8.5-9 GB, wired memory 9.1 GB instead of 11.7 GB.
  Tessellated views: 14-29 FPS (were 8-15). Heavy view (62 tessellated draws, 156k patches, 202 passes, 4,700 draws):
  13-16 FPS, main thread 62 ms but on the CPU only 56% of the time, GPU 67-80% busy, 60-90k decompressions/s.
- MoltenVK's own timing (`MVK_CONFIG_PERFORMANCE_TRACKING`, in `/private/tmp/sc-game-stderr-<pid>.log`): ~3.5 Metal command
  buffers per frame, GPU execution up to 36 ms each in heavy views, CPU encoding only ~6 ms per frame.
- Refused by the whitelist (current values): `r.TSR.UpscalingPreset` (0), `r.PlanetTerrain.Tessellation.QuadSize` (16),
  `r.PlanetTerrain.Tessellation.MaxSubdivisions` (7), `r_TessellationTriangleSize` (8). Upscaling exists as a menu option
  (Options > Graphics > Upscaling: Native AA / Quality 66% / Balanced 57% / Performance 50%; profile attribute `Upscaling`).
- Why paging persisted: under sustained pressure macOS lets compressed memory take ~60% of the NON-WIRED RAM before it swaps
  any of it out (measured in every run: compressor 7.5-9 GB, uncompressed anonymous ~4.4 GB, out of ~15 GB non-wired). The
  game's ~14 GB of never-touched "server" data therefore sits in RAM compressed and the live data cycles through the compressor.
  The 2.6 GB freed by the MoltenVK fix went straight to the compressor.
- Dead end, tested with `winegrid/fbtest`: backing the heap with an SSD file instead (18 GB heap, 9 GB hot, 8 GB pinned):
  370k page touches/s as compressed memory, 6k/s file-backed. Every miss becomes a disk read. Do not revisit.
- What works: `winegrid/balloon`. Pin a few GB for a few seconds: the kernel moves its OLDEST compressed data to swap and it
  stays there. On the live game (m20, view with 11 tessellated draws): `balloon 6 12 8` -> compressor 8.4 -> 3.6 GB of RAM,
  uncompressed anonymous 4.6 -> 7.5 GB, 2.5 GB free, swap 7.4 -> 11.2 GB, decompressions 45k/s -> 150-1,000/s, FPS 25-27 ->
  30.6-31 (that looks like the unfocused-window cap, `sys_maxIdleFps`; now set to 60 in user.cfg). FPS dipped to 9-16 for ~10 s
  during the squeeze; the game survived. In the synthetic test a harder squeeze on a uniformly hot heap did NOT last (whole
  segments come back from swap), so it must target cold data: after loading, and later only when paging builds up.
- Automated: `winegrid/balloon auto <pid> [log]` (squeezes when compressed memory > 5.0 GB AND > 10k decompressions/s for 6 s,
  45 s cooldown, stops inflating at 3.6 GB compressed or when uncompressed memory would fall under 2.8 GB, skips when disk free
  < 6 GB or swap > 24 GB). `tools/squeezer.sh` waits for the world and starts it; `sc-offline-mac.sh play` starts the squeezer
  (`SC_NO_SQUEEZE=1` disables); `./sc-offline-mac.sh squeeze [GB]` does one by hand. Log: `/private/tmp/sc-squeezer.log`.


## 2026-11-09 15:05-16:05 — the mod's menu and ATC: one cause (a dead window handle), fixed by `macfix`

Reported: "my mouse is not working" in the M menu, and a take-off hail from the hangar does not open the doors.

- **Cause.** sc-offline (0.7.7) looks for the game's window one second after it starts and keeps that handle. Under CrossOver
  the game destroys and recreates its window while loading (the mod held `0x200AA`, the live window was `0x600AA`: same slot,
  four generations later). The mod posts to that handle to make the game's main thread run the mod's code (its message hook):
  every 200 ms for the features, once per menu frame for the menu. Nothing arrives. Measured with the game in front and no
  input: the mod's code ran **0.0 times/s**. It only ran when the game's window got a message of its own (pointer over it, keys).
  - Menu: its frames are built on the game thread, so it redrew only while the pointer moved over the game outside the menu.
    Clicks arrived (ImGui got them) but showed seconds late; ImGui's own drawn pointer was not where the pointer was (the
    system cursor is hidden over the menu by design). The clamp of the cursor to the menu was also lost.
  - ATC: Retrieve hands you the hangar pad in the mod's 100 ms tick. The ATC registered the lift's new ship 128 ms after
    it appeared, the mod's tick came 224 ms after, so `owner 0 -> you (rc20)` never ran, then the mod's own spawn cancel
    (rc16) reset the pad to Reserved / no ship / no owner. Game.log: the hail arrives as `LandRequest`, "Request to LAND added
    to the queue due to all landing pads being occupied", `mode 1 notification 2` every 15 s. Hotkeys (F6-F8) are polled in
    the same hook, so they missed presses too.
- **Fix: `macfix/`** (`macfix.c` -> `macfix.dll`, a native sc-offline plugin in `data/plugins/macfix/`; `install.sh`). It
  (1) finds the real game window (class `CryENGINE`) and writes it into the mod's handle when that one is dead, (2) posts an
  empty message to the game window every 50 ms as the mod's own thread means to, (3) sits behind the mod's ChangeTokenState
  hook (it swaps the mod's saved original pointer) so the pad keeps you as owner whatever the order (tested for both orders
  in `macfix_test.exe`), (4) re-applies the menu's cursor clip, (5) runs mod commands from `C:\users\crossover\macfix.cmd`
  (`creative.god on`, `quantum.travel L2`, `?list`), replies in `C:\users\crossover\macfix.log`. Switches:
  `C:\users\crossover\macfix.ctl` (`padfix fixgame keepalive diag status clip` + spares `moves nudge wait oscursor`).
  All addresses are for the 0.7.7 `dinput8.dll` (sha256 d6d43463..., checked at start; another build: it does nothing).
- **Measured in the running game** (helper injected with `macfix/inject.exe`, no restart): mod code 0 -> 16/s idle; with the
  menu open 40-60 menu frames/s without any input, one injected click switches a tab, ImGui's pointer is drawn at the
  pointer, clip stays on the menu (one re-clip), closing the menu gives the game the foreground back.
- Not explained, not needed any more: at times the Mac's cursor and Wine's cursor position differed by ~110 px over the menu.
- **Tools:** `tools/front.sh` (game to the front through System Events; `NSRunningApplication.activate` is refused while
  another app is active), `tools/sendkey.exe` now also `at:x,y click rclick down up`, `tools/wininfo.exe [watch N ms]`
  (foreground, capture, cursor, clip), `macfix/inject.exe <proc> <dll> [unload]`, `macfix/peek.exe` (read process memory).
  Mod source at the installed tag for reading: scratchpad `modsrc/` (github.com/scuba-sc/sc-offline v0.7.7, sco-core 31cf53a).

## 2026-11-09 16:05-16:20 — run m21: plugins on, god mode, teleport, command channel

- Game restarted with `data/plugins/macfix` and the mod's own `creative` plugin switched on (its `disabled` marker removed:
  that file is all the launcher's Plugins page toggles; there is no launcher window on the Mac). mod.log: `[plugin] loaded
  creative 1.0.0`, `loaded macfix 1.1.0`, 14 found / 13 loaded. World after 304 s. Mod code 20-21 runs/s idle (was 0).
- **Commands without the menu:** `tools/modcmd.sh "<command>" ...` writes `macfix.cmd`, the plugin runs each line on the game
  thread through `sco_api.invoke` and logs the reply. `?list` gave 33 commands (`creative.god <on>`, `creative.noclip`,
  `creative.noclip_speed`, `creative.status`, `quantum.travel <place> <altitude 100-20000>`, `quantum.bookmark`,
  `quantum.save_bookmark`, `quantum.scan`, `teleport.save/go`, `spawn.ship <class> <height>`, `crew.sit <seat>`,
  `crew.power_on`, `npc.*`, `build.*`, `loadout.*`, `sco.reload <plugin>`). A plugin cannot reload itself (`busy`).
- **God mode works:** `creative.god on -> OK: God mode on`, `creative.status -> god on, noclip off, ...`.
- **Teleport works on foot:** `quantum.travel "Spaceport interior" 100` -> `[tp] into zone 'Spaceport_interior' at (0, 0,
  2000)` (a place with unknown radius always arrives 2000 m out, whatever altitude is asked). With `creative.noclip on` and
  `creative.noclip_speed 200` it is a 10 s flight down (look straight down, hold W). From a ship seat a teleport is logged
  but you stay put (seen in m20: status says "Teleported to Hurston L2", zone stays the hangar).
- How the mod resolves a place (travel.cpp `GoToPlace`): the game's own find-entity-by-name on the third column of
  `locations.txt`; a zone -> `TeleportIntoZone(zone, (0, 0, radius+altitude))`, any other object -> `TeleportToEntity(id,
  min(altitude, 50))`. So single objects can be added to `locations.txt` (done: `asop*` = the ship terminals
  `ShipSelectorKiosk_ShipsOnly_Area18-00N`, `hangar pad`, `hangar lift`). The list is read once per game start.
  macfix adds `!find <entity>`, `!goto <entity> [up]`, `!pose` on the same two routines (dinput8 RVAs 0xD540, 0xA300).
- FPS seen in m21 (lean profile, evening light): hab 20, 2 km over Orison 27, over the platforms 32, inside structure 48.
- Driving the game from scripts: `tools/front.sh`, `tools/keys.sh <sendkey args>` (keys, `mouse:dx,dy`, `at:x,y`, `click`),
  `tools/look.sh <name>` (screenshot into the scratchpad), `screencapture -v -V <s> file.mov` for video (brew's ffmpeg is
  broken: missing libx265).

## 2026-11-09 16:20-17:16 — run m22: ship terminal, ATC take-off (works), video, more script commands, god mode and menu checks

- **ATC take-off works.** Three Retrieves at the August Dunlow ship terminal (Avenger Stalker, an AI-variant Stalker, Avenger
  Titan): each time the mod's own `pad token ...: owner 0 -> you (rc20)` step ran in the right order (the keepalive alone was
  enough; padfix stood by). In the seat the hint read "REQUEST TAKEOFF"; the hail gave mod.log `[atc] NotifyPlayer mode 2`,
  Game.log `RequestMode: [TakeOffRequest], NotificationType: [TakingOffApproved]`, the hangar door `openState[Opening]`.
  Flew out twice (Stalker, Titan). Deliver, Retrieve and Store at the terminal all work. Not tried: landing / returning
  to the hangar.
- **Video:** `videos/takeoff-full.mov` (111 s, 1280x826, 68 MB: sit down, power, thrusters, hail, door, lift-off, fly-out,
  Orison at night), `videos/takeoff-preview.mov` (same, 480 wide, 10 MB), `videos/takeoff-full-frames.png` (six frames),
  `videos/takeoff-part1-hail-and-door.mov` (first try in the Stalker up to the door opening; 129 s, 170 MB).
  `screencapture -v` writes ~10 MB/s and ended by itself after 128.8 s when memory got tight, so: `tools/rec <out.mov>
  <seconds> [width=1280] [fps=30]` (ScreenCaptureKit, H.264 ~5 Mbit/s, SIGINT ends early). `avconvert --source a.mov
  --preset PresetMediumQuality|Preset1920x1080 --output b.mov` re-encodes; brew's ffmpeg/ffprobe are broken, so duration
  and frames come from Swift snippets (`swift tools/dur.swift <file>`, `swift tools/frames.swift <file> <out.png> <s>...`).
- **The recipe, as run by script** (everything through `tools/keys.sh` and `tools/modcmd.sh`):
  1. `!tp Spaceport_interior -3.44 55.61 0.05` (in front of the first terminal), `mouse:-960,0 mouse:0,330 F:120`, wait 9 s:
     the ship list is open.
  2. The terminal's pointer is Wine's cursor (only `screencapture -C` shows it). It moves with *relative* mouse input, about
     3 Wine px per unit, starts at the screen centre (864,558), and the view pans with it. Absolute moves (`at:`) swing the
     view and close the terminal. From the entry position `mouse:100,60` then `mouse:24,10` is the action button of the 4th
     visible row; in the dialog `mouse:-76,3` (or `-90,61`) is REQUEST DELIVERY / GO BACK. Wheel: ~0.6 row per notch. `S`
     (700 ms) leaves the terminal.
  3. The list is sorted by display name; rows with the same name follow `ships.txt`: the first of a group is the plain
     class, the others are AI variants that arrive with an NPC in the pilot seat (`AEGS_Avenger_Stalker_PU_AI_Advocacy`:
     `!sit` says "seat is taken"). Store such a ship and take the first row of a group.
  4. `!tp <ship entity> 0 4.5 -0.4` (Avenger cabin), `!sit <ship entity>`, `crew.power_on`, then **`I`: after power-on the
     thrusters are off** and Space/W do nothing until then. Hail: `+LALT 400 N:150 300 -LALT` (a quick chord is not seen).
     The door is fully open ~12 s later. `SPACE:750` lifts ~10 m, then `W`.
  Bindings from `Data\Libs\Config\defaultProfile.xml` (`/opt/homebrew/bin/python3.14 -I tools/p4kfind.py`): `v_atc_request`
  lalt+n, `v_atc_loading_area_request` ralt+n, `v_power_toggle` u, `v_power_toggle_thrusters` i; hold Y to leave a seat.
  Mouse look: ~800 units = 90 degrees. `!pose` yaw is not reliable in noclip.
- **More own commands in macfix** (on `game.vehicles` / `teleport.spatial` and dinput8 RVA 0xB0B0 `TeleportIntoZone`):
  `!seats <ship entity>`, `!sit <ship entity> [seat words]`, `!power <ship entity>`, `!tp <zone entity> x y z`. The mod's
  own `crew.sit pilot` needs you aboard already ("No target ship"); `crew.stand_all` does not unseat a player another
  plugin seated (hold Y); teleports do nothing while seated.
- **Adding commands without a restart:** `macfix/build-ext.sh [N]` builds the same source with `-DMACFIX_EXT`, copies it to
  `C:\users\crossover\macfix\macfix-extN.dll` and injects it; it runs commands through the loaded plugin's `sco_api`
  pointers (their RVAs are read from the *installed* `macfix.dll`, which must therefore be the loaded build). Its files:
  `macfix-ext.cmd` / `macfix-ext.log`; `EXT=1 tools/modcmd.sh ...`. Two ext copies would share one command file: unload the
  old one first (`inject.exe StarCitizen.exe <dll> unload`).
- **Memory in m22:** opening the hangar door onto the city started a paging burst (75k decompressions/s, 2-5 FPS) until the
  squeezer ran (16:47 squeeze 2: 8 GB pinned, 3.7 GB to swap); a third squeeze at 17:04. Swap 12.6-14 GB, disk free down
  to 7.7 GB during the session (16 GB again after the game stopped). A cross-city saved-spot jump: 125k decompressions/s
  and 9.6k swap-ins/s for a short while, one 1.8 s frame.
- **FPS seen (lean profile):** hab 20-30, spaceport hall 36-53, ship terminal UI 25-37, hangar in the seat 25-29, hangar
  door open onto the city 21-25, flying over Orison at night 18-21, terrace outside the habs at night 16-21.
- **God mode against a fall** (17:09): `!tp Spaceport_interior -3.44 55.61 120.05` with `creative.god on`: `!pose` samples
  show at least 66 m of fall, the last two 0.6 s steps ~19 m each (about 30 m/s), landing on the spaceport roof: no
  injury, no death. Not run with god mode off (it would kill the character). God mode = sco-core `game.creative`
  `set_god_mode`, held every half second.
- **The menu the way the user uses it** (injected pointer and keys): `M`, Player tab, one click on "God mode" -> the strip
  says "God mode on", `creative.status -> god on` (`videos/menu-god-mode-on.png`). The line under it ("... Turn it on in
  the launcher's Plugins page.") is fixed text in the mod (`src/builtins/loadout_ui.cpp:93`), shown whether the plugin is
  on or not. Travel tab: typing in the search box, dragging "Arrive N m above the ground" (minimum 101 m), a row + "Go to
  asop" -> "Teleported to asop." all work. Single objects arrive 50 m up, i.e. on the roof above the hall, so the `asop*`
  rows are of little use from the menu: use **saved spots** (exact position). Saved: `hab` (ObjectContainer-028 7.36
  -36.00 48.62) and `terminal` (Spaceport_interior -3.44 55.61 0). "Go" on `hab` from the spaceport arrived before the
  room had streamed in and fell through to a terrace; a second "Go" 15 s later stood in the room. The view behind the
  menu does not turn while the pointer moves over it.
- 17:16 game stopped (user away); `macfix/install.sh` installed the final build (with `!seats !sit !power !tp`).

## 2026-11-09 17:16-17:26 — run m23: the final macfix build from a cold start (left running for the user)

- `tools/measure-run.sh m23-final 360000` (100 h: no limit in practice). mod.log: `loaded macfix 1.1.0`, 14 found / 13
  loaded, `creative 1.0.0 native loaded`. macfix.log: `sc-offline 0.7.7 found`, padfix installed, the game's window found,
  mod code 17-21 runs/s idle (33-72 with the menu open). World after 305 s; squeeze 1 at 17:22:54 (6.2 GB to swap).
  At 17:25: paging 238 decompressions/s, swap 10.4 GB, disk free 12 GB.
- Standing in the hab after leaving the bed, the mod's menu open: 35 FPS (28.6 ms), the highest hab reading of the day (Entered after the session, from the screenshot taken at 17:23.)
- The new commands answer in the plugin build (`!pose`, `!find Spaceport_interior -> loaded`, `!seats x -> no such ship
  loaded`).
- Menu from a cold start, injected pointer: `M`, one click on "God mode" -> `creative.status -> god on` (god mode is off at
  every game start). Travel tab: search `termi` (typing M into the box does not close the menu) -> the saved spot
  `terminal` survived the restart -> "Go" -> standing at the ship terminals (`Spaceport_interior -3.44 55.61 0`), no fall
  this time, 48-51 FPS. The X closes the menu: foreground back to the game window, cursor clip released.
- The hab differs per start (bed at ObjectContainer-028 8.00 -27.75 36.85 in m23, another zone id than m22's), so the saved
  spot `hab` leads to a hab room, not necessarily the one you woke up in.
- Left for the user: player at the ship terminals, god mode on, menu closed, `caffeinate` holds the screen until ~21:50.
- Still open: landing / returning to the hangar, the user's own hand on the mouse in the menu, and from before: a steady
  25-30 FPS in the heaviest city views, the "pretty" profile, removing the diagnostics (`MVKSHIM_FRAME_STATS`,
  `MVK_CONFIG_PERFORMANCE_TRACKING*`, `r_DisplayInfo`).

## 2026-11-09 18:40-20:10 — runs m23 to m26: live settings, what limits each view, the upscaler, MoltenVK experiments, the `pretty` profile

- **Live settings (new).** The mod keeps a pointer to the game's console (cvars.cpp `g_console`, dinput8 RVA 0x7D4FC0).
  macfix uses it: `!cvar <name>` reads a console variable, `!cvar <name> <number>` sets it, `!exec <line>` runs a console
  line (console slots 0xC0 find variable, 0x130 run line; variable slots 0x20 read float, 0x38 set float). The user.cfg
  whitelist does not apply. Variable names: `strings` on StarCitizen.exe gives about 7,600 candidates (`DumpCommandsVars`
  writes nothing in this build). `tools/fps.sh [s]` reads the shim's counters (frame rate, render passes, draws,
  tessellated draws; needs `MVKSHIM_FRAME_STATS=1`) and the graphics chip's load from `ioreg`, so a setting can be
  switched and measured in seconds in one and the same scene. `tools/bench.sh terrace|hall` puts the character at a
  fixed spot and heading. Values read: r_Width x r_Height 1670 x 1080, r.TSR.UpscalingPreset 0, e_GsmLodsNum 5,
  g_VIM_populationManagerTargetFPS 20, sys_max_worker_threads 0 with 14 logical CPUs seen.
- **What limits which view** (`r_DisplayInfo 3` adds per-thread load and the replication layer's line):
  - Spaceport hall (calm, 1,000-1,600 draws): bound by the graphics chip. Upscaler, lean profile: native 37.1 FPS,
    Quality 45.9, Balanced 49.4, Performance 49.7, back to native 36.9 (m24); 35.3 -> 47.1 -> 35.3 in m23.
  - Terrace with the city in view (4,800-5,400 draws, 30-40 tessellated draws, 105-150k patches): 21-23 FPS by day,
    24-30 after the in-game sunset, when draws fall to about 3,900. Main thread 44-51 ms, of which the replication
    layer's main-thread sync ("RL ... MTSync") is 16-20 ms (3 ms in the hall); render thread 7-8 ms; paging quiet
    (under 100 decompressions/s); no thread saturated (seven threads at 55-65%). The upscaler gives 10% at most here.
  - No effect within noise there (windows of 15-25 s differ by 3 FPS on their own; the scene's load drifts with the time
    of day): e_ViewDistRatio 40 -> 15, e_LodRatio 3 -> 1, r.PlanetTerrain.Tessellation.MaxSubdivisions 7 -> 2,
    e_GsmLodsNum 5 -> 2, e_DynamicLights 0 (one 31 FPS reading did not repeat), e_ShadowsCastViewDistRatio and its
    Lights twin, e_ViewDistRatioLights 25 -> 8, g_VIM_V2_PerPlayerQuota 8, g_VIM_populationManagerTargetFPS 45,
    sv_AISystem 0.
- **MoltenVK patch 02** (`patches/moltenvk-1.4.2-02-tess-prepass-async-submit.patch`): two code paths that can be
  switched while the game runs (`tools/live.sh <switch> on|off|default`, files in `/private/tmp/mvk-live/`, or the
  bottle's `MVK_SC_TESS_PREPASS=1` / `MVK_SC_ASYNC_SUBMIT=1`). Both are off by default; neither changed the frame rate.
  - *tess-prepass*: the vertex and tessellation-control compute stages of a render pass's tessellated draws go to a
    MTLCommandBuffer of their own, queued ahead of the one carrying the pass, so the pass is ended and restarted once
    instead of once per draw. Measured in m25: 22.5 -> 2.5 restarts per frame, no errors, no visible difference; frame
    rate 26.1 / 25.5 / 30.1 off against 24.6 / 33.2 / 26.1 on, i.e. unchanged within noise. The first build (m24) never
    took the new path: it excluded command buffers that carry timestamp queries, which is all of them here; fixed by
    dropping the stage-counter fence at the split.
  - *async-submit*: queue submissions are encoded on MoltenVK's dispatch queue instead of the game's render thread.
    87 submissions a second moved; frame rate unchanged in hall and terrace. The render thread is not the limit.
  - MoltenVK prints a `[mvk-live]` line every 5 s (frames, tessellated draws, restarts, splits). Its "gpu ms per frame"
    (GPUEndTime - GPUStartTime summed over command buffers) rose from 28.7 to 42 with tess-prepass on while the frame
    rate did not fall: the spans of the extra command buffers overlap, so that sum is not a usable measure.
- **Dead end: a memory reserve.** `balloon` held 3 GB pinned while calm, to hand back at the first sign of paging. It
  never filled: taking 0.1-0.5 GB already pushed decompressions past 10,000 a second (free memory 0.05-0.6 GB). This
  Mac has no slack to hold back; removed. The drops after a hangar door opens or a jump across the city stay.
- **Prettier and faster: the `pretty` profile.** The lean profile's distances and pools, plus the groups Shading,
  PostProcessing, ShadowScreenSpace, TextureFiltering, TextureDetail and Fog at 3, Particles and ShadowMaps at 2, mip
  bias 0, and the upscaler at Quality. Switched live in m25: hall 45.0 / 44.7 FPS (lean native 37.5, lean with Quality
  46.5); terrace 25.0 / 22.6 (lean native 22.9 / 22.3). Picture compared at panel resolution: cleaner edges and
  reflections. From a cold start (m26, 20:03): hall 50.6 / 47.5 / 56.0 FPS, terrace 24.4 / 30.0.
- **Settings that stay** (macfix): `C:\users\crossover\macfix.cvars`, "name value" per line; set once you are in the world
  and again when the game puts one back or the file changes. m26: `[cvars] 10 held: 10 set now` at spawn.
  `tools/profile.sh <name>` installs `profiles/<name>.cvars` there, or removes it. Profiles: `pretty` (now selected),
  `fast` (lean with the upscaler at Balanced), `lean`, `low`.
- Housekeeping: shader dumps and MoltenVK's timing report are out of the bottle's environment (417 MB of dumps deleted);
  the start-up dialog clicker (PowerShell, about 350 MB) is stopped once the world is up; `patches/install-moltenvk.sh`
  builds and installs MoltenVK with both patches; `tools/build.sh` builds the helper tools.
- Other software on the Mac while measuring: the Claude app about 2.2 GB, a Java process 0.4 GB, WindowServer 0.7 GB.
- Still open: the city view is bound by the game's own main thread and its built-in server sync, not by anything the
  kit touches; the drops when a new part of the city has to be paged in; landing; other Macs.

## 2026-11-12 10:25-12:00 — without CrossOver: the game on the official WineHQ build, and a launcher

Question: does the kit need CrossOver, or will an open-source Wine do? Answer: the official WineHQ build runs the game,
at about the same frame rate in the one view compared, once five things CrossOver was supplying are supplied.

- **Candidates.** WineHQ's official macOS packages (`Gcenx/macOS_Wine_builds`): stable 11.0_1, tested; devel 11.21,
  downloaded and not needed. The Sikarugir engine `WS12WineSikarugir11.0_1` (it carries MSync): its first process exits
  during start-up with no message when run outside its own wrapper app, so it was dropped.
- **What CrossOver was supplying**, found one failure at a time on WineHQ 11.0:
  1. `ROSETTA_ADVERTISE_AVX=1`. CrossOver's `wine` wrapper sets it for every bottle. Without it the game stops within
     seconds with a breakpoint exception (0x80000003 in StarCitizen.exe).
  2. DXVK. The game asks Direct3D 11 for the adapters first (Game.log, "Logging video adapters": name, video memory,
     feature level, displays connected) and only then draws with Vulkan. On Wine's own Direct3D: "Feature level:
     unknown", then "None of the adapters available to the game process have a display connected", then the fatal
     "Failed to open render library". DXVK for macOS 1.10.3 (`Gcenx/DXVK-macOS`, the `-builtin` repack: `d3d11.dll`
     and `d3d10core.dll`, Wine's own `dxgi.dll` stays) cures it. `WINEDLLPATH` does not put it ahead of Wine's own
     files (Wine 11 looks in its own folder first), so the two files go into `system32` with the "Wine builtin DLL"
     stamp at offset 0x40 zeroed, and `d3d11,d3d10core=n,b`.
  3. The adapter's identity. Wine's `dxgi` takes it from wined3d, which says "NVIDIA GeForce 6800". With
     `VideoPciVendorID` 0x1002 and `VideoPciDeviceID` 0x66af under `HKCU\Software\Wine\Direct3D` it is the identity the
     shim gives the Vulkan device, and the game logs "Radeon RX Vega 20 (vendor = 0x1002, device = 0x66af), Feature
     level: DX11.1 (SM 5.0), Displays connected: yes".
  4. Feature level 11.1. This DXVK grants it only with `logicOp` and `variableMultisampleRate`, and MoltenVK reports
     neither. The shim now advertises both and strips them at `vkCreateDevice` (`MVKSHIM_NO_FL111=1` turns that off).
     `tools/dxgitest.c` prints what DXGI and Direct3D 11 report in a prefix; it reads the same in the CrossOver bottle
     and in the new one (feature level b100, one output).
  5. The dialog clicker. PowerShell 7 does not start in a plain prefix. `tools/dismiss.c` does the same job as a
     160 KB native program.
- **The Wine build is not modified.** The kit's libraries are found through `DYLD_LIBRARY_PATH` (the shim under both
  names Wine asks for, `libMoltenVK.dylib` and `libvulkan.1.dylib`), and winegrid is loaded with
  `DYLD_INSERT_LIBRARIES`: the WineHQ builds are ad-hoc signed, so dyld honours both, and `winegrid.log` says
  "on: 1 mmap and 1 munmap import(s) of ntdll.so redirected". The variables have to be set by `env` after
  `sandbox-exec`: a system program that is handed them directly refuses to start ("have 'x86_64,arm64', need
  'arm64e'").
- **MSync** is not in the WineHQ builds (no `WINEMSYNC` in `ntdll.so` or `wineserver`). Nothing hung without it.
- **Measured**, `pretty` profile, the view from the hab bed, 240 s in the world, all on 12 November:

  | Run | Wine | In the world after | Frame rate once the squeeze is done |
  |:--|:--|:--|:--|
  | oss-stable-3 | WineHQ 11.0, cold shader cache | 273 s | 32 to 33 |
  | cx-base-1 | CrossOver 26.3 | 304 s | 29 to 38, mostly 36 to 38 |
  | oss-stable-4 | WineHQ 11.0 | 273 s | 27 to 34, with compiler runs on the Mac at the same time |
  | launcher, second start | WineHQ 11.0, the launcher's own prefix | 279 s | 34 to 36 |

  One view only. The other scenes of Section 5 have not been measured on WineHQ.
- **The publisher's launcher** on WineHQ 11.0: its installer stops as it does under CrossOver. What it would install
  is one 7z archive stored inside it; found by the 7z signature, with its length taken from its start header, it
  unpacks with macOS's own `tar` in 9 s, and the launcher draws its sign-in page with the same Chromium flags as
  before (`--use-gl=angle --use-angle=swiftshader --in-process-gpu`).
- **The launcher** (`launcher/`): one program with the setup and start logic (`Sources/Engine.swift`), a window that
  shows it (`ui/`), and the same commands on the command line. Tried: a complete fresh setup in a temporary folder up
  to the sign-in step (Wine 177 MB, DXVK and the prefix in about 40 s; the publisher's launcher 346 MB; the mod from
  its release), each download checked against its published checksum; adopting the installed game by rename (146 GB,
  instant, a link left behind); start, stop and the live readings. A start goes boot, renderer at 75 s, level at
  181 s, seeded at 225 s, world at 279 s. Not tried: the game's download after sign-in (no room for a second copy),
  any other Mac.
- A stopped or crashed run leaves the mod in the game folder (`Bin64/sc-offline.installed`); the launcher runs the
  mod's own `uninstall` after every stop and before it opens the publisher's launcher, and refuses to open that
  launcher while the game runs.
- The launcher's window was built twice on this day. The first was one large gauge in the handbook's style; the
  owner asked for a plain modern launcher instead, and that is what ships: a sidebar, a panel with a drawn backdrop,
  one bright button, a slim bar under one line of status, four tiles. In flight on the test machine it read 26.8 GB
  held by the game against 24 installed, 30 FPS, 11.7 GB of swap (12:36).
