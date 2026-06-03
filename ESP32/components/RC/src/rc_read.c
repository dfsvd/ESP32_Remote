#include "rc_read.h"
#include <stdlib.h>

/* =========================================================================
 * ADC 调试模块 — 需同时定义 ADC_DEBUG 和 ADC_DEBUG_INTERVAL_MS 才编译
 * 用法: CMakeLists.txt 添加 add_compile_definitions(ADC_DEBUG
 * ADC_DEBUG_INTERVAL_MS=1000) ADC_DEBUG_INTERVAL_MS: 打印间隔 (ms), 范围 [10,
 * 10000]
 * ========================================================================= */
#if defined(ADC_DEBUG) && defined(ADC_DEBUG_INTERVAL_MS)
#if ADC_DEBUG_INTERVAL_MS < 10
#undef ADC_DEBUG_INTERVAL_MS
#define ADC_DEBUG_INTERVAL_MS 10
#elif ADC_DEBUG_INTERVAL_MS > 10000
#undef ADC_DEBUG_INTERVAL_MS
#define ADC_DEBUG_INTERVAL_MS 10000
#endif
#define ADC_DEBUG_MAX_SAMPLES 300
static bool s_dbg_active = false;
static uint32_t s_dbg_last_print = 0;
static uint32_t s_dbg_last_sa_chk = 0;
static bool s_dbg_sa_prev = false;
static uint16_t s_dbg_buf[4][ADC_DEBUG_MAX_SAMPLES];
static uint16_t s_dbg_cnt = 0;
static const char *s_dbg_names[4] = {"Roll", "Pitch", "Thr", "Yaw"};

static int cmp_u16(const void *a, const void *b) {
  uint16_t va = *(const uint16_t *)a;
  uint16_t vb = *(const uint16_t *)b;
  return (va > vb) - (va < vb);
}

static void adc_debug_print_summary(void) {
  if (s_dbg_cnt == 0) {
    ESP_LOGI("ADC_DBG", "无样本");
    return;
  }
  for (int ch = 0; ch < 4; ch++) {
    uint16_t sorted[ADC_DEBUG_MAX_SAMPLES];
    uint32_t sum = 0;
    uint16_t vmin = 4095, vmax = 0;
    for (int i = 0; i < s_dbg_cnt; i++) {
      uint16_t v = s_dbg_buf[ch][i];
      sorted[i] = v;
      sum += v;
      if (v < vmin)
        vmin = v;
      if (v > vmax)
        vmax = v;
    }
    uint16_t mean = (uint16_t)(sum / s_dbg_cnt);
    qsort(sorted, s_dbg_cnt, sizeof(uint16_t), cmp_u16);
    uint16_t median = sorted[s_dbg_cnt / 2];
    uint16_t p10 = sorted[s_dbg_cnt * 10 / 100];
    uint16_t p90 = sorted[s_dbg_cnt * 90 / 100];
    ESP_LOGI("ADC_DBG",
             "[%s] n=%u mean=%u med=%u min=%u max=%u p10=%u p90=%u Δ=%u",
             s_dbg_names[ch], s_dbg_cnt, mean, median, vmin, vmax, p10, p90,
             vmax - vmin);
  }
}

static void adc_debug_poll(const uint16_t src[16], const uint16_t src_raw[16]) {
  uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

  if (now - s_dbg_last_sa_chk >= 50) {
    s_dbg_last_sa_chk = now;
    bool sa = (gpio_get_level(RC_SWITCH_SA_PIN) == 0);
    if (sa && !s_dbg_sa_prev) {
      s_dbg_active = !s_dbg_active;
      if (s_dbg_active) {
        s_dbg_cnt = 0;
        s_dbg_last_print = now;
        ESP_LOGI("ADC_DBG", "========== 进入调试模式 ==========");
      } else {
        s_dbg_active = false;
        adc_debug_print_summary();
        ESP_LOGI("ADC_DBG", "========== 退出调试模式 ==========");
      }
    }
    s_dbg_sa_prev = sa;
  }

  if (!s_dbg_active)
    return;
  if (now - s_dbg_last_print < ADC_DEBUG_INTERVAL_MS)
    return;
  s_dbg_last_print = now;

  ESP_LOGI("ADC_DBG", "#%u R:r%u→%u P:r%u→%u T:r%u→%u Y:r%u→%u", s_dbg_cnt,
           src_raw[0], src[0], src_raw[1], src[1], src_raw[2], src[2],
           src_raw[3], src[3]);

  if (s_dbg_cnt < ADC_DEBUG_MAX_SAMPLES) {
    for (int i = 0; i < 4; i++)
      s_dbg_buf[i][s_dbg_cnt] = src[i];
    s_dbg_cnt++;
  }
}
#else
#define adc_debug_poll(src, raw) ((void)0)
#endif

static TaskHandle_t s_task_handle;
static adc_channel_t channel[4] = {ADC_roll, ADC_pitch, ADC_throttle, ADC_yaw};

channel_cal_t limit[16] = {
    //  min,  mid,  max
    {999, 1500, 2001}, // CH1 (Roll)
    {999, 1500, 2001}, // CH2 (Pitch)
    {999, 1500, 2001}, // CH3 (Throttle)
    {999, 1500, 2001}, // CH4 (Yaw)
    {999, 1500, 2001}, // CH5
    {999, 1500, 2001}, // CH6
    {999, 1500, 2001}, // CH7
    {999, 1500, 2001}, // CH8
    {999, 1500, 2001}, // CH9
    {999, 1500, 2001}, // CH10
    {999, 1500, 2001}, // CH11
    {999, 1500, 2001}, // CH12
    {999, 1500, 2001}, // CH13
    {999, 1500, 2001}, // CH14
    {999, 1500, 2001}, // CH15
    {999, 1500, 2001}  // CH16
};

uint8_t epa_pos[16] = {100, 100, 100, 100, 100, 100, 100, 100,
                       100, 100, 100, 100, 100, 100, 100, 100};
uint8_t epa_neg[16] = {100, 100, 100, 100, 100, 100, 100, 100,
                       100, 100, 100, 100, 100, 100, 100, 100};
uint16_t rev_mask = 0;

// 预留字段
uint8_t ch_map[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
uint8_t stick_mode = 2;
uint8_t btn_cfg[4] = {0};

portMUX_TYPE cfg_lock = portMUX_INITIALIZER_UNLOCKED;

static const char *TAG = "ADC";

static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle,
                                     const adc_continuous_evt_data_t *edata,
                                     void *user_data) {
  BaseType_t mustYield = pdFALSE;

  vTaskNotifyGiveFromISR(s_task_handle, &mustYield);

  return (mustYield == pdTRUE);
}

static void continuous_adc_init(adc_channel_t *channel, uint8_t channel_num,
                                adc_continuous_handle_t *out_handle) {
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
  for (int i = 0; i < channel_num; i++) {
    adc_pattern[i].atten = ADC_ATTEN_DB_12;
    adc_pattern[i].channel = channel[i];
    adc_pattern[i].unit = ADC_UNIT_1;
    adc_pattern[i].bit_width = ADC_BITWIDTH_12;

    ESP_LOGI(TAG, "adc_pattern[%d].atten is :%" PRIx8, i, adc_pattern[i].atten);
    ESP_LOGI(TAG, "adc_pattern[%d].channel is :%" PRIx8, i,
             adc_pattern[i].channel);
    ESP_LOGI(TAG, "adc_pattern[%d].unit is :%" PRIx8, i, adc_pattern[i].unit);
  }
  dig_cfg.adc_pattern = adc_pattern;
  ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));

  *out_handle = handle;
}

// 将原始 ADC 值，根据三点校准，映射到 1000~2000 (标准 FPV 范围)
static uint16_t map_joystick(uint16_t raw, channel_cal_t cal) {
  // 1. 限制两端死区，防止电位器抖动超过极值
  if (raw <= cal.raw_min)
    return 1000;
  if (raw >= cal.raw_max)
    return 2000;

  // 2. 分段线性映射
  if (raw < cal.raw_mid) {
    // 下半段: min ~ mid 映射到 1000 ~ 1500
    if (cal.raw_mid == cal.raw_min)
      return 1000; // 防止除以 0
    return 1000 +
           (uint32_t)(raw - cal.raw_min) * 500 / (cal.raw_mid - cal.raw_min);
  } else {
    // 上半段: mid ~ max 映射到 1500 ~ 2000
    if (cal.raw_max == cal.raw_mid)
      return 1500; // 防止除以 0
    return 1500 +
           (uint32_t)(raw - cal.raw_mid) * 500 / (cal.raw_max - cal.raw_mid);
  }
}

// EPA 行程缩放 + REV 反向，在 map_joystick 之后调用
static uint16_t apply_epa_rev(uint16_t mapped, uint8_t ch) {
  if (ch >= 16)
    return mapped;

  // 1. REV 反向：绕 1500 镜像
  if (rev_mask & (1 << ch)) {
    mapped = 3000 - mapped;
  }

  // 2. EPA 行程缩放
  int32_t offset = (int32_t)mapped - 1500;
  uint8_t scale = (offset >= 0) ? epa_pos[ch] : epa_neg[ch];
  if (scale != 100) {
    offset = offset * scale / 100;
    mapped = (uint16_t)(1500 + offset);
  }

  // 钳位在 1000~2000
  if (mapped < 1000)
    mapped = 1000;
  if (mapped > 2000)
    mapped = 2000;
  return mapped;
}

// 将映射值和原始值写入 joy 结构体的对应字段
static void set_channel(fpv_joystick_report_t *joy, uint8_t idx,
                        uint16_t mapped, uint16_t raw) {
  switch (idx) {
  case 0:
    joy->roll = mapped;
    joy->raw_roll = raw;
    break;
  case 1:
    joy->pitch = mapped;
    joy->raw_pitch = raw;
    break;
  case 2:
    joy->throttle = mapped;
    joy->raw_throttle = raw;
    break;
  case 3:
    joy->yaw = mapped;
    joy->raw_yaw = raw;
    break;
  case 4:
    joy->aux1 = mapped;
    joy->raw_aux1 = raw;
    break;
  case 5:
    joy->aux2 = mapped;
    joy->raw_aux2 = raw;
    break;
  case 6:
    joy->aux3 = mapped;
    joy->raw_aux3 = raw;
    break;
  case 7:
    joy->aux4 = mapped;
    joy->raw_aux4 = raw;
    break;
  case 8:
    joy->sw1 = mapped;
    break;
  case 9:
    joy->sw2 = mapped;
    break;
  case 10:
    joy->sw3 = mapped;
    break;
  case 11:
    joy->sw4 = mapped;
    break;
  case 12:
    joy->sw5 = mapped;
    break;
  case 13:
    joy->sw6 = mapped;
    break;
  case 14:
    joy->sw7 = mapped;
    break;
  case 15:
    joy->sw8 = mapped;
    break;
  }
}

// 通道映射管道: 源数据 → ch_map 重映射 → EPA/REV → joy 结构体
static void apply_ch_map_to_joy(fpv_joystick_report_t *joy,
                                const uint16_t src[16],
                                const uint16_t src_raw[16]) {
  for (uint8_t i = 0; i < 16; i++) {
    uint8_t s = ch_map[i];
    uint16_t val, raw;
    if (s < 16) {
      val = src[s];
      raw = src_raw[s];
    } else {
      val = 1500;
      raw = 1500;
    }
    val = apply_epa_rev(val, i);
    set_channel(joy, i, val, raw);
  }
}

// =================================================================================
// 滑动窗口均值滤波配置
// =================================================================================
#define FILTER_N                                                               \
  16 // 滤波窗口大小。值越大摇杆越平滑，但响应会有微小延迟；值越小越灵敏。16
     // 是个不错的平衡点。
#define MAX_ADC_CH 10 // ESP32 ADC1 最多通常是 8 或 10 个通道，定义 10 足够安全

static uint16_t moving_average_filter(uint32_t ch, uint16_t new_val) {
  // 使用静态二维数组，分别为每个通道保存独立的历史数据
  static uint16_t history[MAX_ADC_CH][FILTER_N] = {0};
  static uint8_t idx[MAX_ADC_CH] = {0};
  static uint32_t sum[MAX_ADC_CH] = {0};
  static uint8_t count[MAX_ADC_CH] = {0};

  // 安全检查，防止意外的通道号导致数组越界
  if (ch >= MAX_ADC_CH)
    return new_val;

  // 滑动窗口核心逻辑：减去最老的值，加上最新的值
  sum[ch] -= history[ch][idx[ch]];
  history[ch][idx[ch]] = new_val;
  sum[ch] += new_val;

  // 移动覆盖指针，满一圈回到起点 (环形队列)
  idx[ch]++;
  if (idx[ch] >= FILTER_N)
    idx[ch] = 0;

  // 处理刚开机时的边界情况：如果数组还没填满，就除以当前已有的个数，防止初始数据偏小
  // (摇杆归零)
  if (count[ch] < FILTER_N) {
    count[ch]++;
    return sum[ch] / count[ch];
  }

  return sum[ch] / FILTER_N;
}
void key_init(void) {

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

// SA/SD 按键触发状态 (per-pin)
static struct {
  int last_level;
  int toggle;
  int press_cnt;
  TickType_t first_pt;
} s_sa_sd[2];

static int sa_sd_idx(gpio_num_t pin) {
  return (pin == RC_SWITCH_SA_PIN) ? 1 : 0;
}

/**
 * @brief SA/SD 自复位按键三模式读取
 * @param pin  GPIO 编号
 * @param mode 0=触摸(默认), 1=单击切换, 2=双击切换
 * @return uint16_t 1000 或 2000
 */
static uint16_t read_sa_sd(gpio_num_t pin, uint8_t mode) {
  int level = gpio_get_level(pin);
  int idx = sa_sd_idx(pin);

  if (mode == 0) {
    // 触摸: 按住=2000, 松开=1000 (上拉输入，按下=0，松开=1)
    s_sa_sd[idx].toggle = 0;
    return level ? 1000 : 2000;
  }

  // 检测上升沿 (松开→按下)
  if (s_sa_sd[idx].last_level == 0 && level == 1) {
    if (mode == 1) {
      // 单击: 每次按下翻转
      s_sa_sd[idx].toggle = !s_sa_sd[idx].toggle;
    } else if (mode == 2) {
      // 双击: 500ms 内两次才翻转
      TickType_t now = xTaskGetTickCount();
      if (s_sa_sd[idx].press_cnt == 0 ||
          (now - s_sa_sd[idx].first_pt) > pdMS_TO_TICKS(500)) {
        s_sa_sd[idx].first_pt = now;
        s_sa_sd[idx].press_cnt = 1;
      } else {
        s_sa_sd[idx].press_cnt++;
        if (s_sa_sd[idx].press_cnt >= 2) {
          s_sa_sd[idx].toggle = !s_sa_sd[idx].toggle;
          s_sa_sd[idx].press_cnt = 0;
        }
      }
    }
  }
  s_sa_sd[idx].last_level = level;
  return s_sa_sd[idx].toggle ? 2000 : 1000;
}

// SC 3-pos 二态模式: 记录上次非中位值
static struct {
  gpio_num_t pin;
  uint16_t last_non_mid;
} s_sc_last = {GPIO_NUM_NC, 1500};

/**
 * @brief SB/SC 拨码开关两模式读取
 * @param pin1  主 GPIO (SB 唯一引脚, SC 上引脚)
 * @param pin2  SC 下引脚; 0xFF 表示 2 段开关
 * @param mode  0=三态(默认), 1=二态(中位归最近端)
 * @return uint16_t 1000/1500/2000
 */
static uint16_t read_sb_sc(gpio_num_t pin1, gpio_num_t pin2, uint8_t mode) {
  if (pin2 == 0xFF) {
    // 2 段开关 (SB)
    return read_2pos_switch(pin1, false);
  }

  if (mode == 0) {
    return read_3pos_switch(pin1, pin2);
  }

  // 二态: 中位 → 最近的非中位值
  int up_val = gpio_get_level(pin1);
  int down_val = gpio_get_level(pin2);
  if (s_sc_last.pin == GPIO_NUM_NC)
    s_sc_last.pin = pin1;

  if (up_val == 0 && down_val == 1) {
    s_sc_last.last_non_mid = 2000;
    return 2000;
  }
  if (up_val == 1 && down_val == 0) {
    s_sc_last.last_non_mid = 1000;
    return 1000;
  }
  return s_sc_last.last_non_mid;
}

/**
 * @brief 读取实体开关，填入源数组 (不经 EPA/REV，由 apply_ch_map_to_joy
 * 统一处理)
 * @param src     源映射值数组 (1000~2000)
 * @param src_raw 源原始值数组 (开关无 ADC，与 src 相同)
 */
void update_switch_channels(uint16_t src[16], uint16_t src_raw[16]) {
  // 4个实体开关 → 物理源索引 4~7
  src[4] = read_sa_sd(RC_SWITCH_SA_PIN, btn_cfg[0]);       // SA → 物理源4
  src[5] = read_sb_sc(RC_SWITCH_SB_PIN, 0xFF, btn_cfg[1]); // SB → 物理源5
  src[6] = read_sb_sc(RC_SWITCH_SC_UP_PIN, RC_SWITCH_SC_DOWN_PIN,
                      btn_cfg[2]);                   // SC → 物理源6
  src[7] = read_sa_sd(RC_SWITCH_SD_PIN, btn_cfg[3]); // SD → 物理源7

  src_raw[4] = src[4];
  src_raw[5] = src[5];
  src_raw[6] = src[6];
  src_raw[7] = src[7];
}

void ADC_TASK(void *arg) {
  uint32_t ret_num = 0;
  uint8_t result[READ_LEN] = {0};
  fpv_joystick_report_t *joy = (fpv_joystick_report_t *)arg;

  s_task_handle = xTaskGetCurrentTaskHandle();

  adc_continuous_handle_t handle = NULL;
  continuous_adc_init(channel, sizeof(channel) / sizeof(adc_channel_t),
                      &handle);

  adc_continuous_evt_cbs_t cbs = {
      .on_conv_done = s_conv_done_cb,
  };

  ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(handle, &cbs, NULL));
  key_init(); // 必须在 adc_continuous_start 之前，否则 gpio_config 会覆盖 GPIO
              // 39 的 ADC 配置
  ESP_ERROR_CHECK(adc_continuous_start(handle));
  uint32_t adc_stall_ms = 0;
  while (1) {
    // 等待 ISR 通知，超时 200ms 防止 ADC 外设静默停止
    uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
    if (notified == 0) {
      adc_stall_ms += 200;
      if (adc_stall_ms == 200) {
        ESP_LOGW(TAG, "ADC ISR 超时，外设可能已停止");
      }
      // 连续超时超过 2 秒 → 尝试重启 ADC 外设
      if (adc_stall_ms >= 2000) {
        ESP_LOGW(TAG, "ADC 已停止 %lums，尝试重启...", adc_stall_ms);
        adc_continuous_stop(handle);
        vTaskDelay(pdMS_TO_TICKS(10));
        adc_continuous_start(handle);
        adc_stall_ms = 0;
      }
    } else {
      adc_stall_ms = 0;
    }

    uint16_t src[16] = {1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500,
                        1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500};
    uint16_t src_raw[16] = {1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500,
                            1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500};

    // 一次性排空 DMA 缓冲区中的所有数据
    while (1) {
      if (adc_continuous_read(handle, result, READ_LEN, &ret_num, 0) ==
          ESP_OK) {
        uint32_t num_parsed_samples = 0;
        adc_continuous_data_t parsed_data[ret_num / SOC_ADC_DIGI_RESULT_BYTES];
        esp_err_t parse_ret = adc_continuous_parse_data(
            handle, result, ret_num, parsed_data, &num_parsed_samples);

        if (parse_ret == ESP_OK) {
          uint32_t batch_sum[MAX_ADC_CH] = {0};
          uint8_t batch_count[MAX_ADC_CH] = {0};

          for (int i = 0; i < num_parsed_samples; i++) {
            if (parsed_data[i].valid) {
              uint32_t ch = parsed_data[i].channel;
              if (ch < MAX_ADC_CH) {
                batch_sum[ch] += parsed_data[i].raw_data;
                batch_count[ch]++;
              }
            }
          }

          for (int ch = 0; ch < MAX_ADC_CH; ch++) {
            if (batch_count[ch] > 0) {
              uint16_t batch_avg = batch_sum[ch] / batch_count[ch];
              uint16_t final_raw = moving_average_filter(ch, batch_avg);

              // ADC→1000~2000 映射
              switch (ch) {
              case ADC_roll:
                src[0] = 3000 - map_joystick(final_raw, limit[0]);
                src_raw[0] = final_raw;
                break;

              case ADC_pitch: {
                uint8_t out = (stick_mode == 1) ? 2 : 1;
                src[out] = map_joystick(final_raw, limit[1]);
                src_raw[out] = final_raw;
                break;
              }

              case ADC_throttle: {
                uint8_t out = (stick_mode == 1) ? 1 : 2;
                src[out] = map_joystick(final_raw, limit[2]);
                src_raw[out] = final_raw;
                break;
              }

              case ADC_yaw:
                src[3] = 3000 - map_joystick(final_raw, limit[3]);
                src_raw[3] = final_raw;
                break;
              }
            }
          }
        }
      } else {
        // DMA 缓冲已排空，退出内层循环，回到外层等待 ISR
        break;
      }
    }

    // 读取开关 → 填 src[4..7]
    update_switch_channels(src, src_raw);
    adc_debug_poll(src, src_raw);
    // ch_map 重映射 + EPA/REV → 写入 joy 结构体 (临界区: 不可与写入者交错)
    portENTER_CRITICAL(&cfg_lock);
    apply_ch_map_to_joy(joy, src, src_raw);
    portEXIT_CRITICAL(&cfg_lock);
  }
}