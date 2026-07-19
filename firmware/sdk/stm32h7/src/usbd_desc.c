#include "usbd_desc.h"
#include "usbd_ctlreq.h"
#include "stm32h7xx_hal.h"

#define USB_SIZ_STRING_SERIAL 26U

static uint8_t *device_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *lang_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *manufacturer_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *product_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *serial_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *config_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *interface_descriptor(USBD_SpeedTypeDef speed, uint16_t *length);

USBD_DescriptorsTypeDef AGINTI_Desc = {
    device_descriptor, lang_descriptor, manufacturer_descriptor,
    product_descriptor, serial_descriptor, config_descriptor,
    interface_descriptor};

__ALIGN_BEGIN static uint8_t device_desc[USB_LEN_DEV_DESC] __ALIGN_END = {
    0x12U, USB_DESC_TYPE_DEVICE, 0x00U, 0x02U, 0x02U, 0x02U, 0x00U,
    USB_MAX_EP0_SIZE, LOBYTE(USBD_VID), HIBYTE(USBD_VID),
    LOBYTE(USBD_PID), HIBYTE(USBD_PID), 0x00U, 0x02U,
    USBD_IDX_MFC_STR, USBD_IDX_PRODUCT_STR, USBD_IDX_SERIAL_STR, 0x01U};
__ALIGN_BEGIN static uint8_t lang_desc[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {
    USB_LEN_LANGID_STR_DESC, USB_DESC_TYPE_STRING,
    LOBYTE(USBD_LANGID_STRING), HIBYTE(USBD_LANGID_STRING)};
__ALIGN_BEGIN static uint8_t string_desc[128] __ALIGN_END;
__ALIGN_BEGIN static uint8_t serial_desc[USB_SIZ_STRING_SERIAL] __ALIGN_END = {
    USB_SIZ_STRING_SERIAL, USB_DESC_TYPE_STRING};

static void hex_string(uint32_t value, uint8_t *destination, uint8_t digits) {
  for (uint8_t i = 0; i < digits; ++i) {
    const uint8_t nibble = (uint8_t)((value >> 28) & 0xFU);
    destination[2U * i] = (nibble < 10U)
                               ? (uint8_t)((uint8_t)'0' + nibble)
                               : (uint8_t)((uint8_t)'A' + nibble - 10U);
    destination[(2U * i) + 1U] = 0U;
    value <<= 4;
  }
}

static uint8_t *device_descriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed; *length = sizeof(device_desc); return device_desc;
}
static uint8_t *lang_descriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed; *length = sizeof(lang_desc); return lang_desc;
}
static uint8_t *manufacturer_descriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed; USBD_GetString((uint8_t *)"AgInTi", string_desc, length); return string_desc;
}
static uint8_t *product_descriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed; USBD_GetString((uint8_t *)"AgInTi C12880MA Spectrometer", string_desc, length); return string_desc;
}
static uint8_t *config_descriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed; USBD_GetString((uint8_t *)"CDC Config", string_desc, length); return string_desc;
}
static uint8_t *interface_descriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed; USBD_GetString((uint8_t *)"C12880 Stream", string_desc, length); return string_desc;
}
static uint8_t *serial_descriptor(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  uint32_t serial0 = HAL_GetUIDw0() + HAL_GetUIDw2();
  const uint32_t serial1 = HAL_GetUIDw1();
  if (serial0 != 0U) {
    hex_string(serial0, &serial_desc[2], 8U);
    hex_string(serial1, &serial_desc[18], 4U);
  }
  *length = sizeof(serial_desc); return serial_desc;
}
