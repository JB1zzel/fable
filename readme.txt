# Fable is a tiny text editor and Spellchecker

A small CMD text editor for Windows, built on a `tui.h` console abstraction. Single binary, no installer, no dependencies beyond `dict.smd`.

## What it does

- Full-screen text editing in an 80x25 CMD window
- Mark / cut / paste with an internal clipboard
- Find, find & replace
- Paragraph reflow to a chosen width (default 80, max 120), word-aware, skips indented blocks
- In-process interactive spellchecker with suggestions
- Spell-mark toggle: highlight misspellings in red on demand
- Help screen (`F1`)

![Fable editor](screenshots/block select.png)

![Fable editor](screenshots/spell checker.png)

![Fable editor](screenshots/spelling highlighted.png)

## Files

| File | Purpose |
|---|---|
| `tiny.c` | The editor |
| `tinyspell.c` / `.h` | Spell library — dictionary, lookup, suggestions, interactive UI |
| `tui.h` | Console abstraction (`tui_*` functions) |
| `tui_console.c` | Windows CMD backend |
| `dict.smd` | The dictionary, ~88,873 words, ~497 KB |

## Building

```powershell
tcc tiny.c tui_console.c tinyspell.c -luser32 -o tiny.exe
```

One binary. No `spell.exe`, no temp files, no `system()` calls (except `cls` at startup).

## Running

```
tiny
tiny myfile.txt
```

If `dict.smd` isn't found at startup, you're prompted for its path. If you skip, Tiny still works — spell features just report "Dictionary not loaded."

## The keys

| Key | Action |
|---|---|
| Arrows | Move cursor |
| Shift+Left/Right | Word left / right |
| Home / End | Line start / end |
| PgUp / PgDn | Page up / down |
| Enter | Split line |
| Backspace | Delete char / merge up at col 0 |
| Delete | Delete char / merge down at end |
| F1 | Help screen |
| F2 | Reflow paragraph (prompts for width, default 80, Esc cancels) |
| F3 / Ctrl+S | Save |
| F4 | Interactive spellcheck |
| F5 | Toggle spell marks (red misspellings) |
| Ctrl+O | Open file |
| Ctrl+F | Find |
| Ctrl+R | Find & replace all |
| Ctrl+B | Mark block; end mark copies to clipboard |
| Ctrl+X | Cut marked block |
| Ctrl+P | Paste clipboard |
| Ctrl+N | New file |
| Esc | Cancel mark / prompt, else quit (prompts if modified) |

## Spellchecking

**F4** runs the interactive spellchecker in-process. It flattens the buffer into a text string, walks each word, and for every unknown word shows the sentence with the word in `[brackets]`, plus up to 95 suggestions in pages of 5. You can pick a guess, ignore the word, type your own replacement (checked against the dictionary), or press `Esc` to quit and save. The buffer is updated in place — **no file is written to disk** until you press F3 / Ctrl+S.

**F5** toggles red spell marks. It takes a snapshot of the document's misspellings and highlights them. **Any edit clears the marks** (the snapshot is stale) — press F5 again for a fresh snapshot.

## Why it's light

The dictionary holds about 88,000 English words, but it is not stored as a list. It is **front-coded**: words are sorted, and each word stores only the part that differs from the one before it. `AARDVARK` is followed by `AARDVARKS`, which stores just `S` plus a note saying "reuse the first 8 letters." On average a word costs about 1.6 bytes instead of 7, so 88,000 words fit in under 500 KB instead of several MB.

A small restart table stores every 8th word in full, so lookup can jump into the middle of the dictionary and read forward at most 7 words. That's what makes lookups fast despite the compression.

## Limits

- Only A–Z letters are checked. Numbers and punctuation are left alone.
- Names, places and technical terms are not in the dictionary — ignore them or type your own replacement (option 8 in the spell menu).
- The five guesses are suggestions, not guarantees.
- `MAXLINES 4096` / `MAXLINE 1024` — a document can be at most 4,096 lines, each up to 1,023 characters. Longer lines are truncated on load; extra lines beyond 4,096 are dropped.
- `dict.smd` must be found at startup or spelled out when prompted.
