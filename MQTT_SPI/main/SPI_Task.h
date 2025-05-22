/*
 * SPI_Task.h
 *
 *  Created on: Aug 26, 2024
 *      Author: ahmed
 */

#ifndef MAIN_SPI_TASK_H_
#define MAIN_SPI_TASK_H_

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"

#ifdef CONFIG_IDF_TARGET_ESP32
#define RCV_HOST    HSPI_HOST
#else
#define RCV_HOST    SPI2_HOST
#endif

#define GPIO_HANDSHAKE      2
#define GPIO_MOSI           13
#define GPIO_MISO           12
#define GPIO_SCLK           14
#define GPIO_CS             15

void SPI_Task(void *par);

esp_err_t SPI_trans_data(uint8_t *TXdata, uint8_t *Rxdata, uint16_t len);

#endif /* MAIN_SPI_TASK_H_ */
