
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_event_loop.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "os.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/ledc.h"

/*=============================================================================================*/
/*========阿里云参数==========*/
/*
 aliyun_port：端口号（默认为1883）

 aliyun_host:为ProductKey+".iot-as-mqtt"+{地区region}

 aliyun_username:DeviceName+ProductKey

 aliyun_clientid:ProductKey+"."+DeviceName+"|"+"连接模式securemode"+"加密方式"+"时间戳"
 
 aliyun_password:阿里云配置为密钥模式可见
*/

#define SSid "iPhone"   //路由器名字
#define Password "1234567890"  //密码


#define aliyun_port 1883
#define aliyun_host "a1qPbGR8UOq.iot-as-mqtt.cn-shanghai.aliyuncs.com"
#define aliyun_username "test&a1qPbGR8UOq"
#define aliyun_clientid "a1qPbGR8UOq.test|securemode=2,signmethod=hmacsha256,timestamp=1653120433227|"
#define aliyun_passwd "c7befa8fbccc9cf50b66b7807c5f4ef0fcf8b6e60d93467c328f4317e61a0492"
/*=============================================================================================*/
/*全局变量*/
cJSON *root; 
cJSON *next;
char *data; 
char *TAG = "demo";
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
int led_flag = 0;
esp_mqtt_client_config_t mqtt_cfg = {
        .host = aliyun_host,//MQTT 地址
		.port = 1883,   //MQTT端口
		.username = aliyun_username,//用户名
		.password = aliyun_passwd,//密码
        .client_id = aliyun_clientid,
    };

cJSON *data_json;
esp_mqtt_client_handle_t client;


/*=============================================================================================*/

//初始化MQTT
/*Cjson处理数据与阿里云交互*/
/*主要设置参数和topic*/
/*格式：DATA={"method":"topic,   //topic
"id":"1618765059",    //id号（不重要）
"params":{"temperature":32}, //设置参数
"version":"1.0.0"}*/   //不重要


/*阿里云JSON构造函数*/
/*参数 :第一个为需要上传构造的标识符 第二个是值*/
/*比较耗内存*/
/*使用的CJSON变量为Malloc动态分配 用完需要释放*/
static char* set_json_value(char*params_data,int value)
{
    root = cJSON_CreateObject(); //创建父级JSON对象
    next = cJSON_CreateObject(); //子级JSON对象
    /*构造JSON字符串*/
    /*树形结构 一级一级的构建*/
    cJSON_AddItemToObject(root,"meathod",cJSON_CreateString("/sys/a1hJQyR4PtB/test1/thing/event/property/post"));//替换成你自己的
    cJSON_AddItemToObject(root,"id",cJSON_CreateString("1618765059"));
    cJSON_AddItemToObject(root,"params",next);
    cJSON_AddItemToObject(next,params_data,cJSON_CreateNumber(value));
    cJSON_AddItemToObject(root,"version",cJSON_CreateString("1.0.0"));
    data = cJSON_Print(root);//输出为字符串
    
    cJSON_Delete(root);//父级对象释放了 无需释放next 否则大量内存入口找不到 导致内存紊乱
    
    return data;
    
}
/*阿里云Json解析*/

static int jSontoint(cJSON* cjson,char * str)
{
    int mode = 0;
    
    cJSON * cjson1 = cJSON_GetObjectItem(cjson,"params"); /*用来找params的json对象*/
    mode = cJSON_GetObjectItem(cjson1,str)->valueint; /*通过字符串mqtt_key找出对应的值并转为整数型*/
    cJSON_Delete(cjson);/*free*/
   // cJSON_Delete(cjson1);/*free*/
    
    return mode; 
}


/*wifi 事件回调*/
/*判断event_id*/
/*如果事件id为sta启动，则连接wifi  如果事件为获取IP地址 则将标志位BIT0写1 如果是断开事件 标志位清0*/
/*event_handle创建成功返回ESP_OK*/
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            
            break;
        default:
            break;
    }
    return ESP_OK;
}
//初始化wifi
void app_wifi_initialise(void)
{   
    nvs_flash_init(); //初始化nvs
    tcpip_adapter_init();//初始化tcp/ip
    wifi_event_group = xEventGroupCreate(); //创建wifi事件组
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL)); //事件循环初始化（传入wifi事件回调函数）
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();  //wifi使用初始化设置
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    /*使用wifi sta模式 作为节点连入路由器*/
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = SSid,
            .password = Password,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}
// 等待wifi成功连接
/*判断bit0 为1的话跳出 为0的话继续等待*/
void app_wifi_wait_connected()
{
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);

}

/*数据采集任务*/

/*MQTT事件处理*/
/*通过回调函数来到这里*/
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client; /*临时结构体 用于接收mqtt事件对象*/
    int msg_id;  /*事件id*/
    /*判断事件id*/
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED://连接MQTT成功
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            /*发起订阅topic0*/
            msg_id = esp_mqtt_client_subscribe(client, "/a1hJQyR4PtB/test1/user/get", 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
            /*发起订阅topic1*/
            msg_id = esp_mqtt_client_subscribe(client, "/sys/a1hJQyR4PtB/test1/thing/deviceinfo/update_reply", 1);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
            /*发起订阅topic2*/
            msg_id = esp_mqtt_client_subscribe(client, "/sys/a1hJQyR4PtB/test1/thing/service/property/set", 1);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

           
            break;
        case MQTT_EVENT_DISCONNECTED://断开MQTT
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
 
        case MQTT_EVENT_SUBSCRIBED://订阅成功
        	printf("_---------订阅--------\n");
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            msg_id = esp_mqtt_client_publish(client, "/World", "data", 0, 0, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED://取消订阅
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED://发布成功
        	printf("_--------发布----------\n");
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);

            break;
        case MQTT_EVENT_DATA://数据接收
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("主题长度:%d 数据长度:%d\n",event->topic_len,event->data_len);
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);

            led_flag = jSontoint(cJSON_Parse(event->data),"light_power");
            break;
        
        case MQTT_EVENT_ERROR://MQTT错误
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
           ESP_LOGI(TAG,"NO CONNECTION!!!!");
           break;  

            
            
    }
    return ESP_OK;
}
 
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
   
    mqtt_event_handler_cb(event_data);/*事件处理函数入口*/
    
    
    
    
}

void app_main(void)
{
    gpio_set_direction(2, GPIO_MODE_OUTPUT);
    gpio_set_level(2,0);
    app_wifi_initialise();//wifi初始化
    printf("\n______________等待wifi连接____________\n");
    app_wifi_wait_connected(); //等待WIFI连接成功
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    printf("\n______________wifi连接成功____________\n");
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);//注册事件
     
 
    esp_mqtt_client_start(client);//启动mQTT
    
                  
    while(1){
        
        if(led_flag==1)
        {
            gpio_set_level(2, 1);
        }
        else
        {
            gpio_set_level(2,0);
        }
    	
    }


}
