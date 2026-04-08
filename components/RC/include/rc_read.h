#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_adc/adc_continuous.h"
#include "driver/gpio.h"

#define READ_SIZE 1024
#define READ_LEN  256
#define FRE_HZ    20000

typedef struct {
    uint16_t raw_min;
    uint16_t raw_mid;
    uint16_t raw_max;
} channel_cal_t;

typedef struct {
    // ==========================================================
    // 1. 最终输出数据 (发给 USB / 模拟器用的 16 个通道，全是 1000~2000)
    // ==========================================================
    // CH1 ~ CH4: 摇杆主轴
    uint16_t roll;      // CH1
    uint16_t pitch;     // CH2
    uint16_t throttle;  // CH3
    uint16_t yaw;       // CH4
    
    // CH5 ~ CH8: 模拟辅助通道 (滑块/旋钮)
    uint16_t aux1;      // CH5
    uint16_t aux2;      // CH6
    uint16_t aux3;      // CH7
    uint16_t aux4;      // CH8

    // CH9 ~ CH16: 数字开关通道 (2段/3段开关)
    uint16_t sw1;       // CH9
    uint16_t sw2;       // CH10
    uint16_t sw3;       // CH11
    uint16_t sw4;       // CH12
    uint16_t sw5;       // CH13
    uint16_t sw6;       // CH14
    uint16_t sw7;       // CH15
    uint16_t sw8;       // CH16

    // ==========================================================
    // 2. 校准专用数据 (只发给网页端，存放 0~4095 的原始 ADC 值)
    // ==========================================================
    // 只有前 8 个模拟通道需要物理校准
    uint16_t raw_roll;
    uint16_t raw_pitch;
    uint16_t raw_throttle;
    uint16_t raw_yaw;
    
    uint16_t raw_aux1;
    uint16_t raw_aux2;
    uint16_t raw_aux3;
    uint16_t raw_aux4;
    
} fpv_joystick_report_t;

#define ADC_roll        ADC_CHANNEL_6
#define ADC_pitch       ADC_CHANNEL_5
#define ADC_throttle    ADC_CHANNEL_3
#define ADC_yaw         ADC_CHANNEL_4
// #define ADC_aux1        ADC_CHANNEL_0
// #define ADC_aux2        ADC_CHANNEL_1
// #define ADC_aux3        ADC_CHANNEL_2
// #define ADC_aux4        ADC_CHANNEL_3

//下面使用这里要添加
#define GPIO_SW         (1ULL << GPIO_NUM_16)| (1ULL << GPIO_NUM_17)| (1ULL << GPIO_NUM_18)   

#define READ_KEY_CH1    read_2pos_switch(GPIO_NUM_16,true)
#define READ_KEY_CH2    read_3pos_switch(GPIO_NUM_17,GPIO_NUM_18)
#define READ_KEY_CH3    1500
#define READ_KEY_CH4    1500
#define READ_KEY_CH5    1500
#define READ_KEY_CH6    1500
#define READ_KEY_CH7    1500
#define READ_KEY_CH8    1500

extern channel_cal_t limit[16];
void ADC_TASK(void *arg);

#ifdef __cplusplus
}
#endif