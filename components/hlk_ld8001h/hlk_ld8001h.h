#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/log.h"

namespace esphome
{
	namespace hlk_ld8001h
	{

		static const char *const TAG = "hlk_ld8001h";

		// MODBUS register addresses
		static const uint16_t REG_SPACE_HEIGHT = 0x0001;		// R/- Space height (mm)
		static const uint16_t REG_WATER_LEVEL = 0x0003;			// R/- Water level (mm)
		static const uint16_t REG_INSTALLATION_HEIGHT = 0x0005; // R/W Installation height (cm)
		static const uint16_t REG_DEVICE_ADDRESS = 0x03F4;		// R/W Device address
		static const uint16_t REG_BAUD_RATE = 0x03F6;			// R/W Baud rate
		static const uint16_t REG_RANGE = 0x07D4;				// R/W Range (m)

		class HLKLD8001HSensor : public sensor::Sensor, public PollingComponent, public uart::UARTDevice
		{
		public:
			HLKLD8001HSensor() : PollingComponent(2000) {}

			void set_installation_height(uint16_t installation_height) { this->installation_height_ = installation_height; }
			void set_range(uint16_t range) { this->range_ = range; }
			void set_modbus_address(uint8_t modbus_address) { this->modbus_address_ = modbus_address; }
			void set_water_depth_sensor(sensor::Sensor *water_depth_sensor) { this->water_depth_sensor_ = water_depth_sensor; }
			void set_has_installation_height(bool has_installation_height) { this->has_installation_height_ = has_installation_height; }

			void setup() override
			{
				ESP_LOGCONFIG(TAG, "Setting up HLK-LD8001H...");

				// Initialize variables
				this->last_successful_read_ = 0;
				this->setup_complete_ = false;
				this->last_modbus_operation_ = 0;

				// Configure the sensor
				if (configure_sensor())
				{
					ESP_LOGI(TAG, "HLK-LD8001H setup complete");
					this->setup_complete_ = true;
				}
				else
				{
					ESP_LOGW(TAG, "HLK-LD8001H setup incomplete - will retry during updates");
				}
			}

			void dump_config() override
			{
				ESP_LOGCONFIG(TAG, "HLK-LD8001H Radar Sensor:");
				LOG_SENSOR("  ", "Distance to Water", this);
				if (this->water_depth_sensor_ != nullptr && this->has_installation_height_)
				{
					LOG_SENSOR("  ", "Water Depth", this->water_depth_sensor_);
				}
				if (this->has_installation_height_)
				{
					ESP_LOGCONFIG(TAG, "  Installation Height: %dcm", this->installation_height_);
				}
				else
				{
					ESP_LOGCONFIG(TAG, "  Installation Height: Not set");
				}
				ESP_LOGCONFIG(TAG, "  Range: %dm", this->range_);
				ESP_LOGCONFIG(TAG, "  Modbus Address: 0x%02X", this->modbus_address_);
				LOG_UPDATE_INTERVAL(this);
				check_uart_settings(115200);
			}

			void update() override
			{
				// If setup wasn't completed, try again
				if (!this->setup_complete_)
				{
					ESP_LOGI(TAG, "Retrying setup...");
					if (configure_sensor())
					{
						ESP_LOGI(TAG, "HLK-LD8001H setup complete");
						this->setup_complete_ = true;
					}
					else
					{
						ESP_LOGW(TAG, "HLK-LD8001H setup still incomplete - will retry next update");
						return;
					}
				}

				// Read empty height (distance to water)
				float empty_height = read_space_height();

				if (empty_height >= 0)
				{
					// Valid reading for empty height
					publish_state(empty_height);
					this->last_successful_read_ = millis();
					ESP_LOGD(TAG, "Published empty height: %.1f mm", empty_height);

					// Read water level if a sensor is attached and installation height is set
					if (this->water_depth_sensor_ != nullptr && this->has_installation_height_)
					{
						float water_level = read_water_level();

						if (water_level >= 0)
						{
							this->water_depth_sensor_->publish_state(water_level);
							ESP_LOGD(TAG, "Published water level: %.1f mm", water_level);
						}
						else
						{
							ESP_LOGW(TAG, "Failed to read water level from device");
						}
					}
				}
				else
				{
					// Check if we haven't had a successful reading in a while
					if (millis() - this->last_successful_read_ > 30000)
					{
						ESP_LOGW(TAG, "No valid readings for over 30 seconds!");

						// Try to reconfigure if no readings
						ESP_LOGI(TAG, "Attempting to reconfigure sensor...");
						configure_sensor();
					}
				}
			}

		protected:
			uint16_t installation_height_{200}; // Default 2m (200cm)
			uint16_t range_{10};				// Default 10m
			uint8_t modbus_address_{0x01};		// Default address 1
			uint32_t last_successful_read_{0};
			uint32_t last_modbus_operation_{0};
			bool setup_complete_{false};
			bool has_installation_height_{false};
			sensor::Sensor *water_depth_sensor_{nullptr};

			// Calculate CRC16 (MODBUS)
			uint16_t calculate_crc(uint8_t *buffer, uint8_t length)
			{
				uint16_t crc = 0xFFFF;

				for (uint8_t i = 0; i < length; i++)
				{
					crc ^= (uint16_t)buffer[i];

					for (uint8_t j = 0; j < 8; j++)
					{
						if (crc & 0x0001)
						{
							crc >>= 1;
							crc ^= 0xA001;
						}
						else
						{
							crc >>= 1;
						}
					}
				}

				return crc;
			}

			// Send MODBUS read request and get response
			bool modbus_read_register(uint16_t reg_address, uint16_t &value, int max_attempts = 3)
			{
				uint8_t request[8];
				uint8_t response[7];

				// Ensure we don't perform operations too quickly
				uint32_t now = millis();
				uint32_t elapsed = now - this->last_modbus_operation_;
				if (elapsed < 30)
				{
					// Add a small delay to prevent operations from taking too long
					delay(30 - elapsed);
				}

				// Retry up to max_attempts times
				for (int attempt = 0; attempt < max_attempts; attempt++)
				{
					// Clear any data in the buffer
					while (available())
					{
						read();
					}

					// Prepare read request
					request[0] = this->modbus_address_; // Modbus address
					request[1] = 0x03;					// Function code (read holding register)
					request[2] = reg_address >> 8;		// Register address high byte
					request[3] = reg_address & 0xFF;	// Register address low byte
					request[4] = 0x00;					// Number of registers high byte
					request[5] = 0x01;					// Number of registers low byte

					// Calculate CRC
					uint16_t crc = calculate_crc(request, 6);
					request[6] = crc & 0xFF; // CRC low byte
					request[7] = crc >> 8;	 // CRC high byte

					// Send request
					ESP_LOGV(TAG, "Sending MODBUS read request for register 0x%04X (attempt %d)", reg_address, attempt + 1);
					write_array(request, 8);

					// Record the time of this operation
					this->last_modbus_operation_ = millis();

					// Wait for response with shorter timeouts
					uint32_t start_time = millis();
					uint8_t bytes_available = 0;
					while ((bytes_available = available()) < 7 && (millis() - start_time < 100))
					{
						// Use shorter delays
						delay(5);
					}

					// Check if we got a response
					if (bytes_available >= 7)
					{
						if (read_array(response, 7))
						{
							// Verify response
							if (response[0] == this->modbus_address_ && response[1] == 0x03 && response[2] == 0x02)
							{
								// Extract value
								value = (response[3] << 8) | response[4];

								// Verify CRC
								uint16_t expected_crc = (response[6] << 8) | response[5];
								uint16_t calculated_crc = calculate_crc(response, 5);

								if (expected_crc == calculated_crc)
								{
									ESP_LOGV(TAG, "Successfully read register 0x%04X: %d", reg_address, value);
									return true;
								}
								else
								{
									ESP_LOGW(TAG, "CRC error reading register 0x%04X", reg_address);
								}
							}
							else
							{
								ESP_LOGW(TAG, "Invalid response format");
							}
						}
						else
						{
							ESP_LOGW(TAG, "Failed to read response");
						}
					}
					else
					{
						ESP_LOGW(TAG, "No response or incomplete response");
					}

					// Shorter delay before retrying
					delay(30);
				}

				ESP_LOGW(TAG, "Failed to read register 0x%04X after %d attempts", reg_address, max_attempts);
				return false;
			}

			// Send MODBUS write request and verify
			bool modbus_write_register(uint16_t reg_address, uint16_t value, int max_attempts = 3)
			{
				uint8_t request[8];
				uint8_t response[8];

				// Ensure we don't perform operations too quickly
				uint32_t now = millis();
				uint32_t elapsed = now - this->last_modbus_operation_;
				if (elapsed < 30)
				{
					// Add a small delay to prevent operations from taking too long
					delay(30 - elapsed);
				}

				// Retry up to max_attempts times
				for (int attempt = 0; attempt < max_attempts; attempt++)
				{
					// Clear any data in the buffer
					while (available())
					{
						read();
					}

					// Prepare write request
					request[0] = this->modbus_address_; // Modbus address
					request[1] = 0x06;					// Function code (write single register)
					request[2] = reg_address >> 8;		// Register address high byte
					request[3] = reg_address & 0xFF;	// Register address low byte
					request[4] = value >> 8;			// Value high byte
					request[5] = value & 0xFF;			// Value low byte

					// Calculate CRC
					uint16_t crc = calculate_crc(request, 6);
					request[6] = crc & 0xFF; // CRC low byte
					request[7] = crc >> 8;	 // CRC high byte

					// Send request
					ESP_LOGV(TAG, "Sending MODBUS write request for register 0x%04X, value %d (attempt %d)",
							 reg_address, value, attempt + 1);
					write_array(request, 8);

					// Record the time of this operation
					this->last_modbus_operation_ = millis();

					// Wait for response with shorter timeouts
					uint32_t start_time = millis();
					uint8_t bytes_available = 0;
					while ((bytes_available = available()) < 8 && (millis() - start_time < 100))
					{
						// Use shorter delays
						delay(5);
					}

					// Check if we got a response
					if (bytes_available >= 8)
					{
						if (read_array(response, 8))
						{
							// Verify response (should echo back the request)
							if (response[0] == this->modbus_address_ && response[1] == 0x06 &&
								response[2] == request[2] && response[3] == request[3] &&
								response[4] == request[4] && response[5] == request[5])
							{

								// Verify CRC
								uint16_t expected_crc = (response[7] << 8) | response[6];
								uint16_t calculated_crc = calculate_crc(response, 6);

								if (expected_crc == calculated_crc)
								{
									ESP_LOGV(TAG, "Successfully wrote register 0x%04X: %d", reg_address, value);

									// Skip verification read to avoid long operations
									return true;
								}
								else
								{
									ESP_LOGW(TAG, "CRC error in write response");
								}
							}
							else
							{
								ESP_LOGW(TAG, "Invalid write response format");
							}
						}
						else
						{
							ESP_LOGW(TAG, "Failed to read write response");
						}
					}
					else
					{
						ESP_LOGW(TAG, "No response or incomplete response to write");
					}

					// Shorter delay before retrying
					delay(30);
				}

				ESP_LOGW(TAG, "Failed to write register 0x%04X after %d attempts", reg_address, max_attempts);
				return false;
			}

			// Read space height
			float read_space_height()
			{
				uint16_t raw_space_height;

				if (modbus_read_register(REG_SPACE_HEIGHT, raw_space_height))
				{
					return raw_space_height; // Value in mm
				}
				else
				{
					return -1; // Error
				}
			}

			// Read water level
			float read_water_level()
			{
				uint16_t raw_water_level;

				if (modbus_read_register(REG_WATER_LEVEL, raw_water_level))
				{
					return raw_water_level; // Value in mm
				}
				else
				{
					return -1; // Error
				}
			}

			// Configure the sensor
			bool configure_sensor()
			{
				ESP_LOGI(TAG, "Configuring HLK-LD8001H sensor...");
				bool success = true;
				delay(30); // Short delay to ensure the sensor is ready

				// Only configure installation height if it's specified
				if (this->has_installation_height_)
				{
					// Check current installation height
					uint16_t current_installation_height;
					if (modbus_read_register(REG_INSTALLATION_HEIGHT, current_installation_height))
					{
						ESP_LOGI(TAG, "Current installation height: %d cm", current_installation_height);

						// Update if different
						if (current_installation_height != this->installation_height_)
						{
							ESP_LOGI(TAG, "Setting installation height to %d cm", this->installation_height_);
							if (!modbus_write_register(REG_INSTALLATION_HEIGHT, this->installation_height_))
							{
								ESP_LOGW(TAG, "Failed to set installation height");
								success = false;
							}
						}
					}
					else
					{
						ESP_LOGW(TAG, "Failed to read current installation height");

						// Try to set it anyway
						ESP_LOGI(TAG, "Attempting to set installation height to %d cm", this->installation_height_);
						if (!modbus_write_register(REG_INSTALLATION_HEIGHT, this->installation_height_))
						{
							ESP_LOGW(TAG, "Failed to set installation height");
							success = false;
						}
					}
				}

				// Check current range
				uint16_t current_range;
				if (modbus_read_register(REG_RANGE, current_range))
				{
					ESP_LOGI(TAG, "Current range: %d m", current_range);

					// Update if different
					if (current_range != this->range_)
					{
						ESP_LOGI(TAG, "Setting range to %d m", this->range_);
						if (!modbus_write_register(REG_RANGE, this->range_))
						{
							ESP_LOGW(TAG, "Failed to set range");
							success = false;
						}
					}
				}
				else
				{
					ESP_LOGW(TAG, "Failed to read current range");

					// Try to set it anyway
					ESP_LOGI(TAG, "Attempting to set range to %d m", this->range_);
					if (!modbus_write_register(REG_RANGE, this->range_))
					{
						ESP_LOGW(TAG, "Failed to set range");
						success = false;
					}
				}

				// Update device address if not default (and only if other settings are successful)
				if (success && this->modbus_address_ != 0x01)
				{
					// We can only change the address if we're communicating successfully with the current address
					ESP_LOGI(TAG, "Setting device address to 0x%02X", this->modbus_address_);
					if (!modbus_write_register(REG_DEVICE_ADDRESS, this->modbus_address_))
					{
						ESP_LOGW(TAG, "Failed to set device address");
						success = false;
					}
				}

				return success;
			}
		};

	} // namespace hlk_ld8001h
} // namespace esphome