#!/usr/bin/env sh
set -eu

cd "$(dirname "$0")/.."

if ! command -v gst-launch-1.0 >/dev/null 2>&1; then
    echo "gst-launch-1.0 is required to prepare the PSP soundtrack" >&2
    exit 1
fi

gst-launch-1.0 -q \
    filesrc location=assets/soundtrack.mp3 ! \
    mpegaudioparse ! mpg123audiodec ! audioconvert ! audioresample ! \
    'audio/x-raw,format=S16LE,rate=44100,channels=2' ! \
    lamemp3enc target=bitrate bitrate=128 cbr=true \
        encoding-engine-quality=2 ! \
    filesink location=assets/soundtrack_psp.mp3

echo "Wrote assets/soundtrack_psp.mp3 (44.1 kHz stereo, 128 kbps)"
