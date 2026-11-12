---
version: 1
slug: "launcher-ui-index-html"
primary_target: "launcher/ui/index.html"
related_targets: ["launcher/ui/app.css","launcher/ui/app.js"]
---

# Surface brief: the launcher app

Scope: the macOS launcher window (`launcher/ui/`, shown in a web view by `launcher/Sources/AppShell.swift`). Visitor mode: Operate.

Audience and job: a Star Citizen owner with a Mac with Apple silicon who wants to play offline and does not want to follow a procedure by hand. Two visits: the first run, where the app fetches and sets everything up and stops only where the person must act (Rosetta's licence, the publisher's sign-in and terms, the choice to move an existing game), and every later run, where the job is one press and then watching the start.

Task and states: not set up, setting up (a step running with progress), holding for the person, ready, starting (boot, renderer, level, seeded), in flight, stopped after a fatal error. The game runs offline only; the multiplayer button exists and is unavailable.

Content: the nine setup steps with their real states, the Mac's own data, the configuration the kit was tested on, live frame rate, the game's memory against installed memory, compressed memory, swap, paging. Every number is read from the machine. Nothing is stated for a Mac that was not measured.

Constraints: no game logos, trade dress or in-game interface styling; the published app carries no imagery of the game (the backdrop is a photograph from NASA's open archives, three come with the app and are credited on the panel; a person may set a picture of their own, which stays on their Mac). Controls are the ordinary ones of a desktop app. The app never signs in and never accepts the publisher's terms for the person. While the game runs the window must cost almost nothing: nothing animates in flight.

History of the direction. 12 November 2026, first build: "Same family, the cockpit side" and the layout "One instrument" (seed key 5969eb4a), built and reviewed. The owner then saw it running and rejected the look, in these words: "поменяй дизайн. он должен быть лаконичным, современным, как в самом лаунчере rsi или curse forge и так далее" (change the design; it must be laconic and modern, like the RSI Launcher itself or CurseForge and the like). That pins the direction below and retires the instrument for this surface. The README keeps the handbook's world. Other answers of the same day still stand: "Publish it for everyone now", "Move it out of CrossOver", buttons for "single player and multi player (soon)", "do everything automated".

Carried over from the first build's finish review, because they are about behaviour and not about the look: rows and controls are corrected in place and never rebuilt on an engine message; the first-run button says Set up; the row that needs the person is always in view and its command is not repeated beside the main button; opened without the app, the page says its readings are simulated.

Unresolved: pressing Install in the publisher's launcher automatically after sign-in; results on other Macs.

## Direction contract

THESIS: A modern game launcher, played straight: one calm dark window, a key-art panel, one bright action and one line of status under it. It refuses the costume of an instrument panel, and anything a person has to read before they can press the button.

OWN-WORLD: Neutral charcoal ground (#0d0e11) with raised surfaces (#16181d, #1d2027) and hairline borders; one accent blue (#4c9dff) for the action and for progress; green, amber and red only for state. The system typeface in sentence case with tabular numerals, radii of 10 to 14 px, authored line icons of one weight. The backdrop is a real photograph (three from NASA's open archives come with the app: Earth under the aurora beside the Milky Way, an orbital sunrise, the Webb telescope's Cosmic Cliffs) or the person's own picture; nothing on it is drawn or generated.

STORY: The person opens it, reads the state in one line, presses the one bright button, watches a slim bar walk through real stages, and while playing sees four numbers. Nothing asks to be studied.

FIRST VIEWPORT: 1180 by 760. A sidebar 208 wide: the name, four pages (Play, Setup, Settings, Log), and at its foot the two standing rules and the version. The Play page: a rounded panel with the backdrop, on it the title Star Citizen, a line saying offline on this Mac, the status line with its bar, and the action row: Single player as the bright button, Multiplayer beside it, unavailable, with a Soon tag. Under the panel, a row of four tiles.

SIGNATURE: The status line. One sentence and one slim bar that is setup progress cut into nine, then the loading stages cut where they were measured, then in flight the game's memory with a mark where this Mac's installed memory ends.

FORM: The category standard, pinned by the owner with the RSI Launcher and CurseForge named as the company it keeps; it replaces the dealt structure "One instrument" (seed key 5969eb4a) for this surface.

FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance.
