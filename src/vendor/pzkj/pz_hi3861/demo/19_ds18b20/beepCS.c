#include <stdio.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "hi_pwm.h"
#include "hi_io.h"
#include "hi_gpio.h"

#define BEEP_PWM_PORT   HI_PWM_PORT_PWM0
#define BEEP_FREQ       2700
#define BEEP_DUTY       50

static void BeepTestTask(void)
{
    // 1. 引脚复用
    hi_io_set_func(HI_IO_NAME_GPIO_9, HI_IO_FUNC_GPIO_9_PWM0_OUT);
    hi_gpio_set_dir(HI_IO_NAME_GPIO_9, HI_GPIO_DIR_OUT);
    
    // 2. 初始化PWM
    hi_pwm_init(BEEP_PWM_PORT);
    printf("PWM init done.\n");
    
    // 3. 启动PWM，一直响
    hi_pwm_start(BEEP_PWM_PORT, BEEP_FREQ, BEEP_DUTY);
    printf("Beep is ON forever.\n");
    
    while (1) {
        osDelay(1000);
    }
}

static void BeepTestEntry(void)
{
    osThreadAttr_t attr = {
        .name = "BeepTest",
        .stack_size = 1024,
        .priority = 25,
    };
    if (osThreadNew((osThreadFunc_t)BeepTestTask, NULL, &attr) == NULL) {
        printf("Failed to create BeepTestTask.\n");
    }
}

SYS_RUN(BeepTestEntry);