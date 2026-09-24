#include "stm32f10x.h"                  // Device header

#include "esp_at.h"
#include "esp_usart.h"
#include "esp_mqtt.h" 
#include "esp_wifi.h" 
#include "my_usart.h" 
#include "usart_command.h"


void mqtt_connect(void)
{
    tell("MQTT init");
    if (!esp_at_send_command("AT+MQTTUSERCFG=0,1,\"esp32c3_001\",\"\",\"\",0,0,\"\"", NULL, NULL, 1000))
    {
        tell("MQTT init failed");
        while (1)
        {};

    }

    tell("MQTT connect");
    if (!esp_at_send_command("AT+MQTTCONN=0,\"broker-cn.emqx.io\",1883,1", NULL, NULL, 4000))
    {
        tell("MQTT connect failed");
        while (1)
        {
        };
    }

    tell("MQTT sub stm32/cmd");
    if (!esp_at_send_command("AT+MQTTSUB=0,\"stm32/cmd\",1", NULL, NULL, 1000))
    {
        tell("MQTT sub stm32/cmd failed");
        while (1)
        {
        };
    }

    tell("MQTT sub stm32/cmd success");
}
