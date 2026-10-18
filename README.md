# AegisVoice

C11 encrypted voice transport with a host demo and FreeRTOS integration for Cortex-A7.

Two devices exchange audio through an untrusted smartphone relay. Each device captures, encrypts, decrypts and plays audio locally. Private identity keys stay on their respective devices; directional session keys exist only at the two endpoints.

The host implementation runs on Windows and Linux. The target is STM32MP157F-DK2, Cortex-A7 core 0. **A bootable board image is not included:** startup, peripheral drivers, Bluetooth/USB stacks and protected key storage still require a BSP.

## Build and run

Requires CMake 3.20+ and a C11 compiler. Host dependencies are included.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/aegisvoice_demo --self-test
./build/aegisvoice_demo speech.wav received.wav
```

Input WAV: mono PCM16, 16 kHz. The output file must not already exist.
Without an input file, `./build/aegisvoice_demo --output received.wav` generates test audio.

On Windows with Visual Studio 2022 C++ tools:

```powershell
.\scripts\build.ps1
.\build\Debug\aegisvoice_demo.exe --self-test
```

The demo runs both endpoints in one process, uses OS randomness and exercises bidirectional traffic, fragmentation, tampering, replay rejection and reconnects.

## Implementation

| Path | Function |
|---|---|
| `src/audio.c` | Independent IMA ADPCM frames: 320 samples / 20 ms to 164 bytes |
| `src/secure.c` | Authenticated X25519 exchange, Ed25519 identities, HKDF-SHA512, XChaCha20-Poly1305 |
| `src/device.c` | Audio and session integration |
| `src/transport.c` | Bounded ATT fragmentation and reassembly |
| `firmware/` | Static FreeRTOS tasks, HAL interface, ARMv7-A cache/DMA/GIC helpers |
| `tests/` | Codec vectors, protocol failures, fragmentation and deterministic task tests |

Firmware buffers and RTOS objects are statically allocated. Bluetooth uses custom GATT characteristics; USB exposes diagnostics. A 204-byte encrypted frame at 50 frames/s requires 81.6 kbit/s per direction before transport overhead.

Cryptographic primitives are provided by [Monocypher 4.0.2](third_party/monocypher/README.md). The application handshake is custom and has not undergone an independent security audit.

[Protocol and key handling](docs/protocol.md) · [Board integration](docs/porting.md)

Original code: [MIT](LICENSE). Third-party licences are retained in `third_party/`.
