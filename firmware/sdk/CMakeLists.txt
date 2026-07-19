cmake_minimum_required(VERSION 3.19)
project(aginti_c12880_firmware VERSION 0.2.0 LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(AGINTI_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../private/official-sources" CACHE PATH
    "Directory containing pinned ST/CMSIS source trees")
set(STM32_HAL_ROOT "${AGINTI_DEPS_DIR}/stm32h7xx-hal-driver" CACHE PATH "STM32H7 HAL root")
set(STM32_CMSIS_DEVICE_ROOT "${AGINTI_DEPS_DIR}/cmsis-device-h7" CACHE PATH "STM32H7 CMSIS device root")
set(CMSIS_CORE_ROOT "${AGINTI_DEPS_DIR}/CMSIS_5" CACHE PATH "CMSIS 5 root")
set(STM32_USB_DEVICE_ROOT "${AGINTI_DEPS_DIR}/stm32-mw-usb-device" CACHE PATH "ST USB device middleware root")
set(AGINTI_CAPTURE_ENGINE "DMA" CACHE STRING "Acquisition engine: DMA or DIRECT")
set_property(CACHE AGINTI_CAPTURE_ENGINE PROPERTY STRINGS DMA DIRECT)

foreach(required
    "${STM32_HAL_ROOT}/Inc/stm32h7xx_hal.h"
    "${STM32_CMSIS_DEVICE_ROOT}/Include/stm32h743xx.h"
    "${CMSIS_CORE_ROOT}/CMSIS/Core/Include/core_cm7.h"
    "${STM32_USB_DEVICE_ROOT}/Core/Inc/usbd_core.h")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "Missing dependency: ${required}. Run scripts/fetch_c12880_firmware_deps.ps1.")
  endif()
endforeach()

set(HAL_SOURCES
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_cortex.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_dma.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_dma_ex.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_exti.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_flash.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_flash_ex.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_gpio.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_pcd.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_pcd_ex.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_pwr.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_pwr_ex.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_rcc.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_rcc_ex.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_adc.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_adc_ex.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_tim.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_hal_tim_ex.c
  ${STM32_HAL_ROOT}/Src/stm32h7xx_ll_usb.c)

set(USB_SOURCES
  ${STM32_USB_DEVICE_ROOT}/Core/Src/usbd_core.c
  ${STM32_USB_DEVICE_ROOT}/Core/Src/usbd_ctlreq.c
  ${STM32_USB_DEVICE_ROOT}/Core/Src/usbd_ioreq.c
  ${STM32_USB_DEVICE_ROOT}/Class/CDC/Src/usbd_cdc.c)

add_executable(aginti_c12880_h743
  src/c12880.c
  src/c12880_protocol.c
  stm32h7/src/main.c
  stm32h7/src/c12880_board.c
  stm32h7/src/c12880_usb.c
  stm32h7/src/usbd_conf.c
  stm32h7/src/usbd_desc.c
  stm32h7/src/stm32h7xx_it.c
  stm32h7/src/syscalls.c
  ${STM32_CMSIS_DEVICE_ROOT}/Source/Templates/system_stm32h7xx.c
  ${STM32_CMSIS_DEVICE_ROOT}/Source/Templates/gcc/startup_stm32h743xx.s
  ${HAL_SOURCES}
  ${USB_SOURCES})

target_include_directories(aginti_c12880_h743 PRIVATE
  include
  stm32h7/include)
target_include_directories(aginti_c12880_h743 SYSTEM PRIVATE
  ${STM32_HAL_ROOT}/Inc
  ${STM32_HAL_ROOT}/Inc/Legacy
  ${STM32_CMSIS_DEVICE_ROOT}/Include
  ${CMSIS_CORE_ROOT}/CMSIS/Core/Include
  ${STM32_USB_DEVICE_ROOT}/Core/Inc
  ${STM32_USB_DEVICE_ROOT}/Class/CDC/Inc)

target_compile_definitions(aginti_c12880_h743 PRIVATE
  STM32H743xx USE_HAL_DRIVER HSE_VALUE=25000000U
  $<$<STREQUAL:${AGINTI_CAPTURE_ENGINE},DMA>:AGINTI_CAPTURE_DMA=1>
  $<$<STREQUAL:${AGINTI_CAPTURE_ENGINE},DIRECT>:AGINTI_CAPTURE_DMA=0>)

target_compile_options(aginti_c12880_h743 PRIVATE
  -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
  -ffunction-sections -fdata-sections -fno-common
  -Wall -Wextra -Wshadow -Wconversion -Wdouble-promotion
  $<$<CONFIG:Debug>:-Og;-g3>
  $<$<NOT:$<CONFIG:Debug>>:-O3;-g1>)

target_link_options(aginti_c12880_h743 PRIVATE
  -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
  -T${CMAKE_CURRENT_SOURCE_DIR}/stm32h7/linker/STM32H743II_FLASH.ld
  -Wl,--gc-sections
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/aginti_c12880_h743.map
  -Wl,--print-memory-usage
  --specs=nano.specs --specs=nosys.specs)

target_link_libraries(aginti_c12880_h743 PRIVATE m c)
set_target_properties(aginti_c12880_h743 PROPERTIES SUFFIX ".elf")

add_custom_command(TARGET aginti_c12880_h743 POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:aginti_c12880_h743>
          ${CMAKE_CURRENT_BINARY_DIR}/aginti_c12880_h743.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:aginti_c12880_h743>
          ${CMAKE_CURRENT_BINARY_DIR}/aginti_c12880_h743.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:aginti_c12880_h743>
  VERBATIM)
