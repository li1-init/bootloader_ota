#include "stm32f10x.h"                  // Device header

#include "esp_at.h"
#include "esp_usart.h"
#include "esp_mqtt.h" 
#include "esp_wifi.h" 
#include "my_usart.h"
#include "usart_command.h"

/* WIFI名称 */
static const char *wifi_ssid = "qwe";
/* WIFI密码 */
static const char *wifi_password = "12345678911";

void wifi_int(void)
{
    tell("Init ESP32...");
    if (!esp_at_init())
    {
        tell("Init ESP32 Failed!!!");
        while (1)
            ;
    }

    tell("Init WIFI...");
    if (!esp_at_wifi_init())
    {
        tell("Init WIFI Failed!!!");
        while (1)
            ;
    }

    tell("Connect WIFI...");
    if (!esp_at_wifi_connect(wifi_ssid, wifi_password))
    {
        tell("Connect WIFI Failed!!!");
        while (1)
            ;
    }

    tell("Success!!!");
}
