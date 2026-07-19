#include "usbd_conf.h"
#include "usbd_core.h"
#include "c12880_board.h"

PCD_HandleTypeDef hpcd_USB_OTG_HS;
static uint32_t usb_class_pool[640U];

void *USBD_static_malloc(uint32_t size) {
  return (size <= sizeof(usb_class_pool)) ? usb_class_pool : NULL;
}
void USBD_static_free(void *pointer) { (void)pointer; }

void HAL_PCD_MspInit(PCD_HandleTypeDef *pcd) {
  if (pcd->Instance != USB_OTG_HS) return;
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = GPIO_PIN_14 | GPIO_PIN_15;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF12_OTG2_FS;
  HAL_GPIO_Init(GPIOB, &gpio);
  __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();
  HAL_PWREx_EnableUSBVoltageDetector();
  HAL_NVIC_SetPriority(OTG_HS_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *pcd) {
  if (pcd->Instance != USB_OTG_HS) return;
  __HAL_RCC_USB1_OTG_HS_CLK_DISABLE();
  HAL_GPIO_DeInit(GPIOB, GPIO_PIN_14 | GPIO_PIN_15);
  HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
}

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *device) {
  hpcd_USB_OTG_HS.Instance = USB_OTG_HS;
  hpcd_USB_OTG_HS.Init.dev_endpoints = 9U;
  hpcd_USB_OTG_HS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_HS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.phy_itface = USB_OTG_EMBEDDED_PHY;
  hpcd_USB_OTG_HS.Init.Sof_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.use_dedicated_ep1 = DISABLE;
  hpcd_USB_OTG_HS.Init.use_external_vbus = DISABLE;
  hpcd_USB_OTG_HS.pData = device;
  device->pData = &hpcd_USB_OTG_HS;
  if (HAL_PCD_Init(&hpcd_USB_OTG_HS) != HAL_OK) return USBD_FAIL;
  (void)HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_HS, 0x200U);
  (void)HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 0U, 0x40U);
  (void)HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 1U, 0x100U);
  (void)HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 2U, 0x40U);
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *device) {
  return (HAL_PCD_DeInit((PCD_HandleTypeDef *)device->pData) == HAL_OK)
             ? USBD_OK : USBD_FAIL;
}
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *device) {
  return (HAL_PCD_Start((PCD_HandleTypeDef *)device->pData) == HAL_OK)
             ? USBD_OK : USBD_FAIL;
}
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *device) {
  return (HAL_PCD_Stop((PCD_HandleTypeDef *)device->pData) == HAL_OK)
             ? USBD_OK : USBD_FAIL;
}
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *device, uint8_t address,
                                  uint8_t type, uint16_t max_packet) {
  HAL_PCD_EP_Open((PCD_HandleTypeDef *)device->pData, address, max_packet, type);
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *device, uint8_t address) {
  HAL_PCD_EP_Close((PCD_HandleTypeDef *)device->pData, address); return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *device, uint8_t address) {
  HAL_PCD_EP_Flush((PCD_HandleTypeDef *)device->pData, address); return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *device, uint8_t address) {
  HAL_PCD_EP_SetStall((PCD_HandleTypeDef *)device->pData, address); return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *device, uint8_t address) {
  HAL_PCD_EP_ClrStall((PCD_HandleTypeDef *)device->pData, address); return USBD_OK;
}
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *device, uint8_t address) {
  PCD_HandleTypeDef *pcd = (PCD_HandleTypeDef *)device->pData;
  return ((address & 0x80U) != 0U) ? pcd->IN_ep[address & 0x7FU].is_stall
                                   : pcd->OUT_ep[address & 0x7FU].is_stall;
}
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *device, uint8_t address) {
  HAL_PCD_SetAddress((PCD_HandleTypeDef *)device->pData, address); return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *device, uint8_t address,
                                    uint8_t *buffer, uint32_t bytes) {
  HAL_PCD_EP_Transmit((PCD_HandleTypeDef *)device->pData, address, buffer, bytes);
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *device, uint8_t address,
                                          uint8_t *buffer, uint32_t bytes) {
  HAL_PCD_EP_Receive((PCD_HandleTypeDef *)device->pData, address, buffer, bytes);
  return USBD_OK;
}
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *device, uint8_t address) {
  return HAL_PCD_EP_GetRxCount((PCD_HandleTypeDef *)device->pData, address);
}
void USBD_LL_Delay(uint32_t delay) { HAL_Delay(delay); }

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *pcd) {
  USBD_LL_SetupStage((USBD_HandleTypeDef *)pcd->pData,
                     (uint8_t *)(void *)pcd->Setup);
}
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *pcd, uint8_t ep) {
  USBD_LL_DataOutStage((USBD_HandleTypeDef *)pcd->pData, ep, pcd->OUT_ep[ep].xfer_buff);
}
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *pcd, uint8_t ep) {
  USBD_LL_DataInStage((USBD_HandleTypeDef *)pcd->pData, ep, pcd->IN_ep[ep].xfer_buff);
}
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *pcd) {
  USBD_LL_SOF((USBD_HandleTypeDef *)pcd->pData);
}
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *pcd) {
  USBD_SpeedTypeDef speed = USBD_SPEED_FULL;
  USBD_LL_SetSpeed((USBD_HandleTypeDef *)pcd->pData, speed);
  USBD_LL_Reset((USBD_HandleTypeDef *)pcd->pData);
}
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *pcd) {
  USBD_LL_Suspend((USBD_HandleTypeDef *)pcd->pData);
}
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *pcd) {
  USBD_LL_Resume((USBD_HandleTypeDef *)pcd->pData);
}
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *pcd, uint8_t ep) {
  USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef *)pcd->pData, ep);
}
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *pcd, uint8_t ep) {
  USBD_LL_IsoINIncomplete((USBD_HandleTypeDef *)pcd->pData, ep);
}
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *pcd) {
  USBD_LL_DevConnected((USBD_HandleTypeDef *)pcd->pData);
}
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *pcd) {
  USBD_LL_DevDisconnected((USBD_HandleTypeDef *)pcd->pData);
}
