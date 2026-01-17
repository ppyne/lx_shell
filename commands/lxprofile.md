# lxprofile

Get or set the default Lx execution profile.

## Usage

```
lxprofile
lxprofile safe|balanced|power
```

## Profiles

Profiles reserve a minimum amount of free heap to reduce hard OOM crashes.

- `safe`  : highest reserve, most conservative
- `balanced`: default reserve, good for most scripts
- `power`: lowest reserve, maximum capacity (higher OOM risk)

When an SD card is mounted, the value is saved to `/media/0/.lxscriptrc`.
