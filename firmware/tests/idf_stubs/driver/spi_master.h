#pragma once
#include "idf_common.h"
typedef enum { SPI1_HOST, SPI2_HOST, SPI3_HOST, SPI_HOST_MAX } spi_host_device_t;
typedef struct spi_device_t *spi_device_handle_t;
typedef struct { int x; } spi_bus_config_t; typedef struct { int x; } spi_device_interface_config_t; typedef struct { int x; } spi_transaction_t;
