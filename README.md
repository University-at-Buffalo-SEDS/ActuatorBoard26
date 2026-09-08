# ActuatorBoard26 firmware

This firmware targets the STM32G491 and builds as a SEDS LaunchCore factory
image. CMake fetches the stable SEDSNet v4.0.20 and LaunchCore v1.0.0 releases during
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
  `0x08004000` (vector table at `0x08004200`).
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

Build an OTA package using the automatically retained previous packaged image
as the delta baseline:

```sh
python3 build.py build --release --ota
```

The resulting `ActuationBoard.seds` contains a delta when a suitable baseline
exists and the delta fits. If this is the first packaged build, or a delta is
not viable, the package uses LaunchCore full-image recovery instead. Use
`--ota-base /path/to/installed.launchcore.img` only to override the automatic
baseline.

The application listens on SEDSNet's P2P stream port `4510`. Messages are
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

The STM32G491 flash limit requires SEDSNet's optional cryptography provider to
remain disabled for this build. Payload compression is enabled: payloads at
least 30 bytes long are compressed when doing so reduces their wire size, and
the encoded packets remain compatible with other SEDSNet v4 endpoints.

## Tests

```sh
python3 build.py test
python3 build.py test --all --release
```

Host tests exercise CAN fragmentation/reassembly and overflow behavior,
inter-thread queue saturation and shared state, output-driver safety behavior,
schema/ID consistency, dependency pins, and the LaunchCore memory map. The
cross-compiled firmware build additionally enforces application and bootloader
flash/RAM limits at link time. `--all` also builds the factory and OTA images,
runs the containerized board and memory simulations, and exercises linked
SEDSNet discovery, synchronization, and command/acknowledgement traffic.
