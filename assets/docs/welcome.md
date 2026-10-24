# Welcome to davedown

davedown is a small, fast markdown viewer and editor. This document opens as an unsaved scratch file, so feel free to type over it.

## Three ways to look at a document

Use the switch in the toolbar, or `Ctrl+1`, `Ctrl+2`, `Ctrl+3`:

1. **Preview** renders the markdown. Links are clickable, task boxes toggle, and code blocks have a copy button.
2. **Source** shows the raw markdown with syntax colouring, line numbers and word wrap.
3. **Edit** is the WYSIWYG mode. Click any block to edit it in place; it renders again when you move on.

## Editing tips

- In Edit mode, press `Enter` to start a new block and `Backspace` at the start of a block to join it with the one above.
- Type a prefix such as `## `, `- `, `1. `, `> ` or ```` ``` ```` at the start of a paragraph to convert it.
- `Tab` and `Shift+Tab` indent and outdent list items.
- `Ctrl+B`, `Ctrl+I`, `Ctrl+E` and `Ctrl+K` apply **bold**, *italic*, `code` and [links](https://commonmark.org).
- `Ctrl+Z` and `Ctrl+Y` undo and redo in every mode.

## Things that render

> Block quotes, with *emphasis* and ~~strikethrough~~ inside them.

```cpp
// Fenced code keeps its indentation and gets a copy button.
int main() {
    return 0;
}
```

| Feature | Preview | Source | Edit |
|:--------|:-------:|:------:|:----:|
| Read    | yes     | yes    | yes  |
| Write   | no      | yes    | yes  |
| Wrap    | yes     | yes    | yes  |

- [x] Task lists
- [ ] Click a box in Preview to toggle it

Pictures referenced relative to the file, like `![diagram](images/diagram.png)`, are shown when the file exists.

---

Themes, interface scale and the default mode live under **View** and in **Preferences**. Everything is saved beside the executable so the folder stays portable.
