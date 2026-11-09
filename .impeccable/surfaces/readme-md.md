---
version: 1
slug: "readme-md"
primary_target: "README.md"
related_targets: []
---

# Surface brief: README.md

Scope: the repository's front page on github.com (README.md) and the figures it loads from `docs/assets/` and `docs/media/`. Visitor mode: Read.

Audience and job: first, a Star Citizen owner with a Mac with Apple silicon, any model, who asks, in order, can my Mac do this, what will it feel like, how do I set it up. The honest first answer: the kit applies to every such Mac and has run on one (the owner's instruction, 9 November 2026: for all Apple silicon Macs, tested only on the M4 Pro with 24 GB). Second, Wine / CrossOver / MoltenVK tinkerers who want the findings and the numbers. Confirmed by the owner: both, players first.

Action: follow Section 4 (Normal procedures) and reach the game world. Proof on hand: measured frame rates per scene, the take-off video and frames, the lab notebook, per-run logs.

Constraints: GitHub-flavoured Markdown with sanitized HTML (no CSS, no scripts), light and dark themes, phone widths. No game logos or in-game interface styling. No invented approvals, stamps, serials or signatures. The two standing rules, offline only and research and study only, stated up front (the owner's instruction, 9 November 2026: present the work as educational and research). No line may read as if the kit were for the M4 Pro or for 24 GB only, and no result is stated for a Mac that was not measured. The notice states purpose; it never claims approval or compliance. Every number comes from a logged run.

Chosen direction: Pilot's operating handbook (the owner's choice in the direction round, over the dealt direction "The 24 GB map"). Memorable moment: a cover that documents a laptop the way a handbook documents an aircraft, with the tested machine's 24 GB as its red line, followed by a short Notice and a Performance specifications page.

Measures: the cover is the title page and takes the full column (880). Figures and plates share one narrower measure (600), centred with their captions, because one image file serves desktop and phone alike and lettering sized to stay at 10 px on a phone would be oversized at 880.

Unresolved: results on other Macs (the issue form "Report a tested configuration" collects them); landing and returning to a hangar; the "pretty" graphics profile; whether github.com honours width media queries on picture sources (it would allow separate phone figures).

## Direction contract

THESIS: This is an operating handbook written as a real one is, for a type and flight-tested on one airframe: Macs with Apple silicon flying Star Citizen offline, outside the envelope they were built for, tested on a 24 GB M4 Pro and published as research. It documents that the way pilots document such machines: limitations, procedures, measured performance. It refuses the category default of logo, badge row, emoji feature bullets and screenshot carousel.

OWN-WORLD: A printed handbook page in the light theme, an instrument face in the dark theme. Ink and paper only, plus the instrument-marking code used strictly as data: green arc for the normal range, yellow arc for the caution range, a red line for the limit. Helvetica Bold capitals for titles and Courier for data, drawn as outlines so they render the same everywhere. Square black thumb-tab numerals for the nine sections (red for Section 3), a dot-leader data plate, a boxed placard, CONDITIONS blocks above performance tables, WARNING / CAUTION / NOTE with handbook definitions, and one red hatched rule reserved for the Emergency procedures heading.

STORY: In one screen the reader learns that the kit is for any Mac with Apple silicon, which machine it was tested on and what frame rates that one reaches, believes it because every figure is signed with its run, date and conditions, then follows Normal procedures to set it up and take off.

FIRST VIEWPORT: A full-width cover plate. Left: the title "sc-offline-mac" at display size, "Operating handbook" and the two-line description beneath it, then the dot-leader data plate (tested on, memory, macOS, CrossOver, game, mod) and two boxed placards, OFFLINE OPERATION ONLY and RESEARCH AND STUDY ONLY. Right: a front and a side line drawing of the Mac with dimension lines, its span given as memory, 24 GB as tested, in red, and its thickness as one page, 16 KB. Directly under the cover: an opening paragraph that links to Section 4, the Performance specifications table, then the take-off sequence. There are no buttons.

SIGNATURE: Challenge-and-response checklists with dot leaders, held to 38 columns so they fit a phone, and the thumb-tab numerals in the section headings. The tabs are not stepped: an image set inside a heading line is 28 px square, too small to show a position among nine. Motion: none; GitHub renders a static page, and the one moving element is the take-off sequence.

FORM: Pilot's operating handbook, candidate 1 of 7 on the ordered grounded list, chosen by the owner over the assigned candidate 5; seed key 1e3a9aeb.

FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance.
