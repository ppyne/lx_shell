# brightness

Set the display backlight level.

## Synopsis

```
brightness <7-255>
```

## Notes

- Values are clamped to 7..255 (0 is not allowed).
- When an SD card is mounted, the value is saved to `/media/0/.lxshellrc`.
