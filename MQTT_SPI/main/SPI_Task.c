/*
 * SPI_Task.c
 *
 *  Created on: Aug 26, 2024
 *      Author: ahmed
 */
#include "SPI_Task.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#define DATA_PACKET_SIZE	256

static void my_post_setup_cb(spi_slave_transaction_t *trans);
static void my_post_trans_cb(spi_slave_transaction_t *trans);

WORD_ALIGNED_ATTR static char SPI_sendbuf[DATA_PACKET_SIZE];
WORD_ALIGNED_ATTR static char SPI_recvbuf[DATA_PACKET_SIZE];
static uint16_t packet_length = 0;
static uint8_t spi_flag = 0;
static uint8_t rx_flag = 0;


static SemaphoreHandle_t tx_sema = NULL;
static SemaphoreHandle_t rx_sema = NULL;


void SPI_Task(void *par)
{
    esp_err_t ret;
	spi_slave_transaction_t t;
	
	tx_sema = xSemaphoreCreateMutex();
	rx_sema = xSemaphoreCreateBinary();
	
    //Configuration for the SPI bus
    spi_bus_config_t buscfg = {
        .mosi_io_num = GPIO_MOSI,
        .miso_io_num = GPIO_MISO,
        .sclk_io_num = GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    //Configuration for the SPI slave interface
    spi_slave_interface_config_t slvcfg = {
        .mode = 0,
        .spics_io_num = GPIO_CS,
        .queue_size = 10,
        .flags = 0,
        .post_setup_cb = my_post_setup_cb,
        .post_trans_cb = my_post_trans_cb
    };

    //Configuration for the handshake line
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(GPIO_HANDSHAKE),
    };

    //Configure handshake line as output
    gpio_config(&io_conf);
    //Enable pull-ups on SPI lines so we don't detect rogue pulses when no master is connected.
    gpio_set_pull_mode(GPIO_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_SCLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_CS, GPIO_PULLUP_ONLY);

    //Initialize SPI slave interface
    ret = spi_slave_initialize(RCV_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
    assert(ret == ESP_OK);

    
    memset(&t, 0, sizeof(t));
    
    t.tx_buffer = SPI_sendbuf;
	t.rx_buffer = SPI_recvbuf;

    while (1) 
    {
		if( tx_sema != NULL )
	   	{  
			if(xSemaphoreTake(tx_sema, portMAX_DELAY))
	        {
				if (1 == spi_flag)
		        {
					t.length    = packet_length * 8;
					ret = spi_slave_transmit(RCV_HOST, &t, portMAX_DELAY);	        
		    		if (rx_flag == 1)
		    		{
						rx_flag = 0;
						xSemaphoreGive(rx_sema);
					}
					spi_flag = 0;
		    	}
		    	xSemaphoreGive(tx_sema);
		    }
        }
    }
}

esp_err_t SPI_trans_data(uint8_t *TXdata, uint8_t *Rxdata, uint16_t len)
{
	esp_err_t ret = ESP_FAIL;
	if (NULL != tx_sema)
	{
		if(xSemaphoreTake(tx_sema, portMAX_DELAY))
	    {
			if (NULL != TXdata)
			{
				memset(SPI_sendbuf, 0x00, DATA_PACKET_SIZE);
				memcpy(SPI_sendbuf, TXdata, len);
				packet_length = len;
			}
			spi_flag = 1;
			if (NULL != Rxdata)
			{
				memset(SPI_recvbuf, 0x00, DATA_PACKET_SIZE);
				rx_flag = 1;
				packet_length = len;
			}
			xSemaphoreGive(tx_sema);
			if (NULL != Rxdata)
			{
				xSemaphoreTake(rx_sema, portMAX_DELAY);
				memcpy(Rxdata, SPI_recvbuf, len);
			}
		}
		ret = ESP_OK;
	}
	return ret;
}



//Called after a transaction is queued and ready for pickup by master. We use this to set the handshake line high.
static void my_post_setup_cb(spi_slave_transaction_t *trans)
{
    gpio_set_level(GPIO_HANDSHAKE, 1);
}

//Called after transaction is sent/received. We use this to set the handshake line low.
static void my_post_trans_cb(spi_slave_transaction_t *trans)
{
    gpio_set_level(GPIO_HANDSHAKE, 0);
}
