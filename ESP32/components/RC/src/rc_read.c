#include "rc_read.h"

static TaskHandle_t s_task_handle;
static adc_channel_t channel[4] = {ADC_roll, ADC_pitch, ADC_throttle, ADC_yaw};

channel_cal_t limit[16] = {
    //  min,  mid,  max
    { 999, 1500, 2001 }, // CH1 (Roll)
    { 999, 1500, 2001 }, // CH2 (Pitch)
    { 999, 1500, 2001 }, // CH3 (Throttle)
    { 999, 1500, 2001 }, // CH4 (Yaw)
    { 999, 1500, 2001 }, // CH5
    { 999, 1500, 2001 }, // CH6
    { 999, 1500, 2001 }, // CH7
    { 999, 1500, 2001 }, // CH8
    { 999, 1500, 2001 }, // CH9
    { 999, 1500, 2001 }, // CH10
    { 999, 1500, 2001 }, // CH11
    { 999, 1500, 2001 }, // CH12
    { 999, 1500, 2001 }, // CH13
    { 999, 1500, 2001 }, // CH14
    { 999, 1500, 2001 }, // CH15
    { 999, 1500, 2001 }  // CH16
};

static const char *TAG = "ADC";

static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t mustYield = pdFALSE;
    
    vTaskNotifyGiveFromISR(s_task_handle, &mustYield);

    return (mustYield == pdTRUE);
}

static void continuous_adc_init(adc_channel_t *channel, uint8_t channel_num, adc_continuous_handle_t *out_handle)
{
    adc_continuous_handle_t handle = NULL;
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = READ_SIZE,
        .conv_frame_size = READ_LEN,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle));
    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = FRE_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
    };
    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    dig_cfg.pattern_num = channel_num;
    for (int i = 0; i < channel_num; i++) 
    {
        adc_pattern[i].atten = ADC_ATTEN_DB_12;
        adc_pattern[i].channel = channel[i];
        adc_pattern[i].unit = ADC_UNIT_1;
        adc_pattern[i].bit_width = ADC_BITWIDTH_12;

        ESP_LOGI(TAG, "adc_pattern[%d].atten is :%"PRIx8, i, adc_pattern[i].atten);
        ESP_LOGI(TAG, "adc_pattern[%d].channel is :%"PRIx8, i, adc_pattern[i].channel);
        ESP_LOGI(TAG, "adc_pattern[%d].unit is :%"PRIx8, i, adc_pattern[i].unit);
    }
    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));

    *out_handle = handle;
}

// 将原始 ADC 值，根据三点校准，映射到 1000~2000 (标准 FPV 范围)
static uint16_t map_joystick(uint16_t raw, channel_cal_t cal) {
    // 1. 限制两端死区，防止电位器抖动超过极值
    if (raw <= cal.raw_min) return 1000;
    if (raw >= cal.raw_max) return 2000;
    
    // 2. 分段线性映射
    if (raw < cal.raw_mid) 
    {
        // 下半段: min ~ mid 映射到 1000 ~ 1500
        if (cal.raw_mid == cal.raw_min) return 1000; // 防止除以 0
        return 1000 + (uint32_t)(raw - cal.raw_min) * 500 / (cal.raw_mid - cal.raw_min);
    } 
    else 
    {
        // 上半段: mid ~ max 映射到 1500 ~ 2000
        if (cal.raw_max == cal.raw_mid) return 1500; // 防止除以 0
        return 1500 + (uint32_t)(raw - cal.raw_mid) * 500 / (cal.raw_max - cal.raw_mid);
    }
}
// =================================================================================
// 滑动窗口均值滤波配置
// =================================================================================
#define FILTER_N 16       // 滤波窗口大小。值越大摇杆越平滑，但响应会有微小延迟；值越小越灵敏。16 是个不错的平衡点。
#define MAX_ADC_CH 10     // ESP32 ADC1 最多通常是 8 或 10 个通道，定义 10 足够安全

static uint16_t moving_average_filter(uint32_t ch, uint16_t new_val) 
{
    // 使用静态二维数组，分别为每个通道保存独立的历史数据
    static uint16_t history[MAX_ADC_CH][FILTER_N] = {0};
    static uint8_t  idx[MAX_ADC_CH] = {0};
    static uint32_t sum[MAX_ADC_CH] = {0};
    static uint8_t  count[MAX_ADC_CH] = {0};

    // 安全检查，防止意外的通道号导致数组越界
    if (ch >= MAX_ADC_CH) return new_val; 

    // 滑动窗口核心逻辑：减去最老的值，加上最新的值
    sum[ch] -= history[ch][idx[ch]];
    history[ch][idx[ch]] = new_val;
    sum[ch] += new_val;

    // 移动覆盖指针，满一圈回到起点 (环形队列)
    idx[ch]++;
    if (idx[ch] >= FILTER_N) idx[ch] = 0;

    // 处理刚开机时的边界情况：如果数组还没填满，就除以当前已有的个数，防止初始数据偏小 (摇杆归零)
    if (count[ch] < FILTER_N) {
        count[ch]++;
        return sum[ch] / count[ch];
    }

    return sum[ch] / FILTER_N;
}
void key_init(void)
{
    
    const gpio_config_t boot_button_config = {
        .pin_bit_mask = GPIO_SW,
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    ESP_ERROR_CHECK(gpio_config(&boot_button_config));

}

/**
 * @brief 读取2段式开关状态
 * @param gpio_num GPIO编号
 * @param inverted 是否反相 (如果是上拉输入，按下为0，则传 true)
 * @return uint16_t 1000 或 2000
 */
uint16_t read_2pos_switch(gpio_num_t gpio_num, bool inverted) {
    int level = gpio_get_level(gpio_num);
    if (inverted) {
        return level ? 1000 : 2000;
    } else {
        return level ? 2000 : 1000;
    }
}

/**
 * @brief 读取3段式开关状态
 * @param gpio_up   向上位置对应的GPIO
 * @param gpio_down 向下位置对应的GPIO
 * @return uint16_t 1000(下), 1500(中), 2000(上)
 */
uint16_t read_3pos_switch(gpio_num_t gpio_up, gpio_num_t gpio_down) {
    // 假设使用内部上拉，接通时为低电平(0)
    int up_val = gpio_get_level(gpio_up);
    int down_val = gpio_get_level(gpio_down);

    if (up_val == 0 && down_val == 1) {
        return 2000; // 向上拨
    } else if (up_val == 1 && down_val == 0) {
        return 1000; // 向下拨
    } else {
        return 1500; // 中间位 (或者异常状态)
    }
}

/**
 * @brief 填充所有开关通道 (CH9-CH16)
 * @param joy 指向遥控器数据结构体的指针
 */
void update_switch_channels(fpv_joystick_report_t *joy) {
    // 这里你需要根据你的实际硬件引脚进行修改
    // 示例：SW1 是按键，SW2 是 3段拨杆
    
    // CH9 (SW1): 假设 GPIO 14，按下为低电平
    joy->sw1 = READ_KEY_CH1;

    // CH10 (SW2): 3段开关，假设向上 GPIO 15, 向下 GPIO 16
    joy->sw2 = READ_KEY_CH2;

    // CH11 ~ CH16 (暂时填默认中位 1500 或根据需要添加)
    joy->sw3 = READ_KEY_CH3;
    joy->sw4 = READ_KEY_CH4;
    joy->sw5 = READ_KEY_CH5;
    joy->sw6 = READ_KEY_CH6;
    joy->sw7 = READ_KEY_CH7;
    joy->sw8 = READ_KEY_CH8;
}

void ADC_TASK(void *arg)
{
    uint32_t ret_num = 0;
    uint8_t result[READ_LEN] = {0};
    fpv_joystick_report_t *joy = (fpv_joystick_report_t*) arg;
   
    s_task_handle = xTaskGetCurrentTaskHandle(); 

    adc_continuous_handle_t handle = NULL;
    continuous_adc_init(channel, sizeof(channel) / sizeof(adc_channel_t), &handle);

    adc_continuous_evt_cbs_t cbs = { .on_conv_done = s_conv_done_cb,};

    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(handle, &cbs, NULL)); 
    ESP_ERROR_CHECK(adc_continuous_start(handle)); 
    key_init();
    while (1) 
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (1) 
        {
            if(adc_continuous_read(handle, result, READ_LEN, &ret_num, 0) == ESP_OK) 
            {
                uint32_t num_parsed_samples = 0;
                adc_continuous_data_t parsed_data[ret_num / SOC_ADC_DIGI_RESULT_BYTES]; 
                esp_err_t parse_ret = adc_continuous_parse_data(handle, result, ret_num, parsed_data, &num_parsed_samples);
                
                if (parse_ret == ESP_OK) 
                {
                    // ==========================================================
                    // ⭐ 你的思路：先对这一批次的数据做“过采样求和”
                    // ==========================================================
                    uint32_t batch_sum[MAX_ADC_CH] = {0}; // 累加器
                    uint8_t  batch_count[MAX_ADC_CH] = {0}; // 计数器

                    for (int i = 0; i < num_parsed_samples; i++) 
                    {
                        if (parsed_data[i].valid) 
                        {
                            uint32_t ch = parsed_data[i].channel;
                            if (ch < MAX_ADC_CH) {
                                batch_sum[ch] += parsed_data[i].raw_data;
                                batch_count[ch]++;
                            }
                        }
                    }
              
                    // ==========================================================
                    // ⭐ 对批次均值进行“滑动滤波”和“摇杆映射” (每个通道每轮只执行1次)
                    // ==========================================================
                    for (int ch = 0; ch < MAX_ADC_CH; ch++) 
                    {
                        if (batch_count[ch] > 0) 
                        {
                            // 1. 算出这一批次里，当前通道的平均值 (约 8 个数据的平均)
                            uint16_t batch_avg = batch_sum[ch] / batch_count[ch];

                            // 2. 把这个批次平均值，送入跨越时间周期的滑动窗口，得到最终平滑值
                            uint16_t final_raw = moving_average_filter(ch, batch_avg);

                            // 3. 映射逻辑：现在每个通道每一轮只映射 1 次！极大减轻单片机负担！
                          
                            switch (ch)
                            {
                                case ADC_roll: 
                                    joy->roll = map_joystick(final_raw, limit[0]);
                                    joy->raw_roll = final_raw; 
                                    break;
                                    
                                case ADC_pitch: 
                                    joy->pitch = map_joystick(final_raw, limit[1]);
                                    joy->raw_pitch = final_raw;
                                    break;
                                    
                                case ADC_throttle: 
                                    joy->throttle = map_joystick(final_raw, limit[2]);
                                    joy->raw_throttle = final_raw;
                                    break;
                                    
                                case ADC_yaw:
                                    joy->yaw = map_joystick(final_raw, limit[3]);
                                    joy->raw_yaw = final_raw;
                                    break;
                            }
                        }
                    }
                }
            } 
            update_switch_channels(joy);
            vTaskDelay(1); 
        }
    }
}