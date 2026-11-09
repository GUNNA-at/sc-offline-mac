---
version: 1
slug: "readme-md"
primary_target: "README.md"
related_targets: []
---

# Surface brief: README.md

Scope: the repository's front page on github.com (README.md) and the figures it loads from `docs/assets/` and `docs/media/`. Visitor mode: Read.

Audience and job: first, a Star Citizen owner with a 24 GB-class Apple Silicon Mac who asks, in order, can my Mac do this, what will it feel like, how do I set it up. Second, Wine / CrossOver / MoltenVK tinkerers who want the findings and the numbers. Confirmed by the owner: both, players first.

Action: follow Section 4 (Normal procedures) and reach the game world. Proof on hand: measured frame rates per scene, the take-off video and frames, the lab notebook, per-run logs.

Constraints: GitHub-flavoured Markdown with sanitized HTML (no CSS, no scripts), light and dark themes, phone widths. No game logos or in-game interface styling. No invented approvals, stamps, serials or signatures. Offline-only rule stated up front. Every number comes from a logged run.

Chosen direction: Pilot's operating handbook (the owner's choice in the direction round, over the dealt direction "The 24 GB map"). Memorable moment: a cover that documents a laptop the way a handbook documents an aircraft, with 24 GB as its red line, followed at once by a Performance specifications page.

Measures: the cover is the title page and takes the full column (880). Figures and plates share one narrower measure (600), centred with their captions, because one image file serves desktop and phone alike and lettering sized to stay at 10 px on a phone would be oversized at 880.

Unresolved: results on other Macs; landing and returning to a hangar; the "pretty" graphics profile; whether github.com honours width media queries on picture sources (it would allow separate phone figures).

## Direction contract

THESIS: This is the operating handbook for one machine: a 24 GB Apple Silicon Mac flying Star Citizen offline, outside the envelope it was built for. It documents that the way pilots document such machines: limitations, procedures, measured performance. It refuses the category default of logo, badge row, emoji feature bullets and screenshot carousel.

OWN-WORLD: A printed handbook page in the light theme, an instrument face in the dark theme. Ink and paper only, plus the instrument-marking code used strictly as data: green arc for the normal range, yellow arc for the caution range, a red line for the limit. Helvetica Bold capitals for titles and Courier for data, drawn as outlines so they render the same everywhere. Square black thumb-tab numerals for the nine sections (red for Section 3), a dot-leader data plate, a boxed placard, CONDITIONS blocks above performance tables, WARNING / CAUTION / NOTE with handbook definitions, and one red hatched rule reserved for the Emergency procedures heading.

STORY: In one screen the reader learns which machine this was measured on and what frame rates it reaches, believes it because every figure is signed with its run, date and conditions, then follows Normal procedures to set it up and take off.

FIRST VIEWPORT: A full-width cover plate. Left: the title "sc-offline-mac" at display size, "Operating handbook" and the two-line description beneath it, then the dot-leader data plate (machine, memory, macOS, CrossOver, game, mod) and the boxed placard OFFLINE OPERATION ONLY. Right: a front and a side line drawing of the Mac with dimension lines, its span given as memory, 24 GB, in red, and its thickness as one page, 16 KB. Directly under the cover: an opening paragraph that links to Section 4, the Performance specifications table, then the take-off sequence. There are no buttons.

SIGNATURE: Challenge-and-response checklists with dot leaders, held to 38 columns so they fit a phone, and the thumb-tab numerals in the section headings. The tabs are not stepped: an image set inside a heading line is 28 px square, too small to show a position among nine. Motion: none; GitHub renders a static page, and the one moving element is the take-off sequence.

FORM: Pilot's operating handbook, candidate 1 of 7 on the ordered grounded list, chosen by the owner over the assigned candidate 5; seed key 1e3a9aeb.

FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance.
