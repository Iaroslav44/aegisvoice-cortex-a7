#ifndef AV_GATT_H
#define AV_GATT_H
/* Project-assigned 128-bit UUIDs, not Bluetooth SIG adopted services.
 * Direction is from the device's perspective. BSP must register this profile.
 * Header only: no Bluetooth host-stack implementation is implied. */
#define AV_GATT_SERVICE    "7b8e0001-9a14-4d72-b51b-67b15331d314"
#define AV_GATT_CONTROL_RX "7b8e0002-9a14-4d72-b51b-67b15331d314"
#define AV_GATT_CONTROL_TX "7b8e0003-9a14-4d72-b51b-67b15331d314"
#define AV_GATT_AUDIO_RX   "7b8e0004-9a14-4d72-b51b-67b15331d314"
#define AV_GATT_AUDIO_TX   "7b8e0005-9a14-4d72-b51b-67b15331d314"
#define AV_GATT_IDENTITY   "7b8e0006-9a14-4d72-b51b-67b15331d314"
#endif
