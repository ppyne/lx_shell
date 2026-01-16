# view

Display a PNG or JPEG image.

The image is scaled to fit the screen while preserving the aspect ratio.

## Usage

```
view <path>
```

## Notes

- Press any key to return to the shell.
- JPEG: baseline JPEG (no progressive/CMYK); very large images may fail due to memory.
- PNG: standard non-animated PNG; interlaced PNG may fail; very large images may fail due to memory.
