#include "c12880_board.h"

void NMI_Handler(void) { for (;;) {} }
void HardFault_Handler(void) { board_panic(0x48415244U); }
void MemManage_Handler(void) { board_panic(0x4D505531U); }
void BusFault_Handler(void) { board_panic(0x42555331U); }
void UsageFault_Handler(void) { board_panic(0x55534147U); }
void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}
void SysTick_Handler(void) { HAL_IncTick(); }
void DMA1_Stream0_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_adc1); }
void EXTI9_5_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(C12880_TRIGGER_PIN); }
void OTG_HS_IRQHandler(void) { HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS); }

