#include "modbus_tcp_gateway.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <lwip/sockets.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

namespace esphome {
namespace modbus_tcp_gateway {
static const char *const TAG = "modbus_tcp_gateway";

uint16_t ModbusTcpGateway::crc16_(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int j = 0; j < 8; ++j)
      crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
  }
  return crc;
}

void ModbusTcpGateway::setup() {
  direction_pin_->setup();
  direction_pin_->digital_write(false);
  server_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (server_fd_ < 0) { ESP_LOGE(TAG, "socket() failed"); mark_failed(); return; }
  int reuse = 1;
  setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);
  if (bind(server_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 || listen(server_fd_, 1) < 0) {
    ESP_LOGE(TAG, "Cannot bind/listen on TCP port %u", port_);
    close(server_fd_); server_fd_ = -1; mark_failed(); return;
  }
  fcntl(server_fd_, F_SETFL, fcntl(server_fd_, F_GETFL, 0) | O_NONBLOCK);
  ESP_LOGI(TAG, "Listening for Modbus TCP on %u", port_);
}

void ModbusTcpGateway::dump_config() {
  ESP_LOGCONFIG(TAG, "Modbus TCP-to-RTU gateway:");
  ESP_LOGCONFIG(TAG, "  TCP port: %u", port_);
  ESP_LOGCONFIG(TAG, "  RTU timeout: %u ms", timeout_ms_);
  ESP_LOGCONFIG(TAG, "  Writes enabled: %s", YESNO(allow_writes_));
  LOG_PIN("  Direction pin: ", direction_pin_);
}

void ModbusTcpGateway::close_client_() {
  if (client_fd_ >= 0) { close(client_fd_); client_fd_ = -1; }
  tcp_buffer_.clear(); rtu_buffer_.clear(); pending_ = false;
  direction_pin_->digital_write(false);
}

void ModbusTcpGateway::accept_client_() {
  if (server_fd_ < 0) return;
  int fd = accept(server_fd_, nullptr, nullptr);
  if (fd < 0) return;
  if (client_fd_ >= 0) { close(fd); return; }
  client_fd_ = fd;
  fcntl(client_fd_, F_SETFL, fcntl(client_fd_, F_GETFL, 0) | O_NONBLOCK);
  ESP_LOGI(TAG, "Modbus TCP client connected");
}

void ModbusTcpGateway::send_tcp_(const uint8_t *pdu, size_t length) {
  if (client_fd_ < 0 || length > 253) return;
  std::vector<uint8_t> frame = {
    static_cast<uint8_t>(transaction_id_ >> 8), static_cast<uint8_t>(transaction_id_),
    0, 0, static_cast<uint8_t>((length + 1) >> 8), static_cast<uint8_t>(length + 1), unit_id_
  };
  frame.insert(frame.end(), pdu, pdu + length);
  if (send(client_fd_, frame.data(), frame.size(), MSG_DONTWAIT) != static_cast<int>(frame.size())) close_client_();
}

void ModbusTcpGateway::send_exception_(uint8_t function, uint8_t code) {
  uint8_t pdu[] = {static_cast<uint8_t>(function | 0x80), code};
  send_tcp_(pdu, sizeof(pdu));
}

void ModbusTcpGateway::start_request_(const uint8_t *frame, size_t length) {
  transaction_id_ = (uint16_t(frame[0]) << 8) | frame[1];
  unit_id_ = frame[6];
  function_ = frame[7];
  if (frame[2] != 0 || frame[3] != 0 || unit_id_ == 0 || length < 8 || length > 260) {
    send_exception_(function_, 0x03); return;
  }
  size_t pdu_len = length - 7;
  if (function_ != 0x03 && function_ != 0x04 && function_ != 0x06 && function_ != 0x10) {
    send_exception_(function_, 0x01); return;
  }
  if ((function_ == 0x06 || function_ == 0x10) && !allow_writes_) {
    send_exception_(function_, 0x01); return;
  }
  if (((function_ == 0x03 || function_ == 0x04 || function_ == 0x06) && pdu_len != 5) ||
      (function_ == 0x10 && (pdu_len < 6 || pdu_len != size_t(6 + frame[12])))) {
    send_exception_(function_, 0x03); return;
  }
  std::vector<uint8_t> rtu;
  rtu.reserve(pdu_len + 3);
  rtu.push_back(unit_id_);
  rtu.insert(rtu.end(), frame + 7, frame + length);
  uint16_t crc = crc16_(rtu.data(), rtu.size());
  rtu.push_back(crc & 0xFF); rtu.push_back(crc >> 8);
  while (available()) { uint8_t ignored; read_byte(&ignored); }
  rtu_buffer_.clear();
  direction_pin_->digital_write(true);
  delayMicroseconds(turnaround_us_);
  write_array(rtu.data(), rtu.size());
  flush();
  delayMicroseconds(turnaround_us_);
  direction_pin_->digital_write(false);
  request_started_ = millis(); pending_ = true; ++requests_;
}

bool ModbusTcpGateway::response_complete_(const std::vector<uint8_t> &b) {
  if (b.size() < 5) return false;
  if (b[1] & 0x80) return b.size() >= 5;
  if (b[1] == 0x03 || b[1] == 0x04) return b.size() >= size_t(b[2]) + 5;
  if (b[1] == 0x06 || b[1] == 0x10) return b.size() >= 8;
  return false;
}

void ModbusTcpGateway::complete_response_(bool timed_out) {
  pending_ = false;
  if (timed_out) { ++timeouts_; send_exception_(function_, 0x0B); return; }
  if (rtu_buffer_.size() < 5 || rtu_buffer_[0] != unit_id_ ||
      (rtu_buffer_[1] != function_ && rtu_buffer_[1] != (function_ | 0x80))) {
    ++crc_errors_; send_exception_(function_, 0x04); return;
  }
  const size_t n = rtu_buffer_.size();
  uint16_t crc = crc16_(rtu_buffer_.data(), n - 2);
  if (rtu_buffer_[n - 2] != (crc & 0xFF) || rtu_buffer_[n - 1] != (crc >> 8)) {
    ++crc_errors_; send_exception_(function_, 0x04); return;
  }
  ++successes_;
  send_tcp_(rtu_buffer_.data() + 1, n - 3);
}

void ModbusTcpGateway::read_rtu_() {
  if (!pending_) return;
  while (available() && rtu_buffer_.size() < 260) {
    uint8_t byte;
    if (!read_byte(&byte)) break;
    rtu_buffer_.push_back(byte);
    if (response_complete_(rtu_buffer_)) { complete_response_(false); return; }
  }
  if (millis() - request_started_ >= timeout_ms_) complete_response_(true);
}

void ModbusTcpGateway::read_client_() {
  if (client_fd_ < 0) return;
  uint8_t buf[256];
  int n = recv(client_fd_, buf, sizeof(buf), MSG_DONTWAIT);
  if (n == 0) { close_client_(); return; }
  if (n < 0 && errno != EWOULDBLOCK && errno != EAGAIN) { close_client_(); return; }
  if (n > 0) tcp_buffer_.insert(tcp_buffer_.end(), buf, buf + n);
  if (tcp_buffer_.size() > 520) { close_client_(); return; }
  if (pending_) return;
  if (tcp_buffer_.size() < 7) return;
  size_t payload_len = (size_t(tcp_buffer_[4]) << 8) | tcp_buffer_[5];
  if (payload_len < 2 || payload_len > 254) { close_client_(); return; }
  size_t length = payload_len + 6;
  if (tcp_buffer_.size() < length) return;
  std::vector<uint8_t> frame(tcp_buffer_.begin(), tcp_buffer_.begin() + length);
  tcp_buffer_.erase(tcp_buffer_.begin(), tcp_buffer_.begin() + length);
  start_request_(frame.data(), frame.size());
}

void ModbusTcpGateway::loop() {
  accept_client_();
  read_rtu_();
  read_client_();
}
}  // namespace modbus_tcp_gateway
}  // namespace esphome
