# Cortex-A7 integration

The reference target is [STM32MP157F-DK2](https://www.st.com/en/evaluation-tools/stm32mp157f-dk2.html), using A7 core 0 with the other A7/M4 cores parked. Host tests and ARM object compilation do not establish hardware operation.

## BSP requirements

Provide startup/DDR/clocks/pinmux, exception stacks, linker script, MMU mappings, GIC and tick timer; audio codec and DMA drivers; Bluetooth controller/host/GATT and USB CDC stacks; entropy, protected identity storage and watchdog handling. The board's Bluetooth 4.1 requires measured duplex throughput; do not assume ATT MTU 247 or data length extension. Check audio routing and external microphone/output hardware against [UM2637](https://www.st.com/resource/en/user_manual/um2637-discovery-kits-with-increasedfrequency-800-mhz-stm32mp157-mpus-stmicroelectronics.pdf).

Integration targets single-core FreeRTOS-Kernel V11.1.0 with `portable/GCC/ARM_CA9`. Validate that port's GIC, security-mode and priority assumptions on the selected SoC. Supply `av_board_config.h`:

| Macro | BSP value |
|---|---|
| AV_BOARD_GIC_BASE | GIC distributor address |
| AV_BOARD_GICC_OFFSET | CPU interface offset from distributor |
| AV_BOARD_GIC_PRIORITY_COUNT | Implemented priority count |
| AV_BOARD_MAX_API_IRQ_PRIORITY | Unshifted RTOS API priority threshold |

Use consistent `-mcpu=cortex-a7 -marm -mfpu=neon-vfpv4 -mfloat-abi=softfp` flags across C, assembly, core, kernel and drivers. The external BSP CMake project enables C and ASM, then includes:

```cmake
set(AV_BUILD_HOST OFF CACHE BOOL "" FORCE)
set(AV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${AEGISVOICE_SOURCE_DIR} aegis-core)
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM INTERFACE
    ${AEGISVOICE_SOURCE_DIR}/firmware ${YOUR_BSP_INCLUDE_DIR})
set(FREERTOS_PORT GCC_ARM_CA9 CACHE STRING "" FORCE)
add_subdirectory(${FREERTOS_KERNEL_DIR} freertos-kernel)
add_subdirectory(${AEGISVOICE_SOURCE_DIR}/firmware aegis-fw)
# Link the BSP executable with aegisvoice_firmware.
```

Section garbage collection is enabled. The linker script must declare the entry point and `KEEP()` vector/registration sections without C references. Check the final map. Compiler `.su` files describe individual frames, not complete task/IRQ stack peaks.

Route IRQ through the official port wrapper. With FPU/NEON in interrupt code, use `vApplicationFPUSafeIRQHandler`. The wrapper acknowledges/ends GIC interrupts; the C dispatcher must not duplicate IAR/EOIR access. The timer ISR calls `FreeRTOS_Tick_Handler()`; `av_board_clear_tick()` clears its source. RTOS-calling IRQ priorities must satisfy the port's API threshold and binary-point rules.

## Runtime and HAL

Create the device and `av_core_adapter`, then call `av_firmware_create()` before enabling producer IRQs or starting the scheduler. Identity, callbacks and their contexts must survive for the boot lifetime. Only the media task accesses the session.

| Task | Priority | Bound per pass |
|---|---:|---|
| Supervisor | 6 | Health check every 50 ms |
| Media | 5 | 2 RX packets, 1 capture; 20 ms playout deadline |
| Bluetooth | 4 | 2 RX and 2 TX packets; 50 ms heartbeat wait |
| USB | 1 | 32 input bytes; 50 ms heartbeat wait |

Stacks and queues are static. Initial stacks are media 4096 words, Bluetooth/USB 1024 each, supervisor 512, idle 256. Validate high-water marks, library allocation and worst-case execution time on hardware.

Callbacks in [av_firmware.h](../firmware/av_firmware.h) must be bounded and nonblocking. `send()` accepts/copies a whole packet or rejects it. `receive()` returns zero length only when empty. USB short reads are allowed; writes copy bytes before return.

Publish Bluetooth RX/link state before calling `av_firmware_bluetooth_notify()` or its ISR variant; USB uses the corresponding USB functions. Capture ISR notification is automatic. Notification slot 0 is reserved. Notifications coalesce; pending queues are drained in bounded passes with a one-tick yield on backlog. The RTOS tick remains 1 kHz.

A connection epoch changes on every connect, disconnect and route change, including changes between polls. Do not wrap/reuse it during a boot; zero denotes disconnected startup. Reset session/playout on epoch changes. `send(epoch,...)` and `playback_copy(epoch,...)` must atomically check the actual epoch. On hardware disconnect, immediately cancel TX/playback, mute and erase PCM.

Handshake timeout is an absolute five seconds. Handshake/encryption failures block the session until the next epoch. Captures completed before authentication or older than 20 ms are discarded. Three decoded frames may wait for playout; a fourth slot allows decoding without damaging queued frames on rejection. Late playout drops stale frames and retains the deadline phase. There is no adaptive clock recovery, packet-loss concealment or acoustic echo cancellation.

The supervisor requires worker heartbeats within 250 ms and capture completions within 100 ms after a 250 ms startup grace period. Capture DMA must therefore run continuously, including before authentication. `fatal()` stops/mutes DMA, clears secrets and resets without returning. It must also work from the stack-overflow hook on a known-good stack without RTOS calls, locks, allocation or formatted output.

USB commands are `help`, `status` and `identity` (public key only). USB backpressure must not block the runtime.

## DMA and cache ownership

Place `av_dma_audio_block` in DMA-accessible RAM with full 64-byte cache-line alignment; keep ownership metadata separate. Maintain stable MMU mappings. Helpers read cache-line size from CTR and reject partial/unaligned ranges. Add outer-cache maintenance if required by the memory system.

RX ownership: CPU -> clean/invalidate -> DMA -> confirmed completion -> invalidate -> CPU.
TX ownership: CPU -> clean -> DMA -> confirmed completion -> CPU.
Serialize leases against their ISR; the BSP supplies descriptor coherency and physical addresses. Memory barriers alone do not make noncoherent DMA safe.

After peripheral completion, submit the buffer ID with `av_firmware_capture_from_isr()`. On queue rejection, the BSP releases it. Queued buffers remain CPU-owned: cyclic DMA must not overwrite them. `capture_claim()` returns synchronized, stable samples; `capture_release()` erases them before DMA reuse. Playback copies samples before returning; it cannot retain a caller's pointer.

GIC helpers configure disabled SPI only. The BSP serializes register updates, clears peripheral conditions and validates pending/active state. Distributor/CPU initialization, security groups, timer PPI and boot routing remain BSP responsibilities.

## Checks

```sh
sh scripts/check-arm.sh
```

This compiles ARM objects using declaration-only support headers. CTest's `firmware_runtime` executes production task loops with deterministic fake queues, time and HAL; it does not model a preemptive RTOS scheduler.

Before deployment, measure stack/RAM use, execution time, IRQ latency, DMA coherence, duplex radio throughput and audio latency on hardware. Exercise reconnects, malformed traffic, USB backpressure, entropy failure and watchdog recovery. Confirm that radio/USB captures contain no plaintext audio or private keys.
