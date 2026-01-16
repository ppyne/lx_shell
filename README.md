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
Upstream project: https://github.com/ppyne/lx
To update:

```bash
cd lib/lx
git fetch --tags
git checkout v1.3.0
```

## License

BSD 3-Clause License. See [`LICENSE`](LICENSE).
