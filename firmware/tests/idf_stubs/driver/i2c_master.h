#pragma once
#include "idf_common.h"
typedef struct i2c_master_bus_t *i2c_master_bus_handle_t; typedef struct i2c_master_dev_t *i2c_master_dev_handle_t;
typedef int i2c_port_num_t; typedef enum { I2C_NUM_0, I2C_NUM_1, I2C_NUM_MAX } i2c_port_t;
typedef struct { int x; } i2c_master_bus_config_t; typedef struct { int x; } i2c_device_config_t; typedef struct { int x; } i2c_operation_job_t;
