# Linux Audio Looper 

A real-time audio looper implemented in C using the ALSA API on Linux.

## Features
- Real-time audio recording
- Multi-track overdubbing
- Clear / Quit / Next track commands

## Build

```bash
gcc linux_audio_looper.c -o looper -lasound
```

## Run

```bash
./looper default
```
