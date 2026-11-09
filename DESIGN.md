---
name: sc-offline-mac
description: The operating handbook for a type and the one airframe that has flown, Star Citizen offline on Macs with Apple silicon, tested on a 24 GB M4 Pro; a README on github.com whose design lives in outlined SVG figures and Markdown conventions.
colors:
  ink: "#1f2328"
  paper: "#ffffff"
  muted: "#59636e"
  hair: "#d1d9e0"
  green: "#1a7f37"
  yellow: "#bf8700"
  red: "#cf222e"
  tabred: "#cf222e"
  ink-dark: "#f0f6fc"
  paper-dark: "#0d1117"
  muted-dark: "#9198a1"
  hair-dark: "#3d444d"
  green-dark: "#3fb950"
  yellow-dark: "#d29922"
  red-dark: "#f85149"
  tabred-dark: "#da3633"
  emergency-numeral: "#ffffff"
typography:
  cover-title:
    fontFamily: "Helvetica-Bold, drawn as outlines"
    fontSize: "74px"
    fontWeight: 700
    letterSpacing: "-0.02em"
  cover-designation:
    fontFamily: "Helvetica-Bold, drawn as outlines"
    fontSize: "25px"
    fontWeight: 700
    letterSpacing: "0.12em"
  cover-description:
    fontFamily: "Helvetica, drawn as outlines"
    fontSize: "27px"
    fontWeight: 400
    lineHeight: 1.26
  cover-data-label:
    fontFamily: "Courier-Bold, drawn as outlines"
    fontSize: "24px"
    fontWeight: 700
    lineHeight: 1.54
  cover-data-value:
    fontFamily: "Courier, drawn as outlines"
    fontSize: "24px"
    fontWeight: 400
    lineHeight: 1.54
  placard:
    fontFamily: "Helvetica-Bold, drawn as outlines"
    fontSize: "25px"
    fontWeight: 700
    letterSpacing: "0.08em"
  tab-numeral:
    fontFamily: "Helvetica-Bold, drawn as outlines"
    fontSize: "20px"
    fontWeight: 700
  figure-title:
    fontFamily: "Helvetica-Bold, drawn as outlines"
    fontSize: "17px"
    fontWeight: 700
    letterSpacing: "0.05em"
  figure-text:
    fontFamily: "Helvetica, drawn as outlines"
    fontSize: "17px"
    fontWeight: 400
  figure-data:
    fontFamily: "Courier-Bold, drawn as outlines"
    fontSize: "17px"
    fontWeight: 700
  figure-data-quiet:
    fontFamily: "Courier, drawn as outlines"
    fontSize: "17px"
    fontWeight: 400
rounded:
  none: "0px"
spacing:
  figure-margin: "20px"
  box-inset: "18px"
  layer-gap: "10px"
  cover-margin: "40px"
components:
  cover-plate:
    backgroundColor: "{colors.paper}"
    textColor: "{colors.ink}"
    typography: "{typography.cover-title}"
    rounded: "{rounded.none}"
    width: "880px"
    height: "700px"
    padding: "40px"
  thumb-tab:
    backgroundColor: "{colors.ink}"
    textColor: "{colors.paper}"
    typography: "{typography.tab-numeral}"
    rounded: "{rounded.none}"
    size: "32px"
  thumb-tab-dark:
    backgroundColor: "{colors.ink-dark}"
    textColor: "{colors.paper-dark}"
    typography: "{typography.tab-numeral}"
    rounded: "{rounded.none}"
    size: "32px"
  thumb-tab-emergency:
    backgroundColor: "{colors.tabred}"
    textColor: "{colors.emergency-numeral}"
    typography: "{typography.tab-numeral}"
    rounded: "{rounded.none}"
    size: "32px"
  thumb-tab-emergency-dark:
    backgroundColor: "{colors.tabred-dark}"
    textColor: "{colors.emergency-numeral}"
    typography: "{typography.tab-numeral}"
    rounded: "{rounded.none}"
    size: "32px"
  placard:
    backgroundColor: "{colors.ink}"
    textColor: "{colors.paper}"
    typography: "{typography.placard}"
    rounded: "{rounded.none}"
    width: "466px"
    height: "56px"
  placard-dark:
    backgroundColor: "{colors.ink-dark}"
    textColor: "{colors.paper-dark}"
    typography: "{typography.placard}"
    rounded: "{rounded.none}"
    width: "466px"
    height: "56px"
  kit-part:
    backgroundColor: "{colors.ink}"
    textColor: "{colors.paper}"
    typography: "{typography.figure-data}"
    rounded: "{rounded.none}"
    width: "176px"
    height: "30px"
  measured-bar:
    backgroundColor: "{colors.ink}"
    rounded: "{rounded.none}"
    height: "16px"
  load-bar:
    backgroundColor: "{colors.ink}"
    textColor: "{colors.paper}"
    typography: "{typography.figure-data}"
    rounded: "{rounded.none}"
    height: "34px"
  hatched-rule:
    backgroundColor: "{colors.tabred}"
    rounded: "{rounded.none}"
    width: "880px"
    height: "14px"
---

# Design System: sc-offline-mac

## Overview

**Creative North Star: "The Pilot's Operating Handbook"**

The README is an operating handbook written the way a real one is: for a type, and flight-tested on one airframe. The type is every Mac with Apple silicon flying Star Citizen offline; the airframe is the one machine it has run on, a MacBook Pro 16-inch with an M4 Pro and 24 GB. The line under the title names the type, the data plate names the airframe (TESTED ON), and every figure with numbers says whose numbers they are. It documents the machine the way pilots document an aircraft, with a cover that carries a data plate, numbered sections on thumb tabs, limitations, emergency and normal procedures as challenge-and-response checklists, and performance figures that state their conditions. The light theme is the printed page, ink on paper. The dark theme is the instrument face, light markings on a dark ground.

The surface is github.com, so the system has exactly two layers. GitHub owns everything Markdown renders: the body face and its sizes, heading sizes and their rules, table borders and striping, code-block grounds and their monospace face, link colour, alert colours and icons, the column width and every breakpoint. None of those are tokens here and none may be overridden. The project owns the SVG figures in `docs/assets/`, drawn by `docs/assets/src/build.py`, and the conventions for how Markdown is written. Every token in this file is a value in that script; the figures' two palettes are GitHub's own light and dark page colours, so a figure sits on the page as if printed on it.

Lettering in figures is Helvetica and Courier converted to outlines, because an SVG shown through an image tag cannot load fonts. One image file serves desktop and phone, so lettering is sized for the phone: the cover is shown at about 0.41 of its drawn size there and a figure at 0.6, which puts the smallest lettering at about 10 px.

**Key Characteristics:**
- Ink and paper, plus an instrument-marking code (green, yellow, red) that appears only where it encodes a measurement or a limit.
- Two faces with fixed jobs: Helvetica Bold capitals name things, Courier states data.
- Square, flat, ruled. No shadows, no gradients, no rounded containers.
- Every figure exists as a light and a dark file, chosen by the reader's colour scheme.
- Checklists set in 38 monospace columns with dot leaders, so they fit a phone without wrapping.
- Static. The one moving element is the take-off sequence, a recording.

## Colors

Two palettes of the same eight roles, one per GitHub theme; each figure is built once from each.

### Primary
- **Ink** (`ink` on the page, `ink-dark` on the instrument face): all lettering, frames, the solid fill of tabs, placards, kit-part labels and measured bars, axes and dimension lines.
- **Paper** (`paper`, `paper-dark`): the ground, equal to GitHub's page colour in that theme. Also the lettering colour on any ink-filled shape, and the knock-out patch laid behind lettering that a line would otherwise cross.

### Secondary
- **Limit Red** (`red`, `red-dark`): the limit. On the cover it is the span dimension labelled MEMORY 24 GB, AS TESTED on a second line; in the loading graph it is the vertical red line at 24 GB and its label, which names the test machine because the line sits at whatever memory a Mac has installed. Line weight 2.6 in the graph.
- **Emergency Red** (`tabred`, `tabred-dark`): the fill of the Section 3 thumb tab and of the hatched rule under that heading, nowhere else. In the light theme it is the same value as Limit Red; in the dark theme it is deeper, because it carries a white numeral (`emergency-numeral`) where Limit Red is drawn as thin lines and lettering on the dark ground.

### Tertiary
- **Normal-range Green** (`green`, `green-dark`): the part of a scale at or above the target, its label, and the dashed target line (1.5 wide, dashes 3 3) drawn behind each bar.
- **Caution Yellow** (`yellow`, `yellow-dark`): the part of a scale below the target. A band colour. In the light theme it reaches only about 3.1:1 against paper, so it is not a lettering colour there.

### Neutral
- **Muted** (`muted`, `muted-dark`): secondary lettering inside figures (notes beside a title, scale numerals, axis titles, legend text, the issue line on the cover, the as-found row) and the dot leaders.
- **Hairline** (`hair`, `hair-dark`): scale ticks behind bars and the rule that separates the as-found row, 1.5 wide.

### Named Rules
**The Marking Code Rule.** Green, yellow and red are instrument markings, not accents. A colour may appear in a figure only where it encodes a measured range, a target or a limit, and the figure must say in words what it means (GREEN: 25 AND ABOVE, RED LINE: TEST MACHINE, 24 GB). Everything else is ink on paper.

**The Two Reds Rule.** Limit Red marks a number the machine cannot exceed. Emergency Red marks Section 3 and only Section 3: its tab and the hatched rule under its heading. Neither is used for emphasis.

**The One Tint Rule.** Ink at 0.38 opacity has a single meaning: the measured spread at the end of a bar, where the true value lies somewhere inside. A dashed ink outline with no fill means not in memory or not part of the kit's result (data in swap, the as-found row). No other tints or fills exist.

## Typography

**Display Font:** Helvetica Bold, drawn as outlines (no fallback: there is no live text in a figure)
**Body Font:** Helvetica Regular, drawn as outlines, inside figures. README prose is GitHub's own sans-serif at GitHub's sizes.
**Label/Mono Font:** Courier and Courier Bold, drawn as outlines, inside figures. Checklists and commands are GitHub's own monospace.

**Character:** A printed technical manual. Helvetica Bold capitals with open tracking name a thing; Courier, the typewriter face of a data plate, states what was measured.

### Hierarchy
- **Display** (`cover-title`: bold, 74, tracking -0.02 em): the name sc-offline-mac on the cover, lower case, once.
- **Headline** (`cover-designation`: bold capitals, 25, tracking 0.12 em; `placard`: bold capitals, 25, tracking 0.08 em): OPERATING HANDBOOK under the title, and the boxed placard.
- **Title** (`figure-title`: bold capitals, 17, tracking 0.05 em): names inside figures: layer names, row titles, CONDITIONS, axis titles.
- **Body** (`cover-description`: regular, 27, lines 34 apart; `figure-text`: regular, 17): sentences and notes inside the cover and figures.
- **Label** (`cover-data-label` and `cover-data-value`: Courier Bold and Courier, 24, rows 37 apart; `figure-data` and `figure-data-quiet`: Courier Bold and Courier, 17): data. Bold for the label of a plate row and for a value that is the point of the figure; regular for the value beside a label, conditions, scale numerals.
- **Tab numeral** (`tab-numeral`: bold, 20 in a 32 square): the section number.

### Named Rules
**The One Size Rule.** Inside a figure every piece of lettering is 17. Hierarchy comes from face, weight, capitals and ink against muted, never from a second size. The cover is the only plate with a scale, and its smallest lettering is 24.

**The Phone Floor Rule.** Lettering is sized by what it becomes in a 360 px column: drawn size times 0.6 for a figure, times 0.41 for the cover, and the result stays at about 10 px. A new figure keeps the 600 measure and the 17 lettering rather than adding detail that needs smaller type.

**The Outline Rule.** Every letter in a figure is a path produced by `textpath` from Helvetica or Courier. No `<text>` elements, no font references, so each figure carries its full reading in the `aria-label` and `<title>` and again in the `alt` of the image.

**The Written Range Rule.** A measured range is written with the word "to" (16 to 25 FPS), in prose, tables and figures alike. An en dash in a table cell means not measured.

## Layout

The column, its width and its breakpoints belong to GitHub. Two measures are set by the project.

- **The cover measure (880).** The cover is the title page and takes the full column, left-aligned in a plain paragraph, 880 by 700, inside a 2-wide ink frame with a 40 margin. The hatched rule shares this measure.
- **The figure measure (600).** Every other figure and every photograph or recording is 600 wide, centred, with its caption centred beneath it after a line break. Drawn figures keep a 20 margin inside the measure; scales run from 20 to 580.

Order of a page: the cover, one opening paragraph that links to Section 4, the Notice, Performance specifications, the take-off sequence, the table of sections, then the nine sections. The three front-matter headings (Notice, Performance specifications, Sections) carry no tab; each of the nine section headings begins with its thumb tab.

Within a figure: boxes are inset 18 from their frame, stacked boxes are 10 apart, a kit-part row is 38 high, a frame-rate row is 52, a loading-graph row is 96. A dot leader starts 12 after its label.

Tables are GitHub's. Columns are left-aligned; only the section-number column is centred. A performance table never stands alone: a **Conditions.** paragraph (profile, resolution, sync, place, runs, date, how it was read) comes before it, or a line stating the runs and date follows it directly.

Checklists are fenced blocks with no language, every line at most 38 characters and every challenge-and-response line exactly 38, which fits a phone column without horizontal scrolling. Commands go in a separate `bash` block after the checklist, never inside it.

## Elevation & Depth

Flat. There are no shadows, no gradients and no blurred edges anywhere in the figures, and Markdown offers none. Order is shown the way a drawing shows it: a paper-coloured patch is laid behind lettering so that a line passes behind it (the red line behind row titles in the loading graph, the scale behind value labels), and the open lid in the cover's side view is an ink stroke with a paper stroke down its middle.

## Shapes

Square and ruled. Frames, tabs, placards, kit-part labels, bars and bands have square corners (radius 0). Outer frames and layer boxes are 2 wide in ink; axes are 2; hairlines, ticks and leaders are 1.5; dimension lines are 1.2 to 1.8.

Rounded corners exist only where a drawing depicts a rounded object: the Mac's lid, screen, notch and base on the cover. They describe the machine, not the system.

Line patterns each mean one thing:
- **Dot leader** (1.5 wide, round caps, dash 0.01 5, muted): joins a label to its value.
- **Short dash 3 3, green:** the target.
- **Dashed outline, ink** (3 2.4 at bar scale, 5 3.5 at load-bar scale): a quantity that is not in memory or not the kit's result.
- **Hatch** (7 of red in every 14, at 45 degrees, 14 high): the emergency edge.

Arrowheads are solid triangles, a little over a third as wide as they are long, at the ends of dimension lines.

## Components

No component is interactive; there are no hover or focus states to specify. Links are GitHub's.

### Cover plate
The title page. Left: the name, OPERATING HANDBOOK, a two-line description that names the type (Star Citizen, offline, on Macs with Apple silicon), the data plate of the machine tested, the two placards. Right: a front and a side line drawing of the machine tested with dimension lines, its span given as MEMORY 24 GB, AS TESTED in Limit Red and its thickness as PAGE 16 KB. The page is every Apple silicon Mac's; the memory is this machine's, and the label says so. Bottom right: the issue and date in muted Courier. Placed with `<picture>` and a dark-scheme `<source>`, width 880, with an `alt` that reads the whole plate aloud.

### Data plate
Rows of label, dot leader, value: label in Courier Bold capitals, value in Courier, values aligned on one left edge (232 on the cover), rows 37 apart. On the cover the first row is TESTED ON and the plate describes the one machine every number was measured on, never the machines the kit is for. The same construction in Markdown is the checklist line.

### Placard
A solid ink box with square corners, 466 by 56 on the cover, holding one line of bold tracked capitals in paper colour, centred. Used twice, stacked 10 apart, one for each standing rule: OFFLINE OPERATION ONLY and RESEARCH AND STUDY ONLY. A placard states a standing rule; it is not a badge, and there is no third.

### Thumb tab
A solid 32 square with the section number in Helvetica Bold 20, paper on ink. Section 3 is Emergency Red with a white numeral. Shown at 28 by 28 at the start of a second-level heading, vertically centred on the heading text, with the alt text "Section N." The tabs are not stepped down the page edge as printed tabs are: at 28 px a position among nine would not read.

### Hatched rule
An 880 by 14 band of 45-degree Emergency Red stripes, placed once, directly under the Emergency procedures heading, with empty alt text.

### Checklist
A fenced block. A condition checklist opens with its condition in capitals on one or two lines, then numbered steps; a procedure checklist is unnumbered. Each line is challenge, a space, a run of full stops, a space, RESPONSE in capitals, filled to 38 columns. The explanation follows in prose under the block: where the log is, why the step exists.

### Figure and caption
Drawn figures use `<picture>` with a light and a dark file; photographs and recordings use a single `<img>`. Width 600, in a centred paragraph, then `<br>` and the caption: "Figure S-N." with the section number and a serial, and a short title. A photograph or recording is signed in its caption with its run and date ("Run m22, 9 November 2026."). A drawn performance figure carries a CONDITIONS block in Courier at its top left and is followed by the **Conditions.** paragraph.

### Layer box and kit-part label
A 2-wide ink frame across the measure with the layer name in tracked bold capitals at the left and a muted note at the right. Inside it, each kit part is a solid ink label 176 by 30 with its name centred in Courier Bold, paper colour, and what it does beside it in Helvetica. The legend restates the code: solid means built and installed by the scripts here, outlined means your own copy.

### Range bar
A measured range as a solid ink bar on a scale: 16 high over hairline ticks every 10 units, with the dashed green target line behind it and the value ("36 to 53") in Courier Bold after the bar on a paper patch. Above the rows, a 9-high band shows the code, yellow below the target and green from it up, each labelled in words. The as-found row is set apart by a hairline, drawn as a dashed outline, and titled in muted.

### Load bar
A 34-high solid ink bar from zero, its reading in Courier Bold paper colour inside it, a title in tracked bold capitals above at the left and the resulting frame rate in Courier Bold at the right. The measured spread continues the bar at 0.38 opacity; data held in swap continues it as a dashed outline with a muted Courier note. The red line crosses all rows at the installed limit.

### Notices
GitHub alerts in GitHub's colours, three kinds only, each defined once in Section 1: CAUTION, ignoring this can break the installation or put the game online; WARNING, ignoring this costs time; NOTE, worth knowing, nothing breaks if skipped.

The Notice in the front matter is not one of them. It is a short section of plain prose under its own heading, like the notice page of a printed information manual: why the work is published (research and education), that it is unofficial, that it holds no game files, that it runs offline. It states purpose and makes no claim of approval or compliance; the full statement is in Section 2.

## Do's and Don'ts

### Do:
- **Do** draw every new figure in `docs/assets/src/build.py` from the `THEMES` roles, and ship it as a `-light` and a `-dark` file placed with `<picture>`.
- **Do** keep figures at the 600 measure with all lettering at 17, and the cover alone at 880.
- **Do** use Helvetica Bold capitals for names and Courier for data, as outlines, and give each figure an `aria-label` and an `alt` that state every value it shows.
- **Do** say in words what a marking colour means inside the figure that uses it.
- **Do** hold checklists to 38 columns with full-stop leaders and capital responses, and put commands in their own `bash` block.
- **Do** state conditions with every performance table, and sign each photograph or recording with its run and date.
- **Do** begin each of the nine section headings with its thumb tab, shown at 28 by 28.
- **Do** leave prose, tables, code blocks, links and alerts in GitHub's own styling.

### Don't:
- **Don't** use green, yellow or red for emphasis, decoration or status. They are data; see The Marking Code Rule.
- **Don't** use the hatched rule or Emergency Red outside Section 3.
- **Don't** add shadows, gradients, tints or rounded containers. Rounded corners belong only to a drawing of a rounded object.
- **Don't** put live text or font references in an SVG, or add a second lettering size to a figure.
- **Don't** set lettering in light-theme Caution Yellow; it is a band colour.
- **Don't** add badges, logos, emoji bullets or a screenshot carousel, and do not use the game's logos, trade dress or interface styling.
- **Don't** invent approvals, stamps, serial numbers or signatures. The handbook borrows the form of an aircraft manual, not its authority.
- **Don't** add a fourth kind of notice or use an alert for anything but its defined meaning.
