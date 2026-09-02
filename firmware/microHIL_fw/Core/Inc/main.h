/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define DOUT_RELAY2_Pin GPIO_PIN_13
#define DOUT_RELAY2_GPIO_Port GPIOC
#define DOUT_RELAY3_Pin GPIO_PIN_14
#define DOUT_RELAY3_GPIO_Port GPIOC
#define DOUT_RELAY4_Pin GPIO_PIN_15
#define DOUT_RELAY4_GPIO_Port GPIOC
#define DOUT_RELAY1_Pin GPIO_PIN_0
#define DOUT_RELAY1_GPIO_Port GPIOC
#define DEBUG_LED_1_Pin GPIO_PIN_1
#define DEBUG_LED_1_GPIO_Port GPIOC
#define DEBUG_LED_2_Pin GPIO_PIN_2
#define DEBUG_LED_2_GPIO_Port GPIOC
#define AIN_AN2_Pin GPIO_PIN_0
#define AIN_AN2_GPIO_Port GPIOA
#define AIN_AN3_Pin GPIO_PIN_1
#define AIN_AN3_GPIO_Port GPIOA
#define AIN_AN4_Pin GPIO_PIN_2
#define AIN_AN4_GPIO_Port GPIOA
#define AIN_AN1_Pin GPIO_PIN_3
#define AIN_AN1_GPIO_Port GPIOA
#define AOUT_AN2_Pin GPIO_PIN_4
#define AOUT_AN2_GPIO_Port GPIOA
#define AOUT_AN1_Pin GPIO_PIN_5
#define AOUT_AN1_GPIO_Port GPIOA
#define AIN_12VOUT1_CURRSENSE_Pin GPIO_PIN_6
#define AIN_12VOUT1_CURRSENSE_GPIO_Port GPIOA
#define AIN_12VOUT2_CURRSENSE_Pin GPIO_PIN_7
#define AIN_12VOUT2_CURRSENSE_GPIO_Port GPIOA
#define DOUT_12VOUT1_Pin GPIO_PIN_4
#define DOUT_12VOUT1_GPIO_Port GPIOC
#define DOUT_12VOUT2_Pin GPIO_PIN_5
#define DOUT_12VOUT2_GPIO_Port GPIOC
#define DIN_IN8_Pin GPIO_PIN_0
#define DIN_IN8_GPIO_Port GPIOB
#define DIN_IN7_Pin GPIO_PIN_1
#define DIN_IN7_GPIO_Port GPIOB
#define DIN_IN6_Pin GPIO_PIN_2
#define DIN_IN6_GPIO_Port GPIOB
#define DIN_IN5_Pin GPIO_PIN_10
#define DIN_IN5_GPIO_Port GPIOB
#define DIN_IN1_Pin GPIO_PIN_12
#define DIN_IN1_GPIO_Port GPIOB
#define DIN_IN2_Pin GPIO_PIN_13
#define DIN_IN2_GPIO_Port GPIOB
#define DIN_IN3_Pin GPIO_PIN_14
#define DIN_IN3_GPIO_Port GPIOB
#define DIN_IN4_Pin GPIO_PIN_15
#define DIN_IN4_GPIO_Port GPIOB
#define DOUT_OUT1_Pin GPIO_PIN_10
#define DOUT_OUT1_GPIO_Port GPIOA
#define DOUT_OUT2_Pin GPIO_PIN_15
#define DOUT_OUT2_GPIO_Port GPIOA
#define DOUT_OUT3_Pin GPIO_PIN_10
#define DOUT_OUT3_GPIO_Port GPIOC
#define DOUT_OUT4_Pin GPIO_PIN_11
#define DOUT_OUT4_GPIO_Port GPIOC
#define DOUT_OUT5_Pin GPIO_PIN_12
#define DOUT_OUT5_GPIO_Port GPIOC
#define DOUT_OUT6_Pin GPIO_PIN_2
#define DOUT_OUT6_GPIO_Port GPIOD
#define DOUT_OUT7_Pin GPIO_PIN_3
#define DOUT_OUT7_GPIO_Port GPIOB
#define DOUT_OUT8_Pin GPIO_PIN_4
#define DOUT_OUT8_GPIO_Port GPIOB
#define CAN1_SILENT_Pin GPIO_PIN_7
#define CAN1_SILENT_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
