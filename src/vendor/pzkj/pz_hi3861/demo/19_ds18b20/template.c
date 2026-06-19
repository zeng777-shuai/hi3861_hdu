/**
 ****************************************************************************************************
 * @file        template.c
 ****************************************************************************************************
 */

#include <stdio.h>
#include <unistd.h>

#include "ohos_init.h"
#include "cmsis_os2.h"

#include "bsp_led.h"
#include "bsp_ds18b20.h"


/**
 * @file template.c
 * 整合：LED闪烁 + DS18B20测温（软件定时器） + Wi-Fi STA模式（原生API + lwip获取IP）
 * 新增：每隔5秒打印当前Wi-Fi IP的任务
 * 新增：蜂鸣器B1（GPIO09/PWM0）每隔3秒响3秒（2700Hz，50%占空比）
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "bsp_led.h"
#include "bsp_ds18b20.h"
#include "wifi_device.h"

// ---------- 新增 lwip 相关头文件 ----------
#include "lwip/netifapi.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/dhcp.h"
#include <arpa/inet.h>   // 用于 inet_ntop

// ---------- 新增 PWM 头文件（用于蜂鸣器） ----------
#include "hi_pwm.h"
#include "hi_gpio.h"
#include "hi_io.h"
/* ************ LED 任务 ************ */
osThreadId_t LED_Task_ID;

void LED_Task(void)
{
    led_init();
    while (1) {
        LED(1);
        usleep(200 * 1000);
        LED(0);
        usleep(200 * 1000);
    }
}

void led_task_create(void)
{
    osThreadAttr_t taskOptions;
    taskOptions.name = "LEDTask";
    taskOptions.attr_bits = 0;
    taskOptions.cb_mem = NULL;
    taskOptions.cb_size = 0;
    taskOptions.stack_mem = NULL;
    taskOptions.stack_size = 1024;
    taskOptions.priority = osPriorityNormal;
    LED_Task_ID = osThreadNew((osThreadFunc_t)LED_Task, NULL, &taskOptions);
}

/* ************ 蜂鸣器任务（B1，GPIO09 / PWM0） ************ */
#define BEEP_PWM_PORT   HI_PWM_PORT_PWM0   // 蜂鸣器使用PWM0
#define BEEP_FREQ       59259              // 160MHz / 59259 ~= 2700Hz
#define BEEP_DUTY       (BEEP_FREQ / 2)    // 50% duty

osThreadId_t Beeper_Task_ID;

void Beeper_Task(void)
{
    hi_u32 ret;

    hi_gpio_init();

    // 复用引脚为PWM0
    hi_io_set_func(HI_IO_NAME_GPIO_9, HI_IO_FUNC_GPIO_9_PWM0_OUT);
    hi_gpio_set_dir(HI_IO_NAME_GPIO_9, HI_GPIO_DIR_OUT);

    // 初始化PWM0
    ret = hi_pwm_init(BEEP_PWM_PORT);
    if (ret != 0) {
        printf("[Beeper] PWM0 init failed, ret=0x%x.\r\n", ret);
        return;
    }
    hi_pwm_set_clock(PWM_CLK_160M);
    printf("[Beeper] PWM0 initialized.\r\n");

    // 一直响，不停止
    ret = hi_pwm_start(BEEP_PWM_PORT, BEEP_DUTY, BEEP_FREQ);
    if (ret != 0) {
        printf("[Beeper] PWM0 start failed, ret=0x%x.\r\n", ret);
        return;
    }
    printf("[Beeper] ON forever.\r\n");

    // 任务保持运行，但不做其他事
    while (1) {
        osDelay(1000);
    }
}

void beeper_task_create(void)
{
    osThreadAttr_t taskOptions;
    taskOptions.name = "BeeperTask";
    taskOptions.attr_bits = 0;
    taskOptions.cb_mem = NULL;
    taskOptions.cb_size = 0;
    taskOptions.stack_mem = NULL;
    taskOptions.stack_size = 1024;
    taskOptions.priority = osPriorityNormal;
    Beeper_Task_ID = osThreadNew((osThreadFunc_t)Beeper_Task, NULL, &taskOptions);
    if (Beeper_Task_ID == NULL) {
        printf("[Beeper] Task creation failed!\r\n");
    } else {
        printf("[Beeper] Task created, toggling every 3 seconds.\r\n");
    }
}

/* ************ 软件定时器回调：读取 DS18B20 温度 ************ */
void AHT20_Timer_Callback(void *arg)
{
    float temp = ds18b20_gettemperture();
    printf("Temp: %.2f°C\r\n", temp);
}

/* ========== Wi-Fi 任务（使用原生 API + lwip 获取 IP） ========== */
#define WIFI_SSID "Mi11"          // 改成你的热点名称
#define WIFI_PAWD "12345678"      // 改成你的热点密码

#define SELECT_WLAN_PORT "wlan0"  // Hi3861 网卡名称
static struct netif *g_lwip_netif = NULL;
static char g_ip_addr[20] = {0};  // 存储获取到的 IP 字符串

osThreadId_t Wifi_Task_ID;

void Wifi_Task(void)
{
    WifiErrorCode err;
    int netId = -1;
    int timeout = 15;   // DHCP 超时时间（秒）

    printf("[WiFi] Starting STA mode...\r\n");

    // 1. 启用 Wi-Fi
    err = EnableWifi();
    if (err != WIFI_SUCCESS) {
        printf("[WiFi] EnableWifi failed, err=%d\r\n", err);
        return;
    }
    printf("[WiFi] Wi-Fi enabled.\r\n");

    // 2. 配置并添加网络
    WifiDeviceConfig config = {0};
    strcpy(config.ssid, WIFI_SSID);
    strcpy(config.preSharedKey, WIFI_PAWD);
    config.securityType = WIFI_SEC_TYPE_PSK;   // 根据你的路由器加密方式调整

    err = AddDeviceConfig(&config, &netId);
    if (err != WIFI_SUCCESS) {
        printf("[WiFi] AddDeviceConfig failed, err=%d\r\n", err);
        return;
    }
    printf("[WiFi] AddDeviceConfig success, netId=%d\r\n", netId);

    // 3. 连接
    err = ConnectTo(netId);
    if (err != WIFI_SUCCESS) {
        printf("[WiFi] ConnectTo failed, err=%d\r\n", err);
        return;
    }
    printf("[WiFi] Connecting...\r\n");

    // 4. 等待连接成功（轮询连接状态）
    while (1) {
        WifiLinkedInfo info;
        err = GetLinkedInfo(&info);
        if (err == WIFI_SUCCESS && info.connState == WIFI_CONNECTED) {
            printf("[WiFi] Connected to AP.\r\n");
            break;
        }
        sleep(1);
    }

    // ---------- 获取 netif 并启动 DHCP（关键步骤） ----------
    g_lwip_netif = netifapi_netif_find(SELECT_WLAN_PORT);
    if (g_lwip_netif == NULL) {
        printf("[WiFi] netif find failed!\r\n");
        return;
    }

    // 启动 DHCP（如果已经自动启动，重复调用无妨）
    dhcp_start(g_lwip_netif);

    // 等待 DHCP 成功获取 IP
    while (timeout > 0) {
        if (dhcp_is_bound(g_lwip_netif) == ERR_OK) {
            // 获取 IP 地址
            ip4_addr_t ipAddr, netMask, gateWay;
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

    // 主循环：保持任务存活，可做心跳或断线重连
    while (1) {
        sleep(10);
    }
}

void wifi_task_create(void)
{
    osThreadAttr_t taskOptions;
    taskOptions.name = "WifiTask";
    taskOptions.attr_bits = 0;
    taskOptions.cb_mem = NULL;
    taskOptions.cb_size = 0;
    taskOptions.stack_mem = NULL;
    taskOptions.stack_size = 4096;
    taskOptions.priority = osPriorityNormal;
    Wifi_Task_ID = osThreadNew((osThreadFunc_t)Wifi_Task, NULL, &taskOptions);
    if (Wifi_Task_ID == NULL) {
        printf("[WiFi] Task creation failed!\r\n");
    } else {
        printf("[WiFi] Task created, ID = %d\r\n", Wifi_Task_ID);
    }
}

/* ========== 每隔5秒打印当前Wi-Fi IP的任务（使用全局IP字符串） ========== */
osThreadId_t PrintIP_Task_ID;

void PrintIP_Task(void)
{
    while (1) {
        if (strlen(g_ip_addr) > 0) {
            printf("[IP Task] Current IP: %s\r\n", g_ip_addr);
        } else {
            printf("[IP Task] IP not ready yet.\r\n");
        }
        osDelay(5000);   // 5秒打印一次
    }
}

void print_ip_task_create(void)
{
    osThreadAttr_t taskOptions;
    taskOptions.name = "PrintIPTask";
    taskOptions.attr_bits = 0;
    taskOptions.cb_mem = NULL;
    taskOptions.cb_size = 0;
    taskOptions.stack_mem = NULL;
    taskOptions.stack_size = 2048;
    taskOptions.priority = osPriorityNormal;
    PrintIP_Task_ID = osThreadNew((osThreadFunc_t)PrintIP_Task, NULL, &taskOptions);
    if (PrintIP_Task_ID == NULL) {
        printf("[IP Task] Creation failed!\r\n");
    } else {
        printf("[IP Task] Created, will print IP every 5 seconds.\r\n");
    }
}

/* ************ 应用入口函数 ************ */
static void template_demo(void)
{
    printf("========== System Start ==========\r\n");

    // 1. 创建 LED 任务
    led_task_create();

    // 2. 创建蜂鸣器任务（新增）
    beeper_task_create();

    // 3. 初始化 DS18B20（带重试）
    while (ds18b20_init()) {
        printf("DS18B20 init error! Retry in 500ms...\r\n");
        usleep(500000);
    }
    printf("DS18B20 init success!\r\n");

    // 4. 创建并启动测温软件定时器（周期 1000 ticks，约 1 秒，可自行调整）
    osTimerId_t timer_id = osTimerNew(AHT20_Timer_Callback, osTimerPeriodic, NULL, NULL);
    if (timer_id == NULL) {
        printf("Timer creation failed!\r\n");
        return;
    }
    if (osTimerStart(timer_id, 1000) != osOK) {
        printf("Timer start failed!\r\n");
        osTimerDelete(timer_id);
        return;
    }
    printf("Temperature timer started (period 1 second).\r\n");

    // 5. 创建 Wi-Fi 连接任务
    wifi_task_create();

    // 6. 创建打印IP的任务
    print_ip_task_create();

    printf("All tasks created. System running...\r\n");
}

SYS_RUN(template_demo);
