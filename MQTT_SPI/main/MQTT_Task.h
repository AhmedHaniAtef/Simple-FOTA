/*
 * MQTT_Task.h
 *
 *  Created on: Aug 28, 2024
 *      Author: ahmed
 */

#ifndef MAIN_MQTT_TASK_H_
#define MAIN_MQTT_TASK_H_

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

void MQTT_Task(void *par);
void mqtt_publish(uint8_t *message, uint16_t len);
void mqtt_listen(uint8_t *rxMessage, uint16_t len);

#endif /* MAIN_MQTT_TASK_H_ */
