# speaker: ESPHome 2026.9.1's component, with two fixes

A copy of `esphome/components/speaker/` from ESPHome **2026.9.1** (the version `build_firmware.sh` pins), loaded
through `external_components` in `firmware/athan.yaml`, so it replaces ESPHome's own. Everything is upstream except
the blocks marked `ATHAN PATCH`. `firmware/tests/syntax_check.sh` compares this folder with the installed ESPHome
and fails on any other difference.

## ATHAN PATCH 1: the speaker follows the stream's format (`media_player/audio_pipeline.cpp`)

A speaker (here the resamplers) takes a stream's sample rate and channel count when it starts and keeps converting
from them while it runs. Upstream hands the format over once, at the first parsed MP3 header. Audio then plays at
the wrong speed and pitch (double speed when stereo is read as mono) when:

- **microMP3 locks onto a false header.** Its probe takes the first four bytes that look like a frame header
  (microMP3 0.4.0, `run_probe()`), and a live radio stream is joined in the middle of a frame. At the first real
  frame it reports `MP3_STREAM_INFO_CHANGED` with the true format. Upstream PR
  [esphome#19028](https://github.com/esphome/esphome/pull/19028) stopped treating that as fatal, and says that
  passing the new format to the speaker "warrants a separate change". As of 2026.10-dev it is not made.
- **The station really changes format,** for example moving to a recording encoded at another rate or in mono.
- **A stream starts while the speaker still runs** from the one before (also a restart the player makes itself).

The patch hands the format only to a stopped speaker (stopping it first, waiting at most 1 s), and hands it over
again whenever the decoder's format changes. The decoder's next write restarts the speaker with it.

## ATHAN PATCH 2: a failed link is dropped (`media_player/speaker_media_player.cpp/.h`)

Upstream reopens a media link that failed (no network, DNS, a dead station) at once, for ever and many times a
second (each try a DNS lookup or a connection, plus a screenful of log), while the player reports PLAYING. The
patch drops the failed item: the player goes idle, and the athan component retries the radio itself, paced
(`radio_tick_()`).

## Upgrading ESPHome

Copy `esphome/components/speaker/` from the new version over this folder, apply the marked blocks again, and
change the version here and in `build_firmware.sh` together. Check upstream first: if ESPHome now re-sends the
stream format to the speaker and stops retrying failed links, delete this folder and take `speaker` out of
`external_components`.
