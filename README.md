<p>
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/cover-dark.svg">
    <img src="docs/assets/cover-light.svg" width="880" alt="sc-offline-mac operating handbook. Star Citizen, offline, on a 24 GB Apple Silicon Mac. Machine: MacBook Pro 16-inch, M4 Pro. Memory: 24 GB. macOS 26.2. CrossOver 26.3. Game: LIVE 4.10.2. Mod: sc-offline 0.7.7. Offline operation only. Issue 1, 9 November 2026. A line drawing of the laptop gives its span as memory, 24 GB, in red, and its thickness as one page, 16 KB.">
  </picture>
</p>

A kit of fixes and scripts that takes Star Citizen on a 24 GB Apple Silicon Mac from 3 to 6 frames per second to 20 to 53 indoors and 16 to 25 with a city in view, offline and single-player, through CrossOver and the [sc-offline](https://github.com/scuba-sc/sc-offline) mod. The game was failing on memory, not on graphics speed. This handbook says what the machine does, where its limits are, and [how to set it up](#-normal-procedures).

## Performance specifications

| Performance | Measured |
|:--|:--|
| Frame rate indoors: hab, spaceport hall, hangar | **20 to 53 FPS** |
| Frame rate with the city in view: hangar door open, flying over it at night | **16 to 25 FPS** |
| Frame rate as found, before the fixes | 3 to 6 FPS |
| Launch to standing in the world | about 5 minutes |
| Memory the game needs: as found, then now | 44 to 59 GB, then 26 to 33 GB |
| Take-off: clearance from the tower to hangar door open | about 12 seconds |
| Network access of the game process | none: a macOS sandbox profile denies it |

One machine, one evening: runs m21 to m23, 9 November 2026. Conditions are in [Section 5](#-performance).

<p align="center">
  <img src="docs/media/takeoff.webp" width="600" alt="An animated sequence from the pilot seat: the hangar door opens onto the city, the ship lifts off and flies out over it at night.">
  <br>
  Figure 0-1. Take-off from the Orison hangar at six times real speed. Run m22, 9 November 2026.
</p>

Power on, clearance from the tower, the door opens, lift-off, out over the city at night. The counters in the corner are the game's own. [The video (9 MB)](docs/media/takeoff-preview.mov) plays at real speed.

## Sections

| No. | Section | You will find |
|:-:|:--|:--|
| 1 | [General](#-general) | What is in the kit and where each part sits |
| 2 | [Limitations](#-limitations) | Offline only, the hardware and versions this was measured on, what is not verified |
| 3 | [Emergency procedures](#-emergency-procedures) | The frame rate collapses, the menu ignores the mouse, the game dies while loading |
| 4 | [Normal procedures](#-normal-procedures) | Setup from an empty bottle, starting the game, the first take-off |
| 5 | [Performance](#-performance) | Frame rates by scene, with conditions, and what each fix gave |
| 6 | [Weight and balance](#-weight-and-balance) | Where 24 GB goes, before and after |
| 7 | [Systems](#-systems) | How each fix works, its log, how to remove it |
| 8 | [Handling, service and maintenance](#-handling-service-and-maintenance) | After an update of CrossOver, the mod or the game |
| 9 | [Supplements](#-supplements) | Test tools, the lab notebook, licence, credits |

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/tab-1-dark.svg"><img src="docs/assets/tab-1-light.svg" width="28" height="28" align="absmiddle" alt="Section 1."></picture> General

The kit does not contain the game, CrossOver or the mod. You bring your own copies of those. The kit adds seven parts around them, each with a script that installs it and a way to take it out again.

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/fig-1-1-systems-dark.svg">
    <img src="docs/assets/fig-1-1-systems-light.svg" width="600" alt="Figure 1-1. Where each part of the kit sits between the game and the Mac: macfix at the game, winegrid and vk1wrap in Wine, mvkshim and the MoltenVK patch at MoltenVK, the squeezer and the sandbox in macOS.">
  </picture>
  <br>
  Figure 1-1. Where each part sits.
</p>

| Part | What it does | What it gave |
|:--|:--|:--|
| [winegrid](winegrid) | A 4 KB commit stops costing a 16 KB page | A 21 GB heap costs 19 GB of memory, not 35 to 50 |
| [MoltenVK patch](patches) | One shared pool of tessellation scratch buffers | Graphics memory 5.0 GB, not 8.5 to 9 |
| [squeezer](winegrid/balloon.c) | Moves cold compressed data out to swap | Paging under 1,000 per second, was 45,000 |
| [mvkshim](mvkshim) | Presents the graphics chip; swaps rejected pipelines for empty ones | The game starts |
| [vk1wrap](vk1wrap) | Answers two Windows-only Vulkan extensions | The game starts |
| [macfix](macfix) | Repairs the mod's dead window handle | Menu, hotkeys and tower work |
| [launcher](sc-offline-mac.sh) | Starts the game with no network, plus the squeezer | One command to play |

Three kinds of notice are used in this handbook, in GitHub's own colours:

> [!CAUTION]
> Ignoring this can break the installation or put the game online.

> [!WARNING]
> Ignoring this costs you time: a crash, a reinstall, a wrong measurement.

> [!NOTE]
> Worth knowing. Nothing breaks if you skip it.

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/tab-2-dark.svg"><img src="docs/assets/tab-2-light.svg" width="28" height="28" align="absmiddle" alt="Section 2."></picture> Limitations

**Offline operation only.** The launcher runs the game inside a `sandbox-exec` profile that denies all network access to the game's process tree, and `./sc-offline-mac.sh nettest` proves it from inside Windows. Nothing here is meant for the official servers and nothing here has been used with them.

**Your own copy.** The repository holds no game files, no CrossOver or Wine binaries and no copy of the mod. It is not affiliated with or endorsed by Cloud Imperium Games or Roberts Space Industries.

**One measured configuration.** Every number in this handbook comes from this machine. Nothing else has been tried.

| Item | As measured |
|:--|:--|
| Machine | MacBook Pro 16-inch, M4 Pro, 24 GB |
| macOS | 26.2 |
| CrossOver | 26.3 (Wine with MSync on, D3DMetal off) |
| Game | LIVE 4.10.2, build 4.10.197.2878, Vulkan renderer |
| Mod | sc-offline 0.7.7, `boot_map = PU` (the Stanton system only) |
| MoltenVK | 1.4.2, patched |
| Display mode | 1728 x 1117, scaled up by macOS |
| Disk | 146 GB for the game, 10 to 14 GB of swap while playing; keep 20 GB free |

**Version locks.**

- macfix works by address inside the mod's `dinput8.dll` 0.7.7. On any other build it checks, logs that it found something else, and does nothing.
- A CrossOver update replaces the patched `ntdll.so` and MoltenVK. Run the install scripts again ([Section 8](#-handling-service-and-maintenance)).

**Not verified.**

- Any other Mac. With less than 24 GB, expect it not to work. With more it should be easier; nobody has measured it.
- Landing, and returning to a hangar.
- Sessions longer than a few hours.
- More than one star system loaded (`boot_map = PU_All` carries 2.6 million entities instead of 1.6 million).

> [!CAUTION]
> Never press **Launch Game** in the RSI Launcher inside this bottle. It installs the anti-cheat service and takes the game online. Use the launcher only to install and verify the game.

> [!WARNING]
> Never run `sample` or `vmmap` on the game. They freeze the process long enough for the engine to declare a hung graphics device and exit. `footprint`, `top`, `vm_stat` and `ps -M` are safe.

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/tab-3-dark.svg"><img src="docs/assets/tab-3-light.svg" width="28" height="28" align="absmiddle" alt="Section 3."></picture> Emergency procedures

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/assets/hatch-dark.svg">
  <img src="docs/assets/hatch-light.svg" width="880" alt="">
</picture>

```
FRAME RATE FALLS TO 2-5 FPS
after a door opens or a teleport
1. Wait ................. 20 TO 60 SEC
2. Squeezer log ................ CHECK
3. No squeeze logged ......... SQUEEZE
```

The squeezer reacts by itself once paging builds up. Its log is `/private/tmp/sc-squeezer.log`. One squeeze by hand:

```bash
./sc-offline-mac.sh squeeze
```

```
MENU (M) IGNORES THE MOUSE,
OR THE TOWER DOES NOT ANSWER
1. macfix log .................. CHECK
2. "sc-offline 0.7.7 found" .. PRESENT
3. "ran N times/s" ........ 15 OR MORE
4. Mod was updated ......... SECTION 8
```

The log is `C:\users\crossover\macfix.log` in the bottle.

```
THE GAME EXITS WHILE LOADING
1. Shim log .................... CHECK
2. Game log .................... CHECK
3. Watchdog lines ........ IN user.cfg
4. Other applications ........... QUIT
```

The shim logs to `/private/tmp/mvkshim.log`, the game to `LIVE\Game.log`. The watchdog lines are the first block of [`config/user.cfg.example`](config/user.cfg.example). The game needs every gigabyte: quit what else is running and start again.

```
TELEPORT DROPPED YOU THROUGH A FLOOR
1. God mode ....................... ON
2. Go .................... PRESS AGAIN
```

The room had not loaded when you arrived. God mode is in the mod's menu, Player tab.

```
SHIP IS POWERED BUT DOES NOT MOVE
1. Thrusters .................. ON (I)
```

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/tab-4-dark.svg"><img src="docs/assets/tab-4-light.svg" width="28" height="28" align="absmiddle" alt="Section 4."></picture> Normal procedures

This is not a one-click installer. These are the steps that were done on the measured machine, in the order that works. Allow an evening, most of it the game download.

### Before you start

```
Mac ............. APPLE SILICON, 24 GB
Free disk ..................... 200 GB
CrossOver ....................... 26.3
Xcode ...................... INSTALLED
Homebrew packages .......... INSTALLED
Star Citizen ........... YOUR OWN COPY
sc-offline mod ................. 0.7.7
```

```bash
brew install mingw-w64 vulkan-headers
```

### The bottle and the game

```
Bottle "StarCitizen" .......... CREATE
Windows version ........... 11, 64-BIT
MSync ............................. ON
D3DMetal ......................... OFF
RSI Launcher ................. INSTALL
sc-launcher.sh ................... RUN
Sign in ..................... YOURSELF
Game, LIVE channel ........... INSTALL
Launch Game button ............. NEVER
```

The RSI Launcher draws a white window under CrossOver unless it is started with the flags in [`sc-launcher.sh`](sc-launcher.sh).

### The mod

```
sc-offline 0.7.7 .............. UNPACK
boot_map .......................... PU
Game path ........................ SET
```

Unpack it to `C:\users\crossover\sc-offline` in the bottle. [`config/sc-offline.ini.example`](config/sc-offline.ini.example) shows the values used.

### The graphics layer

```
Patched MoltenVK ...... BUILD, INSTALL
mvkshim ............... BUILD, INSTALL
vk1wrap ............... BUILD, INSTALL
```

```bash
patches/install-moltenvk.sh
mvkshim/build-and-install.sh
vk1wrap/build-and-install.sh
```

### The memory layer

```
winegrid ..................... INSTALL
Bottle environment ............... SET
```

```bash
winegrid/install.sh
```

The environment goes into the bottle's `cxbottle.conf`; [`config/cxbottle-environment.example`](config/cxbottle-environment.example) has the lines.

### Game settings

```
user.cfg ........................ COPY
Profile ......................... LEAN
```

Copy [`config/user.cfg.example`](config/user.cfg.example) to `LIVE\user.cfg`, then:

```bash
./sc-offline-mac.sh profile lean
```

### The mod's helper

```
macfix ....................... INSTALL
Cheats plugin .............. SWITCH ON
```

```bash
macfix/install.sh
```

The mod ships its cheats plugin (god mode, noclip) switched off, and the switch is in its Windows launcher. Deleting the file `data\plugins\creative\disabled` in the mod's folder is all that switch does.

<p align="center">
  <img src="docs/media/mod-menu.jpg" width="600" alt="The mod's menu over the game, Player tab, with the God mode box ticked and the status line reading God mode on.">
  <br>
  Figure 4-1. The mod's menu with God mode ticked. Run m22, 9 November 2026.
</p>

### Starting

```bash
./sc-offline-mac.sh play
```

```
First window ................ 1 MINUTE
Information boxes ... CLOSE THEMSELVES
In the world ............... 5 MINUTES
Leave the bed ................. HOLD Y
Mod menu ........................... M
```

### First take-off

<p align="center">
  <img src="docs/media/takeoff-frames.jpg" width="600" alt="Six frames from the pilot seat: the closed hangar door, the powered cockpit, the door opening onto the city, the door open, the ship lifted in front of the opening, and flight over the city.">
  <br>
  Figure 4-2. Six moments of the take-off. Run m22, 9 November 2026.
</p>

```
Travel, saved spot .... SHIP TERMINALS
Ship terminal ............... RETRIEVE
Pilot seat ....................... SIT
Power ......................... ON (U)
Thrusters ..................... ON (I)
Tower ............ HOLD L-ALT, PRESS N
Hangar door ........... OPEN IN 12 SEC
Lift ................... SPACE, THEN W
```

> [!NOTE]
> The terminal lists some ships several times under one name. Take the first row of a group: the others arrive with a computer-controlled pilot already in the seat.

> [!NOTE]
> In the measured take-offs the ship was powered through the mod's `crew.power_on` command, and the thrusters were still off afterwards. `U` is the game's default key for power and was not pressed in those runs.

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/tab-5-dark.svg"><img src="docs/assets/tab-5-light.svg" width="28" height="28" align="absmiddle" alt="Section 5."></picture> Performance

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/fig-5-1-frame-rate-dark.svg">
    <img src="docs/assets/fig-5-1-frame-rate-light.svg" width="600" alt="Figure 5-1. Frame rate by scene, in frames per second: spaceport hall 36 to 53, ship terminal screen 25 to 37, hab 20 to 35, hangar in the seat 25 to 29, hangar door open onto the city 21 to 25, flying over the city at night 18 to 21, terrace outside the habs at night 16 to 21. As found, any scene: 3 to 6. The target is 25 and above.">
  </picture>
  <br>
  Figure 5-1. Frame rate by scene.
</p>

**Conditions.** Lean profile ([`profiles/lean.cfg`](profiles/lean.cfg)), 1728 x 1117, vertical sync off, Orison. Runs m21 to m23, 9 November 2026, read from the game's own counter.

| Scene | FPS |
|:--|:--|
| Spaceport hall | 36 to 53 |
| Ship terminal screen | 25 to 37 |
| Hab | 20 to 35 |
| Hangar, in the pilot seat | 25 to 29 |
| Hangar door open onto the city | 21 to 25 |
| Flying over the city, night | 18 to 21 |
| Terrace outside the habs, night | 16 to 21 |

<p align="center">
  <img src="docs/media/spaceport-hall.jpg" width="600" alt="The spaceport hall with its ship terminals; the game's counter in the corner reads 51.4 frames per second.">
  <br>
  Figure 5-2. The spaceport hall at 51 frames per second. Run m23, 9 November 2026.
</p>

**Notes.**

1. Right after a hangar door opens onto the city, or after a jump across it, the frame rate falls to 2 to 5 for a short while until the squeezer has made room.
2. Night is heavier than day: more lights are drawn. The game's clock runs about six times real time, so the same view is day in one launch and night in the next. Do not compare launches without reading the clock.
3. The target is 25 and above everywhere. The city views do not reach it yet.
4. A jump to another planet costs a minute or more at 2 to 3 FPS.

**What each step gave.**

| Step | Runs | Calm | City |
|:--|:--|:--|:--|
| As found | m1, m2, m10 | 3 to 6 | – |
| winegrid | m11 to m13 | 25 to 30 | 10 to 15 |
| Lean profile, sync off | m14, m15 | 31 to 35 | 8 to 15 |
| Patched MoltenVK | m18, m19 | – | 14 to 29 |
| Squeezer | m20 | – | 25 to 31 |
| Today | m21 to m23 | 20 to 35 | 16 to 25 |

Calm is a room with no city in view; City is a view with the city in it. A dash means not measured. The 3 in the first row is the owner's own first session, before run m1; the squeezer's 25 to 31 was a light city view.

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/tab-6-dark.svg"><img src="docs/assets/tab-6-light.svg" width="28" height="28" align="absmiddle" alt="Section 6."></picture> Weight and balance

An aircraft handbook has a chapter on how much the machine may carry and where. For this machine the load is memory, and the limit is 24 GB.

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/fig-6-1-loading-dark.svg">
    <img src="docs/assets/fig-6-1-loading-light.svg" width="600" alt="Figure 6-1. Loading graph. As found the game needed 44 to 59 GB of memory on a Mac that has 24, at 3 to 6 FPS. With winegrid and the patched MoltenVK it needs 26 to 33 GB, at 10 to 30 FPS. With the squeezer 24 GB stays in memory and 10 to 13 GB of data the game never touches again sits in swap, at 16 to 53 FPS.">
  </picture>
  <br>
  Figure 6-1. Loading graph.
</p>

**Why it was overloaded.** Under Rosetta the game works in 4 KB pages; the Mac's memory comes in 16 KB pages. Wine commits memory the way Windows programs ask for it, and this game asks for its heap one 4 KB page at a time. Each of those pieces ended up as its own kernel object, rounded up to 16 KB. A million of them turned 21 GB of data into 35 to 50 GB of demand.

**Loading table.**

| GB of memory | As found | Now |
|:--|:--|:--|
| The game's heap, as the kernel charges it | 35 to 50 | 19 to 22 |
| Graphics memory, wired | 8.5 to 9 | 5.0 |
| Compressed data held in memory | 7.5 to 9 | 3.3 to 3.6 |
| Live, uncompressed data | 4.4 | 7.5 |
| Page decompressions per second | 160,000 to 218,000 | under 1,000 |

The game still needs 26 to 33 GB, more than the Mac has. About 14 GB of that is the offline mod running the whole star system's server-side state, which the game writes once and never reads again. The squeezer puts that part in swap, where it costs disk instead of memory.

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/tab-7-dark.svg"><img src="docs/assets/tab-7-light.svg" width="28" height="28" align="absmiddle" alt="Section 7."></picture> Systems

### winegrid

Wine reserves address space with an anonymous mapping and commits pieces of it later. On macOS the kernel creates the memory object for such a mapping at the first touch, sized to whatever piece is being touched, so every separately committed piece becomes its own object and is rounded up to a 16 KB page. `libwinegrid.dylib` is loaded into Wine before the first reservation and redirects `mmap` and `munmap` inside `ntdll.so`: a reservation becomes a mapping of named memory objects, 128 MB each, and committing a 4 KB piece then costs 4 KB. `winegrid/kt.c` reproduces the problem without Wine: 1 GB of data costs 4 GB one way and 1 GB the other.

Active only when the bottle's environment has `WINEGRID=1`. Log: `/private/tmp/winegrid.log`. Remove: [`winegrid/remove.sh`](winegrid/remove.sh).

### The MoltenVK patch

Metal has no tessellation shaders, so MoltenVK runs those stages as compute passes into temporary buffers of 2, 4 and 8 MB. Each Vulkan command pool kept its own set and reused the oldest first; the game records from dozens of pools, so about 3,700 buffers, 5 to 6 GB, appeared whenever a planet was in view. The patch gives the device one shared set and reuses the newest first: 507 buffers, 1.9 GB.

Source: [`patches/`](patches). Remove: put back the unpatched build, kept beside it as `libMoltenVK.real.dylib.khronos-1.4.2`.

### mvkshim

A small library that sits in front of MoltenVK. It reports the graphics chip as a discrete card, which the game insists on, and advertises a few features the game demands and then never needs. When Apple's shader compiler rejects one of the game's pipelines (a handful of water and particle variants do this every time), it builds an empty pipeline in its place so those few effects draw nothing instead of ending the game.

Log: `/private/tmp/mvkshim.log`. Remove: [`mvkshim/remove-shim.sh`](mvkshim/remove-shim.sh).

### vk1wrap

A `vulkan-1.dll` for the Windows side that answers two Windows-only extensions the game asks for and passes everything else to Wine's own.

Remove: restore `system32\vulkan-1.dll.wine-fake` and delete the `vulkan-1` override in the bottle.

### The squeezer

Under pressure macOS lets compressed memory fill about 60% of what is not wired before it writes any of it to swap. The game's never-touched data therefore sat in memory, compressed, while its live data went in and out of the compressor every frame. `winegrid/balloon` pins a few gigabytes for a few seconds; the kernel answers by moving its oldest compressed data to swap, and it stays there. `balloon auto` does this when compressed memory exceeds 5 GB and paging passes 10,000 decompressions per second.

Log: `/private/tmp/sc-squeezer.log`. Off: `SC_NO_SQUEEZE=1 ./sc-offline-mac.sh play`.

### macfix

The mod finds the game's window one second after start and keeps the handle. Under CrossOver the game recreates its window while loading, so the mod was posting its wake-up messages to a window that no longer existed, and its own code ran zero times per second unless the mouse happened to move. The menu looked dead, hotkeys missed presses, and the step that makes you the owner of your hangar pad lost a race, so the tower queued every take-off request forever. macfix finds the live window, repairs the mod's handle and keeps the wake-ups coming. It also runs mod commands from a file, which is how the test flights in this handbook were flown without hands on the keyboard.

Switches: `C:\users\crossover\macfix.ctl`. Log: `C:\users\crossover\macfix.log`. Remove: delete `data\plugins\macfix` in the mod's folder.

### The launcher and the sandbox

`./sc-offline-mac.sh play` starts the mod's own launcher inside a `sandbox-exec` profile that denies network access to everything it starts, then a dialog dismisser, a crash watcher and the squeezer. `nettest` shows a Windows process inside the same sandbox failing to open a connection or resolve a name.

### Graphics profiles

`./sc-offline-mac.sh profile lean` writes a block of settings into `LIVE/user.cfg` for the next launch. `lean` is below the game's Low on draw distance and model detail, which is what the measurements here used. `low` is the game's own Low. `pretty` raises shading, shadows and textures and has not been measured yet.

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/tab-8-dark.svg"><img src="docs/assets/tab-8-light.svg" width="28" height="28" align="absmiddle" alt="Section 8."></picture> Handling, service and maintenance

```
AFTER A CROSSOVER UPDATE
1. winegrid .................. INSTALL
2. Patched MoltenVK .......... INSTALL
3. mvkshim ................... INSTALL
4. winegrid log ........... SAYS "on:"
```

An update replaces the patched `ntdll.so` and MoltenVK. Run the three install scripts of Section 4 again, then check that `/private/tmp/winegrid.log` says `on:` for `StarCitizen.exe`.

```
AFTER A MOD UPDATE
1. macfix log ................... READ
2. Offsets in macfix.c .......... REDO
3. macfix .................... INSTALL
```

The log names the build macfix found. The method for finding the offsets again is in the notebook, entry of 9 November, 15:05.

```
AFTER A GAME UPDATE
Not tried.
```

Expect to repeat the pipeline and whitelist checks recorded in the notebook.

```
REMOVING EVERYTHING
1. winegrid ................... REMOVE
2. mvkshim .................... REMOVE
3. CrossOver's MoltenVK ...... RESTORE
4. vulkan-1.dll .............. RESTORE
5. Mod ..................... UNINSTALL
```

```bash
winegrid/remove.sh
mvkshim/remove-shim.sh
./sc-offline-mac.sh uninstall
```

CrossOver's own MoltenVK was kept as `libMoltenVK.dylib.cx-26.3.0` beside the others: rename it back to `libMoltenVK.dylib`. Wine's `vulkan-1.dll` was kept as `system32\vulkan-1.dll.wine-fake`: copy it back and delete the `vulkan-1` override in the bottle.

**Logs.**

- Game: `LIVE\Game.log`
- Mod: `data\mod.log` in the mod's folder
- macfix: `C:\users\crossover\macfix.log`
- winegrid: `/private/tmp/winegrid.log`
- MoltenVK shim: `/private/tmp/mvkshim.log`
- Squeezer: `/private/tmp/sc-squeezer.log`
- Launcher: `sc-offline-mac.log` in the bottle's folder

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/tab-9-dark.svg"><img src="docs/assets/tab-9-light.svg" width="28" height="28" align="absmiddle" alt="Section 9."></picture> Supplements

### Flying it from a script

The test flights were flown by script: mod commands through macfix, keys and pointer through Wine.

```bash
tools/modcmd.sh "creative.god on"
tools/modcmd.sh "?list"
tools/keys.sh 300 M:80
tools/look.sh shot1
tools/rec out.mov 120
```

In order: run a mod command and print its reply, list every command the mod offers, bring the game forward and press M, take a screenshot with the pointer in it, record the screen for 120 seconds.

macfix adds its own commands for testing: `!pose`, `!find <entity>`, `!goto <entity>`, `!tp <zone> x y z`, `!seats <ship>`, `!sit <ship>`, `!power <ship>`. The recipe from ship terminal to open sky is in the notebook.

### The lab notebook

[`docs/NOTEBOOK.md`](docs/NOTEBOOK.md) is the dated record: every run, every number, and the dead ends with the measurement that closed them. [`runs/`](runs) holds the per-run logs, one line every 30 seconds.

### The figures

The cover and figures are drawn by [`docs/assets/src/build.py`](docs/assets/src/build.py) from the numbers in that file.

### Licence and credits

GPL-3.0, see [`LICENSE`](LICENSE). The mod's plugin interface requires it of plugins, and macfix is one.

This work stands on [sc-offline](https://github.com/scuba-sc/sc-offline) by scuba-sc, [MoltenVK](https://github.com/KhronosGroup/MoltenVK) by the Khronos Group, and Wine as shipped in CrossOver by CodeWeavers. Star Citizen is a trademark of Cloud Imperium Games; this project has no connection to them.

### Log of revisions

| Issue | Date | Change |
|:--|:--|:--|
| 1 | 9 November 2026 | First issue |
