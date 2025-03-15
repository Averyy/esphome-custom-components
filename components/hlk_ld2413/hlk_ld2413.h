#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/log.h"

namespace esphome
{
  namespace hlk_ld2413
  {

    static const char *const TAG = "hlk_ld2413";

    // Protocol constants from datasheet section 5
    // For reading data (device to ESP)
    static const uint8_t FRAME_HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
    static const uint8_t FRAME_END[4] = {0xF8, 0xF7, 0xF6, 0xF5};

    // For sending commands (ESP to device)
    static const uint8_t COMMAND_HEADER[4] = {0xFD, 0xFC, 0xFB, 0xFA};
    static const uint8_t COMMAND_FOOTER[4] = {0x04, 0x03, 0x02, 0x01};

    // Command codes
    static const uint16_t CMD_READ_FIRMWARE_VERSION = 0x0000;
    static const uint16_t CMD_ENTER_CONFIG_MODE = 0x00FF;
    static const uint16_t CMD_EXIT_CONFIG_MODE = 0x00FE;
    static const uint16_t CMD_SET_MIN_DISTANCE = 0x0074;
    static const uint16_t CMD_SET_MAX_DISTANCE = 0x0075;
    static const uint16_t CMD_UPDATE_THRESHOLD = 0x0072;
    static const uint16_t CMD_SET_REPORT_CYCLE = 0x0071;
    static const uint16_t CMD_READ_REPORT_CYCLE = 0x0070;

    class HLKLD2413Sensor : public sensor::Sensor, public PollingComponent, public uart::UARTDevice
    {
    public:
      HLKLD2413Sensor() : PollingComponent(DEFAULT_REPORT_CYCLE) {}

      void set_min_distance(uint16_t min_distance) { this->min_distance_ = min_distance; }
      void set_max_distance(uint16_t max_distance) { this->max_distance_ = max_distance; }
      void set_report_cycle(uint16_t report_cycle) { this->report_cycle_ = report_cycle; }
      void set_calibrate_on_boot(bool calibrate_on_boot) { this->calibrate_on_boot_ = calibrate_on_boot; }

      void setup() override
      {
        ESP_LOGCONFIG(TAG, "Setting up HLK-LD2413...");
        flush();

        // Initialize counters
        this->frames_received_ = 0;
        this->last_successful_read_ = millis();
        this->last_buffer_check_ = millis();
        this->last_distance_ = 0;
        this->has_new_reading_ = false;

        // Wait for the sensor to initialize properly
        ESP_LOGI(TAG, "Waiting for sensor to initialize...");
        for (int i = 0; i < 10; i++)
        {
          delay(100);
          yield();
        }

        // Configure the sensor with the parameters
        configure_sensor();
        ESP_LOGI(TAG, "HLK-LD2413 setup complete");
        yield();
      }

      void loop() override
      {
        yield();
        if (available() > 0)
        {
          process_buffer(false);
        }
        yield();
      }

      void dump_config() override
      {
        ESP_LOGCONFIG(TAG, "HLK-LD2413 Radar Sensor:");
        LOG_SENSOR("  ", "Distance", this);
        ESP_LOGCONFIG(TAG, "  Min Distance: %dmm", this->min_distance_);
        ESP_LOGCONFIG(TAG, "  Max Distance: %dmm", this->max_distance_);
        ESP_LOGCONFIG(TAG, "  Report Cycle: %dms", this->report_cycle_);
        ESP_LOGCONFIG(TAG, "  Calibrate on Boot: %s", this->calibrate_on_boot_ ? "Yes" : "No");
        LOG_UPDATE_INTERVAL(this);
        check_uart_settings(115200);
      }

      void update() override
      {
        yield();
        process_buffer(true);

        if (this->has_new_reading_)
        {
          publish_state(this->last_distance_);
          ESP_LOGI(TAG, "Published distance: %.1f mm", this->last_distance_);
          this->has_new_reading_ = false;
        }
        else
        {
          uint32_t now = millis();
          if (now - this->last_successful_read_ > 5000)
          {
            ESP_LOGW(TAG, "No valid readings for over 5000 ms. Sensor may be disconnected or malfunctioning.");
            if (available() > 0)
            {
              ESP_LOGW(TAG, "Buffer has data but no valid readings. Dumping for debugging:");
              dump_buffer();
            }
            else
            {
              ESP_LOGW(TAG, "Buffer is empty. No data being received from sensor.");
            }
          }
        }
        yield();
      }

      // Dumps the contents of the buffer for debugging
      void dump_buffer()
      {
        int bytes_available = available();
        if (bytes_available == 0)
        {
          ESP_LOGD(TAG, "Buffer is empty");
          return;
        }

        int bytes_to_dump = std::min(bytes_available, 64);
        ESP_LOGD(TAG, "Dumping %d bytes from buffer (total available: %d)", bytes_to_dump, bytes_available);

        uint8_t buffer[64];
        if (!read_array(buffer, bytes_to_dump))
        {
          ESP_LOGW(TAG, "Failed to read data from buffer");
          return;
        }

        // Log the buffer contents in chunks of 16 bytes
        for (int i = 0; i < bytes_to_dump; i += 16)
        {
          yield();
          int chunk_size = std::min(16, bytes_to_dump - i);
          char log_str[100];
          char *ptr = log_str;

          for (int j = 0; j < chunk_size; j++)
          {
            ptr += sprintf(ptr, "%02X ", buffer[i + j]);
          }

          ESP_LOGD(TAG, "Buffer[%d-%d]: %s", i, i + chunk_size - 1, log_str);
        }

        // Check for data frame headers in the buffer
        bool found_frame = false;
        for (int i = 0; i < bytes_to_dump - 13; i++)
        {
          // Check for frame header
          if (buffer[i] == FRAME_HEADER[0] &&
              buffer[i + 1] == FRAME_HEADER[1] &&
              buffer[i + 2] == FRAME_HEADER[2] &&
              buffer[i + 3] == FRAME_HEADER[3])
          {
            found_frame = true;
            ESP_LOGI(TAG, "Found data frame header at position %d", i);

            // Check if we have enough bytes for a complete frame
            if (i + 13 < bytes_to_dump)
            {
              uint16_t data_length = buffer[i + 4] | (buffer[i + 5] << 8);
              ESP_LOGI(TAG, "Data frame length: %d", data_length);

              // Check for frame end
              if (buffer[i + 10] == FRAME_END[0] &&
                  buffer[i + 11] == FRAME_END[1] &&
                  buffer[i + 12] == FRAME_END[2] &&
                  buffer[i + 13] == FRAME_END[3])
              {
                // Extract the float distance value
                float distance;
                memcpy(&distance, &buffer[i + 6], 4);

                // Log the raw bytes of the distance
                ESP_LOGI(TAG, "Distance bytes: %02X %02X %02X %02X",
                         buffer[i + 6], buffer[i + 7], buffer[i + 8], buffer[i + 9]);
                ESP_LOGI(TAG, "Found complete data frame with distance: %.1f mm", distance);

                // Store the latest reading
                this->last_distance_ = distance;
                this->has_new_reading_ = true;
                this->frames_received_++;
                this->last_successful_read_ = millis();
                break;
              }
              else
              {
                ESP_LOGW(TAG, "Found data frame header but end sequence doesn't match at position %d", i);
                ESP_LOGW(TAG, "Expected: %02X %02X %02X %02X, Got: %02X %02X %02X %02X",
                         FRAME_END[0], FRAME_END[1], FRAME_END[2], FRAME_END[3],
                         buffer[i + 10], buffer[i + 11], buffer[i + 12], buffer[i + 13]);
              }
            }
            else
            {
              ESP_LOGW(TAG, "Found data frame header but not enough bytes for complete frame");
            }
          }
        }

        if (!found_frame)
        {
          ESP_LOGD(TAG, "No data frame headers found in buffer");
        }
      }

    protected:
      static const uint16_t DEFAULT_MIN_DISTANCE = 250;   // mm
      static const uint16_t DEFAULT_MAX_DISTANCE = 10000; // mm
      static const uint16_t DEFAULT_REPORT_CYCLE = 160;   // ms

      uint16_t min_distance_{DEFAULT_MIN_DISTANCE};
      uint16_t max_distance_{DEFAULT_MAX_DISTANCE};
      uint16_t report_cycle_{DEFAULT_REPORT_CYCLE};
      uint32_t last_successful_read_{0};
      uint32_t frames_received_{0};
      uint32_t last_buffer_check_{0};
      float last_distance_{0};
      bool has_new_reading_{false};
      bool calibrate_on_boot_{false};

      // Helper function to log a hex buffer
      void log_hex_buffer(const uint8_t *buffer, size_t length, const char *prefix, int log_level = 0)
      {
        if (length == 0)
          return;

        char hex_log[100] = {0};
        int pos = 0;

        for (size_t i = 0; i < length && pos < 95; i++)
        {
          pos += snprintf(hex_log + pos, 100 - pos, "%02X ", buffer[i]);
        }

        switch (log_level)
        {
        case 1: // Debug level
          ESP_LOGD(TAG, "%s: %s", prefix, hex_log);
          break;
        case 2: // Warning level
          ESP_LOGW(TAG, "%s: %s", prefix, hex_log);
          break;
        default: // Info level (0)
          ESP_LOGI(TAG, "%s: %s", prefix, hex_log);
        }
      }

      // Helper function to check if a response is valid
      bool is_valid_response(const uint8_t *buffer, size_t length, uint16_t expected_cmd, bool *status_ok = nullptr)
      {
        if (length < 14)
          return false;

        // Check header
        if (buffer[0] != COMMAND_HEADER[0] || buffer[1] != COMMAND_HEADER[1] ||
            buffer[2] != COMMAND_HEADER[2] || buffer[3] != COMMAND_HEADER[3])
        {
          return false;
        }

        // Check command
        uint16_t cmd = buffer[6] | (buffer[7] << 8);
        if (cmd != expected_cmd && buffer[6] != expected_cmd)
        {
          return false;
        }

        // Check status if requested
        if (status_ok != nullptr)
        {
          uint16_t status = buffer[8] | (buffer[9] << 8);
          *status_ok = (status == 0);
        }

        // Check footer
        if (buffer[10] != COMMAND_FOOTER[0] || buffer[11] != COMMAND_FOOTER[1] ||
            buffer[12] != COMMAND_FOOTER[2] || buffer[13] != COMMAND_FOOTER[3])
        {
          return false;
        }

        return true;
      }

      // Send a command to the sensor
      void send_command(uint16_t command, const uint8_t *data = nullptr, uint16_t data_length = 0)
      {
        static uint8_t buffer[32];                        // Max expected packet size
        size_t buffer_size = 4 + 2 + 2 + data_length + 4; // header + length + command + data + footer

        if (buffer_size > sizeof(buffer))
        {
          ESP_LOGE(TAG, "Command buffer overflow, needed %d bytes", buffer_size);
          return;
        }

        // Construct packet
        memcpy(buffer, COMMAND_HEADER, 4);

        // Data length (2 bytes, little endian)
        uint16_t payload_length = 2 + data_length; // Command + data
        buffer[4] = payload_length & 0xFF;
        buffer[5] = (payload_length >> 8) & 0xFF;

        // Command (2 bytes, little endian)
        buffer[6] = command & 0xFF;
        buffer[7] = (command >> 8) & 0xFF;

        // Data (if any)
        if (data != nullptr && data_length > 0)
        {
          memcpy(buffer + 8, data, data_length);
        }

        // Footer
        memcpy(buffer + 4 + 2 + 2 + data_length, COMMAND_FOOTER, 4);

        // Clear any pending data before sending
        flush();
        delay(50);

        // Send command
        ESP_LOGD(TAG, "Sending command 0x%04X with %d bytes of data", command, data_length);

        // Log the full command for debugging
        if (command == CMD_ENTER_CONFIG_MODE)
        {
          log_hex_buffer(buffer, buffer_size, "Command bytes", 1); // 1 = Debug level
        }

        this->write_array(buffer, buffer_size);
        delay(100); // Give device time to respond
      }

      // Send a command and wait for response
      bool send_command_and_wait(uint16_t command, const uint8_t *data = nullptr, uint16_t data_length = 0,
                                 const char *cmd_name = nullptr, uint32_t max_wait_time = 200, int max_attempts = 5)
      {
        if (cmd_name == nullptr)
        {
          cmd_name = "command";
        }

        ESP_LOGI(TAG, "Sending %s (0x%04X)", cmd_name, command);

        // Try multiple times
        for (int attempt = 0; attempt < max_attempts; attempt++)
        {
          if (attempt > 0)
          {
            ESP_LOGW(TAG, "Retrying %s (attempt %d of %d)", cmd_name, attempt + 1, max_attempts);
            delay(100);
            flush();
          }

          // Prepare and send the command
          if (data != nullptr && data_length > 0)
          {
            send_command(command, data, data_length);
          }
          else
          {
            send_command(command);
          }

          // Wait for response
          uint32_t start_time = millis();
          bool valid_ack_received = false;

          while (millis() - start_time < max_wait_time && !valid_ack_received)
          {
            int bytes_available = available();
            if (bytes_available > 0)
            {
              ESP_LOGD(TAG, "Received response to %s (%d bytes)", cmd_name, bytes_available);

              // Read response
              uint8_t ack_buffer[20] = {0};
              int bytes_to_read = std::min(bytes_available, 20);

              if (read_array(ack_buffer, bytes_to_read))
              {
                log_hex_buffer(ack_buffer, bytes_to_read, "Response");

                // Check if this is a valid ACK
                bool status_ok = false;
                if (is_valid_response(ack_buffer, bytes_to_read, command, &status_ok))
                {
                  if (status_ok)
                  {
                    ESP_LOGI(TAG, "Valid %s ACK received with SUCCESS status", cmd_name);
                  }
                  else
                  {
                    ESP_LOGW(TAG, "Valid %s ACK received with FAILURE status", cmd_name);
                  }
                  valid_ack_received = true;
                  break;
                }
                // Check if this looks like a data frame
                else if (bytes_to_read >= 4 &&
                         ack_buffer[0] == FRAME_HEADER[0] &&
                         ack_buffer[1] == FRAME_HEADER[1] &&
                         ack_buffer[2] == FRAME_HEADER[2] &&
                         ack_buffer[3] == FRAME_HEADER[3])
                {
                  if (command == CMD_EXIT_CONFIG_MODE)
                  {
                    ESP_LOGI(TAG, "Received data frame instead of ACK - sensor is already in data mode");
                    valid_ack_received = true;
                    break;
                  }
                  else
                  {
                    ESP_LOGW(TAG, "Received data frame instead of ACK - sensor may be in wrong mode");
                  }
                }
                else
                {
                  ESP_LOGW(TAG, "Unexpected response format for %s", cmd_name);
                }
              }

              // Flush any remaining data to start fresh
              flush();
            }
            delay(10);
          }

          // If we got a valid ACK, we're done
          if (valid_ack_received)
          {
            ESP_LOGI(TAG, "%s successful", cmd_name);
            delay(50); // Short delay to stabilize
            return true;
          }
        }

        // If we get here, we failed after multiple attempts
        ESP_LOGW(TAG, "Failed to execute %s after %d attempts, continuing anyway", cmd_name, max_attempts);
        return true; // Return true anyway to allow configuration to proceed
      }

      // Enter configuration mode
      bool enter_config_mode()
      {
        // Command value: 0x0001 (little endian)
        uint8_t cmd_value[2] = {0x01, 0x00};
        return send_command_and_wait(CMD_ENTER_CONFIG_MODE, cmd_value, 2, "enter config mode", 200, 5);
      }

      // Exit configuration mode
      bool exit_config_mode()
      {
        bool result = send_command_and_wait(CMD_EXIT_CONFIG_MODE, nullptr, 0, "exit config mode", 200, 5);

        // After exiting configuration mode, the sensor should start sending data frames
        ESP_LOGI(TAG, "Waiting for sensor to transition to data mode...");
        delay(200); // Reduced from 500ms to avoid watchdog

        return result;
      }

      // Set minimum detection distance
      bool set_min_detection_distance(uint16_t distance_mm)
      {
        uint8_t distance_bytes[2];
        distance_bytes[0] = distance_mm & 0xFF;
        distance_bytes[1] = (distance_mm >> 8) & 0xFF;

        char cmd_name[40];
        snprintf(cmd_name, sizeof(cmd_name), "set min distance to %d mm", distance_mm);

        return send_command_and_wait(CMD_SET_MIN_DISTANCE, distance_bytes, 2, cmd_name);
      }

      // Set maximum detection distance
      bool set_max_detection_distance(uint16_t distance_mm)
      {
        uint8_t distance_bytes[2];
        distance_bytes[0] = distance_mm & 0xFF;
        distance_bytes[1] = (distance_mm >> 8) & 0xFF;

        char cmd_name[40];
        snprintf(cmd_name, sizeof(cmd_name), "set max distance to %d mm", distance_mm);

        return send_command_and_wait(CMD_SET_MAX_DISTANCE, distance_bytes, 2, cmd_name);
      }

      // Set reporting cycle
      bool set_reporting_cycle_config(uint16_t cycle_ms)
      {
        uint8_t cycle_bytes[2];
        cycle_bytes[0] = cycle_ms & 0xFF;
        cycle_bytes[1] = (cycle_ms >> 8) & 0xFF;

        char cmd_name[40];
        snprintf(cmd_name, sizeof(cmd_name), "set reporting cycle to %d ms", cycle_ms);

        return send_command_and_wait(CMD_SET_REPORT_CYCLE, cycle_bytes, 2, cmd_name);
      }

      // Perform threshold calibration
      bool calibrate_threshold()
      {
        // Calibration needs more time and may need multiple attempts
        return send_command_and_wait(CMD_UPDATE_THRESHOLD, nullptr, 0, "threshold calibration", 500, 5);
      }

      // Configure the sensor with the current settings
      bool configure_sensor()
      {
        ESP_LOGI(TAG, "Configuring HLK-LD2413 sensor...");
        bool success = true;

        // Enter config mode
        bool config_mode_result = enter_config_mode();
        if (!config_mode_result)
        {
          ESP_LOGE(TAG, "Failed to enter config mode");
          return false;
        }

        // Set min detection distance
        bool min_distance_result = set_min_detection_distance(this->min_distance_);
        if (!min_distance_result)
        {
          ESP_LOGW(TAG, "Failed to set min detection distance");
          success = false;
        }

        // Set max detection distance
        bool max_distance_result = set_max_detection_distance(this->max_distance_);
        if (!max_distance_result)
        {
          ESP_LOGW(TAG, "Failed to set max detection distance");
          success = false;
        }

        // Set reporting cycle
        bool report_cycle_result = set_reporting_cycle_config(this->report_cycle_);
        if (!report_cycle_result)
        {
          ESP_LOGW(TAG, "Failed to set reporting cycle");
          success = false;
        }

        // Only calibrate if calibrate_on_boot is enabled
        if (this->calibrate_on_boot_)
        {
          // Calibrate threshold
          bool calibrate_result = calibrate_threshold();
          if (!calibrate_result)
          {
            ESP_LOGW(TAG, "First calibration attempt failed, trying once more");
            delay(200);

            // Try calibration one more time
            calibrate_result = calibrate_threshold();
            if (!calibrate_result)
            {
              ESP_LOGW(TAG, "Failed to calibrate threshold after two attempts");
              success = false;
            }
          }
        }
        else
        {
          ESP_LOGI(TAG, "Skipping calibration as calibrate_on_boot is disabled");
        }

        // Exit config mode
        bool exit_config_result = exit_config_mode();
        if (!exit_config_result)
        {
          ESP_LOGE(TAG, "Failed to exit config mode");
          return false;
        }

        // Wait for data frames to start coming in
        ESP_LOGI(TAG, "Waiting for data frames...");

        // Store current frame count to check if dump_buffer finds any frames
        int initial_frame_count = this->frames_received_;

        // Check for data frames
        bool data_frames_received = false;
        int check_count = 0;
        const int max_checks = 20; // Limit checks to avoid watchdog reset

        while (!data_frames_received && check_count < max_checks)
        {
          yield();
          dump_buffer();

          // Check if dump_buffer found any frames (frame count increased)
          if (this->frames_received_ > initial_frame_count)
          {
            data_frames_received = true;
            ESP_LOGI(TAG, "Data frames detected! Configuration successful.");
            break;
          }

          delay(100);
          check_count++;
        }

        if (!data_frames_received)
        {
          ESP_LOGW(TAG, "No complete data frames found after %d checks. Configuration may not be successful.", check_count);
          success = false;
        }

        return success;
      }

      // Processes the incoming data buffer looking for valid frames
      void process_buffer(bool should_publish = false)
      {
        if (available() == 0)
          return;

        int bytes_available = available();

        // Log buffer status occasionally
        if (this->frames_received_ % 50 == 0 || should_publish)
        {
          ESP_LOGV(TAG, "Buffer has %d bytes available", bytes_available);
        }

        // If buffer is getting too large, dump it for debugging but don't flush
        if (bytes_available > 256)
        {
          ESP_LOGW(TAG, "Buffer large (%d bytes), dumping for debugging", bytes_available);
          dump_buffer();
          return; // Let dump_buffer handle it
        }

        // Keep track of how many valid frames we found in this processing cycle
        int valid_frames_found = 0;
        int max_iterations = 10; // Reduced from 50 to avoid watchdog
        int iterations = 0;

        // Read a chunk of data from the buffer
        uint8_t buffer[64];
        int bytes_to_read = std::min(bytes_available, 64);

        if (!read_array(buffer, bytes_to_read))
        {
          ESP_LOGW(TAG, "Failed to read data from buffer");
          return;
        }

        // Look for data frames in the buffer
        bool found_frame = false;
        bool found_complete_frame = false;

        for (int i = 0; i < bytes_to_read - 13 && iterations < max_iterations; i++, iterations++)
        {
          // Yield every few iterations to avoid watchdog reset
          if (iterations % 3 == 0)
          {
            yield();
          }

          // Check for frame header
          if (buffer[i] == FRAME_HEADER[0] &&
              buffer[i + 1] == FRAME_HEADER[1] &&
              buffer[i + 2] == FRAME_HEADER[2] &&
              buffer[i + 3] == FRAME_HEADER[3])
          {
            found_frame = true;

            // Check if we have enough bytes for a complete frame
            if (i + 13 < bytes_to_read)
            {
              uint16_t data_length = buffer[i + 4] | (buffer[i + 5] << 8);

              // Check for frame end
              if (buffer[i + 10] == FRAME_END[0] &&
                  buffer[i + 11] == FRAME_END[1] &&
                  buffer[i + 12] == FRAME_END[2] &&
                  buffer[i + 13] == FRAME_END[3])
              {
                found_complete_frame = true;

                // Extract the float distance value
                float distance;
                memcpy(&distance, &buffer[i + 6], 4);

                // Update counters
                this->frames_received_++;
                this->last_successful_read_ = millis();
                valid_frames_found++;

                // Validate range
                if (distance >= this->min_distance_ && distance <= this->max_distance_)
                {
                  // Store the latest reading
                  this->last_distance_ = distance;
                  this->has_new_reading_ = true;

                  // Only log and publish if requested (during update)
                  if (should_publish)
                  {
                    ESP_LOGI(TAG, "Distance updated: %.1f mm (frame #%d)", distance, this->frames_received_);
                    break; // Exit the loop once we find a valid frame
                  }
                }
                else
                {
                  ESP_LOGW(TAG, "Distance out of range: %.1f mm (min: %d, max: %d)",
                           distance, this->min_distance_, this->max_distance_);

                  // Special case: if distance is 0, publish it anyway
                  // This allows detecting when no object is in range
                  if (distance == 0.0f)
                  {
                    this->last_distance_ = 0.0f;
                    this->has_new_reading_ = true;

                    if (should_publish)
                    {
                      ESP_LOGI(TAG, "Publishing zero distance (no object detected)");
                      break;
                    }
                  }
                }
              }
              else if (should_publish)
              {
                ESP_LOGW(TAG, "Found data frame header but end sequence doesn't match");
              }
            }
            else if (should_publish)
            {
              ESP_LOGW(TAG, "Found data frame header but not enough bytes for complete frame");
            }
          }
        }

        // Log frame status if publishing
        if (should_publish)
        {
          if (valid_frames_found > 0)
          {
            ESP_LOGI(TAG, "Found %d valid frames in this processing cycle", valid_frames_found);
          }
          else
          {
            if (!found_frame)
            {
              ESP_LOGD(TAG, "No data frame headers found in buffer");
            }
            else if (!found_complete_frame)
            {
              ESP_LOGW(TAG, "Found data frame headers but no complete frames");
            }
          }

          if (iterations >= max_iterations)
          {
            ESP_LOGW(TAG, "Hit iteration limit (%d) in process_buffer", max_iterations);
          }
        }

        yield();
      }
    };

  } // namespace hlk_ld2413
} // namespace esphome