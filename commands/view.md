# view

Display a PNG or JPEG image.

The image is scaled to fit the screen while preserving the aspect ratio.

## Usage

```
view <path>
```

## Notes

- Press `r` to rotate 90 degrees (cycles 0/90/180/270).
- Press any other key to return to the shell.
- JPEG: baseline JPEG (no progressive/CMYK); very large images may fail due to memory.
- PNG: standard non-animated PNG; interlaced PNG may fail; very large images may fail due to memory.
