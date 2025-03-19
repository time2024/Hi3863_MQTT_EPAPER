#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmsis_os2.h"
#include "MQTTClient.h"
#include "MQTTClientPersistence.h"
#include "osal_debug.h"
#include "hal_gpio.h"
#include "los_memory.h"
#include "los_task.h"
#include "soc_osal.h"
#include "app_init.h"
#include "common_def.h"
#include "wifi_connect.h"
#include "watchdog.h"
#include "cjson_demo.h"
#include "mqtt_demo.h"
#include "pinctrl.h"
#include "gpio.h"
#include <stdarg.h>
#include "cJSON.h"
#include "./GUI/GUI_Paint.h"
#include "epaper.h"
#include "spi.h"
#include "stdio.h"

/*************************** 配置参数 ********************************/
#define ADDRESS "a8f1044001.st1.iotda-device.cn-north-4.myhuaweicloud.com" // 华为云IoT平台地址
#define CLIENTID "67caafdd8e04aa0690bb1b62_1111_0_0_2025030803"            // 设备客户端ID
#define TOPIC "MQTT Examples"                                              // 默认主题
#define PAYLOAD "Hello World!"                                             // 负载
#define QOS 1                                                              // 服务质量等级

/* WiFi连接配置 */
#define CONFIG_WIFI_SSID "HUAWEI Pura 70 Pro+" // 要连接的WiFi 热点账号
#define CONFIG_WIFI_PWD "12345678"             // 要连接的WiFi 热点密码

/* 任务配置 */
#define MQTT_STA_TASK_PRIO 24           // MQTT任务优先级
#define MQTT_STA_TASK_STACK_SIZE 0x8000 // 任务栈大小
#define TIMEOUT 10000L                  // 超时时间
#define MSG_MAX_LEN 16384               // 消息最大长度
#define MSG_QUEUE_SIZE 4                // 消息队列容量

/* spi配置 */
#define SPI_WAIT_CYCLES 0x10

/*************************** 全局变量 ********************************/
static unsigned long g_msg_queue;                                                      // 消息队列句柄
volatile MQTTClient_deliveryToken deliveredtoken;                                      // 消息传递令牌
char *g_username = "67caafdd8e04aa0690bb1b62_1111";                                    // deviceId or nodeId              // 设备用户名
char *g_password = "0c0a742a5c3d8d41afa096ed0d53aaffeb5ce99663bb9fe8994d6d37d24def04"; // 设备密码
MQTTClient client;                                                                     // MQTT客户端实例
extern int MQTTClient_init(void);

/*************************** 函数声明 ********************************/
// spi初始化
static void app_spi_init_pin(void)
{
    uapi_pin_set_mode(CONFIG_EPD_RST_PIN, PIN_MODE_0);
    uapi_gpio_set_dir(CONFIG_EPD_RST_PIN, GPIO_DIRECTION_OUTPUT);

    uapi_pin_set_mode(CONFIG_EPD_DC_PIN, PIN_MODE_0);
    uapi_gpio_set_dir(CONFIG_EPD_DC_PIN, GPIO_DIRECTION_OUTPUT);

    uapi_pin_set_mode(CONFIG_EPD_CS_PIN, PIN_MODE_0);
    uapi_gpio_set_dir(CONFIG_EPD_CS_PIN, GPIO_DIRECTION_OUTPUT);

    uapi_pin_set_mode(CONFIG_EPD_PWR_PIN, PIN_MODE_3);
    uapi_gpio_set_dir(CONFIG_EPD_PWR_PIN, GPIO_DIRECTION_OUTPUT);

    uapi_pin_set_mode(CONFIG_EPD_BUSY_PIN, PIN_MODE_0);
    uapi_gpio_set_dir(CONFIG_EPD_BUSY_PIN, GPIO_DIRECTION_INPUT);

    uapi_pin_set_mode(CONFIG_EPD_DIN_PIN, PIN_MODE_3);
    uapi_pin_set_mode(CONFIG_EPD_CLK_PIN, PIN_MODE_3);
}

static void app_spi_master_init_config(void)
{
    spi_attr_t config = {0};
    spi_extra_attr_t ext_config = {0};

    config.is_slave = false;
    config.slave_num = 1;
    config.bus_clk = 2000000;
    config.freq_mhz = 2;
    config.clk_polarity = SPI_CFG_CLK_CPOL_0;
    config.clk_phase = SPI_CFG_CLK_CPHA_0;
    config.frame_format = SPI_CFG_FRAME_FORMAT_MOTOROLA_SPI;
    config.spi_frame_format = HAL_SPI_FRAME_FORMAT_STANDARD;
    config.frame_size = HAL_SPI_FRAME_SIZE_8;
    config.tmod = HAL_SPI_TRANS_MODE_TXRX;
    config.sste = SPI_CFG_SSTE_DISABLE;

    ext_config.qspi_param.inst_len = HAL_SPI_INST_LEN_8;
    ext_config.qspi_param.addr_len = HAL_SPI_ADDR_LEN_16;
    ext_config.qspi_param.wait_cycles = SPI_WAIT_CYCLES;
    uapi_spi_init(CONFIG_EPD_MASTER_BUS_ID, &config, &ext_config);
}

static void Sample_Enter(void)
{
    uapi_gpio_set_val(CONFIG_EPD_DC_PIN, GPIO_LEVEL_LOW);
    uapi_gpio_set_val(CONFIG_EPD_CS_PIN, GPIO_LEVEL_LOW);
    uapi_gpio_set_val(CONFIG_EPD_PWR_PIN, GPIO_LEVEL_HIGH);
    uapi_gpio_set_val(CONFIG_EPD_RST_PIN, GPIO_LEVEL_HIGH);
}

static void Sample_Exit(void)
{
    uapi_gpio_set_val(CONFIG_EPD_DC_PIN, GPIO_LEVEL_LOW);
    uapi_gpio_set_val(CONFIG_EPD_CS_PIN, GPIO_LEVEL_LOW);
    uapi_gpio_set_val(CONFIG_EPD_PWR_PIN, GPIO_LEVEL_LOW);
    uapi_gpio_set_val(CONFIG_EPD_RST_PIN, GPIO_LEVEL_LOW);

    uapi_spi_deinit(CONFIG_EPD_MASTER_BUS_ID); // 不使用SPI清除
}

// Base64解码表
const unsigned char base64_decode_table[256] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 0-15
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 16-31
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 62, 255, 255, 255, 63,   // 32-47
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 255, 255, 255, 255, 255, 255,           // 48-63
    255, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,                          // 64-79
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 255, 255, 255, 255, 255,            // 80-95
    255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,                // 96-111
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 255, 255, 255, 255, 255,            // 112-127
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 128-143
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 144-159
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 160-175
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 176-191
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 192-207
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 208-223
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 224-239
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255  // 240-255
};

// Base64解码函数
static uint16_t base64_decode(const char *encoded_data, unsigned char *decoded_data)
{
    uint16_t i = 0, j = 0;
    unsigned char char_array_4[4], char_array_3[3];
    uint16_t decoded_length = 0;

    while (encoded_data[i] != '\0')
    {
        // 跳过非Base64字符
        if (encoded_data[i] == '=' || base64_decode_table[(unsigned char)encoded_data[i]] == 255)
        {
            i++;
            continue;
        }

        char_array_4[j++] = encoded_data[i++];
        if (j == 4)
        {
            for (uint16_t k = 0; k < 4; k++)
            {
                char_array_4[k] = base64_decode_table[(unsigned char)char_array_4[k]];
            }

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0x0f) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + char_array_4[3];

            for (uint16_t k = 0; k < 3; k++)
            {
                decoded_data[decoded_length++] = char_array_3[k];
            }
            j = 0;
        }
    }

    if (j)
    {
        for (uint16_t k = 0; k < j; k++)
        {
            char_array_4[k] = base64_decode_table[(unsigned char)char_array_4[k]];
        }

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0x0f) << 4) + ((char_array_4[2] & 0x3c) >> 2);

        for (uint16_t k = 0; k < j - 1; k++)
        {
            decoded_data[decoded_length++] = char_array_3[k];
        }
    }

    return decoded_length;
}

/**
 * @brief 消息送达确认回调函数
 * @param context 上下文指针
 * @param dt 传递令牌
 */
void delivered(void *context, MQTTClient_deliveryToken dt)
{
    unused(context);
    printf("Message with token value %d delivery confirmed\r\n", dt);
    deliveredtoken = dt;
}

/**
 * @brief 消息到达回调函数
 * @param context 上下文指针
 * @param topic_name 主题名称
 * @param topic_len 主题长度
 * @param message 消息结构体
 * @return 处理结果 1-成功, -1-失败
 */
int msgArrved(void *context, char *topic_name, int topic_len, MQTTClient_message *message)
{
    unused(context);
    unused(topic_len);

    MQTT_msg *receive_msg = osal_kmalloc(sizeof(MQTT_msg), 0);
    printf("mqtt_message_arrive() success, the topic is %s, the payload is %s \n", topic_name, message->payload);
    receive_msg->msg_type = EN_MSG_PARS;
    receive_msg->receive_payload = message->payload;
    uint32_t ret = osal_msg_queue_write_copy(g_msg_queue, receive_msg, sizeof(MQTT_msg), OSAL_WAIT_FOREVER);
    if (ret != 0)
    {
        printf("ret = %#x\r\n", ret);
        osal_kfree(receive_msg);
        return -1;
    }

    // 提取请求 ID（从主题中提取）
    char *request_id = strstr(topic_name, "request_id=");
    if (request_id == NULL)
    {
        printf("Failed to extract request_id from topic.\n");
        return -1;
    }
    request_id += strlen("request_id="); // 跳过 "request_id=" 前缀
    char *reup_topic = combine_strings(4, "$oc/devices/", g_username, "/sys/commands/response/request_id=", request_id);

    // 创建 JSON 对象
    cJSON *response_root = cJSON_CreateObject();
    cJSON_AddNumberToObject(response_root, "result_code", 0);
    cJSON_AddStringToObject(response_root, "response_name", "COMMAND_RESPONSE");
    cJSON *paras = cJSON_CreateObject();
    cJSON_AddStringToObject(paras, "result", "success");
    cJSON_AddItemToObject(response_root, "paras", paras);

    // 打印 JSON 字符串
    char *json_string = cJSON_Print(response_root);
    // 释放内存
    cJSON_Delete(response_root);

    // 返回命令执行结果
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc = 0;
    pubmsg.payload = json_string;
    pubmsg.payloadlen = (int)strlen(json_string);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    rc = MQTTClient_publishMessage(client, reup_topic, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS)
    {
        printf("mqtt_publish failed\r\n");
    }
    printf("mqtt_publish(), the payload is %s, the topic is %s\r\n", json_string, reup_topic);
    osal_kfree(json_string);
    json_string = NULL;

    osal_kfree(receive_msg);

    return 1;
}

/**
 * @brief 连接丢失回调函数
 * @param context 上下文指针
 * @param cause 断开原因
 */
void connlost(void *context, char *cause)
{
    unused(context);
    printf("mqtt_connection_lost() error, cause: %s\n", cause);
}

/**
 * @brief 订阅指定主题
 * @param topic 要订阅的主题
 * @return 0-成功, 其他-错误码
 */
int mqtt_subscribe(const char *topic)
{
    printf("subscribe start\r\n");
    MQTTClient_subscribe(client, topic, QOS);
    return 0;
}

/**
 * @brief 发布消息到指定主题
 * @param topic 目标主题
 * @param report_msg 要上报的消息结构体
 * @return MQTT错误码
 */
int mqtt_publish(const char *topic, MQTT_msg *report_msg)
{
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc = 0;
    char *msg = make_json("message", report_msg->light);
    pubmsg.payload = msg;
    pubmsg.payloadlen = (int)strlen(msg);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;
    rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS)
    {
        printf("mqtt_publish failed\r\n");
    }
    printf("mqtt_publish(), the payload is %s, the topic is %s\r\n", msg, topic);
    osal_kfree(msg);
    msg = NULL;
    return rc;
}

/**
 * @brief 建立MQTT连接
 * @return MQTT错误码
 */
int mqtt_connect(void)
{
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;
    printf("start mqtt sync subscribe...\r\n");
    MQTTClient_init();
    MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 120; // 保持间隔为120秒，每120秒发送一次消息
    conn_opts.cleansession = 1;
    if (g_username != NULL)
    {
        conn_opts.username = g_username;
        conn_opts.password = g_password;
    }
    MQTTClient_setCallbacks(client, NULL, connlost, msgArrved, delivered);
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to connect, return code %d\n", rc);
        return -1;
    }
    printf("connect success\r\n");
    return rc;
}

/**
 * @brief MQTT主任务函数
 * @return 程序执行状态
 */
int mqtt_task(void)
{
    // 创建MQTT消息
    MQTT_msg *report_msg = osal_kmalloc(sizeof(MQTT_msg), 0);

    int ret = 0;
    uint32_t resize = sizeof(MQTT_msg); // 确保缓冲区大小与消息大小一致
    char *Message = NULL;

    // 连接wifi和MQTT
    wifi_connect(CONFIG_WIFI_SSID, CONFIG_WIFI_PWD);
    ret = mqtt_connect();
    if (ret != 0)
    {
        printf("connect failed, result %d\n", ret);
    }
    osal_msleep(1000); // 1000等待连接成功

    // 订阅主题
    char *cmd_topic = combine_strings(3, "$oc/devices/", g_username, "/sys/commands/#");
    ret = mqtt_subscribe(cmd_topic);
    if (ret < 0)
    {
        printf("subscribe topic error, result %d\n", ret);
    }

    // 属性上报主题
    char *report_topic = combine_strings(3, "$oc/devices/", g_username, "/sys/properties/report");

    // 墨水屏初始化
    app_spi_init_pin();
    app_spi_master_init_config();

    Sample_Enter();

    EPD_Init();
    EPD_display_NUM(EPD_WHITE);
    EPD_lut_GC();
    EPD_refresh();

    EPD_SendCommand(0x50);
    EPD_SendData(0x17);

    osal_mdelay(500);
    uint8_t *BlackImage;
    uint16_t Imagesize = ((EPD_WIDTH % 8 == 0) ? (EPD_WIDTH / 8) : (EPD_WIDTH / 8 + 1)) * EPD_HEIGHT;
    if ((BlackImage = (uint8_t *)malloc(Imagesize)) == NULL)
    {
        printf("Failed to apply for black memory...\r\n");
    }

    while (1)
    {
        // 阻塞式读取队列
        ret = osal_msg_queue_read_copy(g_msg_queue, report_msg, &resize, OSAL_WAIT_FOREVER);
        // printf("Have %d queues\n",osal_msg_queue_get_msg_num(g_msg_queue));
        printf("ret = %#x\r\n", ret); // 成功的话 ret==0，否则ret==-1
        if (ret != 0)
        {
            printf("queue_read ret = %#x\r\n", ret);
            osal_kfree(report_msg);
            break;
        }
        if (report_msg != NULL)
        {
            printf("report_msg->msg_type = %d, report_msg->light = %s\r\n", report_msg->msg_type, report_msg->light);
            switch (report_msg->msg_type)
            {
                // 是接收到的命令
            case EN_MSG_PARS:
                Message = parse_json(report_msg->receive_payload); // Message 是接收到的信息内容
                // printf("receive_message: %s\n",Message);
                // uapi_gpio_toggle(2);
                if (Message[0] == 'B' && Message[1] == 'a' && Message[2] == 's' && Message[3] == 'e' && Message[4] == '6' && Message[5] == '4')
                {
                    if (BlackImage != NULL)
                    {
                        printf("Paint_NewImage\r\n");
                        Paint_NewImage(BlackImage, EPD_WIDTH, EPD_HEIGHT, 270, WHITE);
                        Paint_Clear(WHITE);
                        printf("SelectImage:BlackImage\r\n");
                        Paint_SelectImage(BlackImage);
                        Paint_Clear(WHITE);

                        printf("Drawing:BlackImage\r\n");
                        // uint16_t length = write_cb_para->length - 6;

                        // 分配内存存储解码后的二进制数据
                        unsigned char *decoded_data = (unsigned char *)malloc(14400);
                        if (!decoded_data)
                        {
                            // 如果没分配到空间，则不处理
                            printf("\nERROR: unsigned char *decoded_data = (unsigned char *)malloc(14400);\r\n");
                        }
                        else
                        {
                            // 进行Base64解码
                            Message += 6;
                            uint16_t actual_decoded_length = base64_decode(Message, decoded_data);
                            Paint_DrawBitMap(decoded_data);
                            EPD_display(BlackImage);
                            EPD_lut_GC();
                            EPD_refresh();
                            if(decoded_data)
                            free(decoded_data);
                            osal_mdelay(2000);
                            // printf("Clear...\r\n");
                            // EPD_Clear();
                            // printf("Goto Sleep...\r\n");
                            // EPD_sleep();

                            // free(BlackImage);
                            // BlackImage = NULL;

                            // osal_mdelay(2000); // important, at least 2s
                            // printf("close 5V, Module enters 0 power consumption ...\r\n");

                            // Sample_Exit();
                        }
                    }
                }
                break;
                // 是要上报的属性
            case EN_MSG_REPORT:
                mqtt_publish(report_topic, report_msg);
                break;
            default:
                break;
            }
            // 这里不能释放掉，释放掉会出问题
            // osal_kfree(report_msg);
        }
        osal_msleep(1000); // 1000等待连接成功
    }
    return ret;
}

/**
 * @brief 测试双线程任务
 * @return 程序执行状态
 */
int test_task(void)
{
    // 创建MQTT消息
    MQTT_msg *mqtt_msg;
    // char *Light;
    mqtt_msg = osal_kmalloc(sizeof(MQTT_msg), 0);
    if (mqtt_msg == NULL)
    {
        printf("Memory allocation failed\r\n");
    }

    // gpio初始化
    uapi_pin_set_mode(2, PIN_MODE_0);
    uapi_gpio_set_dir(2, GPIO_DIRECTION_OUTPUT);

    // Hi3863E
    // uapi_pin_set_mode(7, PIN_MODE_0);
    // uapi_gpio_set_dir(7, GPIO_DIRECTION_OUTPUT);
    // uapi_pin_set_mode(9, PIN_MODE_0);
    // uapi_gpio_set_dir(9, GPIO_DIRECTION_OUTPUT);
    // uapi_pin_set_mode(11, PIN_MODE_0);
    // uapi_gpio_set_dir(11, GPIO_DIRECTION_OUTPUT);

    osal_msleep(10000); // 等待10秒，等主进程中网络连接初始化成功

    while (1)
    {
        // 读取本地属性
        // if (uapi_gpio_get_output_val(2))
        if (osal_msg_queue_get_msg_num(g_msg_queue)) // 如果队列里有信息就不要将属性增加到队列，避免造成
        {
        }
        else // 如果队列里没有信息就将属性增加到队列
        {
            if (uapi_gpio_get_output_val(2))
            {
                mqtt_msg->light = "ON";
            }
            else
            {
                mqtt_msg->light = "OFF";
            }
            mqtt_msg->msg_type = EN_MSG_REPORT;

            if (mqtt_msg != NULL)
            {

                printf("light= %s\r\n", mqtt_msg->light);

                uint32_t ret = osal_msg_queue_write_copy(g_msg_queue, mqtt_msg, sizeof(MQTT_msg), OSAL_WAIT_FOREVER);
                if (ret != 0)
                {
                    printf("ret = %#x\r\n", ret);
                    osal_kfree(mqtt_msg);
                    break;
                }
                // 这里可以释放掉，也可以不释放掉
                // osal_kfree(mqtt_msg);
            }
        }

        // 每十秒上报一次属性，不可快过主进程的读取速度（一条每秒）
        osal_msleep(10000);
    }
    return 1;
}

/**
 * @brief MQTT示例入口函数
 */
static void mqtt_sample_entry(void)
{
    uint32_t ret;
    uapi_watchdog_disable();
    ret = osal_msg_queue_create("name", MSG_QUEUE_SIZE, &g_msg_queue, 0, sizeof(MQTT_msg));
    if (ret != OSAL_SUCCESS)
    {
        printf("create queue failure!,error:%x\n", ret);
    }
    printf("create the queue success! queue_id = %d\n", g_msg_queue);

    osThreadAttr_t attr;

    attr.name = "task_handle";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = MQTT_STA_TASK_STACK_SIZE;
    attr.priority = MQTT_STA_TASK_PRIO;

    if (osThreadNew((osThreadFunc_t)mqtt_task, NULL, &attr) == NULL)
    {
        printf("Create task MQTT load fail.\r\n");
    }

    // attr.name = "send_to_web";
    // attr.priority = MQTT_STA_TASK_PRIO;

    // if (osThreadNew((osThreadFunc_t)test_task, NULL, &attr) == NULL)
    // {
    //     printf("Create task test load fail.\r\n");
    // }
}

/* Run the udp_client_sample_task. */
app_run(mqtt_sample_entry);