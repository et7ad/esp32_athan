#pragma once
#include "idf_common.h"
#include "driver/gpio.h"
typedef enum { I2S_ROLE_MASTER, I2S_ROLE_SLAVE } i2s_role_t;
typedef enum { I2S_SLOT_MODE_MONO = 1, I2S_SLOT_MODE_STEREO = 2 } i2s_slot_mode_t;
typedef enum { I2S_STD_SLOT_LEFT = 1, I2S_STD_SLOT_RIGHT = 2, I2S_STD_SLOT_BOTH = 3 } i2s_std_slot_mask_t;
typedef enum { I2S_SLOT_BIT_WIDTH_AUTO = 0, I2S_SLOT_BIT_WIDTH_8BIT = 8, I2S_SLOT_BIT_WIDTH_16BIT = 16, I2S_SLOT_BIT_WIDTH_24BIT = 24, I2S_SLOT_BIT_WIDTH_32BIT = 32 } i2s_slot_bit_width_t;
typedef enum { I2S_DATA_BIT_WIDTH_8BIT = 8, I2S_DATA_BIT_WIDTH_16BIT = 16, I2S_DATA_BIT_WIDTH_24BIT = 24, I2S_DATA_BIT_WIDTH_32BIT = 32 } i2s_data_bit_width_t;
typedef enum { I2S_MCLK_MULTIPLE_128 = 128, I2S_MCLK_MULTIPLE_192 = 192, I2S_MCLK_MULTIPLE_256 = 256, I2S_MCLK_MULTIPLE_384 = 384, I2S_MCLK_MULTIPLE_512 = 512, I2S_MCLK_MULTIPLE_576 = 576, I2S_MCLK_MULTIPLE_768 = 768, I2S_MCLK_MULTIPLE_1024 = 1024, I2S_MCLK_MULTIPLE_1152 = 1152 } i2s_mclk_multiple_t;
typedef enum { I2S_NUM_0, I2S_NUM_1, I2S_NUM_AUTO } i2s_port_t;
typedef struct i2s_channel_obj_t *i2s_chan_handle_t;
typedef enum { I2S_CLK_SRC_DEFAULT } i2s_clock_src_t;
typedef struct { int x; } i2s_std_config_t; typedef struct { int x; } i2s_chan_config_t; typedef struct { int x; } i2s_event_callbacks_t; typedef struct { int x; } i2s_event_data_t;
typedef bool (*i2s_isr_callback_t)(i2s_chan_handle_t, i2s_event_data_t *, void *);
