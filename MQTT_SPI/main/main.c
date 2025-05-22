/* SPI Slave example, receiver (uses SPI Slave driver to communicate with sender)

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"

#include "SPI_Task.h"
#include "portmacro.h"

#include "MQTT_Task.h"

void printHex(const char *array, size_t length);
void main_applicaion(void* parm);

uint8_t SPI_send_data[256];
uint8_t SPI_receive_data[256];
uint8_t MQTT_send_data[256];
uint8_t MQTT_receive_data[256];


//Main application
void app_main(void)
{
	TaskHandle_t spi_task_ptr = 0;
	TaskHandle_t mqtt_task_ptr = 0;
	TaskHandle_t main_app_task_ptr = 0;
   
	xTaskCreate(MQTT_Task, "MQTT_Task", 1024*10, NULL, 1, &mqtt_task_ptr);
	vTaskDelay(2000/portTICK_PERIOD_MS);
	xTaskCreate(SPI_Task, "SPI_Task", 1024*2, NULL, 2, &spi_task_ptr);
	vTaskDelay(2000/portTICK_PERIOD_MS);
	xTaskCreate(main_applicaion, "Main_Application", 1024*2, NULL, 1, &main_app_task_ptr);
	
}

void main_applicaion(void* parm)
{
	while(1)
	{
		memset(SPI_send_data, '\0', 256);
		memset(SPI_receive_data, '\0', 256);
		memset(MQTT_send_data, '\0', 256);
		memset(MQTT_receive_data, '\0', 256);
		
		printf("Waiting for MQTT Message\n");
		
		mqtt_listen(MQTT_receive_data, (uint16_t)256);
		printf("Received from MQTT:\n");
		printHex((const char *)MQTT_receive_data, 256);
		
		memcpy(SPI_send_data, MQTT_receive_data, (uint16_t)256);
		printf("Sending to SPI...\n");
		SPI_trans_data(SPI_send_data, SPI_receive_data, (uint16_t)256);
		printf("Received from SPI:\n");
		printHex((const char *)SPI_receive_data, 256);
		
		memcpy(MQTT_send_data, SPI_receive_data, (uint16_t)256);
		printf("Sending to MQTT...\n");
		mqtt_publish(MQTT_send_data, (uint16_t)256);
		//vTaskDelay(10/portTICK_PERIOD_MS);
	}
}


void printHex(const char *array, size_t length) 
{
    for (size_t i = 0; i < length; i++) {
        printf("%02X ", (unsigned char)array[i]);
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
    printf("\n");
}





