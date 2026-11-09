# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Stack

GitHub-flavoured Markdown rendered on github.com (README.md and docs/), with static SVG and PNG assets kept in the repository. Given by the surface, not chosen: no CSS, no scripts, light and dark themes.

## Users

Primary (confirmed by the owner, "both, players first"): people who own Star Citizen and an Apple Silicon Mac with around 24 GB of memory, and want to play offline, single-player, through CrossOver with the sc-offline mod. They arrive asking three things in order: can my Mac do this, what will it feel like, how do I set it up.

Secondary: people who work on or tinker with Wine, CrossOver and MoltenVK and want the technical findings and the measurements behind them.

## Product Purpose

A kit of fixes, a helper plugin and scripts that takes Star Citizen on a 24 GB Apple Silicon Mac from 3-6 FPS to playable, offline. Success for a reader: they reach the game world on their own Mac and fly a ship out of a hangar at frame rates close to the measured ones.

## Positioning

On this class of Mac the game fails on memory, not on graphics speed, and the kit removes the waste at its sources instead of lowering settings until it fits: Wine's 4 KB commits on 16 KB pages (winegrid), MoltenVK's per-pool tessellation scratch buffers (patched MoltenVK), macOS keeping cold data compressed in RAM (the squeezer), and the offline mod talking to a dead window handle (macfix). Every claim is a measurement from a logged run.

## Operating Context

A terminal on the Mac, CrossOver with one bottle, the game installed through its official launcher by the owner, the sc-offline mod, Xcode command-line tools and mingw-w64 for building. Each fix has an install script and a remove script and keeps the original file it replaces. The game process runs inside a macOS sandbox profile that denies it all network access.

## Capabilities and Constraints

- Offline and single-player only. The kit is never used with the official servers; this is the owner's standing rule.
- The repository ships no game files, no CrossOver or Wine binaries and no copy of the mod. The reader needs their own copy of the game, CrossOver and the mod.
- Measured on one machine only: MacBook Pro 16" M4 Pro, 24 GB, macOS 26.2, CrossOver 26.3, game LIVE 4.10.2, sc-offline 0.7.7, MoltenVK 1.4.2.
- macfix is tied by address to sc-offline 0.7.7 and does nothing on another build until its offsets are redone.
- A CrossOver update overwrites the patched ntdll.so and MoltenVK; the install scripts must be run again.
- Not verified: other Macs, landing and returning to a hangar, play sessions beyond a few hours.
- Licence: GPL-3.0 (required for plugins built against the mod's API header).
- Not affiliated with Cloud Imperium Games or Roberts Space Industries.

## Brand Commitments

Name: sc-offline-mac. No logo. No use of the game's logos, trade dress or in-game interface styling as decoration.

## Evidence on Hand

- `videos/takeoff-full.mov`, `videos/takeoff-preview.mov`, `videos/takeoff-full-frames.png`: a take-off from the Orison hangar.
- `videos/menu-god-mode-on.png`, `ingame-*.png`, `loading-screen-run5.png`: in-game screenshots.
- `RUNBOOK.md`: the dated lab notebook with every measurement and dead end.
- `runs/*/log.txt`: per-run logs (memory, paging, GPU load every 30 s).
- Measured frame rates per scene, before and after each fix.
- Absent, never to be invented: results on other hardware, user testimonials, comparisons with other tools.

## Product Principles

1. Measure first, then claim. A number in the README comes from a logged run and says where it was measured.
2. Offline only, and say so plainly.
3. Reversible. Every change has a remove script and keeps the original.
4. Say what is not verified.
5. Fix causes, not symptoms: quality settings are the last lever, not the first.
