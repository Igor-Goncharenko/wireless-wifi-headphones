#!/bin/bash
ffmpeg -f pulse -i default -c:a pcm_s16le -ar 44100 -ac 1 -f wav udp://192.168.0.232:1234
