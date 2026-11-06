# M5Stack CoreS3 projects

Projects for the [M5Stack CoreS3](https://docs.m5stack.com/en/core/CoreS3): ESP32-S3 at 240 MHz, 16 MB flash, 8 MB PSRAM,
a 320 × 240 touch screen, camera, IMU, microphones and speaker, on native USB.

| Project | What it does |
|---|---|
| [claude-cube](claude-cube/) | Shows your current Claude Code session on the cube: tokens, model, context window, what Claude is doing, cost. Shake to flip pages; it chimes when Claude needs you. |

## Working with the board

- It shows up on macOS as `/dev/cu.usbmodem*` with no driver; use the `cu.*` device, not `tty.*`.
- Flash with esptool v5 (`uvx esptool ...`). PlatformIO's bundled esptool 4.5.1 drops this board's native USB after
  its stub starts.
- Back up the full flash before flashing anything: `uvx esptool --port /dev/cu.usbmodem<N> read-flash 0 ALL backup.bin`.
