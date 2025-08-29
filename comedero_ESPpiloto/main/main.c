/*
*   MENSOFT CONSULTORES SA
*   Programa que registra los eventos de tiempo que se pulsa en un comedero de 5 bocas.
*   Se almacenan los datos en un buffer circular y cada cierto tiempo se envian los
*   datos almacenados a traves de WIFI y el protocolo MQTT.
*
*/


#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include <time.h>
#include <sys/time.h>
#include <esp_event.h>
#include "driver/gpio.h"
#include "macros.h"
#include "esp_timer.h"
#include "esp_mac.h"
// sd library
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include <dirent.h>  
//mqtt
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_system.h"
#include "mqtt_client.h"
//ntp
#include "esp_sntp.h"

#include "driver/i2c.h"

static const char *TAG = "PIGNODE";

//Estructuras
time_t tt_t;
struct timeval tv;
struct tm data;

//Enumeraciones
enum {ON, OFF} estado_wifi = OFF;

// ***********          Variables globales           *********** //
bool presencia = 0;
evento registro_mem[NUM_REGISTROS];
static bool sendData = FALSE;
unsigned char mac_uni_base[6] = {0};
char dir_mac[20];
volatile uint8_t time_to_send = 2; // tiempo de envío en minutos
//mqtt
esp_mqtt_client_handle_t client;

// ***********          Funciones           *********** //
uint8_t read_bocas();
esp_err_t config_pin(void);
uint8_t manejar_eventos();
uint8_t send_data(uint8_t sendType);
//Funciones de buffer
int8_t insertar_buffer(evento dato);
int8_t recoger_buffer(evento * dato);
void avanzar_cabeza();
void avanzar_cola();
int8_t buffer_vacio();
uint16_t size_buffer();
//FUNCIONES SD
static esp_err_t initi_sd_card(void);
static bool save_sd(const char *arg, const char *dir);
static bool crear_archivo(const char *file);
static bool crear_directorio(const char *dir);
static bool buscar_directorio(const char *ruta, const char *nombre_buscado, bool buscar_directorio);
static bool buscar_archivo(const char *ruta, const char *nombre_archivo);
void guardar_en_sd(evento data);
//mqtt
bool fin_dataTosend= FALSE;
bool error_mqtt= false;
static void mqtt_app_start(void);
// wifi
void wifi_connection();
//ntp
void initialize_sntp(void);
//RTC
static uint8_t bcd2dec(uint8_t val);
static uint8_t dec2bcd(uint8_t val);
esp_err_t ds3231_set_time(struct tm *dt);
esp_err_t ds3231_get_time(struct tm *dt);
static void i2c_master_init(void);

void app_main(void)
{
    /*
    *   Funcion principal.
    */
    i2c_master_init();

    nvs_flash_init();

    config_pin();

    // config sd
    esp_err_t err = initi_sd_card();
    if (err != ESP_OK)
    {
        printf("err: %s\n", esp_err_to_name(err));
        return;
    }

    vTaskDelay(500 / portTICK_PERIOD_MS);
    gpio_set_level(WHITELED, 1);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    gpio_set_level(REDLED, 1);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    
    // Congirurar WIFI por primera vez
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_connection();
    vTaskDelay(7000 / portTICK_PERIOD_MS);

    printf("WIFI was initiated ...........\n\n");
    printf("Start client:\n\n");
    esp_efuse_mac_get_default(mac_uni_base);
    sprintf(dir_mac,"%02X:%02X:%02X:%02X:%02X:%02X", mac_uni_base[0],mac_uni_base[1],mac_uni_base[2],mac_uni_base[3],mac_uni_base[4],mac_uni_base[5]);
    printf("Direccion MAC:%s\n", dir_mac);
    esp_wifi_disconnect();
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    uint64_t millis = 0;
    vTaskDelay(500 / portTICK_PERIOD_MS);
    printf("\n\n\n");
    ESP_LOGW(TAG," ---- COMEDERO MENSOFT ----");
    ESP_LOGW(TAG," ---- MAC %s ----", dir_mac);
    ESP_LOGW(TAG," ---- Tiempo de envio %u ---- \n\n", time_to_send);

    
    esp_wifi_connect();
    initialize_sntp(); // CONEXION NTP Y ACTUALIZAS FECHA Y HORA A RTC
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    time_t now;
    struct tm timeinfo;

    // esperar hasta que el sistema tenga hora válida de NTP
    time(&now);
    localtime_r(&now, &timeinfo);
    if ((timeinfo.tm_year + 1900) != 1970) {
        if (ds3231_set_time(&timeinfo) == ESP_OK) {
                ESP_LOGI(TAG, "RTC actualizado con hora NTP");
            } else {
                ESP_LOGE(TAG, "Error al actualizar RTC");
            }
    } else {
        ESP_LOGE(TAG, "Hora NTP no válida");
    }
    mqtt_app_start();
    ESP_LOGI(TAG, "mqtt: OK");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    if (fin_dataTosend == TRUE){
        fin_dataTosend = FALSE;
    }
    esp_mqtt_client_stop(client);
    esp_wifi_disconnect();
    for (;;)
    {
        read_bocas();
        manejar_eventos();
        if ((esp_timer_get_time() - millis) > (SEG*60*time_to_send))//(SEG*60*20) para 20 min
        {
            sendData = TRUE;
            millis = esp_timer_get_time();
        }
        if (sendData == TRUE && pigbuf.full == FALSE)
        {
            send_data(1);
        }
        if(pigbuf.full == TRUE)
        {
            ESP_LOGW(TAG, "El buffer esta lleno");
        }
        
        
        
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

uint8_t send_data(uint8_t sendType)
{
    /*
    *   Funcion para el envio de datos. Realiza 5 intentos de conexion WIFI antes
    *   de intentar enviar. Evalua la respuesta despues de cada envio. Envios de 10 en 10.
    */

    static enum {CONNECT, WAIT, DATA, LIFE, DISCONNECT, RST} state = RST;
    static evento dataToSend;
    char tempChar[300];
    static uint64_t prevGetTime;
    static uint8_t intentos;

    switch (state)
    {
    case RST:
    {
        gpio_set_level(REDLED, 0);
        intentos = 0;
        state = CONNECT;
        break;
    }
    case CONNECT:
    {
        ESP_LOGI(TAG, "Conectando WIFI");
        esp_wifi_connect();
        prevGetTime = esp_timer_get_time();
        state = WAIT;
        break;
    }
    case WAIT:
    {
        if ((esp_timer_get_time() - prevGetTime) > (6*SEG))
        {
            ESP_LOGI(TAG,"ESPERA SUPERADA");
            if(estado_wifi == ON)
            {
                state = LIFE;
                intentos = 0;
            }
            if(estado_wifi == OFF && intentos <= 5){
                state = CONNECT;
                ESP_LOGW(TAG, "No se ha podido conectar. Ientento %u", intentos);
                vTaskDelay(2000 / portTICK_PERIOD_MS);
            }else if(intentos > 5){
                state = DISCONNECT;
                intentos = 0;
            }
            ESP_LOGI(TAG,"ESPERA SUPERADA");
            state = LIFE;
            
        }
        break;
    }
    case LIFE:
    { 
        ESP_LOGI(TAG,"LIFE");
        sntp_restart(); // ACTUALIZACION FECHA Y HORA EN RTC
        time_t now;
        struct tm timeinfo;
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        // esperar hasta que el sistema tenga hora válida de NTP
        time(&now);
        localtime_r(&now, &timeinfo);

        if ((timeinfo.tm_year + 1900) != 1970) {
            if (ds3231_set_time(&timeinfo) == ESP_OK) {
                    ESP_LOGI(TAG, "RTC actualizado con hora NTP");
                } else {
                    ESP_LOGE(TAG, "Error al actualizar RTC");
                }
        } else {
            ESP_LOGE(TAG, "Hora NTP no válida");
        }
        state = DATA;
        vTaskDelay(200 / portTICK_PERIOD_MS);
        break;
    }
    case DATA:
    {
        uint16_t tam_buff = size_buffer();
        uint8_t checksum = 0;
        bool response;
        for (uint8_t i = 0; i < SEND_BUFFER_SIZE; i++)
        {

                mqtt_app_start();
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                ESP_LOGI(TAG, "mqtt: OK");
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                esp_mqtt_client_stop(client);
                ESP_LOGI(TAG, "intento: %d", i);
                if (fin_dataTosend == TRUE){
                    fin_dataTosend = FALSE;
                    break;
                }
                if (error_mqtt)
                {
                    error_mqtt=false;
                    break;
                }
        }
        if (tam_buff > SEND_BUFFER_SIZE)
        {
            tam_buff = size_buffer();
            ESP_LOGW(TAG, "buffer: %u", tam_buff);
            state = DATA;
        }
        else
        {
            state = DISCONNECT;
        }
        break;
    }
    case DISCONNECT:
    {
        esp_wifi_disconnect();
        sendData = FALSE;
        state = RST;
        gpio_set_level(REDLED, 1);
        break;
    }
    default:{}
    }
    return ESP_OK;
}

uint8_t read_bocas()
{
    /*
    *   Funcion que lee los pines correspondientes a cada boca del comedero
    *   Si se activa una boca, pone en TRUE la posicion del array correspondiente.
    *
    *   - Presencia_ok: funcion para leer el sensor infrarrojo (De Dani)
    *   - read_ultrasonic_sensor: funcion para leer el sensor de ultrasonidos (De Carmen).
    *   - gpio_get_level: funcion para detectar presencia con una boca a GND normal
    *
    *   ¡¡¡¡¡IMPORTANTE!!!!! 
    *   - Para bocas if (leerBoca == 1)
    *   - Para Sensores if (leerBoca == 0)
    */

    static uint8_t prev_leerBoca;
    static uint32_t millis = 0;

    if (esp_timer_get_time() - millis > MICROSEG * DEBOUNCE_TIME)
    {
        uint8_t leerBoca;
        leerBoca = gpio_get_level(SENSE_2);
        
            if (leerBoca != prev_leerBoca)
            {
                if (leerBoca == 0) //Poner a 1 para leer bocas a GND, a 0 para el resto
                {
                    presencia = TRUE;
                }
                else
                {
                    presencia = FALSE;
                }
            }
            prev_leerBoca = leerBoca;
        
        millis = esp_timer_get_time();
    }

    return ESP_OK;
}

esp_err_t config_pin(void)
{
    // Configurar outputs
    gpio_set_direction(REDLED, GPIO_MODE_OUTPUT);
    gpio_set_direction(WHITELED, GPIO_MODE_OUTPUT);
    

    // Configurar inputs
    gpio_set_direction(SENSE_2, GPIO_MODE_INPUT);
    //gpio_set_direction(SENSE_3, GPIO_MODE_INPUT);

    return ESP_OK;
}

uint8_t manejar_eventos()
{
    /*
    *   Funcion que relaciona las bocas activadas con sus correspondientes ID
    *   y cuenta el tiempo que permanecen las bocas activadas.
    *   Almacena el evento en el buffer si tiene ID.
    */
    static bool prevPresencia;
    static bool bocasActivas;
    static bool prevBocasActivas;
    static bool bocasSinId;
    static bool reset = TRUE;
    static evento registro;

    if (reset)
    {
        prevPresencia = FALSE;
        bocasActivas = FALSE;
        bocasSinId = FALSE;
        reset = FALSE;
    }

    if ((presencia == TRUE) && (prevPresencia == FALSE))
    {
        gpio_set_level(WHITELED, 0);
        time_t tt = time(NULL);
        data = *gmtime(&tt);
        bocasSinId = TRUE;
        bocasActivas = TRUE;
        registro.idComedero = ID_COMEDERO;
        registro.timeStamp = tt;
        registro.segComiendo = 0;
        registro.idAnimal = 0;
        registro.tm_year = 0;
        registro.tm_mon = 0;
        registro.tm_mday = 0;
        registro.tm_hour = 0;
        registro.tm_min = 0;
        registro.tm_sec = 0;

        
        ESP_LOGI(TAG, "Boca activada\n");
    }

    if ((presencia == FALSE) && (prevPresencia == TRUE))
    {
        ESP_LOGI(TAG, "Boca Desactivada");

        gpio_set_level(WHITELED, 1);
        bocasActivas = FALSE;
    }
    prevPresencia = presencia;

    reset = TRUE;

    if ((bocasActivas == FALSE) && (prevBocasActivas == TRUE))
    {
        time_t tt = time(NULL);
        data = *gmtime(&tt);
        registro.segComiendo = tt - registro.timeStamp;
        // OBTENER FECHA Y HORA RTC
        struct tm now = {0};

        if (ds3231_get_time(&now) == ESP_OK) {
            // Llenás tu estructura "registro" con lo mismo que hacías antes
            registro.tm_year = (now.tm_year + 1900) % 100;  // Año 2 dígitos
            registro.tm_mon  = now.tm_mon + 1;              // Mes
            registro.tm_mday = now.tm_mday;                 // Día
            registro.tm_hour = now.tm_hour;                 // Hora
            registro.tm_min  = now.tm_min;                  // Min
            registro.tm_sec  = now.tm_sec;                  // Sec
        }

        ESP_LOGI(TAG, "Evento: facha:%02d-%02d-%02d %02d:%02d:%02d ID:%u SegC:%lu Crotal:%lld", registro.tm_mday, registro.tm_mon, registro.tm_year,
                    registro.tm_hour, registro.tm_min, registro.tm_sec,ID_COMEDERO, registro.segComiendo, registro.idAnimal);

        if (sendData == FALSE)
        {
        }


        if ((registro.segComiendo < 10000) && (registro.segComiendo != 0))
        {
            if(insertar_buffer(registro) == OK)
            {
                ESP_LOGI(TAG, "Evento insertado en el buffer");
            }else
            {
                ESP_LOGW(TAG, "No se ha podido insertar el evento");
            }
        }
    }
    prevBocasActivas = bocasActivas;


    if(bocasActivas)
    {
        reset = FALSE;
    }
    

    return ESP_OK;
}
//Funciones de buffer
int8_t insertar_buffer(evento dato)
{
    if(pigbuf.full == FALSE)
    {
        registro_mem[pigbuf.head] = dato;
        avanzar_cabeza();
        return OK;
    }else
    {
        return ERROR;
    }
    
}

inline void avanzar_cabeza()
{
    pigbuf.head ++;
    if (pigbuf.head == NUM_REGISTROS)
    {
        pigbuf.head = 0;
    }
    pigbuf.full = (pigbuf.head == pigbuf.tail) ? TRUE : FALSE;
}

inline void avanzar_cola()
{
    pigbuf.tail ++;
    pigbuf.full = FALSE;
    if (pigbuf.tail == NUM_REGISTROS)
    {
        pigbuf.tail = 0;
    }
    
}

int8_t recoger_buffer(evento * data)
{
    if(!buffer_vacio())
    {
        *data = registro_mem[pigbuf.tail];
        avanzar_cola();
        return OK;
    }else
    {
        return ERROR;
    }
}

int8_t buffer_vacio()
{
    if ((pigbuf.full == FALSE) && (pigbuf.head == pigbuf.tail))
    {
        return TRUE;
    }
    return FALSE;
}

uint16_t size_buffer()
{
    uint16_t size;

    if(pigbuf.head >= pigbuf.tail)
    {
        size = pigbuf.head - pigbuf.tail;
    }else{
        size = NUM_REGISTROS + pigbuf.head - pigbuf.tail;
    }
    return size;
}

// wifi 
static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    /*
    *   Control de Conexion de WIFI.
    */
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
        estado_wifi = OFF;
        break;
    case IP_EVENT_STA_GOT_IP:
        printf("WiFi got IP ... \n\n");
        estado_wifi = ON;
        break;
    default:
        break;
    }
}

void wifi_connection()
{
    /*
    *   Inicializa la configuracion WIFI.
    */
    
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_initiation);

    // 2 - Wi-Fi Configuration Phase
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    wifi_config_t wifi_configuration = {
        .sta = {
            .ssid = SSID,
            .password = PASWORD}};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);

    // 3 - Wi-Fi Start Phase
    esp_wifi_start();

    // 4- Wi-Fi Connect Phase
    esp_wifi_connect();
}

//mqtt

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    static evento dataToSend;
    
    ESP_LOGI(TAG, "INSIDE_HANDLER_MQTT");
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        if (recoger_buffer(&dataToSend) == OK)
            {
                char trama [200];
                char dir [100];
                snprintf(dir, sizeof(dir), "mensoft_silos/%s/feeders", dir_mac);
                snprintf(trama, sizeof(trama), "%02d%02d%02d%02d%02d%02d/%lu/%llu", dataToSend.tm_mday, dataToSend.tm_mon, dataToSend.tm_year,
                        dataToSend.tm_hour, dataToSend.tm_min, dataToSend.tm_sec, dataToSend.segComiendo, dataToSend.idAnimal);
                ESP_LOGI(TAG, "trama:%s",trama);
                msg_id = esp_mqtt_client_publish(client, dir, trama, 0, 1, 0);
                ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
                msg_id = esp_mqtt_client_subscribe(client, dir, 1);
                ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
                guardar_en_sd(dataToSend);
                fin_dataTosend = FALSE;
            }else{
                char dir2 [100];
                char trama2 [200];
                snprintf(dir2, sizeof(dir2), "mensoft_silos/%s/life", dir_mac);
                struct tm now;
                struct tm timeinfo = {0};
                if(timeinfo.tm_year < (2025 - 1900)) {
                    ESP_LOGI(TAG, "Waiting for system time...");
                    vTaskDelay(2000 / portTICK_PERIOD_MS);
                    time(&now);
                    localtime_r(&now, &timeinfo);
                }
                snprintf(trama2, sizeof(trama2), "%02d%02d%02d%02d%02d%02d", timeinfo.tm_mday, (timeinfo.tm_mon + 1), (timeinfo.tm_year + 1900)%100,
                        timeinfo.tm_hour + 2, timeinfo.tm_min, timeinfo.tm_sec);
                msg_id = esp_mqtt_client_publish(client, dir2, trama2, 0, 1, 0);
                ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
                msg_id = esp_mqtt_client_subscribe(client, dir2, 0);
                ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
                fin_dataTosend = TRUE;
            }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d, return code=0x%02x ", event->msg_id, (uint8_t)*event->data);
        //msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        int msg_id = -1;
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
        error_mqtt=true;
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
    //.broker.address.uri = CONFIG_BROKER_URL,
    .broker.address.uri = "mqtt://146.59.252.144:8082",
    .credentials.username = "jordi",
    .credentials.authentication.password = "men33",
    .session.keepalive = 30,
    .session.disable_clean_session = false
};
    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    ESP_LOGI(TAG, "mqtt init ok");
}

//sdcard
void guardar_en_sd(evento data) {
    struct tm now;
    float temperature;
    char tempChar[300];
    sprintf(tempChar, "hour;%02d;min;%02d;seg;%02d;mac;%s;dateDevice;%lu;seconds;%lu;identifier;%llu", 
            data.tm_hour, data.tm_min, data.tm_sec, dir_mac, data.timeStamp, data.segComiendo, data.idAnimal);
    char nombre[200];char path[405];char path2[200];
    memset(nombre, 0, sizeof(nombre));
    sprintf(nombre, "%02d", data.tm_year);  
    if (buscar_directorio(MOUNT_POINT, nombre, true) == FALSE)
    {
        memset(path, 0, sizeof(path));
        snprintf(path, sizeof(path), "%s/%s", MOUNT_POINT, nombre);
        crear_directorio(path);
        memset(path, 0, sizeof(path));
    }
    memset(nombre, 0, sizeof(nombre));
    sprintf(nombre, "%02d/%02d", data.tm_year, data.tm_mon );  
    if (buscar_directorio(MOUNT_POINT, nombre, true) == FALSE)
    {
        snprintf(path, sizeof(path), "%s/%s", MOUNT_POINT, nombre);
        crear_directorio(path);
        memset(path, 0, sizeof(path));
    }
    memset(nombre, 0, sizeof(nombre));
    sprintf(nombre, "%02d/%02d/%02d", data.tm_year, data.tm_mon , data.tm_mday);  
    if (buscar_directorio(MOUNT_POINT, nombre, true) == FALSE)
    {
        snprintf(path, sizeof(path), "%s/%s", MOUNT_POINT, nombre);
        crear_directorio(path);
        memset(path, 0, sizeof(path));
    }
    memset(nombre, 0, sizeof(nombre));
    sprintf(path2, "%s/%02d/%02d/%02d/", MOUNT_POINT, data.tm_year, data.tm_mon , data.tm_mday);  
    sprintf(nombre, "%02d.CSV", data.tm_hour);  
    printf("a rut:%s y %s\n", path2, nombre);

    if (buscar_archivo(path2, nombre) == FALSE)
    {
        memset(path, 0, sizeof(path));
        snprintf(path, sizeof(path), "%s%s", path2, nombre);
        crear_archivo(path);
        memset(path, 0, sizeof(path));
    }
    memset(path, 0, sizeof(path));
    snprintf(path, sizeof(path), "%s%s", path2, nombre);
    printf("a rut:%s\n", path);
    save_sd(tempChar, path);
}
static esp_err_t initi_sd_card(void)
{  
    esp_err_t ret;

    //ESP_LOGI(TAG, "Inicializando SD por SPI...");
    printf("Inicializando SD por SPI...\n\n");
    gpio_reset_pin(PWR_SD);
    gpio_set_direction(PWR_SD, GPIO_MODE_OUTPUT);
    gpio_set_level(PWR_SD, 0);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    // Configurar SPI host
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST; // o SPI2_HOST si usás HSPI

    // Configurar pines SPI
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, 1);
    if (ret != ESP_OK) {
        //ESP_LOGE(TAG, "Fallo al inicializar el bus SPI: %s", esp_err_to_name(ret));
        printf("Fallo al inicializar el bus SPI: %s\n", esp_err_to_name(ret));
        return ret;
    }

    // Configurar CS
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    // Montar la tarjeta
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        //ESP_LOGE(TAG, "Fallo al montar la tarjeta SD: %s", esp_err_to_name(ret));
        printf("Fallo al montar la tarjeta SD: %s\n", esp_err_to_name(ret));
        return ret;
    }

    //ESP_LOGI(TAG, "Tarjeta montada correctamente. Tipo: %d", card->ocr);
    printf("Tarjeta montada correctamente. Tipo: %ld\n", card->ocr);
    return ret;
}
static bool save_sd(const char *arg, const char *dir)
{
    // Crear archivo y escribir en modo append
    FILE *f = fopen(dir, "a");  // crea si no existe
    if (f == NULL) {
        ESP_LOGE(TAG, "No se pudo abrir archivo para escribir");
        return false;
    }

    fprintf(f, "arg: %s\n", arg);
    fclose(f);
    ESP_LOGI(TAG, "Archivo escrito exitosamente");
    return true;
}
static bool buscar_directorio(const char *ruta, const char *nombre_buscado, bool buscar_directorio)
{
    DIR *dir = opendir(ruta);
    if (dir == NULL) {
        printf("No se pudo abrir el directorio: %s\n", ruta);
        return false;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (buscar_directorio && entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, nombre_buscado) == 0) {
                closedir(dir);
                return true;
            }
        }
        if (!buscar_directorio && entry->d_type == DT_REG) {
            if (strcmp(entry->d_name, nombre_buscado) == 0) {
                closedir(dir);
                return true;
            }
        }
    }

    closedir(dir);
    return false;
}
static bool buscar_archivo(const char *ruta, const char *nombre_archivo) {
    DIR *dir = opendir(ruta);
    if (dir == NULL) {
        printf("No se pudo abrir el directorio: %s\n", ruta);
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            printf("Comparando: '%s' con '%s'\n", entry->d_name, nombre_archivo);  // <-- AGREGAR ESTO
            if (strcmp(entry->d_name, nombre_archivo) == 0) {
                closedir(dir);
                return true;
            }
        }
    }

    closedir(dir);
    return false;
}
static bool crear_directorio(const char *dir)
{
    // Crear directorio
    int ret = mkdir(dir, 0777);
    if (ret != 0) {
        if (errno == EEXIST) {
            ESP_LOGI(TAG, "El directorio   existe: %s", dir);
            return true;
        } else {
            ESP_LOGE(TAG, "No se pudo crear el directorio: %s (errno: %d - %s)", 
                     dir, errno, strerror(errno));
            return false;
        }
    } else {
        ESP_LOGI(TAG, "Directorio creado exitosamente: %s", dir);
        return true;
    }
}
static bool crear_archivo(const char *file)
{
    char ruta_completa[128]; 
    int n = snprintf(ruta_completa, sizeof(ruta_completa), "%s", file);
    
    if (n < 0 || n >= sizeof(ruta_completa)) {
        ESP_LOGE(TAG, "Ruta del archivo demasiado larga");
        return false;
    }

    FILE *f = fopen(ruta_completa, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "No se pudo crear el archivo: %s", ruta_completa);
        return false;
    }

    fprintf(f, "Archivo creado exitosamente\n");
    fclose(f);
    ESP_LOGI(TAG, "Archivo creado: %s", ruta_completa);
    return true;
}

// ntp 
void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

// RTC DS3231
static uint8_t bcd2dec(uint8_t val) {
    return ((val >> 4) * 10 + (val & 0x0F));
}
static uint8_t dec2bcd(uint8_t val) {
    return ((val / 10) << 4) | (val % 10);
}
static void i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}
esp_err_t ds3231_set_time(struct tm *dt) {
    uint8_t data[7];
    data[0] = dec2bcd(dt->tm_sec);
    data[1] = dec2bcd(dt->tm_min);
    data[2] = dec2bcd(dt->tm_hour + 2);                // Ajuste de zona horaria ESPAÑA VERANO
    data[3] = dec2bcd(dt->tm_wday ? dt->tm_wday : 1);   // si tm_wday==0 poner 1
    data[4] = dec2bcd(dt->tm_mday);
    data[5] = dec2bcd(dt->tm_mon + 1);                  // tm_mon 0–11
    data[6] = dec2bcd((dt->tm_year + 1900) % 100);      // Año 2 dígitos

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true);             // empezar en reg 0x00
    i2c_master_write(cmd, data, 7, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd,
                                         1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    return ret;
}
// --- Leer hora y fecha ---
esp_err_t ds3231_get_time(struct tm *dt) {
    uint8_t data[7];
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true); // registro segundos
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 6, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, data + 6, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        dt->tm_sec  = bcd2dec(data[0] & 0x7F);
        dt->tm_min  = bcd2dec(data[1]);
        dt->tm_hour = bcd2dec(data[2] & 0x3F);
        dt->tm_mday = bcd2dec(data[4]);
        dt->tm_mon  = bcd2dec(data[5] & 0x1F) - 1;   // struct tm cuenta meses 0–11
        dt->tm_year = bcd2dec(data[6]) + 100;       // años desde 1900 → 2000+X
        dt->tm_wday = bcd2dec(data[3]);             // día de la semana (1–7)
        dt->tm_isdst = -1;                          // sin horario de verano
    }
    return ret;
}