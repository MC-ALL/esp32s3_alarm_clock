#include <environment_bh1750.h>

#include <hw_config.h>

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "env_bh1750";

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_bh1750_dev;

int environment_bh1750_init(void)
{
	const i2c_master_bus_config_t bus_config = {
		.i2c_port = APP_BH1750_I2C_PORT,
		.sda_io_num = APP_PIN_BH1750_SDA,
		.scl_io_num = APP_PIN_BH1750_SCL,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags.enable_internal_pullup = true,
	};
	const i2c_device_config_t dev_config = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = APP_BH1750_I2C_ADDR,
		.scl_speed_hz = APP_BH1750_I2C_HZ,
	};

	esp_err_t err = i2c_new_master_bus(&bus_config, &s_i2c_bus);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
		return (int)err;
	}

	err = i2c_master_bus_add_device(s_i2c_bus, &dev_config, &s_bh1750_dev);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
		environment_bh1750_deinit();
		return (int)err;
	}

	return 0;
}

void environment_bh1750_deinit(void)
{
	if (s_bh1750_dev != NULL) {
		(void)i2c_master_bus_rm_device(s_bh1750_dev);
		s_bh1750_dev = NULL;
	}

	if (s_i2c_bus != NULL) {
		(void)i2c_del_master_bus(s_i2c_bus);
		s_i2c_bus = NULL;
	}
}

int environment_bh1750_measure_lux(float *lux_out)
{
	if (s_bh1750_dev == NULL || lux_out == NULL) {
		return -1;
	}

	static const uint8_t bh1750_cmd = 0x10;
	uint8_t raw[2] = { 0 };

	esp_err_t err = i2c_master_transmit(s_bh1750_dev, &bh1750_cmd, sizeof(bh1750_cmd), 100);
	if (err != ESP_OK) {
		return (int)err;
	}

	vTaskDelay(pdMS_TO_TICKS(180));

	err = i2c_master_receive(s_bh1750_dev, raw, sizeof(raw), 100);
	if (err != ESP_OK) {
		return (int)err;
	}

	const uint16_t level = ((uint16_t)raw[0] << 8) | raw[1];
	*lux_out = (float)level / 1.2f;
	return 0;
}
