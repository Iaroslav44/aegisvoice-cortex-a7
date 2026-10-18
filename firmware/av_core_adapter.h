#ifndef AV_CORE_ADAPTER_H
#define AV_CORE_ADAPTER_H
#include "av_firmware.h"
#include "av/device.h"

typedef struct {
    av_device *device;
    uint32_t last_sample;
    bool have_sample;
    bool reset_ok;
} av_core_adapter;

/* Device/identity/RNG context must remain alive throughout firmware execution.
 * Only the media task may use device after creating firmware tasks. */
bool av_core_adapter_init(av_core_adapter *adapter, av_device *device,
                          av_firmware_app_ops *ops);
#endif
