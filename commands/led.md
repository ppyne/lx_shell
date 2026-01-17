# led

Control the Cardputer ADV RGB LED.

## Synopsis

```
led -c #RRGGBB [-i 0-255]
led -R <0-255> -G <0-255> -B <0-255> [-i 0-255]
led -b <ms> (-c #RRGGBB | -R <n> -G <n> -B <n>) [-i 0-255]
led -m <ms> [-i 0-255]
```

## Options

- `-c #RRGGBB` set color using hex RGB
- `-R` / `-G` / `-B` set color components
- `-b <ms>` blink period in milliseconds
- `-m <ms>` cycle hue over a period in milliseconds
- `-i <n>` intensity / brightness (0-255)

## Notes

- `-m` cannot be combined with `-b` or explicit color options.
- Press any key to return to the shell (steady, blink, or hue cycle).
- While `led` runs, the display backlight is forced to 255 to keep the LED stable, then restored on exit.

## Examples

```
led -c #ff8800
led -R 0 -G 128 -B 255 -i 200
led -b 500 -c #00ff00
led -m 2000 -i 80
```
