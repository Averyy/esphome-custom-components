#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"

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

      void set_min_distance(uint16_t min_distance)
      {
        this->min_distance_ = min_distance;
      }

      void set_max_distance(uint16_t max_distance)
      {
        this->max_distance_ = max_distance;
      }

      void set_report_cycle(uint16_t report_cycle)
      {
        this->report_cycle_ = report_cycle;
      }

      void set_calibrate_on_boot(bool calibrate_on_boot)
      {
        this->calibrate_on_boot_ = calibrate_on_boot;
      }

      void setup() override
      {
        ESP_LOGCONFIG(TAG, "Setting up HLK-LD2413...");

        // Initial flush to clear buffer
        flush();

        // Initialize counters
        this->frames_received_ = 0;
        this->last_successful_read_ = millis();
        this->last_buffer_check_ = millis();
        this->last_distance_ = 0;
        this->has_new_reading_ = false;

        // Wait for the sensor to initialize properly
        // This is critical for reliable communication
        ESP_LOGI(TAG, "Waiting for sensor to initialize...");

        // Use multiple shorter delays with yield() instead of one long delay
        for (int i = 0; i < 10; i++)
        {
          delay(100);
          yield(); // Allow other tasks to run
        }

        // Configure the sensor with the parameters
        configure_sensor();

        ESP_LOGI(TAG, "HLK-LD2413 setup complete");

        // Final yield to ensure watchdog is reset
        yield();
      }

      void loop() override
      {
        // Yield at the beginning to prevent watchdog reset
        yield();

        // Only process data if there's something in the buffer
        if (available() > 0)
        {
          // Process the buffer but don't publish state (update() will do that)
          process_buffer(false);
        }

        // Yield at the end to prevent watchdog reset
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
        // Yield at the beginning to prevent watchdog reset
        yield();

        // Process the buffer and publish results
        process_buffer(true);

        // Check if we have a new reading
        if (this->has_new_reading_)
        {
          // Publish the distance
          publish_state(this->last_distance_);
          ESP_LOGI(TAG, "Published distance: %.1f mm", this->last_distance_);
          this->has_new_reading_ = false;
        }
        else
        {
          // Check if we haven't received a valid reading for a while
          uint32_t now = millis();
          if (now - this->last_successful_read_ > 5000)
          {
            ESP_LOGW(TAG, "No valid readings for over 5000 ms. Sensor may be disconnected or malfunctioning.");

            // Dump the buffer for debugging
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

        // Yield at the end to prevent watchdog reset
        yield();
      }

      // Dumps the contents of the buffer for debugging
      void dump_buffer()
      {
        // Check how many bytes are in the buffer
        int bytes_available = available();

        if (bytes_available == 0)
        {
          ESP_LOGD(TAG, "Buffer is empty");
          return;
        }

        // Limit the number of bytes to dump to avoid flooding the logs
        int bytes_to_dump = std::min(bytes_available, 64);
        ESP_LOGD(TAG, "Dumping %d bytes from buffer (total available: %d)", bytes_to_dump, bytes_available);

        // Read bytes into a temporary buffer
        uint8_t buffer[64];
        if (!read_array(buffer, bytes_to_dump))
        {
          ESP_LOGW(TAG, "Failed to read data from buffer");
          return;
        }

        // Log the buffer contents in chunks of 16 bytes
        for (int i = 0; i < bytes_to_dump; i += 16)
        {
          // Yield to avoid watchdog reset
          yield();

          // Calculate how many bytes to log in this chunk
          int chunk_size = std::min(16, bytes_to_dump - i);

          // Build the log string
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

                // Log the distance
                ESP_LOGI(TAG, "Found complete data frame with distance: %.1f mm", distance);

                // Store the latest reading
                this->last_distance_ = distance;
                this->has_new_reading_ = true;
                this->frames_received_++;
                this->last_successful_read_ = millis();

                // Break after finding one complete frame
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

      // Send a command to the sensor
      void send_command(uint16_t command, const uint8_t *data = nullptr, uint16_t data_length = 0)
      {
        // Static buffer for commands (adjust size as needed)
        static uint8_t buffer[32]; // Max expected packet size

        // Calculate total buffer size
        size_t buffer_size = 4 + 2 + 2 + data_length + 4; // header + length + command + data + footer

        // Validate buffer size
        if (buffer_size > sizeof(buffer))
        {
          ESP_LOGE(TAG, "Command buffer overflow, needed %d bytes", buffer_size);
          return;
        }

        // Construct packet
        // Header
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
          ESP_LOGD(TAG, "Command bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                   buffer[0], buffer[1], buffer[2], buffer[3],
                   buffer[4], buffer[5], buffer[6], buffer[7],
                   buffer[8], buffer[9], buffer[10], buffer[11],
                   buffer[12], buffer[13]);
        }

        this->write_array(buffer, buffer_size);

        // Wait for device to process
        delay(100); // Increased delay to give device more time to respond
      }

      // Check for a valid response to a command
      bool check_command_response(uint16_t command, uint32_t timeout_ms = 500) // Increased timeout
      {
        uint32_t start_time = millis();

        // Log available bytes before waiting
        int initial_bytes = available();
        if (initial_bytes > 0)
        {
          ESP_LOGD(TAG, "Before waiting: %d bytes available", initial_bytes);
        }

        // Command response format: Header + Length + Command + Status + Footer
        // Status 0x0000 = success, 0x0001 = failure

        while (millis() - start_time < timeout_ms)
        {
          int bytes_available = available();
          if (bytes_available >= 12) // Minimum response size
          {
            ESP_LOGD(TAG, "Response detected: %d bytes available", bytes_available);

            // Peek at the first few bytes to see if they match the header
            uint8_t peek_buffer[4];
            bool header_found = false;

            // Read the first 4 bytes to check for header
            if (!read_array(peek_buffer, 4))
              continue;

            // Check if header matches
            if (peek_buffer[0] == COMMAND_HEADER[0] && peek_buffer[1] == COMMAND_HEADER[1] &&
                peek_buffer[2] == COMMAND_HEADER[2] && peek_buffer[3] == COMMAND_HEADER[3])
            {
              header_found = true;
              ESP_LOGD(TAG, "Command header found");
            }
            else
            {
              // If not a command header, log and skip
              ESP_LOGD(TAG, "Unexpected bytes: %02X %02X %02X %02X",
                       peek_buffer[0], peek_buffer[1], peek_buffer[2], peek_buffer[3]);
              continue;
            }

            if (!header_found)
            {
              continue;
            }

            uint16_t length;
            uint8_t length_bytes[2];
            if (!read_array(length_bytes, 2))
              continue;
            length = length_bytes[0] | (length_bytes[1] << 8);

            ESP_LOGD(TAG, "Response length: %d", length);

            uint16_t resp_command;
            uint8_t cmd_bytes[2];
            if (!read_array(cmd_bytes, 2))
              continue;
            resp_command = cmd_bytes[0] | (cmd_bytes[1] << 8);

            ESP_LOGD(TAG, "Response command: 0x%04X (expected 0x%04X)", resp_command, command);

            // Check if this is a response to our command
            if (resp_command != command)
            {
              // Skip this frame
              for (int i = 0; i < length - 2 + 4; i++)
              {
                uint8_t dummy;
                read_byte(&dummy);
              }
              continue;
            }

            uint16_t status;
            uint8_t status_bytes[2];
            if (!read_array(status_bytes, 2))
              continue;
            status = status_bytes[0] | (status_bytes[1] << 8);

            ESP_LOGD(TAG, "Response status: %d", status);

            // Skip remaining data
            for (int i = 0; i < length - 4; i++)
            {
              uint8_t dummy;
              read_byte(&dummy);
            }

            // Check footer
            uint8_t footer[4];
            if (!read_array(footer, 4))
              continue;

            // Verify footer
            if (footer[0] != COMMAND_FOOTER[0] || footer[1] != COMMAND_FOOTER[1] ||
                footer[2] != COMMAND_FOOTER[2] || footer[3] != COMMAND_FOOTER[3])
            {
              ESP_LOGD(TAG, "Invalid footer: %02X %02X %02X %02X",
                       footer[0], footer[1], footer[2], footer[3]);
              continue;
            }

            // Success if status is 0
            return (status == 0);
          }

          delay(10);
        }

        ESP_LOGW(TAG, "Timeout waiting for response to command 0x%04X", command);
        return false;
      }

      // Enter configuration mode
      bool enter_config_mode()
      {
        ESP_LOGI(TAG, "Entering configuration mode");

        // Clear any pending data
        flush();
        delay(50);

        // Try multiple times to enter config mode
        const int max_attempts = 5;
        for (int attempt = 0; attempt < max_attempts; attempt++)
        {
          if (attempt > 0)
          {
            ESP_LOGW(TAG, "Retrying enter config mode (attempt %d of %d)", attempt + 1, max_attempts);
            delay(100);
            flush();
          }

          // Send the exact command format according to the datasheet Table 5-1 and 5-2
          // Command word: 0x00FF
          // Command value: 0x0001
          static uint8_t cmd_buffer[14] = {
              0xFD, 0xFC, 0xFB, 0xFA, // Header
              0x04, 0x00,             // Length (4 bytes - command word + value)
              0xFF, 0x00,             // Command word 0x00FF (little endian)
              0x01, 0x00,             // Command value 0x0001 (little endian)
              0x04, 0x03, 0x02, 0x01  // Footer
          };

          ESP_LOGD(TAG, "Sending raw config mode command");
          ESP_LOGD(TAG, "Command bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                   cmd_buffer[0], cmd_buffer[1], cmd_buffer[2], cmd_buffer[3],
                   cmd_buffer[4], cmd_buffer[5], cmd_buffer[6], cmd_buffer[7],
                   cmd_buffer[8], cmd_buffer[9], cmd_buffer[10], cmd_buffer[11],
                   cmd_buffer[12], cmd_buffer[13]);

          this->write_array(cmd_buffer, sizeof(cmd_buffer));

          // Wait for device to process, but not too long to avoid watchdog
          delay(50);

          // Check for response
          uint32_t start_time = millis();
          bool valid_ack_received = false;
          uint32_t max_wait_time = 200; // Reduced to avoid watchdog

          while (millis() - start_time < max_wait_time && !valid_ack_received)
          {
            int bytes_available = available();
            if (bytes_available > 0)
            {
              ESP_LOGD(TAG, "Received response to config mode command (%d bytes)", bytes_available);

              // Read some bytes for logging
              uint8_t ack_buffer[20] = {0};
              int bytes_to_read = std::min(bytes_available, 20);

              if (read_array(ack_buffer, bytes_to_read))
              {
                // Log the response in hex format
                char hex_log[100] = {0};
                int pos = 0;

                for (int i = 0; i < bytes_to_read && pos < 95; i++)
                {
                  pos += snprintf(hex_log + pos, 100 - pos, "%02X ", ack_buffer[i]);
                }

                ESP_LOGI(TAG, "Enter config response: %s", hex_log);

                // Check if this is a valid ACK according to the logs
                // The sensor is returning: FD FC FB FA 04 00 FF 01 00 00 04 03 02 01
                if (bytes_to_read >= 14 &&
                    ack_buffer[0] == COMMAND_HEADER[0] &&             // FD
                    ack_buffer[1] == COMMAND_HEADER[1] &&             // FC
                    ack_buffer[2] == COMMAND_HEADER[2] &&             // FB
                    ack_buffer[3] == COMMAND_HEADER[3] &&             // FA
                    ack_buffer[6] == 0xFF &&                          // FF (command word)
                    (ack_buffer[7] == 0x00 || ack_buffer[7] == 0x01)) // 00 or 01 (command word)
                {
                  // The ACK is valid if it has the correct header and command word
                  // Status is at bytes 8-9, can be 0x0000 (success) or 0x0001 (failure)
                  uint16_t status = ack_buffer[8] | (ack_buffer[9] << 8);
                  if (status == 0)
                  {
                    ESP_LOGI(TAG, "Valid enter config ACK received with SUCCESS status");
                  }
                  else
                  {
                    ESP_LOGW(TAG, "Valid enter config ACK received with FAILURE status: %02X %02X",
                             ack_buffer[8], ack_buffer[9]);
                  }

                  // Check footer if we have enough bytes
                  if (bytes_to_read >= 14 &&
                      ack_buffer[10] == COMMAND_FOOTER[0] && // 04
                      ack_buffer[11] == COMMAND_FOOTER[1] && // 03
                      ack_buffer[12] == COMMAND_FOOTER[2] && // 02
                      ack_buffer[13] == COMMAND_FOOTER[3])   // 01
                  {
                    ESP_LOGI(TAG, "Footer is valid");
                  }
                  else
                  {
                    ESP_LOGW(TAG, "Footer is invalid or incomplete, but accepting response anyway");
                    if (bytes_to_read >= 14)
                    {
                      ESP_LOGW(TAG, "Footer bytes: %02X %02X %02X %02X",
                               ack_buffer[10], ack_buffer[11], ack_buffer[12], ack_buffer[13]);
                    }
                  }

                  valid_ack_received = true;
                  break; // Exit the loop, we got what we wanted
                }
                // Check if this looks like a data frame
                else if (bytes_to_read >= 4 &&
                         ack_buffer[0] == FRAME_HEADER[0] &&
                         ack_buffer[1] == FRAME_HEADER[1] &&
                         ack_buffer[2] == FRAME_HEADER[2] &&
                         ack_buffer[3] == FRAME_HEADER[3])
                {
                  ESP_LOGW(TAG, "Received data frame instead of ACK - sensor is in data mode, continuing to try");
                  // Don't break, keep trying
                }
                else
                {
                  ESP_LOGW(TAG, "Unexpected response format for enter config command");
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
            ESP_LOGI(TAG, "Successfully entered configuration mode");
            delay(100); // Short delay to stabilize
            return true;
          }
        }

        // If we get here, we failed to enter config mode after multiple attempts
        ESP_LOGW(TAG, "Failed to enter config mode after %d attempts, continuing anyway", max_attempts);
        return true; // Return true anyway to allow configuration to proceed
      }

      // Exit configuration mode
      bool exit_config_mode()
      {
        ESP_LOGI(TAG, "Exiting configuration mode");

        // Clear any pending data
        flush();
        delay(50);

        // Try multiple times to exit config mode
        const int max_attempts = 3;
        for (int attempt = 0; attempt < max_attempts; attempt++)
        {
          if (attempt > 0)
          {
            ESP_LOGW(TAG, "Retrying exit config mode (attempt %d of %d)", attempt + 1, max_attempts);
            delay(100);
            flush();
          }

          // Send the exact command format according to the datasheet Table 5-1 and 5-2
          // Command word: 0x00FE
          // Command value: None (no value needed for this command)
          static uint8_t cmd_buffer[12] = {
              0xFD, 0xFC, 0xFB, 0xFA, // Header
              0x02, 0x00,             // Length (2 bytes - command word only, no value)
              0xFE, 0x00,             // Command word 0x00FE (little endian)
              0x04, 0x03, 0x02, 0x01  // Footer
          };

          ESP_LOGD(TAG, "Sending raw exit config mode command");
          ESP_LOGD(TAG, "Command bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                   cmd_buffer[0], cmd_buffer[1], cmd_buffer[2], cmd_buffer[3],
                   cmd_buffer[4], cmd_buffer[5], cmd_buffer[6], cmd_buffer[7],
                   cmd_buffer[8], cmd_buffer[9], cmd_buffer[10], cmd_buffer[11]);

          this->write_array(cmd_buffer, sizeof(cmd_buffer));

          // Wait for device to process, but not too long to avoid watchdog
          delay(50);

          // Check for response
          uint32_t start_time = millis();
          bool valid_ack_received = false;
          uint32_t max_wait_time = 200; // Reduced to avoid watchdog

          while (millis() - start_time < max_wait_time && !valid_ack_received)
          {
            int bytes_available = available();
            if (bytes_available > 0)
            {
              ESP_LOGD(TAG, "Received response to exit config mode command (%d bytes)", bytes_available);

              // Read some bytes for logging
              uint8_t ack_buffer[20] = {0};
              int bytes_to_read = std::min(bytes_available, 20);

              if (read_array(ack_buffer, bytes_to_read))
              {
                // Log the response in hex format
                char hex_log[100] = {0};
                int pos = 0;

                for (int i = 0; i < bytes_to_read && pos < 95; i++)
                {
                  pos += snprintf(hex_log + pos, 100 - pos, "%02X ", ack_buffer[i]);
                }

                ESP_LOGI(TAG, "Exit config response: %s", hex_log);

                // Check if this is a valid ACK according to the logs
                // The sensor is returning: FD FC FB FA 04 00 FE 01 00 00 04 03 02 01
                if (bytes_to_read >= 14 &&
                    ack_buffer[0] == COMMAND_HEADER[0] &&             // FD
                    ack_buffer[1] == COMMAND_HEADER[1] &&             // FC
                    ack_buffer[2] == COMMAND_HEADER[2] &&             // FB
                    ack_buffer[3] == COMMAND_HEADER[3] &&             // FA
                    ack_buffer[6] == 0xFE &&                          // FE (command word)
                    (ack_buffer[7] == 0x00 || ack_buffer[7] == 0x01)) // 00 or 01 (command word)
                {
                  // The ACK is valid if it has the correct header and command word
                  // Status is at bytes 8-9, can be 0x0000 (success) or 0x0001 (failure)
                  uint16_t status = ack_buffer[8] | (ack_buffer[9] << 8);
                  if (status == 0)
                  {
                    ESP_LOGI(TAG, "Valid exit config ACK received with SUCCESS status");
                  }
                  else
                  {
                    ESP_LOGW(TAG, "Valid exit config ACK received with FAILURE status: %02X %02X",
                             ack_buffer[8], ack_buffer[9]);
                  }

                  // Check footer if we have enough bytes
                  if (bytes_to_read >= 14 &&
                      ack_buffer[10] == COMMAND_FOOTER[0] && // 04
                      ack_buffer[11] == COMMAND_FOOTER[1] && // 03
                      ack_buffer[12] == COMMAND_FOOTER[2] && // 02
                      ack_buffer[13] == COMMAND_FOOTER[3])   // 01
                  {
                    ESP_LOGI(TAG, "Footer is valid");
                  }
                  else
                  {
                    ESP_LOGW(TAG, "Footer is invalid or incomplete, but accepting response anyway");
                    if (bytes_to_read >= 14)
                    {
                      ESP_LOGW(TAG, "Footer bytes: %02X %02X %02X %02X",
                               ack_buffer[10], ack_buffer[11], ack_buffer[12], ack_buffer[13]);
                    }
                  }

                  valid_ack_received = true;
                  break; // Exit the loop, we got what we wanted
                }
                // Check if this looks like a data frame
                else if (bytes_to_read >= 4 &&
                         ack_buffer[0] == FRAME_HEADER[0] &&
                         ack_buffer[1] == FRAME_HEADER[1] &&
                         ack_buffer[2] == FRAME_HEADER[2] &&
                         ack_buffer[3] == FRAME_HEADER[3])
                {
                  ESP_LOGI(TAG, "Received data frame instead of ACK - sensor is already in data mode");
                  // This is actually good - it means we're already in data mode
                  valid_ack_received = true;
                  break;
                }
                else
                {
                  ESP_LOGW(TAG, "Unexpected response format for exit config command");
                }
              }

              // Flush any remaining data to start fresh
              flush();
            }
            delay(10);
          }

          // If we got a valid ACK or data frame, we're done
          if (valid_ack_received)
          {
            ESP_LOGI(TAG, "Successfully exited configuration mode");
            delay(100); // Short delay to stabilize
            return true;
          }
        }

        // If we get here, we failed to exit config mode after multiple attempts
        ESP_LOGW(TAG, "Failed to exit config mode after %d attempts, continuing anyway", max_attempts);

        // After exiting configuration mode, the sensor should start sending data frames
        ESP_LOGI(TAG, "Waiting for sensor to transition to data mode...");
        delay(200); // Reduced from 500ms to avoid watchdog

        return true; // Return true anyway to allow configuration to proceed
      }

      // Set minimum detection distance
      bool set_min_detection_distance(uint16_t distance_mm)
      {
        ESP_LOGI(TAG, "Setting minimum detection distance to %d mm", distance_mm);

        // Clear any pending data
        flush();
        delay(50);

        // Prepare command buffer with the exact format according to Tables 5-1 and 5-2
        // Command word: 0x0074
        // Command value: distance_mm (2 bytes)
        static uint8_t cmd_buffer[14] = {
            0xFD, 0xFC, 0xFB, 0xFA, // Header
            0x04, 0x00,             // Length (4 bytes - command word + value)
            0x74, 0x00,             // Command word 0x0074 (little endian)
            0x00, 0x00,             // Command value: distance (will be filled in)
            0x04, 0x03, 0x02, 0x01  // Footer
        };

        // Set the distance value (little endian)
        cmd_buffer[8] = distance_mm & 0xFF;
        cmd_buffer[9] = (distance_mm >> 8) & 0xFF;

        ESP_LOGD(TAG, "Sending raw set min distance command: %d mm", distance_mm);
        ESP_LOGD(TAG, "Command bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 cmd_buffer[0], cmd_buffer[1], cmd_buffer[2], cmd_buffer[3],
                 cmd_buffer[4], cmd_buffer[5], cmd_buffer[6], cmd_buffer[7],
                 cmd_buffer[8], cmd_buffer[9], cmd_buffer[10], cmd_buffer[11],
                 cmd_buffer[12], cmd_buffer[13]);

        this->write_array(cmd_buffer, sizeof(cmd_buffer));

        // Wait for device to process
        delay(100);

        // Check for response
        uint32_t start_time = millis();
        bool valid_ack_received = false;
        uint32_t max_wait_time = 200; // Reduced to avoid watchdog

        while (millis() - start_time < max_wait_time && !valid_ack_received)
        {
          int bytes_available = available();
          if (bytes_available > 0)
          {
            ESP_LOGD(TAG, "Received response to set min distance command (%d bytes)", bytes_available);

            // Read some bytes for logging
            uint8_t ack_buffer[20] = {0};
            int bytes_to_read = std::min(bytes_available, 20);

            if (read_array(ack_buffer, bytes_to_read))
            {
              // Log the response in hex format
              char hex_log[100] = {0};
              int pos = 0;

              for (int i = 0; i < bytes_to_read && pos < 95; i++)
              {
                pos += snprintf(hex_log + pos, 100 - pos, "%02X ", ack_buffer[i]);
              }

              ESP_LOGI(TAG, "Set min distance response: %s", hex_log);

              // Check if this is a valid ACK according to the logs
              // The sensor is returning: FD FC FB FA 04 00 74 01 00 00 04 03 02 01
              if (bytes_to_read >= 14 &&
                  ack_buffer[0] == COMMAND_HEADER[0] &&             // FD
                  ack_buffer[1] == COMMAND_HEADER[1] &&             // FC
                  ack_buffer[2] == COMMAND_HEADER[2] &&             // FB
                  ack_buffer[3] == COMMAND_HEADER[3] &&             // FA
                  ack_buffer[6] == 0x74 &&                          // 74 (command word)
                  (ack_buffer[7] == 0x00 || ack_buffer[7] == 0x01)) // 00 or 01 (command word)
              {
                // The ACK is valid if it has the correct header and command word
                // Status is at bytes 8-9, can be 0x0000 (success) or 0x0001 (failure)
                uint16_t status = ack_buffer[8] | (ack_buffer[9] << 8);
                if (status == 0)
                {
                  ESP_LOGI(TAG, "Valid set min distance ACK received with SUCCESS status");
                }
                else
                {
                  ESP_LOGW(TAG, "Valid set min distance ACK received with FAILURE status: %02X %02X",
                           ack_buffer[8], ack_buffer[9]);
                }

                // Check footer if we have enough bytes
                if (bytes_to_read >= 14 &&
                    ack_buffer[10] == COMMAND_FOOTER[0] && // 04
                    ack_buffer[11] == COMMAND_FOOTER[1] && // 03
                    ack_buffer[12] == COMMAND_FOOTER[2] && // 02
                    ack_buffer[13] == COMMAND_FOOTER[3])   // 01
                {
                  ESP_LOGI(TAG, "Footer is valid");
                }
                else
                {
                  ESP_LOGW(TAG, "Footer is invalid or incomplete, but accepting response anyway");
                  if (bytes_to_read >= 14)
                  {
                    ESP_LOGW(TAG, "Footer bytes: %02X %02X %02X %02X",
                             ack_buffer[10], ack_buffer[11], ack_buffer[12], ack_buffer[13]);
                  }
                }

                valid_ack_received = true;
                break;
              }
              else
              {
                ESP_LOGW(TAG, "Unexpected response format for set min distance command");
              }
            }

            // Flush any remaining data to start fresh
            flush();
          }
          delay(10);
        }

        if (!valid_ack_received)
        {
          ESP_LOGW(TAG, "No valid response to set min distance command, continuing anyway");
        }

        return true; // Always return true to allow configuration to proceed
      }

      // Set maximum detection distance
      bool set_max_detection_distance(uint16_t distance_mm)
      {
        ESP_LOGI(TAG, "Setting maximum detection distance to %d mm", distance_mm);

        // Clear any pending data
        flush();
        delay(50);

        // Prepare command buffer with the exact format according to Tables 5-1 and 5-2
        // Command word: 0x0075
        // Command value: distance_mm (2 bytes)
        static uint8_t cmd_buffer[14] = {
            0xFD, 0xFC, 0xFB, 0xFA, // Header
            0x04, 0x00,             // Length (4 bytes - command word + value)
            0x75, 0x00,             // Command word 0x0075 (little endian)
            0x00, 0x00,             // Command value: distance (will be filled in)
            0x04, 0x03, 0x02, 0x01  // Footer
        };

        // Set the distance value (little endian)
        cmd_buffer[8] = distance_mm & 0xFF;
        cmd_buffer[9] = (distance_mm >> 8) & 0xFF;

        ESP_LOGD(TAG, "Sending raw set max distance command: %d mm", distance_mm);
        ESP_LOGD(TAG, "Command bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 cmd_buffer[0], cmd_buffer[1], cmd_buffer[2], cmd_buffer[3],
                 cmd_buffer[4], cmd_buffer[5], cmd_buffer[6], cmd_buffer[7],
                 cmd_buffer[8], cmd_buffer[9], cmd_buffer[10], cmd_buffer[11],
                 cmd_buffer[12], cmd_buffer[13]);

        this->write_array(cmd_buffer, sizeof(cmd_buffer));

        // Wait for device to process
        delay(100);

        // Check for response
        uint32_t start_time = millis();
        bool valid_ack_received = false;
        uint32_t max_wait_time = 200; // Reduced to avoid watchdog

        while (millis() - start_time < max_wait_time && !valid_ack_received)
        {
          int bytes_available = available();
          if (bytes_available > 0)
          {
            ESP_LOGD(TAG, "Received response to set max distance command (%d bytes)", bytes_available);

            // Read some bytes for logging
            uint8_t ack_buffer[20] = {0};
            int bytes_to_read = std::min(bytes_available, 20);

            if (read_array(ack_buffer, bytes_to_read))
            {
              // Log the response in hex format
              char hex_log[100] = {0};
              int pos = 0;

              for (int i = 0; i < bytes_to_read && pos < 95; i++)
              {
                pos += snprintf(hex_log + pos, 100 - pos, "%02X ", ack_buffer[i]);
              }

              ESP_LOGI(TAG, "Set max distance response: %s", hex_log);

              // Check if this is a valid ACK according to the logs
              // The sensor is returning: FD FC FB FA 04 00 75 01 00 00 04 03 02 01
              if (bytes_to_read >= 14 &&
                  ack_buffer[0] == COMMAND_HEADER[0] &&             // FD
                  ack_buffer[1] == COMMAND_HEADER[1] &&             // FC
                  ack_buffer[2] == COMMAND_HEADER[2] &&             // FB
                  ack_buffer[3] == COMMAND_HEADER[3] &&             // FA
                  ack_buffer[6] == 0x75 &&                          // 75 (command word)
                  (ack_buffer[7] == 0x00 || ack_buffer[7] == 0x01)) // 00 or 01 (command word)
              {
                // The ACK is valid if it has the correct header and command word
                // Status is at bytes 8-9, can be 0x0000 (success) or 0x0001 (failure)
                uint16_t status = ack_buffer[8] | (ack_buffer[9] << 8);
                if (status == 0)
                {
                  ESP_LOGI(TAG, "Valid set max distance ACK received with SUCCESS status");
                }
                else
                {
                  ESP_LOGW(TAG, "Valid set max distance ACK received with FAILURE status: %02X %02X",
                           ack_buffer[8], ack_buffer[9]);
                }

                // Check footer if we have enough bytes
                if (bytes_to_read >= 14 &&
                    ack_buffer[10] == COMMAND_FOOTER[0] && // 04
                    ack_buffer[11] == COMMAND_FOOTER[1] && // 03
                    ack_buffer[12] == COMMAND_FOOTER[2] && // 02
                    ack_buffer[13] == COMMAND_FOOTER[3])   // 01
                {
                  ESP_LOGI(TAG, "Footer is valid");
                }
                else
                {
                  ESP_LOGW(TAG, "Footer is invalid or incomplete, but accepting response anyway");
                  if (bytes_to_read >= 14)
                  {
                    ESP_LOGW(TAG, "Footer bytes: %02X %02X %02X %02X",
                             ack_buffer[10], ack_buffer[11], ack_buffer[12], ack_buffer[13]);
                  }
                }

                valid_ack_received = true;
                break;
              }
              else
              {
                ESP_LOGW(TAG, "Unexpected response format for set max distance command");
              }
            }

            // Flush any remaining data to start fresh
            flush();
          }
          delay(10);
        }

        if (!valid_ack_received)
        {
          ESP_LOGW(TAG, "No valid response to set max distance command, continuing anyway");
        }

        return true; // Always return true to allow configuration to proceed
      }

      // Set reporting cycle
      bool set_reporting_cycle_config(uint16_t cycle_ms)
      {
        ESP_LOGI(TAG, "Setting reporting cycle to %d ms", cycle_ms);

        // Clear any pending data
        flush();
        delay(50);

        // Prepare command buffer with the exact format according to Tables 5-1 and 5-2
        // Command word: 0x0071
        // Command value: cycle_ms (2 bytes)
        static uint8_t cmd_buffer[14] = {
            0xFD, 0xFC, 0xFB, 0xFA, // Header
            0x04, 0x00,             // Length (4 bytes - command word + value)
            0x71, 0x00,             // Command word 0x0071 (little endian)
            0x00, 0x00,             // Command value: cycle (will be filled in)
            0x04, 0x03, 0x02, 0x01  // Footer
        };

        // Set the cycle value (little endian)
        cmd_buffer[8] = cycle_ms & 0xFF;
        cmd_buffer[9] = (cycle_ms >> 8) & 0xFF;

        ESP_LOGD(TAG, "Sending raw set reporting cycle command: %d ms", cycle_ms);
        ESP_LOGD(TAG, "Command bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 cmd_buffer[0], cmd_buffer[1], cmd_buffer[2], cmd_buffer[3],
                 cmd_buffer[4], cmd_buffer[5], cmd_buffer[6], cmd_buffer[7],
                 cmd_buffer[8], cmd_buffer[9], cmd_buffer[10], cmd_buffer[11],
                 cmd_buffer[12], cmd_buffer[13]);

        this->write_array(cmd_buffer, sizeof(cmd_buffer));

        // Wait for device to process
        delay(100);

        // Check for response
        uint32_t start_time = millis();
        bool valid_ack_received = false;
        uint32_t max_wait_time = 200; // Reduced to avoid watchdog

        while (millis() - start_time < max_wait_time && !valid_ack_received)
        {
          int bytes_available = available();
          if (bytes_available > 0)
          {
            ESP_LOGD(TAG, "Received response to set reporting cycle command (%d bytes)", bytes_available);

            // Read some bytes for logging
            uint8_t ack_buffer[20] = {0};
            int bytes_to_read = std::min(bytes_available, 20);

            if (read_array(ack_buffer, bytes_to_read))
            {
              // Log the response in hex format
              char hex_log[100] = {0};
              int pos = 0;

              for (int i = 0; i < bytes_to_read && pos < 95; i++)
              {
                pos += snprintf(hex_log + pos, 100 - pos, "%02X ", ack_buffer[i]);
              }

              ESP_LOGI(TAG, "Set reporting cycle response: %s", hex_log);

              // Check if this is a valid ACK according to the logs
              // The sensor is returning: FD FC FB FA 04 00 71 01 00 00 04 03 02 01
              if (bytes_to_read >= 14 &&
                  ack_buffer[0] == COMMAND_HEADER[0] &&             // FD
                  ack_buffer[1] == COMMAND_HEADER[1] &&             // FC
                  ack_buffer[2] == COMMAND_HEADER[2] &&             // FB
                  ack_buffer[3] == COMMAND_HEADER[3] &&             // FA
                  ack_buffer[6] == 0x71 &&                          // 71 (command word)
                  (ack_buffer[7] == 0x00 || ack_buffer[7] == 0x01)) // 00 or 01 (command word)
              {
                // The ACK is valid if it has the correct header and command word
                // Status is at bytes 8-9, can be 0x0000 (success) or 0x0001 (failure)
                uint16_t status = ack_buffer[8] | (ack_buffer[9] << 8);
                if (status == 0)
                {
                  ESP_LOGI(TAG, "Valid set reporting cycle ACK received with SUCCESS status");
                }
                else
                {
                  ESP_LOGW(TAG, "Valid set reporting cycle ACK received with FAILURE status: %02X %02X",
                           ack_buffer[8], ack_buffer[9]);
                }

                // Check footer if we have enough bytes
                if (bytes_to_read >= 14 &&
                    ack_buffer[10] == COMMAND_FOOTER[0] && // 04
                    ack_buffer[11] == COMMAND_FOOTER[1] && // 03
                    ack_buffer[12] == COMMAND_FOOTER[2] && // 02
                    ack_buffer[13] == COMMAND_FOOTER[3])   // 01
                {
                  ESP_LOGI(TAG, "Footer is valid");
                }
                else
                {
                  ESP_LOGW(TAG, "Footer is invalid or incomplete, but accepting response anyway");
                  if (bytes_to_read >= 14)
                  {
                    ESP_LOGW(TAG, "Footer bytes: %02X %02X %02X %02X",
                             ack_buffer[10], ack_buffer[11], ack_buffer[12], ack_buffer[13]);
                  }
                }

                valid_ack_received = true;
                break;
              }
              else
              {
                ESP_LOGW(TAG, "Unexpected response format for set reporting cycle command");
              }
            }

            // Flush any remaining data to start fresh
            flush();
          }
          delay(10);
        }

        if (!valid_ack_received)
        {
          ESP_LOGW(TAG, "No valid response to set reporting cycle command, continuing anyway");
        }

        return true; // Always return true to allow configuration to proceed
      }

      // Perform threshold calibration
      bool calibrate_threshold()
      {
        ESP_LOGI(TAG, "Performing threshold calibration");

        // Clear any pending data
        flush();
        delay(50);

        // Prepare command buffer with the exact format according to Tables 5-1 and 5-2
        // Command word: 0x0072
        // Command value: 0x0000 (assuming no specific value needed)
        static uint8_t cmd_buffer[14] = {
            0xFD, 0xFC, 0xFB, 0xFA, // Header
            0x04, 0x00,             // Length (4 bytes - command word + value)
            0x72, 0x00,             // Command word 0x0072 (little endian)
            0x00, 0x00,             // Command value 0x0000 (little endian)
            0x04, 0x03, 0x02, 0x01  // Footer
        };

        ESP_LOGD(TAG, "Sending raw threshold calibration command");
        ESP_LOGD(TAG, "Command bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 cmd_buffer[0], cmd_buffer[1], cmd_buffer[2], cmd_buffer[3],
                 cmd_buffer[4], cmd_buffer[5], cmd_buffer[6], cmd_buffer[7],
                 cmd_buffer[8], cmd_buffer[9], cmd_buffer[10], cmd_buffer[11],
                 cmd_buffer[12], cmd_buffer[13]);

        // Try sending the calibration command up to 3 times
        bool valid_ack_received = false;
        int attempt = 1;
        const int max_attempts = 3;

        while (!valid_ack_received && attempt <= max_attempts)
        {
          // Send the command
          this->write_array(cmd_buffer, sizeof(cmd_buffer));

          // Calibration needs more time - increase wait time
          delay(200);

          // Check for response with longer timeout
          uint32_t start_time = millis();
          uint32_t max_wait_time = 500; // Increased from 200ms to 500ms

          while (millis() - start_time < max_wait_time && !valid_ack_received)
          {
            yield(); // Allow other tasks to run

            int bytes_available = available();
            if (bytes_available > 0)
            {
              ESP_LOGD(TAG, "Received response to calibration command (%d bytes)", bytes_available);

              // Read some bytes for logging
              uint8_t ack_buffer[20] = {0};
              int bytes_to_read = std::min(bytes_available, 20);

              if (read_array(ack_buffer, bytes_to_read))
              {
                // Log the response in hex format
                char hex_log[100] = {0};
                int pos = 0;

                for (int i = 0; i < bytes_to_read && pos < 95; i++)
                {
                  pos += snprintf(hex_log + pos, 100 - pos, "%02X ", ack_buffer[i]);
                }

                ESP_LOGI(TAG, "Calibration response: %s", hex_log);

                // Check if this is a valid ACK according to the logs
                // The sensor should return: FD FC FB FA 04 00 72 01 00 00 04 03 02 01
                if (bytes_to_read >= 14 &&
                    ack_buffer[0] == COMMAND_HEADER[0] &&             // FD
                    ack_buffer[1] == COMMAND_HEADER[1] &&             // FC
                    ack_buffer[2] == COMMAND_HEADER[2] &&             // FB
                    ack_buffer[3] == COMMAND_HEADER[3] &&             // FA
                    ack_buffer[6] == 0x72 &&                          // 72 (command word)
                    (ack_buffer[7] == 0x00 || ack_buffer[7] == 0x01)) // 00 or 01 (command word)
                {
                  // The ACK is valid if it has the correct header and command word
                  // Status is at bytes 8-9, can be 0x0000 (success) or 0x0001 (failure)
                  uint16_t status = ack_buffer[8] | (ack_buffer[9] << 8);
                  if (status == 0)
                  {
                    ESP_LOGI(TAG, "Valid calibration ACK received with SUCCESS status");
                    valid_ack_received = true;
                  }
                  else
                  {
                    ESP_LOGW(TAG, "Valid calibration ACK received with FAILURE status: %02X %02X",
                             ack_buffer[8], ack_buffer[9]);
                  }

                  // Check footer if we have enough bytes
                  if (bytes_to_read >= 14 &&
                      ack_buffer[10] == COMMAND_FOOTER[0] && // 04
                      ack_buffer[11] == COMMAND_FOOTER[1] && // 03
                      ack_buffer[12] == COMMAND_FOOTER[2] && // 02
                      ack_buffer[13] == COMMAND_FOOTER[3])   // 01
                  {
                    ESP_LOGI(TAG, "Footer is valid");
                  }
                  else
                  {
                    ESP_LOGW(TAG, "Footer is invalid or incomplete");
                    if (bytes_to_read >= 14)
                    {
                      ESP_LOGW(TAG, "Footer bytes: %02X %02X %02X %02X",
                               ack_buffer[10], ack_buffer[11], ack_buffer[12], ack_buffer[13]);
                    }
                  }

                  break;
                }
                else if (bytes_to_read >= 8 &&
                         ack_buffer[0] == FRAME_HEADER[0] &&
                         ack_buffer[1] == FRAME_HEADER[1] &&
                         ack_buffer[2] == FRAME_HEADER[2] &&
                         ack_buffer[3] == FRAME_HEADER[3])
                {
                  // We received a data frame instead of an ACK
                  ESP_LOGW(TAG, "Received data frame instead of ACK - sensor may have exited config mode");
                  // Flush and try to re-enter config mode
                  flush();
                  break;
                }
                else
                {
                  ESP_LOGW(TAG, "Unexpected response format for calibration command");
                }
              }
            }
            delay(50); // Increased from 10ms to 50ms
          }

          if (!valid_ack_received)
          {
            if (attempt < max_attempts)
            {
              ESP_LOGW(TAG, "Retrying calibration command (attempt %d of %d)", attempt + 1, max_attempts);
              flush();    // Clear any pending data
              delay(100); // Wait before retrying
            }
          }

          attempt++;
        }

        if (!valid_ack_received)
        {
          ESP_LOGW(TAG, "No valid response to calibration command after %d attempts", max_attempts);
          return false; // Return false to indicate calibration failed
        }

        ESP_LOGI(TAG, "Calibration successful");
        return true;
      }

      // Configure the sensor with the current settings
      bool configure_sensor()
      {
        ESP_LOGI(TAG, "Configuring HLK-LD2413 sensor...");
        bool success = true;

        // Enter config mode
        bool config_mode_result = enter_config_mode();
        ESP_LOGI(TAG, "Enter config mode result: %s", config_mode_result ? "Success" : "Failure");
        if (!config_mode_result)
        {
          ESP_LOGE(TAG, "Failed to enter config mode");
          return false;
        }

        // Set min detection distance
        bool min_distance_result = set_min_detection_distance(this->min_distance_);
        ESP_LOGI(TAG, "Set min distance result: %s", min_distance_result ? "Success" : "Failure");
        if (!min_distance_result)
        {
          ESP_LOGW(TAG, "Failed to set min detection distance");
          success = false;
        }

        // Set max detection distance
        bool max_distance_result = set_max_detection_distance(this->max_distance_);
        ESP_LOGI(TAG, "Set max distance result: %s", max_distance_result ? "Success" : "Failure");
        if (!max_distance_result)
        {
          ESP_LOGW(TAG, "Failed to set max detection distance");
          success = false;
        }

        // Set reporting cycle
        bool report_cycle_result = set_reporting_cycle_config(this->report_cycle_);
        ESP_LOGI(TAG, "Set reporting cycle result: %s", report_cycle_result ? "Success" : "Failure");
        if (!report_cycle_result)
        {
          ESP_LOGW(TAG, "Failed to set reporting cycle");
          success = false;
        }

        // Only calibrate if calibrate_on_boot is enabled
        bool calibrate_result = true;
        if (this->calibrate_on_boot_)
        {
          // Calibrate threshold
          calibrate_result = calibrate_threshold();
          ESP_LOGI(TAG, "Calibrate threshold result: %s", calibrate_result ? "Success" : "Failure");
          if (!calibrate_result)
          {
            ESP_LOGW(TAG, "First calibration attempt failed, trying once more");

            // Wait a bit before retrying
            delay(200);

            // Try calibration one more time
            calibrate_result = calibrate_threshold();
            ESP_LOGI(TAG, "Second calibration attempt result: %s", calibrate_result ? "Success" : "Failure");

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
        ESP_LOGI(TAG, "Exit config mode result: %s", exit_config_result ? "Success" : "Failure");
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
          // Yield to avoid watchdog reset
          yield();

          // Dump the buffer to check for data frames
          dump_buffer();

          // Check if dump_buffer found any frames (frame count increased)
          if (this->frames_received_ > initial_frame_count)
          {
            data_frames_received = true;
            ESP_LOGI(TAG, "Data frames detected! Configuration successful.");
            break;
          }

          // Wait a bit before checking again
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
        // Safety check
        if (available() == 0)
        {
          return;
        }

        // Check how many bytes are in the buffer
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

        // Process as much of the buffer as we can, but limit to avoid watchdog
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
            // ESP_LOGD(TAG, "Found data frame header at position %d", i);

            // Check if we have enough bytes for a complete frame
            if (i + 13 < bytes_to_read)
            {
              uint16_t data_length = buffer[i + 4] | (buffer[i + 5] << 8);
              // ESP_LOGD(TAG, "Data frame length: %d", data_length);

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

                // Log the distance
                // ESP_LOGD(TAG, "Found complete data frame with distance: %.1f mm", distance);

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
                }
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

        // If we found valid frames, log it
        if (valid_frames_found > 0 && should_publish)
        {
          ESP_LOGI(TAG, "Found %d valid frames in this processing cycle", valid_frames_found);
        }
        else if (should_publish)
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

        // If we hit the iteration limit, log it
        if (iterations >= max_iterations)
        {
          ESP_LOGW(TAG, "Hit iteration limit (%d) in process_buffer, yielding to avoid watchdog reset", max_iterations);
        }

        // Final yield to ensure watchdog is reset
        yield();
      }
    };

  } // namespace hlk_ld2413
} // namespace esphome