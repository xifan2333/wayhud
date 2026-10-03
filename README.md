# wayhud

Universal, modern suckless Wayland on-screen HUD (Heads-Up Display) styled with pure **GTK CSS**.

Built for Wayland compositors supporting `wlr-layer-shell-unstable-v1` (River, Sway, Hyprland, Niri).

## Philosophy

- **KISS & Pure C**: Minimalist architecture, < 60KB binary, ~8MB memory footprint, < 2ms startup.
- **Pure GTK CSS**: Styled directly through standard GTK CSS (`~/.config/wayhud/style.css`), no ugly custom flags.
- **Smart Key Multiplier**: Automatically collapses rapid repetitive keystrokes into clean multipliers (e.g. `Backspace × 4`).
- **Pipeline Native (POSIX `isatty`)**:
  - `... | wayhud`: Automatically senses stdin pipe and acts as an on-screen text/status HUD.
  - `wayhud | ...`: Automatically streams captured keys to stdout.
  - `wayhud`: Standalone on-screen key HUD / keycaster reading hardware `/dev/input/`.
- **100% Intangible**: Fully click-through (empty input region) and zero focus stealing (`keyboard_mode = none`).

## Installation

```bash
make
sudo make install
```

This installs the binary and the `wayhud(1)` man page into standard system directories.

### Privilege Management (No `input` group required)

To read hardware keystrokes without adding your user account to the `input` group, `wayhud` implements **secure early privilege dropping**: it opens hardware event descriptors at startup, then permanently drops all elevated privileges back to the invoking user before connecting to Wayland or parsing user stylesheets:

```bash
# Option A: SUID root with automatic privilege dropping (Recommended)
sudo make install-suid

# Option B: Linux File Capabilities (read-only without SUID)
sudo make install-caps
```

## Usage

```bash
# 1. Standalone Keycaster with default ~/.config/wayhud/style.css
wayhud

# 2. Universal Text HUD / OSD via Unix pipe
echo "Volume: 80%" | wayhud
echo "Build Completed!" | wayhud -s "window { margin-top: 20px; margin-right: 20px; } label { color: #9ece6a; }"

# 3. Stream captured keys to log or pipeline
wayhud | grep "Super"
```

## GTK CSS Configuration

`wayhud` locates style sheets strictly following the **XDG Base Directory Specification**:
1. `$XDG_CONFIG_HOME/wayhud/style.css` (defaults to `~/.config/wayhud/style.css`);
2. Traverses system `$XDG_CONFIG_DIRS/wayhud/style.css` (defaults to `/etc/xdg/wayhud/style.css`);
3. Seamlessly dereferences **symbolic links** for GNU Stow, chezmoi, and dotfile managers.

```css
window {
    /* Positioning via standard GTK CSS margins */
    margin-bottom: 195px;
    margin-left: 4px;

    /* Box Model */
    max-width: 220px;
    padding: 6px 12px;

    /* Card visual styling */
    background-color: rgba(24, 24, 37, 0.85);
    border-radius: 8px;
    border: 1px solid rgba(255, 255, 255, 0.1);

    /* Timeout / fade duration */
    transition-duration: 1.2s;
}

label {
    /* Typography */
    font-family: "JetBrainsMono Nerd Font";
    font-size: 16px;
    color: #cdd6f4;

    /* Text shadow / outline */
    text-shadow: 0 0 2px rgba(0, 0, 0, 0.85);
}
```

## Options

```text
  -s, --style <file|css>   GTK CSS stylesheet file path or inline CSS string
                           [default: $XDG_CONFIG_HOME/wayhud/style.css]
  -n, --name <name>        Instance name matching window#<name> and label#<name>
                           [default: keys]
  -h, --help               Show this help message and exit
  -v, --version            Show version information
```

See `man 1 wayhud` for comprehensive documentation on all supported GTK CSS properties and lifecycle details.

## Development

Tools are managed with [mise](https://mise.jdx.dev/) and git hooks/linting with [hk](https://hk.jdx.dev/):

```bash
# Run linting (clang-format, cppcheck, clang-tidy)
make lint
# or: hk check --all

# Auto-fix formatting
make format
# or: hk fix --all
```

## License

MIT License.
