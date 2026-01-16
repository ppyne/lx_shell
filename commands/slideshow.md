# slideshow

Browse images in a folder with optional timed autoplay.

## Usage

```sh
slideshow [-t seconds] <path>
```

## Controls

- `fn` + right: next image
- `fn` + left: previous image
- `r`: rotate 90 degrees (cycles 0/90/180/270). Disabled when `-t` is used.
- any other key: exit slideshow

## Notes

- Supported formats: PNG, JPG, JPEG.
- Images are scaled to fit the screen while preserving aspect ratio.
- The timer starts after each image finishes rendering.
- The `-t` value is clamped to 2..120 seconds.
- If a key is pressed while an image is loading, it is applied immediately after the render completes.
