#pragma once
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/gpio.h"
#include <cstdint>
#include <vector>

namespace esphome {
namespace modbus_tcp_gateway {
class ModbusTcpGateway : public Component, public uart::UARTDevice {
 public:
  void set_direction_pin(GPIOPin *pin) { direction_pin_ = pin; }
  void set_port(uint16_t port) { port_ = port; }
  void set_response_timeout(uint32_t ms) { timeout_ms_ = ms; }
  void set_turnaround_us(uint32_t us) { turnaround_us_ = us; }
  void set_allow_writes(bool value) { allow_writes_ = value; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
 protected:
  void close_client_();
  void accept_client_();
  void read_client_();
  void start_request_(const uint8_t *frame, size_t length);
  void read_rtu_();
  void complete_response_(bool timed_out);
  void send_exception_(uint8_t function, uint8_t code);
  void send_tcp_(const uint8_t *pdu, size_t length);
  static uint16_t crc16_(const uint8_t *data, size_t len);
  static bool response_complete_(const std::vector<uint8_t> &buffer);
  GPIOPin *direction_pin_{nullptr};
  uint16_t port_{5020};
  uint32_t timeout_ms_{1000};
  uint32_t turnaround_us_{200};
  bool allow_writes_{false};
  int server_fd_{-1};
  int client_fd_{-1};
  std::vector<uint8_t> tcp_buffer_;
  std::vector<uint8_t> rtu_buffer_;
  uint16_t transaction_id_{0};
  uint8_t unit_id_{0};
  uint8_t function_{0};
  uint32_t request_started_{0};
  bool pending_{false};
  uint32_t requests_{0}, successes_{0}, timeouts_{0}, crc_errors_{0};
};
}  // namespace modbus_tcp_gateway
}  // namespace esphome
