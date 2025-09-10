# openRTSP buffer sizing: defaults, environment variables, and autosizing

This document describes how openRTSP in this repository sizes its recording buffers, how you can override them with environment variables or CLI flags, and how the built‑in resolution‑based autosizing works.

Applies to:
- Composite recording outputs:
  - MP4: `-4` (QuickTimeFileSink)
  - MKV: `-x` (MatroskaFileSink)
- Per-stream sinks when not using composite outputs (e.g., raw H.264 elementary stream sinks)

## Why this change?
Older live555 defaults (20–100 KB) were designed for SD-era video. Modern 1080p–4K IDR frames can be hundreds of KB to several MB. Too-small buffers cause truncation like:

```
MultiFramedRTPSource::doGetNextFrame1(): The total received frame size exceeds the client's buffer size (100000).  XXXXX bytes of trailing data will be dropped!
QuickTimeFileSink::afterGettingFrame(): The input frame data was too large for our buffer.  XXXXX bytes of trailing data was dropped!
```

We’ve raised the defaults and added flexible overrides so capture works reliably on embedded targets and NFS-backed storage.

## Effective buffer size precedence
From highest to lowest priority:
1) Command-line flags
   - `-b <bytes>`: File sink buffer (used by MP4/MKV composite sinks and other sinks)
   - `-B <bytes>`: Socket receive buffer (RTP input)
2) Environment variables (read at runtime if CLI not used)
   - `OPENRTSP_FILE_SINK_BUFFER`
   - `OPENRTSP_SOCKET_BUFFER`
3) Autosizing by resolution (if neither CLI nor env is set)
   - Uses SDP’s `a=x-dimensions: <w>,<h>` when available; otherwise falls back to `-w/-h`
   - Heuristic: `recommended = 512 KiB + 0.25 bytes per pixel`, clamped to `[1 MiB, 32 MiB]`
4) Built-in defaults (if none of the above applied)
   - File sink: `2,000,000` bytes
   - Socket receive: `2,097,152` bytes (2 MiB)

Additionally, if the socket buffer size is not explicitly set (via CLI or env), it will be raised to at least the chosen file sink buffer size to avoid a too-small socket becoming the bottleneck.

## Quick examples

- Simple MP4 capture with autosizing and defaults (good on embedded targets):
```
openRTSP -t -4 -Q "rtsp://user:pass@<ip>/ch0" > sample.mp4
```

- Set buffers via environment once (e.g., in camera shell init):
```
export OPENRTSP_FILE_SINK_BUFFER=2000000
export OPENRTSP_SOCKET_BUFFER=2097152
openRTSP -t -4 -Q "rtsp://user:pass@<ip>/ch0" > /mnt/nfs/cap.mp4
```

- Explicit CLI overrides (take precedence over env & autosizing):
```
openRTSP -t -4 -Q -b 4000000 -B 4194304 "rtsp://user:pass@<ip>/ch0" > sample.mp4
```

- MKV instead of MP4:
```
openRTSP -t -x -Q "rtsp://user:pass@<ip>/ch0" > sample.mkv
```

## Heuristic details
- We size primarily for worst-case IDR frames.
- Formula: `512 KiB + 0.25 bytes/pixel` provides a reasonable safety margin for common GOP structures.
- Clamping to [1 MiB, 32 MiB] avoids unreasonably small or huge allocations.
- For 1080p (1920×1080) this yields ~2.0–2.1 MiB; for 4K (3840×2160) ~2.6–2.7 MiB.

## Guidance for embedded/NFS setups
- Prefer RTP-over-TCP (`-t`) to avoid UDP loss on busy systems and when writing to NFS.
- The autosized file sink buffer is usually sufficient for 1080p and 4K. If you still see truncation messages, increase with `-b` or env var.
- Ensure the socket buffer is at least as large as the file sink buffer (the code auto-raises it if you didn’t specify `-B`).

## Where this lives in code
- Variable declarations and default values:
  - `testProgs/playCommon.cpp`: `fileSinkBufferSize`, `socketInputBufferSize`
- CLI parsing that marks an explicit override:
  - `-b`, `-B` cases in `playCommon.cpp`
- Environment overrides and autosizing, applied after SDP parsing and before receiver setup:
  - In `continueAfterDESCRIBE()` just before creating RTP sources
- The composite sinks constructed with these sizes:
  - MP4: `QuickTimeFileSink::createNew(..., fileSinkBufferSize, ...)`
  - MKV: `MatroskaFileSink::createNew(..., fileSinkBufferSize, ...)`

## Troubleshooting
- If you still see truncation:
  - Confirm `fileSinkBufferSize` (print logs or temporarily instrument)
  - Increase buffers via env or `-b/-B`
  - Use `-t` (RTP/TCP) and consider larger `-B` for high bitrate or multiple streams
- On very constrained devices, avoid setting buffers excessively high for multiple concurrent recordings; start with autosizing and adjust.

