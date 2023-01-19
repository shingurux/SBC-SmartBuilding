//conexion ADC
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
//conexion thingsBoard
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "sdkconfig.h"
//librerias para telegram
#include "esp_tls.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_http_client.h"
#include "freertos/event_groups.h"



static const char *TAG = "MQTT_EXAMPLE";
static uint32_t amperios;

//***************   Funciones para el ADC   *****************
//***************                           *****************

#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   64          //Multisampling


static esp_adc_cal_characteristics_t *adc_chars;
#if CONFIG_IDF_TARGET_ESP32
static const adc_channel_t channel = ADC_CHANNEL_6;     //GPIO34 if ADC1, GPIO14 if ADC2
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
#elif CONFIG_IDF_TARGET_ESP32S2
static const adc_channel_t channel = ADC_CHANNEL_6;     // GPIO7 if ADC1, GPIO17 if ADC2
static const adc_bits_width_t width = ADC_WIDTH_BIT_13;
#endif
static const adc_atten_t atten = ADC_ATTEN_DB_0;
static const adc_unit_t unit = ADC_UNIT_1;

static void check_efuse(void)
{
#if CONFIG_IDF_TARGET_ESP32
    //Check if TP is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        printf("eFuse Two Point: Supported\n");
    } else {
        printf("eFuse Two Point: NOT supported\n");
    }
    //Check Vref is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
        printf("eFuse Vref: Supported\n");
    } else {
        printf("eFuse Vref: NOT supported\n");
    }
#elif CONFIG_IDF_TARGET_ESP32S2
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        printf("eFuse Two Point: Supported\n");
    } else {
        printf("Cannot retrieve eFuse Two Point calibration values. Default calibration values will be used.\n");
    }
#else
#error "This example is configured for ESP32/ESP32S2."
#endif
}


static void print_char_val_type(esp_adc_cal_value_t val_type)
{
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        printf("Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        printf("Characterized using eFuse Vref\n");
    } else {
        printf("Characterized using Default Vref\n");
    }
}
void configureADC(){
    if (unit == ADC_UNIT_1) {
        adc1_config_width(width);
        adc1_config_channel_atten(channel, atten);
    } else {
        adc2_config_channel_atten((adc2_channel_t)channel, atten);
    }
    //Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);
    print_char_val_type(val_type);

}


void turnOffRelay(){
    int rele=0;
    gpio_set_level(GPIO_NUM_15,rele);
        
}
void tunrOnRelay(){
    int rele=1;
    gpio_set_level(GPIO_NUM_15,rele);
}


uint32_t multisampling(){
    uint32_t adc_reading = 0;
    //Multisampling
    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        if (unit == ADC_UNIT_1) {
            adc_reading += adc1_get_raw((adc1_channel_t)channel);
        } else {
            int raw;
            adc2_get_raw((adc2_channel_t)channel, width, &raw);
            adc_reading += raw;
        }
    }
    adc_reading /= NO_OF_SAMPLES;

    if(adc_reading>20) turnOffRelay();
    else tunrOnRelay();

    return adc_reading;
}



//***************   Funciones para bot Telegram    ********************
//***************                                  ********************

/*HTTP buffer*/
#define MAX_HTTP_RECV_BUFFER 1024
#define MAX_HTTP_OUTPUT_BUFFER 2048

/* TAGs for the system*/
static const char *TAG0 = "HTTP_CLIENT Handler";
static const char *TAG1 = "wifi station";
static const char *TAG2 = "Sending getMe";
static const char *TAG3 = "Sending sendMessage";


/*Telegram configuration*/
#define TOKEN "5946979126:AAHQwPyuuIWFiHfURenF8xlnqlDHaseSHxk"
char url_string[512] = "https://api.telegram.org/bot";
// Using in the task strcat(url_string,TOKEN)); the main direct from the url will be in url_string
//The chat id that will receive the message
#define chat_ID1 "-648456728" //group
#define chat_ID2 "5939864152"

extern const char telegram_certificate_pem_start[] asm("_binary_telegram_certificate_pem_start");
extern const char telegram_certificate_pem_end[]   asm("_binary_telegram_certificate_pem_end");

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG0, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG0, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG0, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG0, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG0, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG0, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG0, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG0, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                if (output_buffer != NULL) {
                    free(output_buffer);
                    output_buffer = NULL;
                }
                output_len = 0;
                ESP_LOGI(TAG0, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG0, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}


static void https_telegram_getMe_perform(void) {
	char buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
	char url[512] = "";
    esp_http_client_config_t config = {
        .url = "https://api.telegram.org",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = _http_event_handler,
        .cert_pem = telegram_certificate_pem_start,
        .user_data = buffer,        // Pass address of local buffer to get response
    };
    /* Creating the string of the url*/
    //Copy the url+TOKEN
    strcat(url,url_string);
    //Adding the method
    strcat(url,"/getMe");
    //ESP_LOGW(TAG2, "url es: %s",url);
    //ESP_LOGW(TAG, "Iniciare");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    //You set the real url for the request
    esp_http_client_set_url(client, url);
    //ESP_LOGW(TAG, "Selecting the http method");
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    //ESP_LOGW(TAG, "Perform");
    esp_err_t err = esp_http_client_perform(client);

    //ESP_LOGW(TAG, "Revisare");
    if (err == ESP_OK) {
        ESP_LOGI(TAG2, "HTTPS Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
        ESP_LOGW(TAG2, "Desde Perform el output es: %s",buffer);
    } else {
        ESP_LOGE(TAG2, "Error perform http request %s", esp_err_to_name(err));
    }

    ESP_LOGW(TAG2, "Cerrar Cliente");
    esp_http_client_close(client);
    ESP_LOGW(TAG, "Limpiare");
    esp_http_client_cleanup(client);
}

static void http_telegram_readMessage_perfor(void){
    
    while(1){
        char buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
        char url[512] = "";
        esp_http_client_config_t config = {
            .url = "https://api.telegram.org",
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .event_handler = _http_event_handler,
            .cert_pem = telegram_certificate_pem_start,
            .user_data = buffer,        // Pass address of local buffer to get response
        };
        /* Creating the string of the url*/
        //Copy the url+TOKEN
        strcat(url,url_string);
        //Adding the method
        strcat(url,"/getUpdates");
        //ESP_LOGW(TAG2, "url es: %s",url);
        //ESP_LOGW(TAG, "Iniciare");
        esp_http_client_handle_t client = esp_http_client_init(&config);
        //You set the real url for the request
        esp_http_client_set_url(client, url);
        //ESP_LOGW(TAG, "Selecting the http method");
        esp_http_client_set_method(client, HTTP_METHOD_GET);
        //ESP_LOGW(TAG, "Perform");
        esp_err_t err = esp_http_client_perform(client);

        if (err == ESP_OK) {
            ESP_LOGI(TAG2, "HTTPS Status = %d, content_length = %d",
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
            ESP_LOGW(TAG2, "Desde BOT el mensaje es: %s",buffer);
        } else {
            ESP_LOGE(TAG2, "Error perform http request %s", esp_err_to_name(err));
        }

        ESP_LOGW(TAG2, "Cerrar Cliente");
        esp_http_client_close(client);
        ESP_LOGW(TAG, "Limpiare");
        esp_http_client_cleanup(client);

        vTaskDelay(3000);
    }
    
}


static void https_telegram_sendMessage_perform_post(void) {


    while(1){   //colocado para que envie constantemente cada 5 seg
        char url[512] = "";
        char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // Buffer to store response of http request
        esp_http_client_config_t config = {
            .url = "https://api.telegram.org",
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .event_handler = _http_event_handler,
            .cert_pem = telegram_certificate_pem_start,
            .user_data = output_buffer,
        };
        //POST
        ESP_LOGW(TAG3, "Iniciare");
        esp_http_client_handle_t client = esp_http_client_init(&config);

        /* Creating the string of the url*/
        //Copy the url+TOKEN
        strcat(url,url_string);
        //Passing the method
        strcat(url,"/sendMessage");
        //ESP_LOGW(TAG3, "url string es: %s",url);
        //You set the real url for the request
        esp_http_client_set_url(client, url);


        ESP_LOGW(TAG3, "Enviare POST");
        /*Here you add the text and the chat id
        * The format for the json for the telegram request is: {"chat_id":123456789,"text":"Here goes the message"}
        */
        // The example had this, but to add the chat id easierly I decided not to use a pointer
        //const char *post_data = "{\"chat_id\":852596694,\"text\":\"Envio de post\"}";
        char post_data[512] = "";
        sprintf(post_data,"{\"chat_id\":%s,\"text\":\"conexion correcta entre ESP32-Telegram \"}",chat_ID1);

        sprintf(post_data,"{\"chat_id\":%s,\"text\":\"corriente consumida:%d \"}",chat_ID1, amperios );

        //ESP_LOGW(TAG, "El json es es: %s",post_data);
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, post_data, strlen(post_data));

        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            ESP_LOGI(TAG3, "HTTP POST Status = %d, content_length = %d",
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
            ESP_LOGW(TAG3, "Desde Perform el output es: %s",output_buffer);

        } else {
            ESP_LOGE(TAG3, "HTTP POST request failed: %s", esp_err_to_name(err));
        }

         ESP_LOGW(TAG0, "Limpiare");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        ESP_LOGI(TAG3, "esp_get_free_heap_size: %d", esp_get_free_heap_size ());

        vTaskDelay(5000);
    }

}

static void http_test_task(void *pvParameters) {
    /* Creating the string of the url*/
    // You concatenate the host with the Token so you only have to write the method
	strcat(url_string,TOKEN);
    ESP_LOGW(TAG0, "Wait 2 second before start");
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    ESP_LOGW(TAG, "https_telegram_getMe_perform");
    https_telegram_getMe_perform();
    /* The functions https_telegram_getMe_native_get and https_telegram_sendMessage_native_get usually reboot the esp32 at when you use it after another and
     *  the second one finish, but I don't know why. Either way, it still send the message and obtain the getMe response, but the perform way is better
     *  for both options, especially for sending message with Json.*/
    //ESP_LOGW(TAG, "https_telegram_getMe_native_get");
    //https_telegram_getMe_native_get();


    ESP_LOGW(TAG0, "https_telegram_sendMessage_perform_post");
    https_telegram_sendMessage_perform_post();

    ESP_LOGI(TAG0, "Finish http example");
    vTaskDelete(NULL);
}


//***************   Funciones para ThingsBoard  ******************
//***************                               ******************

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "mqtt://demo.thingsboard.io",
        .event_handle = mqtt_event_handler,
        .port = 1883,
        .username = "DJM1fP0vY0gCMZYLm467", //token
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
    while(1){
    cJSON *root = cJSON_CreateObject();
    amperios= multisampling();
    cJSON_AddNumberToObject(root,"Amperios", amperios); //for the current ac
    
    char *post_data = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(client, "v1/devices/me/telemetry", post_data, 0, 1, 0);
    cJSON_Delete(root);

    // Free is intentional, it's client responsibility to free the result of cJSON_Print
    free(post_data);

    //envio a telegram ->comprobar el metodo htt_test_task()
    //xTaskCreatePinnedToCore(&http_test_task, "http_test_task", 8192*4, NULL, 5, NULL,1);

    vTaskDelay(500);
    }
}

//********************************  MAIN    *************************

void tareaPrueba(){
    int cont=0;
    while(1){
        printf("ESTADO CONTADOR: %d",cont);
        cont++;

        vTaskDelay(5000);
    }
   
}

void app_main(void)
{   
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //configure pin for relay
    gpio_reset_pin(GPIO_NUM_15);
    gpio_set_direction(GPIO_NUM_15, GPIO_MODE_OUTPUT);
    //int rele=1;
    //Check if Two Point or Vref are burned into eFuse
    check_efuse();

    //Configure ADC
    configureADC();

    //thingsBoard
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    //telegram
    xTaskCreatePinnedToCore(&http_test_task, "http_test_task", 8192*4, NULL, 5, NULL,1);
    xTaskCreatePinnedToCore(&http_telegram_readMessage_perfor,"http_telegram_readMessage_perfor", 8192*4, NULL, 6, NULL,1); 
    xTaskCreatePinnedToCore(&mqtt_app_start, "mqtt_app_start", 8192*4, NULL, 5, NULL,0);
    //xTaskCreatePinnedToCore(&tareaPrueba,"tareaPrueba",8192*4, NULL, 5, NULL,1);
    //mqtt_app_start(); //la medicion de AC ocurre aqui
}


/*
                version 2.0 (18/01/2023)
En esta version la esp32 es capaz de conectarse al wifi, conectarse con thingsboar y telegram.

Thingsboard:  se publica periodicamente los datos recogidos por el sensor de medicion  (esta parte esta correcta) 

Telegram:   la ESP32 se conecta con el bot de telegram y es capaz de envir un mensaje y nostrar los datos de medicion (esto correcto)
        FALLOS: no conseguimos que lea los mensajes publicados para realizar los comandos.

*/