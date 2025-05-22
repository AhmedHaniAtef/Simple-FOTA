/*
 * Bootloader.h
 *
 *  Created on: Sep 7, 2024
 *      Author: ahmed
 */

#ifndef INC_BOOTLOADER_H_
#define INC_BOOTLOADER_H_

/*********************************** Includes *********************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "stm32f4xx_hal.h"

#include "main.h"
#include "crc.h"
#include "spi.h"
#include "usb_device.h"
#include "gpio.h"

#include "Bootloader_cfg.h"
/******************************************************************************/

/*********************************** Defines **********************************/
#define BOOTLOADER_BUFFER_SIZE  (256)
/******************************************************************************/

/*********************************** Macro functions **************************/
/******************************************************************************/

/*********************************** Data Types *******************************/

typedef void (*appPtr)(void);

typedef enum
{
    BL_OK = 0,
    BL_ERROR,
}BL_status_t;

typedef enum
{
    BOOT_NOT_NEEDED = 0xAA,
    BOOT_NEEDED = 0xFF,
}BOOT_status_t;

typedef enum
{
    CRC_OK = 0,
    CRC_NOT_OK,
}CRC_check_t;

typedef enum
{
    ACK_SIGNAL = 0xFF,
    NACK_SIGNAL = 1,
}ACK_t;

typedef enum
{
    SECTOR_0 = 0,
    SECTOR_1,
    SECTOR_2,
    SECTOR_3,
    SECTOR_4,
    SECTOR_5,
    MASS_ERASE = 0xFF,
}erase_sectors_t;

typedef enum
{
    BL_GET_VERSION = 0,
    BL_ERASE_SECTORS,
    BL_WRITE_PROGRAM,
    BL_JUMP_TO_ADDRESS,
    WAIT_FOR_ACK_SIGNAL,
    REPEATED_SIGNAL,
}BL_Command_t;
/******************************************************************************/

/*********************************** Function declaration *********************/
void bootloader_app(void);
void bootloader_init(void);
/******************************************************************************/

#endif /* INC_BOOTLOADER_H_ */
