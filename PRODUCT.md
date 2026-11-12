# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

Two surfaces. The README on github.com, and since 12 November 2026 a launcher app for macOS on Apple silicon whose interface is a web page shown in a system web view.

## Stack

The README: GitHub-flavoured Markdown rendered on github.com (README.md and docs/), with static SVG and PNG assets kept in the repository. Given by the surface, not chosen: no CSS, no scripts, light and dark themes.

The launcher (`launcher/`): a Swift program that is both a window and a command-line tool. `Sources/Engine.swift` holds all setup and start logic; the window shows `ui/index.html`, `app.css` and `app.js` in a WKWebView and only passes messages. Default window 1180 by 760, minimum 980 by 680, dark only.

## Users

Primary (confirmed by the owner, "both, players first"): people who own Star Citizen and a Mac with Apple silicon, any model, and want to play offline, single-player, with the sc-offline mod, since 12 November 2026 without CrossOver: the launcher sets up WineHQ's own Mac build. They arrive asking three things in order: can my Mac do this, what will it feel like, how do I set it up. The first answer has to be honest: the kit applies to every Mac with Apple silicon and has run on one.

Secondary: people who work on or tinker with Wine, CrossOver and MoltenVK and want the technical findings and the measurements behind them.

## Product Purpose

A kit of fixes, a helper plugin and scripts for running Star Citizen offline on Macs with Apple silicon, published for research and education (the owner's instruction, 9 November 2026): a study of why a large Windows program runs out of memory there and what fixes it. On the one machine tested, an M4 Pro with 24 GB, it took the game from 3-6 FPS to playable. Success for a reader: they reach the game world on their own Mac and fly a ship out of a hangar at frame rates close to the measured ones.

## Positioning

On a Mac with Apple silicon the game fails on memory, not on graphics speed (every such Mac has 16 KB pages; the game under Rosetta asks for memory 4 KB at a time), and the kit removes the waste at its sources instead of lowering settings until it fits: Wine's 4 KB commits on 16 KB pages (winegrid), MoltenVK's per-pool tessellation scratch buffers (patched MoltenVK), macOS keeping cold data compressed in RAM (the squeezer), and the offline mod talking to a dead window handle (macfix). Every claim is a measurement from a logged run.

## Operating Context

A terminal on the Mac, CrossOver with one bottle, the game installed through its official launcher by the owner, the sc-offline mod, Xcode command-line tools and mingw-w64 for building. Each fix has an install script and a remove script and keeps the original file it replaces. The game process runs inside a macOS sandbox profile that denies it all network access.

## Capabilities and Constraints

- Offline and single-player only. The kit is never used with the official servers; this is the owner's standing rule.
- The repository ships no game files, no CrossOver or Wine binaries and no copy of the mod. The launcher downloads Wine, DXVK, the mod and the publisher's launcher from their own sources, each checked against its published checksum; the person needs their own copy of the game. A release carries the compiled launcher with the kit's own parts (the owner's decision, 12 November 2026: "Publish it for everyone now").
- The launcher never signs in and never accepts the publisher's terms for the person, never presses Launch Game in the publisher's launcher, and refuses to open that launcher while the game runs.
- For every Mac with Apple silicon (the owner's instruction, 9 November 2026), tested on one: MacBook Pro 16" M4 Pro, 24 GB, macOS 26.2, CrossOver 26.3, game LIVE 4.10.2, sc-offline 0.7.7, MoltenVK 1.4.2. Never write as if the kit were for the M4 Pro or for 24 GB only, and never state a result for a Mac that was not measured. Intel Macs are out of scope.
- Published for research and education only, non-commercial: nothing is sold and no money is asked for. This is a statement of purpose and a disclaimer. It is not a licence term (GPL-3.0 allows no added restriction) and never a claim that the work complies with anyone's terms or with the law.
- macfix is tied by address to sc-offline 0.7.7 and does nothing on another build until its offsets are redone.
- A CrossOver update overwrites the patched ntdll.so and MoltenVK; the install scripts must be run again.
- Not verified: other Macs, landing and returning to a hangar, play sessions beyond a few hours.
- Licence: GPL-3.0 (required for plugins built against the mod's API header).
- An unofficial fan project, not affiliated with the Cloud Imperium group of companies or Roberts Space Industries. The game imagery in `docs/media` belongs to the publisher and is credited to it; trademarks are named as their owners' property.

## Brand Commitments

Name: sc-offline-mac. No logo. No use of the game's logos, trade dress or in-game interface styling as decoration.

The README keeps the pilot's operating handbook (the owner's choice, 9 November 2026).

The launcher follows the game-launcher standard, played straight: laconic and modern, in the company of the RSI Launcher and CurseForge. This is the owner's standing preference of 12 November 2026, in their words "он должен быть лаконичным, современным, как в самом лаунчере rsi или curse forge", given after an instrument-panel design in the handbook's style was built and rejected. The published app carries no imagery of the game: its backdrop is drawn by the app, and a person may set a picture of their own that stays on their Mac.

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
