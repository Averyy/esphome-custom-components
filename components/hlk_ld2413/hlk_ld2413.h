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
    static const uint8_t FRAME_HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
    static const uint8_t FRAME_END[4] = {0xF8, 0xF7, 0xF6, 0xF5};

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

        ESP_LOGI(TAG, "HLK-LD2413 setup complete");
      }

      void loop() override
      {
        // Check for available data in every loop cycle
        uint32_t now = millis();

        // Simple approach: if there's data, process it
        if (available() > 0)
        {
          // Don't process too frequently to avoid overwhelming the CPU
          if (now - this->last_buffer_check_ > 50)
          {
            this->last_buffer_check_ = now;
            // Process buffer but don't publish state - just store the latest reading
            process_buffer(false);
          }
        }
      }

      void dump_config() override
      {
        ESP_LOGCONFIG(TAG, "HLK-LD2413 Radar Sensor:");
        LOG_SENSOR("  ", "Distance", this);
        ESP_LOGCONFIG(TAG, "  Min Distance: %dmm", this->min_distance_);
        ESP_LOGCONFIG(TAG, "  Max Distance: %dmm", this->max_distance_);
        ESP_LOGCONFIG(TAG, "  Report Cycle: %dms", this->report_cycle_);
        LOG_UPDATE_INTERVAL(this);
        check_uart_settings(115200);
      }

      void update() override
      {
        ESP_LOGV(TAG, "Update called, frames received so far: %d", this->frames_received_);

        // Check if we haven't received data in a while
        uint32_t now = millis();
        if (now - last_successful_read_ > 5000)
        {
          ESP_LOGW(TAG, "No valid frames received in the last 5 seconds");

          // Try to reset the UART connection
          flush();
        }

        // Process any available data during update
        int bytes_available = available();
        if (bytes_available > 0)
        {
          ESP_LOGV(TAG, "Processing %d bytes in update()", bytes_available);
          // Process buffer and publish state if we have a new reading
          process_buffer(true);
        }

        // If we have a new reading since the last update, publish it
        if (this->has_new_reading_)
        {
          ESP_LOGV(TAG, "Publishing new distance: %.1fmm", this->last_distance_);
          this->publish_state(this->last_distance_);
          this->has_new_reading_ = false;
        }

        // Log current state
        float current = this->state;
        if (!std::isnan(current))
        {
          ESP_LOGV(TAG, "Current distance state: %.1fmm", current);
        }
        else
        {
          ESP_LOGV(TAG, "Current distance state: not set yet");
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
        if (this->frames_received_ % 50 == 0)
        {
          ESP_LOGV(TAG, "Buffer has %d bytes available", bytes_available);
        }

        // If buffer is getting too large, clear some data
        if (bytes_available > 128)
        {
          ESP_LOGW(TAG, "Buffer too large (%d bytes), flushing", bytes_available);
          flush();
          return;
        }

        // Simplified frame processing - only look for complete frames
        // This reduces complexity and potential for errors
        while (available() >= 14)
        { // A complete frame is 14 bytes
          uint8_t header_byte;
          if (!read_byte(&header_byte))
          {
            break;
          }

          // If it's not a header byte, continue to the next byte
          if (header_byte != FRAME_HEADER[0])
          {
            continue;
          }

          // Read the next 13 bytes to complete the frame
          uint8_t buffer[13];
          if (!read_array(buffer, 13))
          {
            // Failed to read complete frame
            ESP_LOGW(TAG, "Failed to read complete frame");
            break;
          }

          // Verify frame header
          if (buffer[0] != FRAME_HEADER[1] || buffer[1] != FRAME_HEADER[2] || buffer[2] != FRAME_HEADER[3])
          {
            // Invalid header, continue searching
            continue;
          }

          // Validate frame length bytes - should be "04 00" based on logs
          if (buffer[3] != 0x04 || buffer[4] != 0x00)
          {
            // Invalid length bytes, continue searching
            continue;
          }

          // Extract distance data (4 bytes at position 5-8)
          float distance;
          memcpy(&distance, &buffer[5], 4);

          // Check for frame end
          if (buffer[9] != FRAME_END[0] || buffer[10] != FRAME_END[1] ||
              buffer[11] != FRAME_END[2] || buffer[12] != FRAME_END[3])
          {
            // Invalid frame end, continue searching
            continue;
          }

          // Valid frame found!
          this->frames_received_++;
          this->last_successful_read_ = millis();

          // Validate range
          if (distance >= this->min_distance_ && distance <= this->max_distance_)
          {
            // Store the latest reading
            this->last_distance_ = distance;
            this->has_new_reading_ = true;

            // Only log and publish if requested (during update)
            if (should_publish)
            {
              ESP_LOGV(TAG, "Distance updated: %.1fmm (frame #%d)", distance, this->frames_received_);
            }
          }
        }
      }
    };

  } // namespace hlk_ld2413
} // namespace esphome