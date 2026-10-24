# davedown

A native markdown viewer and editor for the desktop. C++20, SDL3 for the
window and input, Dear ImGui (docking branch) for the interface. The build
produces one folder that can be copied to another machine and run: no
installer, no redistributable, no DLLs.

## What it does

Every open document has three views, switchable from the toolbar or with
`Ctrl+1` / `Ctrl+2` / `Ctrl+3`:

- **Preview** renders the markdown (GFM dialect via md4c): headings, lists,
  task lists you can tick, block quotes, fenced code with a copy button,
  tables with content-aware column widths, images relative to the file,
  links (web links open in the browser, `.md` links open in a new tab,
  `#anchors` scroll to the heading), strikethrough, inline code.
- **Source** is the raw markdown in a monospace, word-wrapped, syntax-coloured
  editor with line numbers. Enter continues list markers, Tab indents.
- **Edit** is the WYSIWYG mode: a block-based live preview. Click a block to
  edit it in place; it renders again when the caret leaves. Enter splits a
  block, Backspace at the start joins it with the previous one (or strips
  its formatting first), arrow keys cross block boundaries, Tab indents list
  items, and typing `## `, `- `, `1. `, `> `, `- [ ] ` or ```` ``` ```` at the
  start of a paragraph converts it.

Everything else you would expect: multiple documents as dockable tabs, an
outline panel of headings that scrolls the view, find with match count and
highlights, undo/redo in every mode, recent files, drag-and-drop to open,
unsaved-changes prompts, eight colour themes (the fwcom token set), interface
scaling from 70% to 200% with DPI awareness, and a modal About box.

Preferences and recent files are saved to `davedown.settings` beside the
executable (or the per-user folder when that is read-only).

## Building

Requirements: Visual Studio 2022 or later (MSVC), CMake 3.24+, Ninja. SDL3,
Dear ImGui, the ImGui Test Engine and stb are fetched by CMake; md4c is
vendored under `third_party/`.

```
tools\build.cmd release
```

configures `C:\buildfiles\davedown\release` with the `release` preset and
builds. The runnable folder is `C:\buildfiles\davedown\release\stage\`:

```
stage\
  davedown.exe
  assets\fonts\      Open Sans, Fira Code, Material Icons
  assets\docs\       welcome.md
  davedown_unit_tests.exe
  davedown_e2e.exe
```

`davedown.exe [file.md ...] [--mode=preview|source|edit]`

The CRT is linked statically (`/MT`) and every runtime path is resolved from
the executable's own directory, so the folder works from a shortcut, a USB
stick or another drive.

## Tests

```
tools\test.cmd release
```

runs both console binaries through CTest:

- `davedown_unit_tests` covers the application layer with no window: the
  block model (parse, serialize, split, merge, prefixes, renumbering), inline
  marker toggling, outline and statistics, settings round-trips, workspace
  document management.
- `davedown_e2e` drives the real interface headlessly with the Dear ImGui Test
  Engine: mode switching by toolbar and keyboard, typing in the source
  editor with undo/redo, WYSIWYG block editing (split, convert, merge),
  toolbar formatting buttons, the About and Preferences modals, theme and
  scale changes, find, open/edit/save round-trip, close-with-changes prompt,
  outline navigation.

## Layout

```
CMakeLists.txt
CMakePresets.json       release / debug presets, binaryDir under C:/buildfiles
src/main.cpp            SDL3 callbacks, renderer, frame loop
src/app/                state and logic, no ImGui: Document, MarkdownModel,
                        Outline, Settings, UndoStack, Workspace
src/ui/                 everything that draws: Theme, Fonts, Widgets,
                        MarkdownView (md4c -> ImGui), TextArea (word-wrapping
                        editor widget), SourceEditor, WysiwygEditor, MainWindow
src/platform/           OS access: Paths, FileDialog (SDL), Shell, Textures
assets/                 fonts, welcome document, Windows resource script
third_party/            md4c, Material Design icon codepoints
tests/                  unit_tests.cpp, e2e_main.cpp + e2e_tests.cpp
tools/                  build.cmd, test.cmd
```

## Notes and limitations

- The WYSIWYG model is line-based and simpler than a full CommonMark parser.
  It round-trips the markdown it produces itself; the first edit in Edit
  mode normalises unusual formatting (for example loose lists become tight,
  setext headings become `#` headings). Source and Preview never rewrite
  the file.
- Tables and raw HTML are edited as text blocks in Edit mode.
- Remote images are not fetched; local PNG, JPEG, BMP, GIF (first frame)
  and TGA are supported.
- Styles, fonts and icons come from fwcom's theme system; the About box
  lists the libraries.
