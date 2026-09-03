/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usb_device.c
  * @version        : v1.0_Cube
  * @brief          : This file implements the USB Device
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/

#include "usb_device.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* Endpunktadressen der beiden CDC-Funktionen, Reihenfolge wie vom
 * CompositeBuilder erwartet: { Bulk IN, Bulk OUT, Interrupt IN }.
 * Fest vorgegeben statt automatisch vergeben, damit der TX-FIFO-Index in
 * usbd_conf.c (= Endpunktnummer) dazu passt. */
static uint8_t CDC_EpAdd_Ctrl[3] = {0x81, 0x01, 0x82}; /* HIL-Protokoll */
static uint8_t CDC_EpAdd_Can[3] = {0x83, 0x03, 0x84};  /* CAN1 / SLCAN */

/* USER CODE END PV */

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

/* USER CODE END PFP */

/* USB Device Core handle declaration. */
USBD_HandleTypeDef hUsbDeviceFS;

/*
 * -- Insert your variables declaration here --
 */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*
 * -- Insert your external function declaration here --
 */
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/**
  * Init USB device Library, add supported class and start the library
  * @retval None
  */
void MX_USB_DEVICE_Init(void)
{
  /* USER CODE BEGIN USB_DEVICE_Init_PreTreatment */

  /* USER CODE END USB_DEVICE_Init_PreTreatment */

  /* Init Device Library, add supported class and start the library. */
  if (USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK)
  {
    Error_Handler();
  }
  /* Zwei CDC-ACM-Funktionen statt einer: Port A traegt das
   * HIL-Kommandoprotokoll, Port B das SLCAN-Interface von CAN1. Die
   * Endpunktadressen werden fest vorgegeben, damit die FIFO-Aufteilung in
   * usbd_conf.c dazu passt (TX-FIFO-Index = Endpunktnummer). */
  if (USBD_RegisterClassComposite(&hUsbDeviceFS, &USBD_CDC, CLASS_TYPE_CDC,
                                  CDC_EpAdd_Ctrl) != USBD_OK)
  {
    Error_Handler();
  }
  hUsbDeviceFS.classId = 0;
  if (USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS) != USBD_OK)
  {
    Error_Handler();
  }

  hUsbDeviceFS.classId = 1;
  if (USBD_RegisterClassComposite(&hUsbDeviceFS, &USBD_CDC, CLASS_TYPE_CDC,
                                  CDC_EpAdd_Can) != USBD_OK)
  {
    Error_Handler();
  }
  hUsbDeviceFS.classId = 1;
  if (USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_CAN) != USBD_OK)
  {
    Error_Handler();
  }
  hUsbDeviceFS.classId = 0;

  if (USBD_Start(&hUsbDeviceFS) != USBD_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN USB_DEVICE_Init_PostTreatment */

  /* USER CODE END USB_DEVICE_Init_PostTreatment */
}

/**
  * @}
  */

/**
  * @}
  */

