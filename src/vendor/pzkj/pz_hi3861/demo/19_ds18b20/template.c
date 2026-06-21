/**
 ****************************************************************************************************
 * @file        template.c
 ****************************************************************************************************
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ohos_init.h"
#include "cmsis_os2.h"

#include "bsp_ds18b20.h"
#include "bsp_led.h"
#include "bsp_mqtt.h"
#include "MQTTPacket.h"
#include "wifi_device.h"

#include "lwip/dhcp.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/netifapi.h"
#include "lwip/sockets.h"
#include <arpa/inet.h>

#include "hi_gpio.h"
#include "hi_io.h"
#include "hi_pwm.h"

#define WIFI_SSID "Mi11"
#define WIFI_PAWD "12345678"
#define SELECT_WLAN_PORT "wlan0"

#define DEVICE_ID "6a371d797f2e6c302f7d2beb_hi3861_AHT20"
#define MQTT_CLIENT_ID "6a371d797f2e6c302f7d2beb_hi3861_AHT20_0_1_2026062105"
#define MQTT_USER_NAME "6a371d797f2e6c302f7d2beb_hi3861_AHT20"
#define MQTT_PASS_WORD "826877e4f143393ceeb60c137ad0a13c163ba0bb26a127f5dd7e6a1739563ae6"
#define SERVER_IP_ADDR "117.78.5.125"
#define SERVER_IP_PORT 1883
#define MQTT_TOPIC_PUB_PROPERTIES "$oc/devices/%s/sys/properties/report"
#define MQTT_TOPIC_SUB_COMMANDS "$oc/devices/%s/sys/commands/#"
#define MQTT_TOPIC_PUB_COMMANDS_REQ "$oc/devices/%s/sys/commands/response/request_id=%s"
#define IOT_SERVICE_ID "AHT20"
#define MQTT_DATA_MAX 256
#define MQTT_REPORT_PERIOD_S 5
#define BEEP_MQTT_KEEPALIVE_S 60
#define BEEP_MQTT_REPORT_PERIOD_S 20

#define BEEP_DEVICE_ID "6a3514bd7f2e6c302f7cb245_hi3861beep1"
#define BEEP_MQTT_CLIENT_ID "6a3514bd7f2e6c302f7cb245_hi3861beep1_0_0_2026062100"
#define BEEP_MQTT_USER_NAME "6a3514bd7f2e6c302f7cb245_hi3861beep1"
#define BEEP_MQTT_PASS_WORD "aa0b3ce6326c3be8738d1c361f4521f372b4f680f6dd990c421903c5df19114d"
#define BEEP_SERVICE_ID "hi3861beep1"
#define BEEP_MQTT_BUFF_MAX 512

#define BEEP_PWM_PORT HI_PWM_PORT_PWM0
#define BEEP_FREQ 59259
#define BEEP_DUTY (BEEP_FREQ / 2)

static struct netif *g_lwip_netif = NULL;
static char g_ip_addr[20] = {0};
static float g_latest_temperature = 0.0f;
static float g_latest_humidity = 0.0f;
static int g_sensor_ready = 0;
static int g_aht20_mqtt_ready = 0;
static int g_beeper_ready = 0;
static int g_beeper_on = 0;

static osThreadId_t g_led_task_id;
static osThreadId_t g_wifi_task_id;
static osThreadId_t g_print_ip_task_id;
static osThreadId_t g_sensor_task_id;
static osThreadId_t g_mqtt_report_task_id;
static osThreadId_t g_beep_mqtt_task_id;

static void LED_Task(void)
{
    led_init();
    while (1) {
        LED(1);
        usleep(200 * 1000);
        LED(0);
        usleep(200 * 1000);
    }
}

static void led_task_create(void)
{
    osThreadAttr_t taskOptions = {0};
    taskOptions.name = "LEDTask";
    taskOptions.stack_size = 1024;
    taskOptions.priority = osPriorityNormal;
    g_led_task_id = osThreadNew((osThreadFunc_t)LED_Task, NULL, &taskOptions);
}

static void beeper_init(void)
{
    hi_u32 ret;

    hi_gpio_init();
    hi_io_set_func(HI_IO_NAME_GPIO_9, HI_IO_FUNC_GPIO_9_PWM0_OUT);
    hi_gpio_set_dir(HI_IO_NAME_GPIO_9, HI_GPIO_DIR_OUT);

    ret = hi_pwm_init(BEEP_PWM_PORT);
    if (ret != 0) {
        printf("[Beeper] PWM0 init failed, ret=0x%x.\r\n", ret);
        return;
    }

    hi_pwm_set_clock(PWM_CLK_160M);
    hi_pwm_stop(BEEP_PWM_PORT);
    g_beeper_ready = 1;
    g_beeper_on = 0;
    printf("[Beeper] PWM0 initialized, default OFF.\r\n");
}

static void beeper_set(int on)
{
    hi_u32 ret;

    if (!g_beeper_ready) {
        printf("[Beeper] Not ready.\r\n");
        return;
    }

    if (on) {
        ret = hi_pwm_start(BEEP_PWM_PORT, BEEP_DUTY, BEEP_FREQ);
        if (ret != 0) {
            printf("[Beeper] PWM0 start failed, ret=0x%x.\r\n", ret);
            return;
        }
        g_beeper_on = 1;
        printf("[Beeper] ON.\r\n");
    } else {
        ret = hi_pwm_stop(BEEP_PWM_PORT);
        if (ret != 0) {
            printf("[Beeper] PWM0 stop failed, ret=0x%x.\r\n", ret);
            return;
        }
        g_beeper_on = 0;
        printf("[Beeper] OFF.\r\n");
    }
}

static void AHT20_Task(void)
{
    while (1) {
        g_latest_temperature = ds18b20_gettemperture();
        g_latest_humidity = g_humidity;
        g_sensor_ready = 1;

        printf("Temp: %.2f C, Humidity: %.2f %%RH\r\n", g_latest_temperature, g_latest_humidity);
        sleep(1);
    }
}

static void aht20_task_create(void)
{
    osThreadAttr_t taskOptions = {0};
    taskOptions.name = "AHT20Task";
    taskOptions.stack_size = 2048;
    taskOptions.priority = osPriorityNormal;
    g_sensor_task_id = osThreadNew((osThreadFunc_t)AHT20_Task, NULL, &taskOptions);
    if (g_sensor_task_id == NULL) {
        printf("[AHT20] Task creation failed!\r\n");
    }
}

static void Wifi_Task(void)
{
    WifiErrorCode err;
    WifiDeviceConfig config = {0};
    int netId = -1;
    int timeout = 15;

    printf("[WiFi] Starting STA mode...\r\n");

    err = EnableWifi();
    if (err != WIFI_SUCCESS) {
        printf("[WiFi] EnableWifi failed, err=%d\r\n", err);
        return;
    }

    strcpy(config.ssid, WIFI_SSID);
    strcpy(config.preSharedKey, WIFI_PAWD);
    config.securityType = WIFI_SEC_TYPE_PSK;
    err = AddDeviceConfig(&config, &netId);
    if (err != WIFI_SUCCESS) {
        printf("[WiFi] AddDeviceConfig failed, err=%d\r\n", err);
        return;
    }

    err = ConnectTo(netId);
    if (err != WIFI_SUCCESS) {
        printf("[WiFi] ConnectTo failed, err=%d\r\n", err);
        return;
    }

    while (1) {
        WifiLinkedInfo info;
        err = GetLinkedInfo(&info);
        if (err == WIFI_SUCCESS && info.connState == WIFI_CONNECTED) {
            printf("[WiFi] Connected to AP.\r\n");
            break;
        }
        sleep(1);
    }

    g_lwip_netif = netifapi_netif_find(SELECT_WLAN_PORT);
    if (g_lwip_netif == NULL) {
        printf("[WiFi] netif find failed!\r\n");
        return;
    }

    dhcp_start(g_lwip_netif);
    while (timeout > 0) {
        if (dhcp_is_bound(g_lwip_netif) == ERR_OK) {
            ip4_addr_t ipAddr;
            ip4_addr_t netMask;
            ip4_addr_t gateWay;

            netifapi_netif_get_addr(g_lwip_netif, &ipAddr, &netMask, &gateWay);
            inet_ntop(AF_INET, &ipAddr, g_ip_addr, sizeof(g_ip_addr));
            printf("[WiFi] Got IP: %s\r\n", g_ip_addr);
            break;
        }
        printf(".");
        sleep(1);
        timeout--;
    }

    if (timeout <= 0) {
        printf("[WiFi] DHCP timeout!\r\n");
    }

    while (1) {
        sleep(10);
    }
}

static void wifi_task_create(void)
{
    osThreadAttr_t taskOptions = {0};
    taskOptions.name = "WifiTask";
    taskOptions.stack_size = 4096;
    taskOptions.priority = osPriorityNormal;
    g_wifi_task_id = osThreadNew((osThreadFunc_t)Wifi_Task, NULL, &taskOptions);
    if (g_wifi_task_id == NULL) {
        printf("[WiFi] Task creation failed!\r\n");
    }
}

static void PrintIP_Task(void)
{
    while (1) {
        if (strlen(g_ip_addr) > 0) {
            printf("[IP Task] Current IP: %s\r\n", g_ip_addr);
        } else {
            printf("[IP Task] IP not ready yet.\r\n");
        }
        osDelay(5000);
    }
}

static void print_ip_task_create(void)
{
    osThreadAttr_t taskOptions = {0};
    taskOptions.name = "PrintIPTask";
    taskOptions.stack_size = 2048;
    taskOptions.priority = osPriorityNormal;
    g_print_ip_task_id = osThreadNew((osThreadFunc_t)PrintIP_Task, NULL, &taskOptions);
    if (g_print_ip_task_id == NULL) {
        printf("[IP Task] Creation failed!\r\n");
    }
}

static int build_property_report(char *topic, int topic_len, unsigned char *payload, int payload_len)
{
    int topic_ret = snprintf(topic, topic_len, MQTT_TOPIC_PUB_PROPERTIES, DEVICE_ID);
    int payload_ret = snprintf((char *)payload, payload_len,
                               "{\"services\":[{\"service_id\":\"%s\",\"properties\":{"
                               "\"temperature\":%.2f,\"humidity\":%.2f,\"beep\":\"%s\"}}]}",
                               IOT_SERVICE_ID, g_latest_temperature, g_latest_humidity,
                               g_beeper_on ? "ON" : "OFF");

    if (topic_ret <= 0 || topic_ret >= topic_len || payload_ret <= 0 || payload_ret >= payload_len) {
        return -1;
    }

    return payload_ret;
}

static int get_request_id(const char *topic, char *request_id, int request_id_len)
{
    const char *key = "request_id=";
    char *pos = strstr(topic, key);

    if (pos == NULL || request_id == NULL || request_id_len <= 0) {
        return -1;
    }

    pos += strlen(key);
    snprintf(request_id, request_id_len, "%s", pos);
    return 0;
}

static int parse_beep_command(const char *payload)
{
    if (payload == NULL) {
        return -1;
    }

    if (strstr(payload, "command_name") == NULL) {
        return -1;
    }

    if (strstr(payload, "beep") == NULL && strstr(payload, "controlBeeper") == NULL) {
        return -1;
    }

    if (strstr(payload, "ON") != NULL) {
        beeper_set(1);
        return 0;
    }

    if (strstr(payload, "OFF") != NULL) {
        beeper_set(0);
        return 0;
    }

    return -1;
}

static void send_command_response(const char *request_id, int result_code)
{
    char topic[MQTT_DATA_MAX] = {0};
    char payload[32] = {0};

    if (request_id == NULL || strlen(request_id) == 0) {
        return;
    }

    snprintf(topic, sizeof(topic), MQTT_TOPIC_PUB_COMMANDS_REQ, DEVICE_ID, request_id);
    snprintf(payload, sizeof(payload), "{\"result_code\":%d}", result_code);
    MQTTClient_pub(topic, (unsigned char *)payload, strlen(payload));
}

static int8_t mqtt_command_callback(unsigned char *topic, unsigned char *payload)
{
    char request_id[64] = {0};
    int result_code = 1;

    if (topic == NULL || payload == NULL) {
        return -1;
    }

    printf("[MQTT] Command topic: %s\r\n", topic);
    printf("[MQTT] Command payload: %s\r\n", payload);

    if (parse_beep_command((const char *)payload) == 0) {
        result_code = 0;
    }

    if (get_request_id((const char *)topic, request_id, sizeof(request_id)) == 0) {
        send_command_response(request_id, result_code);
    }

    return 0;
}

static void Mqtt_Recv_Task(void)
{
    while (1) {
        MQTTClient_sub();
        usleep(200 * 1000);
    }
}

static void mqtt_recv_task_create(void)
{
    osThreadAttr_t taskOptions = {0};
    taskOptions.name = "MqttRecvTask";
    taskOptions.stack_size = 4096;
    taskOptions.priority = osPriorityNormal;
    osThreadNew((osThreadFunc_t)Mqtt_Recv_Task, NULL, &taskOptions);
}

static int g_beep_socket_fd = -1;
static unsigned char g_beep_mqtt_buf[BEEP_MQTT_BUFF_MAX] = {0};

static int beep_transport_send(unsigned char *buf, int buflen)
{
    int rc = send(g_beep_socket_fd, buf, buflen, 0);
    return (rc <= 0) ? 0 : 1;
}

static int beep_transport_get(unsigned char *buf, int count)
{
    return recv(g_beep_socket_fd, buf, count, 0);
}

static int beep_mqtt_connect_server(void)
{
    struct sockaddr_in server_addr;
    struct timeval timeout = {5, 0};

    g_beep_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_beep_socket_fd < 0) {
        printf("[BeepMQTT] Create socket failed.\r\n");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_IP_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP_ADDR);

    if (connect(g_beep_socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        printf("[BeepMQTT] Connect server failed.\r\n");
        close(g_beep_socket_fd);
        g_beep_socket_fd = -1;
        return -1;
    }

    setsockopt(g_beep_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    printf("[BeepMQTT] Connected to server.\r\n");
    return 0;
}

static int beep_mqtt_init_client(void)
{
    int len;
    unsigned char sessionPresent = 0;
    unsigned char connack_rc = 0;
    MQTTPacket_connectData mqttData = MQTTPacket_connectData_initializer;

    mqttData.clientID.cstring = BEEP_MQTT_CLIENT_ID;
    mqttData.username.cstring = BEEP_MQTT_USER_NAME;
    mqttData.password.cstring = BEEP_MQTT_PASS_WORD;
    mqttData.cleansession = true;
    mqttData.keepAliveInterval = BEEP_MQTT_KEEPALIVE_S;

    memset(g_beep_mqtt_buf, 0, sizeof(g_beep_mqtt_buf));
    len = MQTTSerialize_connect(g_beep_mqtt_buf, sizeof(g_beep_mqtt_buf), &mqttData);
    if (len <= 0 || beep_transport_send(g_beep_mqtt_buf, len) != 1) {
        printf("[BeepMQTT] Send connect packet failed.\r\n");
        return -1;
    }

    memset(g_beep_mqtt_buf, 0, sizeof(g_beep_mqtt_buf));
    if (MQTTPacket_read(g_beep_mqtt_buf, sizeof(g_beep_mqtt_buf), beep_transport_get) != CONNACK) {
        printf("[BeepMQTT] CONNACK read failed.\r\n");
        return -1;
    }

    if (MQTTDeserialize_connack(&sessionPresent, &connack_rc, g_beep_mqtt_buf,
                                sizeof(g_beep_mqtt_buf)) != 1 || connack_rc != 0) {
        printf("[BeepMQTT] Connect rejected, rc=%d.\r\n", connack_rc);
        return -1;
    }

    printf("[BeepMQTT] Client initialized.\r\n");
    return 0;
}

static int beep_mqtt_subscribe_commands(void)
{
    char sub_topic[MQTT_DATA_MAX] = {0};
    int len;
    int subcount = 0;
    int granted_qos = 0;
    int req_qos = 0;
    unsigned short submsgid = 0;
    MQTTString topicString = MQTTString_initializer;

    snprintf(sub_topic, sizeof(sub_topic), MQTT_TOPIC_SUB_COMMANDS, BEEP_DEVICE_ID);
    topicString.cstring = sub_topic;

    memset(g_beep_mqtt_buf, 0, sizeof(g_beep_mqtt_buf));
    len = MQTTSerialize_subscribe(g_beep_mqtt_buf, sizeof(g_beep_mqtt_buf), 0, 1, 1,
                                  &topicString, &req_qos);
    if (len <= 0 || beep_transport_send(g_beep_mqtt_buf, len) != 1) {
        printf("[BeepMQTT] Subscribe packet failed.\r\n");
        return -1;
    }

    memset(g_beep_mqtt_buf, 0, sizeof(g_beep_mqtt_buf));
    if (MQTTPacket_read(g_beep_mqtt_buf, sizeof(g_beep_mqtt_buf), beep_transport_get) != SUBACK) {
        printf("[BeepMQTT] SUBACK read failed.\r\n");
        return -1;
    }

    if (MQTTDeserialize_suback(&submsgid, 1, &subcount, &granted_qos, g_beep_mqtt_buf,
                               sizeof(g_beep_mqtt_buf)) != 1) {
        printf("[BeepMQTT] SUBACK parse failed.\r\n");
        return -1;
    }

    printf("[BeepMQTT] Subscribed: %s\r\n", sub_topic);
    return 0;
}

static int beep_mqtt_publish(const char *topic, const unsigned char *payload, int payload_len)
{
    int len;
    MQTTString topicString = MQTTString_initializer;

    topicString.cstring = (char *)topic;
    memset(g_beep_mqtt_buf, 0, sizeof(g_beep_mqtt_buf));
    len = MQTTSerialize_publish(g_beep_mqtt_buf, sizeof(g_beep_mqtt_buf), 0, 0, 0, 0,
                                topicString, (unsigned char *)payload, payload_len);
    if (len <= 0 || beep_transport_send(g_beep_mqtt_buf, len) != 1) {
        printf("[BeepMQTT] Publish failed.\r\n");
        return -1;
    }

    return 0;
}

static void beep_mqtt_send_command_response(const char *request_id, int result_code)
{
    char topic[MQTT_DATA_MAX] = {0};
    char payload[32] = {0};

    if (request_id == NULL || strlen(request_id) == 0) {
        return;
    }

    snprintf(topic, sizeof(topic), MQTT_TOPIC_PUB_COMMANDS_REQ, BEEP_DEVICE_ID, request_id);
    snprintf(payload, sizeof(payload), "{\"result_code\":%d}", result_code);
    beep_mqtt_publish(topic, (unsigned char *)payload, strlen(payload));
}

static void beep_mqtt_report_state(void)
{
    char topic[MQTT_DATA_MAX] = {0};
    char payload[MQTT_DATA_MAX] = {0};

    snprintf(topic, sizeof(topic), MQTT_TOPIC_PUB_PROPERTIES, BEEP_DEVICE_ID);
    snprintf(payload, sizeof(payload),
             "{\"services\":[{\"service_id\":\"%s\",\"properties\":{\"beep\":\"%s\"}}]}",
             BEEP_SERVICE_ID, g_beeper_on ? "ON" : "OFF");
    beep_mqtt_publish(topic, (unsigned char *)payload, strlen(payload));
}

static void beep_mqtt_ping(void)
{
    int len;

    memset(g_beep_mqtt_buf, 0, sizeof(g_beep_mqtt_buf));
    len = MQTTSerialize_pingreq(g_beep_mqtt_buf, sizeof(g_beep_mqtt_buf));
    if (len > 0) {
        beep_transport_send(g_beep_mqtt_buf, len);
    }
}

static void beep_mqtt_handle_packet(void)
{
    int qos;
    int payloadlen;
    int packet_type;
    unsigned char dup;
    unsigned char retained;
    unsigned short msgid;
    unsigned char *payload;
    MQTTString receivedTopic;
    char topic[MQTT_DATA_MAX] = {0};
    char payload_text[MQTT_DATA_MAX] = {0};
    char request_id[64] = {0};
    int result_code = 1;

    memset(g_beep_mqtt_buf, 0, sizeof(g_beep_mqtt_buf));
    packet_type = MQTTPacket_read(g_beep_mqtt_buf, sizeof(g_beep_mqtt_buf), beep_transport_get);
    if (packet_type != PUBLISH) {
        return;
    }

    MQTTDeserialize_publish(&dup, &qos, &retained, &msgid, &receivedTopic, &payload, &payloadlen,
                            g_beep_mqtt_buf, sizeof(g_beep_mqtt_buf));

    snprintf(topic, sizeof(topic), "%.*s", receivedTopic.lenstring.len, receivedTopic.lenstring.data);
    snprintf(payload_text, sizeof(payload_text), "%.*s", payloadlen, payload);
    printf("[BeepMQTT] Command topic: %s\r\n", topic);
    printf("[BeepMQTT] Command payload: %s\r\n", payload_text);

    if (parse_beep_command(payload_text) == 0) {
        result_code = 0;
    }

    if (get_request_id(topic, request_id, sizeof(request_id)) == 0) {
        beep_mqtt_send_command_response(request_id, result_code);
    }
}

static void Beep_Mqtt_Task(void)
{
    while (strlen(g_ip_addr) == 0) {
        printf("[BeepMQTT] Waiting for WiFi IP...\r\n");
        sleep(1);
    }
    sleep(10);

    while (1) {
        printf("[BeepMQTT] Connecting as %s...\r\n", BEEP_DEVICE_ID);
        if (beep_mqtt_connect_server() == 0 && beep_mqtt_init_client() == 0) {
            break;
        }

        if (g_beep_socket_fd >= 0) {
            close(g_beep_socket_fd);
            g_beep_socket_fd = -1;
        }
        printf("[BeepMQTT] Init failed, retry in 5 seconds.\r\n");
        sleep(5);
    }

    beep_mqtt_report_state();
    while (1) {
        sleep(BEEP_MQTT_REPORT_PERIOD_S);
        beep_mqtt_report_state();
        beep_mqtt_ping();
    }
}

static void beep_mqtt_task_create(void)
{
    osThreadAttr_t taskOptions = {0};
    taskOptions.name = "BeepMqttTask";
    taskOptions.stack_size = 4096;
    taskOptions.priority = osPriorityNormal;
    g_beep_mqtt_task_id = osThreadNew((osThreadFunc_t)Beep_Mqtt_Task, NULL, &taskOptions);
    if (g_beep_mqtt_task_id == NULL) {
        printf("[BeepMQTT] Task creation failed!\r\n");
    }
}

static void Mqtt_Report_Task(void)
{
    char topic[MQTT_DATA_MAX] = {0};
    char sub_topic[MQTT_DATA_MAX] = {0};
    unsigned char payload[MQTT_DATA_MAX] = {0};

    while (strlen(g_ip_addr) == 0) {
        printf("[MQTT] Waiting for WiFi IP...\r\n");
        sleep(1);
    }

    while (1) {
        printf("[MQTT] Connecting as %s...\r\n", DEVICE_ID);
        if (MQTTClient_connectServer(SERVER_IP_ADDR, SERVER_IP_PORT) == 0) {
            printf("[MQTT] Connected to server.\r\n");
            break;
        }
        printf("[MQTT] Connect server failed, retry in 5 seconds.\r\n");
        sleep(5);
    }

    while (1) {
        if (MQTTClient_init(MQTT_CLIENT_ID, MQTT_USER_NAME, MQTT_PASS_WORD) == 0) {
            printf("[MQTT] Client initialized.\r\n");
            g_aht20_mqtt_ready = 1;
            break;
        }
        printf("[MQTT] Client init failed, retry in 5 seconds.\r\n");
        MQTTClient_unConnectServer();
        sleep(5);
        while (MQTTClient_connectServer(SERVER_IP_ADDR, SERVER_IP_PORT) != 0) {
            printf("[MQTT] Reconnect server failed, retry in 5 seconds.\r\n");
            sleep(5);
        }
    }

    p_MQTTClient_sub_callback = mqtt_command_callback;
    snprintf(sub_topic, sizeof(sub_topic), MQTT_TOPIC_SUB_COMMANDS, DEVICE_ID);
    if (MQTTClient_subscribe(sub_topic) == 0) {
        mqtt_recv_task_create();
    } else {
        printf("[MQTT] Subscribe commands failed.\r\n");
    }

    while (1) {
        if (g_sensor_ready) {
            memset(topic, 0, sizeof(topic));
            memset(payload, 0, sizeof(payload));

            int payload_len = build_property_report(topic, sizeof(topic), payload, sizeof(payload));
            if (payload_len > 0) {
                printf("[MQTT] Publish: %s\r\n", payload);
                MQTTClient_pub(topic, payload, payload_len);
            } else {
                printf("[MQTT] Build property report failed.\r\n");
            }
        } else {
            printf("[MQTT] Sensor data not ready yet.\r\n");
        }

        sleep(MQTT_REPORT_PERIOD_S);
    }
}

static void mqtt_report_task_create(void)
{
    osThreadAttr_t taskOptions = {0};
    taskOptions.name = "MqttReportTask";
    taskOptions.stack_size = 4096;
    taskOptions.priority = osPriorityNormal;
    g_mqtt_report_task_id = osThreadNew((osThreadFunc_t)Mqtt_Report_Task, NULL, &taskOptions);
    if (g_mqtt_report_task_id == NULL) {
        printf("[MQTT] Report task creation failed!\r\n");
    }
}

static void template_demo(void)
{
    printf("========== System Start ==========\r\n");

    led_task_create();
    beeper_init();

    while (ds18b20_init()) {
        printf("AHT20 init error! Retry in 500ms...\r\n");
        usleep(500000);
    }
    printf("AHT20 init success!\r\n");

    aht20_task_create();
    printf("AHT20 task started (period 1 second).\r\n");

    wifi_task_create();
    print_ip_task_create();
    mqtt_report_task_create();

    printf("All tasks created. System running...\r\n");
}

SYS_RUN(template_demo);
