#include "notecard.h"
#include "esphome/core/log.h"
#include <regex>
#include <map>

namespace esphome
{
	namespace notecard
	{
		static const char *TAG = "notecard";
		static const uint32_t RESPONSE_TIMEOUT = 500;
		static const uint32_t POLLING_DELAY = 50;

		// Helper function to extract a specific field from JSON response
		std::string extract_json_value(const std::string &json, const std::string &field_name)
		{
			// For string values
			std::string pattern = "\"" + field_name + "\"\\s*:\\s*\"([^\"]+)\"";
			std::regex re_string(pattern);
			std::smatch match;
			if (std::regex_search(json, match, re_string) && match.size() > 1)
			{
				return match.str(1);
			}

			// For numeric values (including floating point)
			pattern = "\"" + field_name + "\"\\s*:\\s*(-?[0-9]+\\.?[0-9]*)";
			std::regex re_number(pattern);
			if (std::regex_search(json, match, re_number) && match.size() > 1)
			{
				return match.str(1);
			}

			// For boolean values
			pattern = "\"" + field_name + "\"\\s*:\\s*(true|false)";
			std::regex re_bool(pattern);
			if (std::regex_search(json, match, re_bool) && match.size() > 1)
			{
				return match.str(1);
			}

			return "";
		}

		void Notecard::setup()
		{
			ESP_LOGCONFIG(TAG, "Setting up Notecard...");

			// Seed the random number generator
			srand(millis());

			// Give the Notecard time to stabilize
			// Previously 100ms but that was too short for the Notecard to stabilize on reboots
			delay(200);

			// Clear UART buffer thoroughly
			flush_rx_();

			// We'll initialize the Notecard with required configuration
			if (!initialize())
			{
				ESP_LOGE(TAG, "Failed to initialize Notecard");
				this->mark_failed();
				return;
			}

			initialized_ = true;
			ESP_LOGCONFIG(TAG, "Notecard successfully configured");
		}

		void Notecard::loop()
		{
			// Empty loop - our component is command-based
		}

		bool Notecard::initialize()
		{
			// Clear buffer before starting configuration
			flush_rx_();

			// Step 1: Configure hub settings
			if (!check_and_configure_hub_())
			{
				return false;
			}

			// Check if this is a WiFi Notecard
			std::string version_response;
			if (send_command_and_get_response_("{\"req\":\"card.version\"}", version_response))
			{
				is_wifi_notecard_ = (version_response.find("\"wifi\":true") != std::string::npos);
			}

			// Step 2: Configure location tracking (skip for WiFi Notecards)
			if (!is_wifi_notecard_ && !check_and_configure_location_())
			{
				return false;
			}

			// Step 3: Configure WiFi (if applicable)
			if (!check_and_configure_wifi_())
			{
				return false;
			}

			return true;
		}

		bool Notecard::check_and_configure_hub_()
		{
			ESP_LOGD(TAG, "Checking hub configuration...");

			// Get current hub settings - with retry logic
			std::string response;
			bool success = false;

			// Try up to 5 times to get hub configuration (increased from 3)
			for (int retry = 0; retry < 5 && !success; retry++)
			{
				if (retry > 0)
				{
					ESP_LOGD(TAG, "Retrying hub.get (attempt %d/5)...", retry + 1);
					delay(100 * retry); // Increasing delay between retries
				}

				success = send_command_and_get_response_("{\"req\":\"hub.get\"}", response);

				if (success)
				{
					break;
				}
			}

			if (!success)
			{
				ESP_LOGE(TAG, "Failed to get hub configuration after multiple attempts");
				return false;
			}

			bool need_config = false;

			// Extract values using regex for more reliable JSON parsing
			std::string product = extract_json_value(response, "product");
			std::string mode = extract_json_value(response, "mode");
			std::string inbound_str = extract_json_value(response, "inbound");
			std::string outbound_str = extract_json_value(response, "outbound");

			// Check for project ID in response
			if (product != project_id_)
			{
				ESP_LOGD(TAG, "Hub product ID not set correctly (current: %s, expected: %s)",
						 product.c_str(), project_id_.c_str());
				need_config = true;
			}
			// Check for mode in response
			else if (mode != "periodic")
			{
				ESP_LOGD(TAG, "Hub mode not set to periodic (current: %s)", mode.c_str());
				need_config = true;
			}
			// Check for inbound interval in response
			else if (inbound_str.empty() || std::stoi(inbound_str) != (sync_interval_ / 60))
			{
				ESP_LOGD(TAG, "Hub inbound interval not matching %d minutes (current: %s)",
						 sync_interval_ / 60, inbound_str.c_str());
				need_config = true;
			}
			// Check for outbound interval in response
			else if (outbound_str.empty() || std::stoi(outbound_str) != (sync_interval_ / 60))
			{
				ESP_LOGD(TAG, "Hub outbound interval not matching %d minutes (current: %s)",
						 sync_interval_ / 60, outbound_str.c_str());
				need_config = true;
			}
			// Everything is correctly configured
			else
			{
				ESP_LOGD(TAG, "Hub already configured correctly");
			}

			if (need_config)
			{
				ESP_LOGD(TAG, "Configuring hub...");

				std::string hub_config = "{\"req\":\"hub.set\",\"product\":\"" + project_id_ +
										 "\",\"mode\":\"periodic\",\"inbound\":" + std::to_string(sync_interval_ / 60) +
										 ",\"outbound\":" + std::to_string(sync_interval_ / 60) + "}";

				// Note: We removed org from hub.set as it's only used for WiFi config

				if (!send_command_(hub_config))
				{
					ESP_LOGE(TAG, "Failed to configure hub");
					return false;
				}

				ESP_LOGD(TAG, "Hub configured successfully");
			}

			return true;
		}

		bool Notecard::check_and_configure_location_()
		{
			ESP_LOGD(TAG, "Checking location tracking configuration...");

			// Get current location tracking settings
			std::string response;
			if (!send_command_and_get_response_("{\"req\":\"card.location.mode\"}", response))
			{
				ESP_LOGE(TAG, "Failed to get location tracking configuration");
				return false;
			}

			bool need_config = false;

			// Extract values using better parsing
			std::string mode = extract_json_value(response, "mode");
			std::string seconds_str = extract_json_value(response, "seconds");

			// Check if periodic mode is set with correct seconds value
			if (mode != "periodic")
			{
				ESP_LOGD(TAG, "Location tracking mode not set to periodic (current: %s)", mode.c_str());
				need_config = true;
			}
			// Check if correct seconds value is set
			else if (seconds_str.empty() || std::stoi(seconds_str) != sync_interval_)
			{
				ESP_LOGD(TAG, "Location tracking interval incorrect (current: %s, expected: %d)",
						 seconds_str.c_str(), sync_interval_);
				need_config = true;
			}
			else
			{
				ESP_LOGD(TAG, "Location tracking already configured correctly");
			}

			if (need_config)
			{
				ESP_LOGD(TAG, "Configuring location tracking...");

				std::string location_config = "{\"req\":\"card.location.mode\",\"mode\":\"periodic\",\"seconds\":" +
											  std::to_string(sync_interval_) + "}";

				if (!send_command_and_get_response_(location_config, response))
				{
					ESP_LOGE(TAG, "Failed to configure location tracking");
					return false;
				}

				ESP_LOGD(TAG, "Location tracking configured successfully");
			}

			return true;
		}

		bool Notecard::check_and_configure_wifi_()
		{
			ESP_LOGD(TAG, "Checking if Notecard supports WiFi...");

			// Use stored WiFi capability status
			if (!is_wifi_notecard_)
			{
				ESP_LOGD(TAG, "This Notecard does not support WiFi, skipping WiFi configuration");
				return true; // Return success, WiFi config not needed
			}

			// Only proceed with WiFi configuration if org_ is set
			if (org_.empty())
			{
				ESP_LOGD(TAG, "No organization set, skipping WiFi configuration");
				return true;
			}

			ESP_LOGD(TAG, "Configuring WiFi SoftAP...");

			// Get current WiFi settings to check if SSID is configured
			std::string response;
			bool has_ssid = false;
			if (send_command_and_get_response_("{\"req\":\"card.wifi\"}", response))
			{
				// Check if an SSID is already configured
				has_ssid = (response.find("\"ssid\":") != std::string::npos);
				ESP_LOGD(TAG, "WiFi status check: %s configured", has_ssid ? "SSID" : "No SSID");
			}
			else
			{
				ESP_LOGD(TAG, "No existing WiFi configuration or error getting configuration");
			}

			// Build the WiFi configuration command for SoftAP customization
			std::string wifi_config = "{\"req\":\"card.wifi\"";

			// Always set the organization name for the SoftAP page
			wifi_config += ",\"org\":\"" + org_ + "\"";

			// Set a custom name for the SoftAP with MAC address suffix
			std::string wifi_name = sanitize_wifi_name_(org_);
			wifi_config += ",\"name\":\"" + wifi_name + "-\"";

			// If no SSID is configured, enable SoftAP mode
			if (!has_ssid)
			{
				wifi_config += ",\"start\":true";
			}

			wifi_config += "}";

			ESP_LOGD(TAG, "Configuring WiFi SoftAP with command: %s", wifi_config.c_str());

			std::string config_response;
			if (!send_command_and_get_response_(wifi_config, config_response))
			{
				ESP_LOGE(TAG, "Failed to configure WiFi SoftAP");
				return false;
			}

			ESP_LOGD(TAG, "WiFi SoftAP configured successfully");
			return true;
		}

		std::string Notecard::sanitize_wifi_name_(const std::string &org_name)
		{
			std::string result;

			// Convert to lowercase and replace spaces with dashes
			for (char c : org_name)
			{
				if (isalnum(c))
				{
					result += tolower(c);
				}
				else if (c == ' ')
				{
					result += '-';
				}
				else if (c == '-')
				{
					result += c;
				}
			}

			// Ensure name ends with dash
			if (!result.empty() && result.back() != '-')
			{
				result += '-';
			}

			return result;
		}

		bool Notecard::send_data(const std::string &data)
		{
			if (!initialized_)
			{
				ESP_LOGE(TAG, "Notecard not initialized, cannot send data");
				return false;
			}

			std::string command = "{\"req\":\"note.add\",\"file\":\"sensors.qo\",\"body\":" + data + "}";
			std::string response;

			if (!send_command_and_get_response_(command, response))
			{
				ESP_LOGE(TAG, "Failed to send data to Notecard");
				return false;
			}

			ESP_LOGD(TAG, "Data sent successfully to Notecard");
			return true;
		}

		bool Notecard::sync_now()
		{
			if (!initialized_)
			{
				ESP_LOGE(TAG, "Notecard not initialized, cannot trigger sync");
				return false;
			}

			ESP_LOGD(TAG, "Triggering immediate sync with hub.sync");

			std::string command = "{\"req\":\"hub.sync\"}";

			if (!send_command_(command))
			{
				ESP_LOGE(TAG, "Failed to trigger sync with Notecard");
				return false;
			}

			ESP_LOGD(TAG, "Sync triggered successfully");
			return true;
		}

		void Notecard::flush_rx_()
		{
			// Completely clear the buffer with short delays
			for (int i = 0; i < 3; i++)
			{ // Try multiple times
				uint32_t start = millis();
				while (this->available() && (millis() - start < 100))
				{
					this->read();
					yield(); // Feed watchdog
				}
				delay(10); // Short delay between attempts
			}
		}

		bool Notecard::send_command_(const std::string &command)
		{
			std::string response;
			return send_command_and_get_response_(command, response);
		}

		bool Notecard::send_command_and_get_response_(const std::string &command, std::string &response_out)
		{
			ESP_LOGD(TAG, "Sending command: %s", command.c_str());

			flush_rx_(); // Clear any pending data

			// Calculate time since last response to respect polling delays
			uint32_t current_time = millis();
			uint32_t time_since_last_response = current_time - last_response_time_;

			// Ensure polling delay is respected
			if (last_response_time_ > 0 && time_since_last_response < POLLING_DELAY)
			{
				delay(POLLING_DELAY - time_since_last_response);
				yield(); // Feed watchdog
			}

			// Write the command
			this->write_str(command.c_str());
			this->write_str("\n");

			// Short delay before reading response
			delay(20); // Increased from 10ms to 20ms for more reliable response
			yield();   // Feed watchdog

			// Set appropriate timeout based on command type
			uint32_t timeout = RESPONSE_TIMEOUT;
			if (command.find("note.add") != std::string::npos)
			{
				timeout = 2000; // Increase timeout for data operations (2 seconds)
			}

			// Read response
			uint32_t start_time = millis();
			char last_char = 0;
			bool got_complete_response = false;

			response_out.clear();

			while (millis() - start_time < timeout && !got_complete_response)
			{
				yield(); // Feed watchdog

				if (this->available())
				{
					char c = this->read();

					// Look for end of response (newline after carriage return)
					if (c == '\n' && last_char == '\r')
					{
						if (!response_out.empty())
						{
							// Remove the last \r if it exists
							if (response_out.back() == '\r')
							{
								response_out.pop_back();
							}
							ESP_LOGD(TAG, "Received response: %s", response_out.c_str());
							last_response_time_ = millis();
							got_complete_response = true;

							// Check for error in response
							if (response_out.find("\"err\":") != std::string::npos)
							{
								ESP_LOGW(TAG, "Error in response: %s", response_out.c_str());
								return false;
							}
							return true;
						}
						response_out.clear();
					}
					else
					{
						// Add all characters to the response
						response_out += c;
					}
					last_char = c;
				}
				else
				{
					// Short delay to avoid busy waiting but feed watchdog
					delay(5); // Increased from 1ms to 5ms for better performance
					yield();
				}
			}

			if (!got_complete_response)
			{
				ESP_LOGW(TAG, "Timeout waiting for response (timeout was %dms for command type %s)",
						 timeout, command.find("note.add") != std::string::npos ? "note.add" : "standard");
				response_out = "{}";
			}

			return got_complete_response;
		}

		void Notecard::dump_config()
		{
			ESP_LOGCONFIG(TAG, "Notecard:");
			ESP_LOGCONFIG(TAG, "  Project ID: %s", this->project_id_.c_str());
			if (!this->org_.empty())
			{
				ESP_LOGCONFIG(TAG, "  Organization: %s", this->org_.c_str());
			}
			ESP_LOGCONFIG(TAG, "  Sync Interval: %ds", this->sync_interval_);
		}

		float Notecard::get_notecard_temperature()
		{
			std::string response;
			bool success = false;

			// Try up to 5 times to get temperature
			for (int retry = 0; retry < 5 && !success; retry++)
			{
				if (retry > 0)
				{
					ESP_LOGD(TAG, "Retrying card.temp (attempt %d/5)...", retry + 1);
					delay(100 * retry); // Increasing delay between retries
				}

				success = send_command_and_get_response_("{\"req\":\"card.temp\"}", response);

				if (success)
				{
					break;
				}
			}

			if (success)
			{
				std::string value = extract_json_value(response, "value");
				if (!value.empty())
				{
					char *end;
					float temp = strtof(value.c_str(), &end);
					if (end != value.c_str())
					{
						ESP_LOGD(TAG, "Parsed temperature: %f", temp);
						return temp;
					}
					ESP_LOGW(TAG, "Failed to parse temperature value: %s", value.c_str());
				}
				else
				{
					ESP_LOGW(TAG, "No temperature value found in response: %s", response.c_str());
				}
			}
			ESP_LOGW(TAG, "Failed to get temperature from Notecard");
			return 0.0;
		}

		float Notecard::get_notecard_battery_voltage()
		{
			std::string response;
			bool success = false;

			// Try up to 5 times to get battery voltage
			for (int retry = 0; retry < 5 && !success; retry++)
			{
				if (retry > 0)
				{
					ESP_LOGD(TAG, "Retrying card.voltage (attempt %d/5)...", retry + 1);
					delay(100 * retry); // Increasing delay between retries
				}

				success = send_command_and_get_response_("{\"req\":\"card.voltage\"}", response);

				if (success)
				{
					break;
				}
			}

			if (success)
			{
				std::string value = extract_json_value(response, "value");
				if (!value.empty())
				{
					char *end;
					float voltage = strtof(value.c_str(), &end);
					if (end != value.c_str())
					{
						ESP_LOGD(TAG, "Parsed battery voltage: %f", voltage);
						return voltage;
					}
					ESP_LOGW(TAG, "Failed to parse voltage value: %s", value.c_str());
				}
				else
				{
					ESP_LOGW(TAG, "No voltage value found in response: %s", response.c_str());
				}
			}
			ESP_LOGW(TAG, "Failed to get voltage from Notecard");
			return 0.0;
		}

	} // namespace notecard
} // namespace esphome