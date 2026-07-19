#ifndef AGINTI_C12880_USB_H
#define AGINTI_C12880_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool c12880_usb_init(void);
bool c12880_usb_busy(void);
int c12880_usb_transmit(const uint8_t *data, size_t bytes);
size_t c12880_usb_read(uint8_t *destination, size_t capacity);
uint32_t c12880_usb_rx_overruns(void);
void app_usb_tx_complete_isr(void);

#endif

