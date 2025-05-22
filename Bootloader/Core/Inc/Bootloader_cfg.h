/*
 * Bootloader_cfg.h
 *
 *  Created on: Sep 7, 2024
 *      Author: ahmed
 */

#ifndef INC_BOOTLOADER_CFG_H_
#define INC_BOOTLOADER_CFG_H_


/*********************************** Defines **********************************/
#define BOOTLOADER_STOP     (0)
#define BOOTLOADER_USB      (1)
#define BOOTLOADER_USART    (2)
#define BOOTLOADER_SPI      (3)
#define BOOTLOADER_I2C      (4)

#define BOOTLOADER_DEBUG_PROTOCOL   (BOOTLOADER_USB)
/******************************************************************************/

#endif /* INC_BOOTLOADER_CFG_H_ */
