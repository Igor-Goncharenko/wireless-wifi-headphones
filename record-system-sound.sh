#!/bin/bash
parec --device=alsa_output.pci-0000_03_00.6.analog-stereo.monitor --format=s16le --channels=2 --rate=44100 | lame -r - output.mp3
# to play you can use `mplayer output.mp3`
