# BSP parent supplies the ARM toolchain, FreeRTOS port and board configuration.

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|ARM|armv7|cortex-a7)")
    message(FATAL_ERROR "Firmware needs an ARMv7-A cross toolchain and real BSP")
endif()
if(NOT TARGET freertos_kernel)
    message(FATAL_ERROR "Supply FreeRTOS-Kernel with GCC_ARM_CA9 port first")
endif()
if(NOT FREERTOS_PORT STREQUAL "GCC_ARM_CA9")
    message(FATAL_ERROR "This integration requires FREERTOS_PORT=GCC_ARM_CA9")
endif()
if(NOT TARGET av_firmware_adapter)
    message(FATAL_ERROR "Add AegisVoice core first with AV_BUILD_HOST=OFF and AV_BUILD_TESTS=OFF")
endif()

add_library(aegisvoice_firmware STATIC
    av_firmware.c platform/armv7a.c platform/gicv2.c platform/dma_buffer.c)
target_include_directories(aegisvoice_firmware PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(aegisvoice_firmware PUBLIC freertos_kernel av_firmware_adapter)
target_compile_features(aegisvoice_firmware PUBLIC c_std_11)
target_compile_options(aegisvoice_firmware PRIVATE
    -ffreestanding -fno-common -Wall -Wextra -Werror)
# Let the final ELF discard unused code, including unused Monocypher primitives.
foreach(av_target aegisvoice_firmware av_firmware_adapter aegisvoice monocypher)
    target_compile_options(${av_target} PRIVATE -ffunction-sections -fdata-sections)
endforeach()
target_link_options(aegisvoice_firmware INTERFACE -Wl,--gc-sections)
# Per-function reports exclude callees and interrupt stacks.
option(AV_FIRMWARE_STACK_USAGE "Emit compiler .su stack reports" ON)
if(AV_FIRMWARE_STACK_USAGE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(aegisvoice_firmware PRIVATE -fstack-usage)
endif()
