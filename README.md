# M5Stack CoreS3 projects

Projects for the [M5Stack CoreS3](https://docs.m5stack.com/en/core/CoreS3): ESP32-S3 at 240 MHz, 16 MB flash, 8 MB PSRAM,
a 320 × 240 touch screen, camera, IMU, microphones and speaker, on native USB.

| Project | What it does |
|---|---|
| [claude-cube](claude-cube/) | Shows your current Claude Code session on the cube: tokens, model, context window, what Claude is doing, cost. Shake to flip pages; it chimes when Claude needs you. |
| [smoke-test](smoke-test/) | Checks that every part answers and measures memory bandwidth: 27.7 MB/s from PSRAM, which caps a 3.3M INT8 LLM at 8.4 tok/s. |
| [drama-queen](drama-queen/) | A cube that overreacts to everything: screams when tossed, sobs face down, faints on her back, calls her lawyer when slapped. 18 reactions with faces, speech bubbles and a gibberish voice. |
| [balance-ball](balance-ball/) | Tilt the board to keep a ball in the middle of the screen. It worries as a wall gets close and turns red and angry when it hits one; tap the screen to set level. No sound. |
| [sandbox](sandbox/) | A falling-sand box: draw sand, water and walls with your finger, and gravity follows the way you hold the board. Shake to stir. No sound. |
| [paint-dry](paint-dry/) | A boring app, on purpose: a freshly painted wall that dries in real time over three hours. Touch it while wet and your fingerprint stays. No sound. |

## Working with the board

- It shows up on macOS as `/dev/cu.usbmodem*` with no driver; use the `cu.*` device, not `tty.*`.
- Flash with esptool v5 (`uvx esptool ...`). PlatformIO's bundled esptool 4.5.1 drops this board's native USB after
  its stub starts.
- Back up the full flash before flashing anything: `uvx esptool --port /dev/cu.usbmodem<N> read-flash 0 ALL backup.bin`.
- One board, one firmware at a time. Every project's `flash.sh` handles the claude-cube bridge: claude-cube's lifts
  `~/.claude-cube/paused` after flashing; every other project's leaves it in place so the bridge stays off the port.
