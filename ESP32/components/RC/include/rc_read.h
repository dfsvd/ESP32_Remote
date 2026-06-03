#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define READ_SIZE 1024
#define READ_LEN 256
#define FRE_HZ 20000

typedef struct {
    uint16_t raw_min;
    uint16_t raw_mid;
    uint16_t raw_max;
} channel_cal_t;

#define MAX_PROFILES 8
#define PROFILE_NAME_LEN 16

typedef struct {
    uint8_t ch_map[16];
    uint8_t epa_pos[16];
    uint8_t epa_neg[16];
    uint16_t rev_mask;
    uint8_t stick_mode;
    uint8_t btn_cfg[4];
    channel_cal_t limit[16];
} __attribute__((packed)) config_blob_t;

typedef struct {
    // ==========================================================
    // 1. 最终输出数据 (发给 USB / 模拟器用的 16 个通道，全是 1000~2000)
    // ==========================================================
    // CH1 ~ CH4: 摇杆主轴
    uint16_t roll;     // CH1
    uint16_t pitch;    // CH2
    uint16_t throttle; // CH3
    uint16_t yaw;      // CH4

    // CH5 ~ CH8: 模拟辅助通道 (滑块/旋钮)
    uint16_t aux1; // CH5
    uint16_t aux2; // CH6
    uint16_t aux3; // CH7
    uint16_t aux4; // CH8

    // CH9 ~ CH16: 数字开关通道 (2段/3段开关)
    uint16_t sw1; // CH9
    uint16_t sw2; // CH10
    uint16_t sw3; // CH11
    uint16_t sw4; // CH12
    uint16_t sw5; // CH13
    uint16_t sw6; // CH14
    uint16_t sw7; // CH15
    uint16_t sw8; // CH16

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

#define ADC_roll ADC_CHANNEL_5
#define ADC_pitch ADC_CHANNEL_6
#define ADC_throttle ADC_CHANNEL_4
#define ADC_yaw ADC_CHANNEL_3
// #define ADC_aux1        ADC_CHANNEL_0
// #define ADC_aux2        ADC_CHANNEL_1
// #define ADC_aux3        ADC_CHANNEL_2
// #define ADC_aux4        ADC_CHANNEL_3

// 开关引脚 — 以遥控器物理标识命名
#define RC_SWITCH_SA_PIN GPIO_NUM_36      // SA: 按键(自复位) → CH5
#define RC_SWITCH_SB_PIN GPIO_NUM_37      // SB: 2段拨码      → CH6
#define RC_SWITCH_SC_UP_PIN GPIO_NUM_39   // SC: 3段拨码 UP   → CH7
#define RC_SWITCH_SC_DOWN_PIN GPIO_NUM_40 // SC: 3段拨码 DOWN → CH7
#define RC_SWITCH_SD_PIN GPIO_NUM_38      // SD: 按键(自复位) → CH8

#define GPIO_SW                                                                \
    ((1ULL << RC_SWITCH_SA_PIN) | (1ULL << RC_SWITCH_SC_UP_PIN) |              \
     (1ULL << RC_SWITCH_SC_DOWN_PIN) | (1ULL << RC_SWITCH_SD_PIN) |            \
     (1ULL << RC_SWITCH_SB_PIN))

#define READ_KEY_SA read_2pos_switch(RC_SWITCH_SA_PIN, true)  // 按键 → CH8
#define READ_KEY_SB read_2pos_switch(RC_SWITCH_SB_PIN, false) // 2段  → CH6
#define READ_KEY_SC                                                            \
    read_3pos_switch(RC_SWITCH_SC_UP_PIN, RC_SWITCH_SC_DOWN_PIN) // 3段  → CH7
#define READ_KEY_SD read_2pos_switch(RC_SWITCH_SD_PIN, true)     // 按键 → CH5
#define READ_KEY_FIXED1 1500
#define READ_KEY_FIXED2 1500
#define READ_KEY_FIXED3 1500
#define READ_KEY_FIXED4 1500

extern channel_cal_t limit[16];

// EPA 行程调节：每通道正/负行程百分比 (默认 100 = 100%)
extern uint8_t epa_pos[16];
extern uint8_t epa_neg[16];

// REV 反向：位掩码 bit0=CH1 ... bit15=CH16 (0=NOR, 1=REV)
extern uint16_t rev_mask;

// 通道映射: ch_map[dst_ch] = src_physical, 0xFF 表示禁用
extern uint8_t ch_map[16];
// 摇杆模式: 1=日本手 2=美国手 3=Mode3 4=Mode4
extern uint8_t stick_mode;
// 按键触发模式: [0]=SA [1]=SB [2]=SC [3]=SD
//   SA/SD: 0=触摸 1=单击 2=双击
//   SB/SC: 0=三态 1=二态
extern uint8_t btn_cfg[4];

// 共享配置自旋锁: 保护 ch_map / epa / rev / stick_mode / btn_cfg / limit
extern portMUX_TYPE cfg_lock;

void ADC_TASK(void *arg);

#ifdef __cplusplus
}
#endif
