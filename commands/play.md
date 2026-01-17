# play

Play a WAV or MP3 file from the SD card.

## Usage

```
play [-v 0-100] <path>
```

## Notes

- `-v` sets volume (0-100).
- Use `fn+up` / `fn+down` during playback to adjust volume.
- Press any key to stop playback.
- MP3 playback is streamed and may take a moment to start.
- WAV support: PCM (audiofmt=1), 8/16-bit, mono or stereo. Typical 8–48 kHz sample rates.
- MP3 support: MPEG-1/2/2.5 Layer III, mono/stereo, CBR/VBR.
- Playback uses ESP8266Audio with a small file buffer; SD cards with slow random reads may still stutter.
