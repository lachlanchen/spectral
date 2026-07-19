#include "c12880_usb.h"

#include "usbd_cdc.h"
#include "usbd_core.h"
#include "usbd_desc.h"

#include <string.h>

#define USB_RX_RING_BYTES 1024U
#define USB_PACKET_BYTES 64U

USBD_HandleTypeDef hUsbDeviceHS;
static uint8_t usb_rx_packet[USB_PACKET_BYTES];
static uint8_t usb_tx_placeholder[USB_PACKET_BYTES];
static volatile uint8_t usb_rx_ring[USB_RX_RING_BYTES];
static volatile uint16_t usb_rx_head;
static volatile uint16_t usb_rx_tail;
static volatile uint32_t usb_rx_overrun_count;
static volatile bool usb_tx_active;
static USBD_CDC_LineCodingTypeDef line_coding = {1500000U, 0U, 0U, 8U};

static int8_t CDC_Init_FS(void) {
  USBD_CDC_SetTxBuffer(&hUsbDeviceHS, usb_tx_placeholder, 0U);
  USBD_CDC_SetRxBuffer(&hUsbDeviceHS, usb_rx_packet);
  return (int8_t)USBD_OK;
}

static int8_t CDC_DeInit_FS(void) {
  usb_tx_active = false;
  return (int8_t)USBD_OK;
}

static int8_t CDC_Control_FS(uint8_t command, uint8_t *buffer,
                             uint16_t length) {
  (void)length;
  if (command == CDC_SET_LINE_CODING) {
    line_coding.bitrate = (uint32_t)buffer[0] |
                          ((uint32_t)buffer[1] << 8) |
                          ((uint32_t)buffer[2] << 16) |
                          ((uint32_t)buffer[3] << 24);
    line_coding.format = buffer[4];
    line_coding.paritytype = buffer[5];
    line_coding.datatype = buffer[6];
  } else if (command == CDC_GET_LINE_CODING) {
    buffer[0] = (uint8_t)line_coding.bitrate;
    buffer[1] = (uint8_t)(line_coding.bitrate >> 8);
    buffer[2] = (uint8_t)(line_coding.bitrate >> 16);
    buffer[3] = (uint8_t)(line_coding.bitrate >> 24);
    buffer[4] = line_coding.format;
    buffer[5] = line_coding.paritytype;
    buffer[6] = line_coding.datatype;
  }
  return (int8_t)USBD_OK;
}

static int8_t CDC_Receive_FS(uint8_t *buffer, uint32_t *length) {
  for (uint32_t i = 0; i < *length; ++i) {
    const uint16_t next = (uint16_t)((usb_rx_head + 1U) &
                                     (USB_RX_RING_BYTES - 1U));
    if (next == usb_rx_tail) {
      ++usb_rx_overrun_count;
      break;
    }
    usb_rx_ring[usb_rx_head] = buffer[i];
    usb_rx_head = next;
  }
  USBD_CDC_SetRxBuffer(&hUsbDeviceHS, usb_rx_packet);
  (void)USBD_CDC_ReceivePacket(&hUsbDeviceHS);
  return (int8_t)USBD_OK;
}

static int8_t CDC_TransmitCplt_FS(uint8_t *buffer, uint32_t *length,
                                  uint8_t endpoint) {
  (void)buffer; (void)length; (void)endpoint;
  usb_tx_active = false;
  app_usb_tx_complete_isr();
  return (int8_t)USBD_OK;
}

static USBD_CDC_ItfTypeDef usb_interface = {
    CDC_Init_FS, CDC_DeInit_FS, CDC_Control_FS, CDC_Receive_FS,
    CDC_TransmitCplt_FS};

bool c12880_usb_init(void) {
  if (USBD_Init(&hUsbDeviceHS, &AGINTI_Desc, DEVICE_HS) != USBD_OK) return false;
  if (USBD_RegisterClass(&hUsbDeviceHS, &USBD_CDC) != USBD_OK) return false;
  if (USBD_CDC_RegisterInterface(&hUsbDeviceHS, &usb_interface) != USBD_OK)
    return false;
  return USBD_Start(&hUsbDeviceHS) == USBD_OK;
}

bool c12880_usb_busy(void) {
  return usb_tx_active || (hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED);
}

int c12880_usb_transmit(const uint8_t *data, size_t bytes) {
  if ((data == NULL) || (bytes == 0U) || (bytes > UINT32_MAX)) return -1;
  if (c12880_usb_busy()) return 1;
  usb_tx_active = true;
  USBD_CDC_SetTxBuffer(&hUsbDeviceHS, (uint8_t *)(uintptr_t)data,
                       (uint32_t)bytes);
  if (USBD_CDC_TransmitPacket(&hUsbDeviceHS) != USBD_OK) {
    usb_tx_active = false;
    return 1;
  }
  return 0;
}

size_t c12880_usb_read(uint8_t *destination, size_t capacity) {
  if (destination == NULL) return 0U;
  size_t count = 0U;
  while ((count < capacity) && (usb_rx_tail != usb_rx_head)) {
    destination[count++] = usb_rx_ring[usb_rx_tail];
    usb_rx_tail = (uint16_t)((usb_rx_tail + 1U) & (USB_RX_RING_BYTES - 1U));
  }
  return count;
}

uint32_t c12880_usb_rx_overruns(void) { return usb_rx_overrun_count; }

