# Lx Shell for M5Stack Cardputer ADV

Minimal Unix‑like shell optimized for the **Cardputer ADV**.
Includes a terminal, a minimal vi (or nano) editor, core commands, and Lx script execution.

![M5Stack Cardputer ADV](m5stack_cardputer_adv.jpg)

## Features

- Interactive terminal with history, autocompletion, and pager (`more`/`less`)
- Minimal vi and nano editors (navigation, insertion, indentation, visual wrapping)
- Core commands: `battery`, `cat`, `cd`, `cp`, `find`, `ls`, `mount`, `mv`, `rm`, `umount`
- Run Lx scripts (`lx <script.lx>`)
- Output redirection (`>`, `>>`), pipes (`|`), and `tee`
- Screensaver + display sleep
- Keyboard with CP437 extended glyphs and optional debug

## Extended glyphs (Opt key)

Hold `Opt` and press a key to cycle through extra CP437 glyphs. Each press moves
to the next glyph, and releasing `Opt` keeps the current one. UTF-8 input/output
is supported, but only glyphs that map to CP437 are rendered.

**Opt key mapping**

| Key | Cycle sequence |
| --- | --- |
| `a` | àâæÆáäÄåÅªαa |
| `c` | çÇ¢c |
| `d` | δd |
| `e` | éÉèêëεe |
| `f` | ƒφΦf |
| `g` | Γg |
| `i` | îïìí∞i |
| `l` | £∟l |
| `m` | µm |
| `n` | ñÑⁿ∩n |
| `o` | ôöÖòóº°Ωo |
| `p` | ₧π¶p |
| `s` | ßσΣ§s |
| `t` | τΘt |
| `u` | ùûüÜú∩u |
| `v` | √♥♦♣♠v |
| `y` | ÿ¥y |
| `'` | «»↔' |
| `,` | ≤<◄←, |
| `.` | ≥>▼↓•◘∙·■. |
| `/` | ¿?►→/ |
| `;` | ▲↑; |
| `=` | ±≡≈÷= |
| `_` | ±+▬_ |
| `0` | °○◙☺︎☻☼♂♀0 |
| `1` | ¡‼½¼♪♫↕↨1 |
| `2` | ²2 |
| `8` | ÷∞8 |

**UTF-8 to CP437 rendered glyphs**

| Glyph | UTF-8 value | CP437 value |
| --- | --- | --- |
| à | U+00E0 | 0x85 |
| â | U+00E2 | 0x83 |
| æ | U+00E6 | 0x91 |
| Æ | U+00C6 | 0x92 |
| á | U+00E1 | 0xA0 |
| ä | U+00E4 | 0x84 |
| Ä | U+00C4 | 0x8E |
| å | U+00E5 | 0x86 |
| Å | U+00C5 | 0x8F |
| ª | U+00AA | 0xA6 |
| α | U+03B1 | 0xE0 |
| δ | U+03B4 | 0xEB |
| é | U+00E9 | 0x82 |
| É | U+00C9 | 0x90 |
| è | U+00E8 | 0x8A |
| ê | U+00EA | 0x88 |
| ë | U+00EB | 0x89 |
| ε | U+03B5 | 0xEE |
| ƒ | U+0192 | 0x9F |
| φ | U+03C6 | 0xED |
| Φ | U+03A6 | 0xE8 |
| Γ | U+0393 | 0xE2 |
| ÿ | U+00FF | 0x98 |
| ¥ | U+00A5 | 0x9D |
| ù | U+00F9 | 0x97 |
| û | U+00FB | 0x96 |
| ü | U+00FC | 0x81 |
| Ü | U+00DC | 0x9A |
| ú | U+00FA | 0xA3 |
| ∩ | U+2229 | 0xEF |
| î | U+00EE | 0x8C |
| ï | U+00EF | 0x8B |
| ì | U+00EC | 0x8D |
| í | U+00ED | 0xA1 |
| ∞ | U+221E | 0xEC |
| ô | U+00F4 | 0x93 |
| ö | U+00F6 | 0x94 |
| Ö | U+00D6 | 0x99 |
| ò | U+00F2 | 0x95 |
| ó | U+00F3 | 0xA2 |
| º | U+00BA | 0xA7 |
| ° | U+00B0 | 0xF8 |
| Ω | U+03A9 | 0xEA |
| ç | U+00E7 | 0x87 |
| Ç | U+00C7 | 0x80 |
| ¢ | U+00A2 | 0x9B |
| £ | U+00A3 | 0x9C |
| ∟ | U+221F | 0x1C |
| ₧ | U+20A7 | 0x9E |
| π | U+03C0 | 0xE3 |
| ¶ | U+00B6 | 0x14 |
| ß | U+00DF | 0xE1 |
| σ | U+03C3 | 0xE5 |
| Σ | U+03A3 | 0xE4 |
| § | U+00A7 | 0x15 |
| µ | U+00B5 | 0xE6 |
| ñ | U+00F1 | 0xA4 |
| Ñ | U+00D1 | 0xA5 |
| ⁿ | U+207F | 0xFC |
| τ | U+03C4 | 0xE7 |
| Θ | U+0398 | 0xE9 |
| √ | U+221A | 0xFB |
| ♥ | U+2665 | 0x03 |
| ♦ | U+2666 | 0x04 |
| ♣ | U+2663 | 0x05 |
| ♠ | U+2660 | 0x06 |
| ¿ | U+00BF | 0xA8 |
| ? | U+003F | 0x3F |
| ► | U+25BA | 0x10 |
| → | U+2192 | 0x1A |
| ¡ | U+00A1 | 0xAD |
| ‼ | U+203C | 0x13 |
| ½ | U+00BD | 0xAB |
| ¼ | U+00BC | 0xAC |
| ♪ | U+266A | 0x0D |
| ♫ | U+266B | 0x0E |
| ↕ | U+2195 | 0x12 |
| ↨ | U+21A8 | 0x17 |
| « | U+00AB | 0xAE |
| » | U+00BB | 0xAF |
| ↔ | U+2194 | 0x1D |
| ≤ | U+2264 | 0xF3 |
| < | U+003C | 0x3C |
| ◄ | U+25C4 | 0x11 |
| ← | U+2190 | 0x1B |
| ≥ | U+2265 | 0xF2 |
| > | U+003E | 0x3E |
| ▼ | U+25BC | 0x1F |
| ↓ | U+2193 | 0x19 |
| • | U+2022 | 0x07 |
| ◘ | U+25D8 | 0x08 |
| ∙ | U+2219 | 0xF9 |
| · | U+00B7 | 0xFA |
| ■ | U+25A0 | 0xFE |
| ± | U+00B1 | 0xF1 |
| ≡ | U+2261 | 0xF0 |
| ≈ | U+2248 | 0xF7 |
| ÷ | U+00F7 | 0xF6 |
| + | U+002B | 0x2B |
| ▬ | U+25AC | 0x16 |
| ▲ | U+25B2 | 0x1E |
| ↑ | U+2191 | 0x18 |
| ○ | U+25CB | 0x09 |
| ◙ | U+25D9 | 0x0A |
| ☺︎ | U+263A | 0x01 |
| ☻ | U+263B | 0x02 |
| ☼ | U+263C | 0x0F |
| ♂ | U+2642 | 0x0B |
| ♀ | U+2640 | 0x0C |
| ² | U+00B2 | 0xFD |

## Commands

- [battery](commands/battery.md) - battery status
- [cat](commands/cat.md) - print file contents
- [cd](commands/cd.md) - change directory
- [clear](commands/clear.md) - clear terminal
- [cp](commands/cp.md) - copy file
- [find](commands/find.md) - search files
- [less](commands/less.md) - alias for `more`
- [ls](commands/ls.md) - list directory contents
- [lx](commands/lx.md) - run a Lx script
- [lxprofile](commands/lxprofile.md) - set Lx memory profile
- [man](commands/man.md) - command help
- [mkdir](commands/mkdir.md) - create directory
- [more](commands/more.md) - pager for files and output
- [mount](commands/mount.md) - mount SD card
- [mv](commands/mv.md) - move/rename file
- [nano](commands/nano.md) - minimal editor (nano-style)
- [pwd](commands/pwd.md) - print working directory
- [reset](commands/reset.md) - alias for `clear`
- [rm](commands/rm.md) - remove file
- [rmdir](commands/rmdir.md) - remove directory
- [shutdown](commands/shutdown.md) - halt or restart
- [tee](commands/tee.md) - write piped output to a file
- [touch](commands/touch.md) - create/update file
- [umount](commands/umount.md) - unmount SD card
- [vi](commands/vi.md) - minimal editor

## Target hardware

- M5Stack **Cardputer ADV** (ESP32‑S3)
- micro‑SD card for filesystem (`/media/0`)

## Build and flash (PlatformIO)

This project is built with [PlatformIO](https://platformio.org/).

1. Initialize submodules:

```bash
git submodule update --init --recursive
```

2. Build and upload:

```bash
pio run
pio run -t upload
```

3. Open serial monitor:

```bash
pio device monitor
```

## Quick usage

```sh
cd /media/0
ls
lx my_script.lx
lx scripts/lx_stress_test_progressive.lx > output.txt
```

## Shell behavior

- History lives in `/sdcard/.lx_history` on the SD card, loaded on boot and appended
  after each executed line. If the file is missing, history stays in memory only.
- Autocomplete uses `Tab` on the current token. The first token searches `/bin` and
  the current directory, while path tokens list entries from that path. If multiple
  matches exist, they are printed space-separated and the input line is restored.
- `lxprofile` is stored in RAM only; the default is `balanced` and it resets on reboot.

## Lx profiles

The Lx runtime supports memory profiles:

```sh
lxprofile
lxprofile safe|balanced|power
lx --profile power /media/0/my_script.lx
```

Profiles reserve a minimum amount of free heap to avoid hard OOM crashes.

- `safe`  : highest reserve, most conservative
- `balanced`: default reserve, good for most scripts
- `power`: lowest reserve, maximum capacity (higher OOM risk)

## Lx project (submodule)

The Lx engine is integrated as a **submodule** in `lib/lx`.

Upstream project: [github.com/ppyne/lx](https://github.com/ppyne/lx)

To update:

```bash
cd lib/lx
git fetch --tags
git checkout v1.3.0
```

## License

BSD 3-Clause License. See [`LICENSE`](LICENSE).
