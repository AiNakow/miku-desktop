# App Icon Source

Put the icon image here as:

- `assets/app-icon.png`

Build will auto-generate platform icons from this file:

- Windows: `.ico`
- macOS: `.icns` (or `.iconset` when `iconutil` is unavailable)
- Linux: multi-size `hicolor` PNG icons

If `assets/app-icon.png` is missing, build falls back to `drawable-hdpi/def01.png`.
