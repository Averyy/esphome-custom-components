#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"

namespace esphome
{
	namespace notecard
	{
		class Notecard : public Component, public uart::UARTDevice
		{
		public:
			void setup() override;
			void loop() override;
			void dump_config() override;
			float get_setup_priority() const override { return setup_priority::DATA; }

			void set_project_id(const std::string &project_id) { project_id_ = project_id; }
			void set_org(const std::string &org) { org_ = org; }
			void set_sync_interval(uint32_t interval) { sync_interval_ = interval; }

			bool send_data(const std::string &data);
			bool initialize();
			bool sync_now();

			// Helper methods to get specific values from the Notecard
			float get_notecard_temperature();
			float get_notecard_battery_voltage();

		protected:
			std::string project_id_;
			std::string org_;
			uint32_t sync_interval_{14400}; // 4 hours default
			bool initialized_{false};
			bool is_wifi_notecard_{false};
			uint32_t last_response_time_{0};

			void flush_rx_();
			bool send_command_(const std::string &command);
			bool send_command_and_get_response_(const std::string &command, std::string &response_out);
			bool send_command_once_(const std::string &command, std::string &response_out);
			bool wait_for_response_();
			std::string get_response_();
			bool check_and_configure_hub_();
			bool check_and_configure_location_();
			bool check_and_configure_wifi_();
			std::string sanitize_wifi_name_(const std::string &org_name);
		};

	} // namespace notecard
} // namespace esphome