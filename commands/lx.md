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

The default profile is loaded from `/media/0/.lxscriptrc` when available.
It defaults to `power` when no profile is configured.
Using `lx --profile` changes the profile for that run only.
