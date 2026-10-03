# wayhud

Universal, modern suckless Wayland on-screen HUD (Heads-Up Display).

Built for Wayland compositors supporting `wlr-layer-shell-unstable-v1` (River, Sway, Hyprland, Niri).

## Philosophy

- **KISS & Pure C**: Minimalist architecture, < 50KB binary, ~8MB memory footprint, < 2ms startup.
- **Modern Suckless**: Direct `wayland-client` + `wlr-layer-shell` (Version 4) + Cairo/Pango rendering.
- **Pipeline Native (POSIX `isatty`)**:
  - `... | wayhud`: Automatically senses stdin pipe and acts as an on-screen text/status HUD.
  - `wayhud | ...`: Automatically streams captured keystroke combos to stdout.
  - `wayhud`: Standalone on-screen key HUD / keycaster reading hardware `/dev/input/`.
- **100% Intangible**: Fully click-through (empty input region) and zero focus stealing (`keyboard_mode = none`).
- **Independent Margins**: Supports independent bottom and left positioning (`-m 195,4` to sit perfectly above webcam PIP).
- **Precise Width Truncation**: Enforces max pixel width (`-w 185`) so it never extends past custom borders or sidebars.

## Installation

```bash
make
sudo make install
```

If your user is not yet in the `input` group, you can install with the setuid bit:
```bash
sudo make install-suid
```

## Usage

```bash
# 1. Standalone Keycaster (default: bottom-left, width 185px, margin 195,4)
wayhud

# 2. Universal Text HUD / OSD via Unix pipe
echo "Volume: 80%" | wayhud
echo "Build Completed!" | wayhud -a top-right -m 20,20 -c "#9ece6a"

# 3. Stream captured keys to log or pipeline
wayhud | grep "Super"
```

## Options

```text
  -a, --anchor <pos>       Anchor position (bottom-left, bottom, top-right, etc.) [default: bottom-left]
  -m, --margin <b[,l]>     Margin in pixels: bottom,left or uniform margin [default: 195,4]
  -w, --width <px>         Max width in pixels (truncates overflow) [default: 185]
  -t, --timeout <ms>       Fade timeout in milliseconds [default: 1200]
  -f, --font <font>        Font description [default: JetBrainsMono Nerd Font 16]
  -c, --color <hex>        Text color in hex (#ffffff) [default: #ffffff]
  -b, --bg <hex>           Background color (#00000000 for pure transparent) [default: #00000000]
  -h, --help               Show help message and exit
  -v, --version            Show version information
```

## License

MIT License.
