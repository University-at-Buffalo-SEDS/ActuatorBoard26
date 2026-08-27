# ActuatorBoard26 firmware

This firmware targets the STM32G491 and builds as a SEDS LaunchCore factory
image. CMake fetches pinned upstream revisions of both dependencies during
configure; no Git submodules are required.

LaunchCore generates both GNU linker scripts directly from
`Bootloader/board_config.h`. Named linker regions distinguish bootloader and
firmware usage, and the final memory-report target prints their combined flash
usage.

## Build

```sh
python3 build.py build --release
```

The build produces:

- `ActuationBoardBootloader.bin`: the 16 KiB LaunchCore bootloader region.
- `ActuationBoard.launchcore.img`: the packaged application for Slot A at
  `0x08004000` (vector table at `0x08004100`).
- `ActuationBoard.factory.bin`: bootloader, confirmed application, and initial
  metadata in one image for a blank board.

Flash the complete factory image by default:

```sh
python3 build.py flash --release --method stm32prog-cli
```

Use `--app-only` only when LaunchCore and valid metadata are already installed.

## OTA updates

This capacity-constrained board deliberately has no live full-image staging
slot. Normal OTA uses a reversible LaunchCore delta in the 24 KiB region at
`0x08078000`; complete images are accepted only by bootloader recovery.

Generate a delta against the packaged image currently installed on the board:

```sh
cmake -S . -B build/Release_Script \
  -DCMAKE_BUILD_TYPE=Release \
  -DLAUNCHCORE_DELTA_BASE_IMAGE=/path/to/installed.launchcore.img
cmake --build build/Release_Script --target delta-image
```

The application listens on SEDSNet v4.0.2 P2P stream port `4510`. Messages are
little-endian: begin is `01 <patch-size:u32>`, each chunk is
`02 <offset:u32> <up-to-120-bytes>`, finish is `03`, abort is `04`, status is
`05`, and enter-recovery is `06`. Every response is 13 bytes containing
`opcode|0x80`, status, next expected offset, and the maximum patch size. Chunks
must be 8-byte aligned except for the last chunk. A successful finish reboots
and lets LaunchCore validate and apply the reversible delta; the application
confirms a healthy boot after five seconds.

For a full-image recovery, first send the enter-recovery command (or otherwise
force recovery), connect a 3.3 V serial adapter to UART4 PC10/PC11, and run:

```sh
python3 tools/launchcore_recover.py /dev/ttyUSB0 \
  build/Release_Script/ActuationBoard.launchcore.img
```

The UART transport uses 115200 8N1, CRC-protected frames, strictly sequential
offsets, and 256-byte bounded chunks. Recovery validates the complete packaged
image before marking it pending and rebooting.

## Network configuration

The board-owned SEDSNet v4 runtime schema is
[`config/sedsnet.json`](config/sedsnet.json). CMake copies it into the fetched
embedded crate before Cargo builds. Stable C IDs live in
[`Core/Inc/sedsnet_config.h`](Core/Inc/sedsnet_config.h).

The STM32G491 flash limit requires SEDSNet's optional compression and
cryptography features to remain disabled for this build. CAN-FD packets remain
wire-compatible as uncompressed packets.

## Tests

```sh
python3 build.py test
```

Host tests exercise CAN fragmentation/reassembly and overflow behavior,
inter-thread queue saturation and shared state, output-driver safety behavior,
schema/ID consistency, dependency pins, and the LaunchCore memory map. The
cross-compiled firmware build additionally enforces application and bootloader
flash/RAM limits at link time.
