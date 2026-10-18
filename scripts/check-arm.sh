#!/usr/bin/env sh
set -eu

# Object-only check using libc/RTOS declarations; no linked firmware image.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output_dir="$repo_dir/build-arm-objects"
compiler=${ARM_CLANG:-clang}
mkdir -p "$output_dir"

for source in src/audio.c src/transport.c src/secure.c src/device.c \
    firmware/av_core_adapter.c firmware/av_firmware.c \
    firmware/platform/armv7a.c firmware/platform/gicv2.c firmware/platform/dma_buffer.c \
    third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c; do
    name=$(basename "$source" .c)
    "$compiler" --target=armv7a-none-eabi -mcpu=cortex-a7 -marm \
        -mfpu=neon-vfpv4 -mfloat-abi=softfp -ffreestanding -std=c11 \
        -O2 -fstack-usage -Wall -Wextra -Werror -Wpedantic \
        -ffile-prefix-map="$repo_dir"=. \
        -I"$repo_dir/firmware" -I"$repo_dir/include" \
        -I"$repo_dir/firmware/check" -I"$repo_dir/scripts/arm-declarations" \
        -I"$repo_dir/third_party/monocypher" \
        -c "$repo_dir/$source" -o "$output_dir/$name.o"
done
printf '%s\n' "Cortex-A7 objects and compiler stack reports: $output_dir"
