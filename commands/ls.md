# ls

List directory contents.

## Usage

```
ls [options] [path]
```

## Options

- `-a` include hidden entries
- `-l` long format
- `-t` sort by time
- `-r` reverse sort

Flags can be combined (e.g. `-la`, `-ltr`).

## Output format (long)

Example `ls -la`:

```
drw-s-  512 2024-01-01 /media/0
-rwh-- 1024 2024-01-01 hello.txt
```

Fields:

- first char: `d` directory, `-` file
- `r` / `w` : readable / writable (FAT32 read‑only clears `w`)
- `h`       : hidden by name (starts with `.`)
- `H`       : hidden by FAT attribute
- `s`       : system flag (FAT32; always set for virtual entries)
- `a`       : archive flag (FAT32)
- size      : file size (bytes have no suffix, otherwise K/M/G)
- date      : file date
- name      : entry name

## Examples

```
ls
ls -a /media/0
ls -la /media/0
```
