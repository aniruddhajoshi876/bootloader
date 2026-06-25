/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <math.h>
#include <stdio.h>
#include <stdbool.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#define APP_START	0x08008000
#define BOOT_FLAG (*((volatile uint32_t*)0x2001BFFC))
// RAM staging buffer size: 64 KB fits comfortably in the 112 KB of RAM.
#define FW_RAM_MAX  0x10000

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

uint32_t at_app_start;


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void disable(){
				// 1. Disable SysTick
				SysTick->CTRL = 0;
				SysTick->LOAD = 0;
				SysTick->VAL  = 0;

				// 2. De-initialize peripherals used (example)
				HAL_UART_DeInit(&huart1);

				// 3. Clear NVIC registers
				for (uint8_t i = 0; i < 8; i++) {
					NVIC->ICER[i] = 0xFFFFFFFF;
					NVIC->ICPR[i] = 0xFFFFFFFF;
				}

				// 4. Global lock
				__disable_irq();
		  }

void reset_handler(){
			  //reset handler at stack_start + 4u. contains address to function
			  //which must be loaded to pc and must be called. it will later call main()
			  uint32_t reset_handler_adr = *(uint32_t*)(APP_START + 4u); //read reset handler adr

			  void (*reset_handler)(void); //decleare function pointer
			  reset_handler = (void (*)(void)) reset_handler_adr; //define function and cast as function pointer
			  reset_handler(); //call function
		  }

void set_main_nvic(){
	SCB->VTOR = APP_START; //set vector table
	}

void set_msp(){
			  uint32_t app_start =*(uint32_t*) (APP_START);
			  __set_MSP(app_start); //set main stack pointer to start of stack address for main function
			}
void enter_main(){
			  disable();
			  set_main_nvic();
			  set_msp();
			  __enable_irq();
			  reset_handler();
}
uint32_t pg_err;
void erase_flash(){
	  FLASH_EraseInitTypeDef flash_struct={
			  .TypeErase = FLASH_TYPEERASE_PAGES,
			  .Banks = FLASH_BANK_1,
			  .Page = (APP_START - FLASH_BASE) / PAGESIZE ,
			  .NbPages = (FLASH_SIZE - (APP_START - FLASH_BASE)) / PAGESIZE
	  };
	  HAL_FLASHEx_Erase(&flash_struct, &pg_err); //erase all of flash, for now
}



uint32_t size;
bool get_project_size(){
	uint8_t discard;
	// drain any stale/misaligned bytes before attempting the 4-byte receive
	while (HAL_UART_Receive(&huart1, &discard, 1, 10) == HAL_OK) {}
	__HAL_UART_CLEAR_OREFLAG(&huart1);
	while(1){
		HAL_StatusTypeDef status = HAL_UART_Receive(&huart1, (uint8_t*) &size, 4, 6000);
		if (size > 0 && size <= FW_RAM_MAX && status == HAL_OK){
			return true;
		}
		// drain and clear before retrying
		while (HAL_UART_Receive(&huart1, &discard, 1, 10) == HAL_OK) {}
		__HAL_UART_CLEAR_OREFLAG(&huart1);
	}
}


uint8_t firmware_buffer[8];
uint64_t double_word;

// RAM staging buffer: receive the whole firmware first, then flash from here.
uint8_t firmware_ram[FW_RAM_MAX];

uint64_t pack_double_word(uint8_t *buffer)
{
    uint64_t word = 0;

    word |= ((uint64_t)buffer[0] << 0);
    word |= ((uint64_t)buffer[1] << 8);
    word |= ((uint64_t)buffer[2] << 16);
    word |= ((uint64_t)buffer[3] << 24);
    word |= ((uint64_t)buffer[4] << 32);
    word |= ((uint64_t)buffer[5] << 40);
    word |= ((uint64_t)buffer[6] << 48);
    word |= ((uint64_t)buffer[7] << 56);

    return word;
}

HAL_StatusTypeDef write_to_flash(uint32_t project_size)
{
    // 1. Receive the entire firmware into RAM in one go. No flash programming
    //    happens between bytes, so the CPU keeps up with the 115200 baud stream
    //    and no bytes are lost to UART overrun.
    HAL_StatusTypeDef uart_status =
        HAL_UART_Receive(&huart1, firmware_ram, project_size, HAL_MAX_DELAY);

    if (uart_status != HAL_OK)
    {
        return uart_status;
    }

    // 2. Flash from the RAM buffer, one double-word (8 bytes) at a time.
    uint32_t start_adr = APP_START;
    uint32_t bytes_written = 0;

    while (bytes_written < project_size)
    {
        // Fill with 0xFF in case this is the last partial chunk
        for (uint8_t i = 0; i < 8; i++)
        {
            uint32_t idx = bytes_written + i;
            firmware_buffer[i] = (idx < project_size) ? firmware_ram[idx] : 0xFF;
        }

        double_word = pack_double_word(firmware_buffer);

        HAL_StatusTypeDef flash_status =
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, start_adr, double_word);

        if (flash_status != HAL_OK)
        {
            return flash_status;
        }

        start_adr += 8;
        bytes_written += 8;
    }

    return HAL_OK;
}


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_TIM1_Init();
  /* USER CODE BEGIN 2 */
  uint8_t rx_byte;
  if (BOOT_FLAG == 0xDEADBEEF){
    //HAL_UART_Receive(&huart1, &rx_byte, 1, 200);
    BOOT_FLAG = 0x00; //clear flag
	  //enter update mode
	  printf("U received\n");

	  HAL_FLASH_Unlock();


	  if (get_project_size()){
		  printf("size received\n");
		  printf("receiving and writing %lu bytes\n", size);
	  }



	  erase_flash();

	  printf("erased flash\n");


	  write_to_flash(size);

	  HAL_FLASH_Lock();
	  printf("written to flash, entering main\n");
	  enter_main();

  }
  HAL_StatusTypeDef status;
  status = HAL_UART_Receive(&huart1, &rx_byte, 1, 9000);

  while (1){
	  if (status == HAL_OK && rx_byte=='U'){
		  //enter update mode
		  printf("U received\n");

		  HAL_FLASH_Unlock();


		  if (get_project_size()){
			  printf("size received\n");
			  printf("receiving and writing %lu bytes\n", size);
		  }



		  erase_flash();
		  printf("erased flash\n");


		  HAL_StatusTypeDef write_status = write_to_flash(size);

		  HAL_FLASH_Lock();
		  printf("written to flash, entering main\n");
		  enter_main();

	  } else{
		  at_app_start = *(uint32_t*) APP_START; //read whats written at start of vector table
		  if (0x20000000 < at_app_start && at_app_start <= 0x2001C000){ //this range was chosen since it must be a value in the ram

			  enter_main(); //the value at app start address is valid and holds location of top of vector table
		  }

		  else{
			  //printf("waiting...\r\n");
			  status = HAL_UART_Receive(&huart1, &rx_byte, 1, HAL_MAX_DELAY); //keepwaiting in bootloader mode, if byte received return to top of while loop
		  }
	  }
  }


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
