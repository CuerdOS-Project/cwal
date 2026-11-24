# cwal

cwal is a Pywal clone in C. It reads an image, runs k-means
color quantization to extract a 16-color palette, applies dark/light
adjustment, pushes the result to every open terminal via OSC escape sequences,
and writes a `colors.json` plus 36 templated config files to the cache dir.

## What's new in 0.1 release
- **Template engine**: all 36 pywal templates are vendored under `templates/`
  and applied on every run. Output files land in `~/.cache/wal/` with the
  same names pywal produces (`colors.sh`, `colors.css`, `colors.Xresources`,
  `colors-kitty.conf`, `colors.hs`, `colors.yml`, etc.), so existing dotfiles
  keep working.
- **User templates**: anything in `$XDG_CONFIG_HOME/wal/templates/` is
  processed in the same pass as the built-ins.
- **Placeholder syntax**: `{color0}`, `{color0.strip}`, `{color0.rgb}`,
  `{color0.xrgba}`, `{color0.rgba}`, `{color0.alpha}`, `{color0.alpha_dec}`,
  `{color0.decimal}`, `{color0.octal}`, `{color0.red}`, `{color0.lighten(20)}`,
  `{color0.darken(50)}`, `{color0.saturate(60)}` — same semantics as pywal's
  `util.Color`. Literal braces are escaped with `{{` / `}}`.
- **`--skip-export`**: skip the template pass (only `colors.json` and the
  `sequences` blob are written).
- **`make install`** now installs both the binary and the templates to
  `$PREFIX/share/cwal/templates/`. `TEMPLATE_DIR=/path` overrides the path at
  build time.
- **Runtime template lookup**: `$CWAL_TEMPLATES_DIR` env var → `./templates/`
  in the cwd → compiled-in install path.

## Build
Dependencies are only a C11 toolchain and `make`:
```bash
make
make test
```
Install the binary **and** the templates:
```bash
sudo make install            # /usr/local/bin/cwal + /usr/local/share/cwal/templates
sudo make install-bin        # binary only (templates already shipped elsewhere)
```
To install under another prefix: `make install PREFIX=/usr TEMPLATE_DIR=/usr/share/cwal/templates`.

## Usage
```
cwal [OPTIONS] -i IMAGE
  -i IMG          image to derive palette from (JPEG/PNG/BMP/GIF/PSD)
  -l              generate a light colorscheme
  -b BG           override the background color (e.g. -b '#1e1e2e' or -b 1e1e2e)
  -a ALPHA        terminal background transparency, URxvt-style (e.g. -a 90)
  --backend NAME  accepted for compatibility (always 'kmeans' in this build)
  --saturate AMT  extra saturation in [0.0, 1.0]
  --vte           skip OSC 708 (work around VTE escape quirks)
  -s, --skip-term don't push colors to terminals
  --skip-export   don't run the template engine
  -q, --quiet     suppress informational output
  -n              no-op (wallpaper is never set, kept for pywal compat)
  -c, --clean-cache  delete all cached colorschemes
  -R, --restore   restore previous colorscheme from colors.json
  --preview       print the palette of the last colorscheme
  -v, --version   print version and exit
  -h, --help      show this help
```
Tip: `cwal -i wallpaper.jpg -q` is the typical invocation; `cwal -R` re-applies
the last palette to a freshly opened terminal.

## Configuration
cwal is configured entirely via environment variables and the XDG dirs:
| var | default | meaning |
| --- | --- | --- |
| `XDG_CACHE_HOME` | `~/.cache` | where `wal/` (output) lives |
| `XDG_CONFIG_HOME` | `~/.config` | where `wal/templates/` (user templates) lives |
| `PYWAL_CACHE_DIR` | — | overrides the cache dir entirely (pywal compat) |
| `CWAL_TEMPLATES_DIR` | — | overrides the built-in templates dir at runtime |

Build-time knobs (passed to `make`):
| var | default | meaning |
| --- | --- | --- |
| `PREFIX` | `/usr/local` | install prefix for the binary |
| `TEMPLATE_DIR` | `$PREFIX/share/cwal/templates` | install path for templates |
| `CC` | `cc` | C compiler |
| `CFLAGS` | `-std=c11 -O2 -Wall -Wextra -Wpedantic …` | extra compiler flags |

## Template engine
A template is any text file with placeholders in Python `str.format` syntax,
where each color is a pywal `Color` object:
| placeholder | output (example) |
| --- | --- |
| `{color0}` | `#1e1e2e` |
| `{color0.strip}` | `1e1e2e` |
| `{color0.rgb}` | `30,30,46` |
| `{color0.xrgba}` | `1e/1e/2e/ff` |
| `{color0.rgba}` | `rgba(30,30,46,1.0)` |
| `{color0.alpha}` | `[100]#1e1e2e` |
| `{color0.alpha_dec}` | `1.0` |
| `{color0.decimal}` | `#3159486` |
| `{color0.octal}` | `#3617056` |
| `{color0.red}` | `0.118` |
| `{color0.lighten(20)}` | `#4a4a62` (lightened 20%) |
| `{color0.darken(50)}` | `#0f0f17` |
| `{color0.saturate(60)}` | `#272731` |

Keys: `wallpaper`, `alpha`, `background`, `foreground`, `cursor`,
`color0`...`color15`. Drop a file in `~/.config/wal/templates/` to add your
own — it's processed on the next run and written to `~/.cache/wal/` with the
same filename.

To use the generated files in your dotfiles:
```bash
. ~/.cache/wal/colors.sh                       # shell
xrdb -merge ~/.cache/wal/colors.Xresources     # Xresources
include ~/.cache/wal/colors-kitty.conf         # kitty
```

## How the palette is built
Each run:
1. Load the image with the vendored `stb_image.h` (JPEG/PNG/BMP/GIF/PSD).
2. Subsample to at most 10000 pixels by striding.
3. Run k-means with k-means++ initialization (k=16, 15 Lloyd iterations).
4. Sort centroids darkest-first by YIQ luminance.
5. Apply pywal's `backends/wal.py:adjust()` — darken the background, blend
   `color7`/`color15` toward `#EEEEEE`, derive `color8` from `color7`.
6. Optional `--saturate` pass (skips `color0`, `color7`, `color8`, `color15`).
7. Build the palette dict, write `colors.json`, build the OSC sequence blob,
   push it to every `/dev/pts/[0-9]*` (Linux/BSD) or `/dev/ttys00*` (macOS),
   and run the template engine.

The `colors.json` cache filename matches pywal's scheme:
`{sanitized_path}_{dark|light}_{backend}_{sat}_{filesize}_{cachever}.json`.
A second run on the same image + flags loads the cache instead of regenerating.

## Files written
| path | what |
| --- | --- |
| `$XDG_CACHE_HOME/wal/colors.json` | the palette (pywal format) |
| `$XDG_CACHE_HOME/wal/sequences` | raw OSC escape blob, `cat`able into a new terminal |
| `$XDG_CACHE_HOME/wal/schemes/*.json` | per-image cache files |
| `$XDG_CACHE_HOME/wal/colors.{sh,css,Xresources,…}` | 6 templated config files |

## What's intentionally absent
- Built-in theme library (250+ themes in pywal) — bring your own `colors.json`.
- Wallpaper setter (`feh`, `nitrogen`, `swww`…) — call it yourself.
- Reload of GTK/i3/sway/xrdb/polybar — `wal -e` territory.
- `-o script` post-hooks.

If you need any of those, run pywal itself; cwal is the core
"image → palette → terminals + templates" loop.

## License
MIT, see [LICENSE](LICENSE). The vendored `vendor/stb_image.h` is public
domain (see its header). pywal is MIT.
