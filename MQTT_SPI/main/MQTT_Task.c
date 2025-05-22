#include "MQTT_Task.h"
#include "portmacro.h"

#define SSID	        "AHani"
#define PASS	        "Ahmed@@@01008524027"
#define MQTT_BUF_SIZE   256
#define MQTT_QOS_SEND	0
#define MQTT_QOS_RECE	1


static const char *TAG = "MQTT_TCP";
static uint8_t mqtt_buffer_send[MQTT_BUF_SIZE];
static uint8_t mqtt_buffer_rece[MQTT_BUF_SIZE];
static SemaphoreHandle_t publish_sema = NULL;
static SemaphoreHandle_t listen_sema = NULL;


static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        printf("WiFi connecting ... \n");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        printf("WiFi connected ... \n");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        printf("WiFi lost connection ... \n");
        break;
    case IP_EVENT_STA_GOT_IP:
        printf("WiFi got IP ... \n\n");
        break;
    default:
        break;
    }
}

void wifi_connection()
{
    esp_netif_init(); //network interdace initialization
	esp_event_loop_create_default(); //responsible for handling and dispatching events
	esp_netif_create_default_wifi_sta(); //sets up necessary data structs for wifi station interface
	wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();//sets up wifi wifi_init_config struct with default values
	esp_wifi_init(&wifi_initiation); //wifi initialised with dafault wifi_initiation
	esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);//creating event handler register for wifi
	esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);//creating event handler register for ip event
	wifi_config_t wifi_configuration ={ //struct wifi_config_t var wifi_configuration
	.sta= {
	    .ssid = "",
	    .password= "", /*we are sending a const char of ssid and password which we will strcpy in following line so leaving it blank*/ 
	  }//also this part is used if you donot want to use Kconfig.projbuild
	};
	strcpy((char*)wifi_configuration.sta.ssid,SSID); // copy chars from hardcoded configs to struct
	strcpy((char*)wifi_configuration.sta.password,PASS);
	esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);//setting up configs when event ESP_IF_WIFI_STA
	esp_wifi_start();//start connection with configurations provided in funtion
	esp_wifi_set_mode(WIFI_MODE_STA);//station mode selected
	esp_wifi_connect(); //connect with saved ssid and pass
	printf( "wifi_init_softap finished. SSID:%s  password:%s",SSID,PASS);
}

esp_mqtt_client_handle_t client;

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    client = event->client;
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        esp_mqtt_client_subscribe(client, "bootloader-send", 0);
        esp_mqtt_client_subscribe(client, "bootloader-receive", 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
    	ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("\nTOPIC=%.*s\r\n", event->topic_len, event->topic);
        if (memcmp(event->topic, "bootloader-send", event->topic_len) == 0)
        {
			printf("MQTT SEND:\n");
		}
        else if (memcmp(event->topic, "bootloader-receive", event->topic_len) == 0)
        {
			memcpy(mqtt_buffer_rece, event->data, event->data_len);
			printf("MQTT RECE:\n");
			xSemaphoreGive(listen_sema);
		}
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    mqtt_event_handler_cb(event_data);
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://broker.hivemq.com",
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}

void MQTT_Task(void *par)
{

    publish_sema = xSemaphoreCreateBinary();
    listen_sema = xSemaphoreCreateBinary();

    nvs_flash_init();
    wifi_connection();

    vTaskDelay(2000 / portTICK_PERIOD_MS);
    printf("WIFI was initiated ...........\n");


    mqtt_app_start();
    
    while (1) {
        if(xSemaphoreTake(publish_sema, portMAX_DELAY))
	    {
			esp_mqtt_client_publish(client,"bootloader-send", (const char*)mqtt_buffer_send, 0, MQTT_QOS_SEND, 0);
		}
        else{
            printf("Waiting for Buffer to be ready ...\n");
        }
	}
}

void mqtt_publish(uint8_t *message, uint16_t len)
{
    memset(mqtt_buffer_send, 0x00, MQTT_BUF_SIZE);
    memcpy(mqtt_buffer_send, message, len);
    xSemaphoreGive(publish_sema);
}

void mqtt_listen(uint8_t *rxMessage, uint16_t len)
{
	memset(mqtt_buffer_rece, 0x00, MQTT_BUF_SIZE);
    xSemaphoreTake(listen_sema, portMAX_DELAY);
    memcpy(rxMessage, mqtt_buffer_rece, len);
}