# lx

Run a Lx script.

## Usage

```
lx <path>
lx --profile <safe|balanced|power> <path>
lx -p <safe|balanced|power> <path>
```

## Profiles

Profiles reserve a minimum amount of free heap to reduce hard OOM crashes.

- `safe`  : highest reserve, most conservative
- `balanced`: default reserve, good for most scripts
- `power`: lowest reserve, maximum capacity (higher OOM risk)

The selected profile is kept in RAM only and resets to `balanced` on reboot.
Using `lx --profile` changes the profile for that run only.
