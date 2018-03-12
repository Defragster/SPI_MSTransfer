#include <SPI_MSTransfer.h>
#include "Stream.h"
#include <SPI.h>
//#include <i2c_t3.h>
#include <deque>
#include <queue>
#include "TeensyThreads.h"

#define Serial4 Serial1
#define Serial5 Serial1
#define Serial6 Serial1


SPI_MSTransfer *_slave_pointer;
std::mutex SPI_MSTransfer::_slave_cb_mutex; // slave callback mutex
std::deque<std::vector<uint16_t>> SPI_MSTransfer::teensy_handler_queue; // slave callback queue
std::deque<std::vector<uint16_t>> SPI_MSTransfer::teensy_stm_queue; // slave queue that the master cherry picks from
std::deque<std::vector<uint16_t>> SPI_MSTransfer::teensy_master_queue; // dormant state, possibly removed if unused.
_slave_handler_ptr SPI_MSTransfer::_slave_handler = nullptr;
_master_handler_ptr SPI_MSTransfer::_master_handler = nullptr;
bool SPI_MSTransfer::watchdogEnabled = 0;
uint32_t SPI_MSTransfer::watchdogFeedInterval = millis();
uint32_t SPI_MSTransfer::watchdogTimeout = 0;

void SPI_MSTransfer::onTransfer(_slave_handler_ptr handler) {
  if ( _master_access ) _master_handler = handler;
  if ( _slave_access ) _slave_handler = handler;
}

SPI_MSTransfer::SPI_MSTransfer(const char *data, uint8_t cs, SPIClass *SPIWire, uint32_t spi_bus_speed) {
  chip_select = cs; spi_port = SPIWire; _master_access = 1; _spi_bus_speed = spi_bus_speed;
  ::pinMode(cs, OUTPUT); // make sure CS is OUTPUT before deassertion.
  ::digitalWriteFast(cs, HIGH); // deassert the CS way before SPI initializes :)
  debugSerial = nullptr;
  if ( !strcmp(data, "Serial") ) { _serial_port_identifier = 0; }
  else if ( !strcmp(data, "Serial1") ) { _serial_port_identifier = 1; }
  else if ( !strcmp(data, "Serial2") ) { _serial_port_identifier = 2; }
  else if ( !strcmp(data, "Serial3") ) { _serial_port_identifier = 3; }
  else if ( !strcmp(data, "Serial4") ) { _serial_port_identifier = 4; }
  else if ( !strcmp(data, "Serial5") ) { _serial_port_identifier = 5; }
  else if ( !strcmp(data, "Serial6") ) { _serial_port_identifier = 6; }






  else if ( !strcmp(data, "Wire") ) { wire_port = 0; }
  else if ( !strcmp(data, "Wire1") ) { wire_port = 1; }
  else if ( !strcmp(data, "Wire2") ) { wire_port = 2; }
  else if ( !strcmp(data, "Wire3") ) { wire_port = 3; }
  else if ( !strcmp(data, "SPI1") ) { remote_spi_port = 1; }
  else if ( !strcmp(data, "SPI2") ) { remote_spi_port = 2; }
  else if ( !strcmp(data, "EEPROM") ) { eeprom_support = 1; }
  else if ( !strcmp(data, "SERVO") ) { servo_support = 1; }
  else if ( !strcmp(data, "FASTLED" ) ) { fastled_support = 1; }
}


void SPI_MSTransfer::debug(Stream &serial) {
  debugSerial = &serial;
}
void SPI_MSTransfer::SPI_assert() {
  spi_port->beginTransaction(SPISettings(_spi_bus_speed, MSBFIRST, SPI_MODE0)); ::digitalWriteFast(chip_select, LOW);
}
void SPI_MSTransfer::SPI_deassert() { ::digitalWriteFast(chip_select, HIGH); spi_port->endTransaction(); }



bool SPI_MSTransfer::command_ack_response(uint16_t *data, uint32_t len) {
  uint8_t resend_count = 0; uint32_t timeout = micros(); uint16_t _crc = 0;
  while ( 1 ) {
    _crc = spi_port->transfer16(0xFFFF);
    if ( _crc == 0xF00D ) {
      spi_port->transfer16(0xBEEF); break;
    }
    else if ( _crc == 0xBAAD ) {
      spi_port->transfer16(0xBEEF); SPI_deassert(); return 0;
    }
    if ( micros() - timeout > 50 ) {
      resend_count++;
      if ( resend_count > 3 ) {
        if ( debugSerial != nullptr ) {
          Serial.print(F("DEBUG: [SLAVE CS ")); Serial.print(chip_select);
          Serial.print(F("] [INFO] FAILED RESENDING ")); Serial.print(resend_count);
          Serial.print(F(" TIMES. TRANSFER ABORTED.")); delay(1000);
        }
        break;
      }
      if ( debugSerial != nullptr ) {
        Serial.print(F("] [INFO] FAILED RESENDING ")); Serial.print(resend_count);
        Serial.print(F(" TIMES. RETRYING...")); delay(1000);
      }
      timeout = micros();
    }
  }
  return 1;
}


void SPI_MSTransfer::digitalWriteFast(uint8_t pin, bool state) { digitalWrite(pin, state); }
void SPI_MSTransfer::digitalWrite(uint8_t pin, bool state) {
  if ( _slave_access ) return;
  uint16_t data[5], checksum = 0, data_pos = 0;
  data[data_pos] = 0x9766; checksum ^= data[data_pos]; data_pos++; // HEADER
  data[data_pos] = sizeof(data) / 2; checksum ^= data[data_pos]; data_pos++; // DATA SIZE
  data[data_pos] = 0x0000; checksum ^= data[data_pos]; data_pos++; // SUB SWITCH STATEMENT
  data[data_pos] = ((uint16_t)(pin << 8) | state); checksum ^= data[data_pos]; data_pos++;
  data[data_pos] = checksum;
  SPI_assert();
  for ( uint16_t i = 0; i < data[1]; i++ ) { spi_port->transfer16(data[i]); }
  if ( !command_ack_response(data, sizeof(data) / 2) ) return; // RECEIPT ACK
  for ( uint8_t i = 0; i < 100; i++ ) {
    if ( spi_port->transfer16(0xFFFF) == 0xAA55 ) {
      uint16_t buf[spi_port->transfer16(0xFFFF)]; buf[0] = 0xAA55; buf[1] = sizeof(buf) / 2; checksum = buf[0]; checksum ^= buf[1];
      for ( uint16_t i = 2; i < buf[1]; i++ ) { delayMicroseconds(_transfer_slowdown); buf[i] = spi_port->transfer16(0xFFFF); if ( i < buf[1] - 1 ) checksum ^= buf[i]; }
      if ( checksum == buf[buf[1] - 1] ) {
        spi_port->transfer16(0xD0D0); // send confirmation
        SPI_deassert(); return;
      }
    }
  }
  SPI_deassert(); return;
}
bool SPI_MSTransfer::digitalReadFast(uint8_t pin) { return digitalRead(pin); }
bool SPI_MSTransfer::digitalRead(uint8_t pin) {
  if ( _slave_access ) return 0;
  uint16_t data[5], checksum = 0, data_pos = 0;
  data[data_pos] = 0x9766; checksum ^= data[data_pos]; data_pos++; // HEADER
  data[data_pos] = sizeof(data) / 2; checksum ^= data[data_pos]; data_pos++; // DATA SIZE
  data[data_pos] = 0x0001; checksum ^= data[data_pos]; data_pos++; // SUB SWITCH STATEMENT
  data[data_pos] = pin; checksum ^= data[data_pos]; data_pos++;
  data[data_pos] = checksum;
  SPI_assert();
  for ( uint16_t i = 0; i < data[1]; i++ ) { spi_port->transfer16(data[i]); }
  if ( !command_ack_response(data, sizeof(data) / 2) ) return 0; // RECEIPT ACK
  for ( uint8_t i = 0; i < 100; i++ ) {
    if ( spi_port->transfer16(0xFFFF) == 0xAA55 ) {
      uint16_t buf[spi_port->transfer16(0xFFFF)]; buf[0] = 0xAA55; buf[1] = sizeof(buf) / 2; checksum = buf[0]; checksum ^= buf[1];
      for ( uint16_t i = 2; i < buf[1]; i++ ) { delayMicroseconds(_transfer_slowdown); buf[i] = spi_port->transfer16(0xFFFF); if ( i < buf[1] - 1 ) checksum ^= buf[i]; }
      if ( checksum == buf[buf[1] - 1] ) {
        spi_port->transfer16(0xD0D0); // send confirmation
        SPI_deassert(); return buf[2];
      }
    }
  }
  SPI_deassert(); return 0;
}
void SPI_MSTransfer::pinMode(uint8_t pin, uint8_t state) {
  if ( _slave_access ) return;
  uint16_t data[5], checksum = 0, data_pos = 0;
  data[data_pos] = 0x9766; checksum ^= data[data_pos]; data_pos++; // HEADER
  data[data_pos] = sizeof(data) / 2; checksum ^= data[data_pos]; data_pos++; // DATA SIZE
  data[data_pos] = 0x0002; checksum ^= data[data_pos]; data_pos++; // SUB SWITCH STATEMENT
  data[data_pos] = ((uint16_t)(pin << 8) | state); checksum ^= data[data_pos]; data_pos++;
  data[data_pos] = checksum;
  SPI_assert();
  for ( uint16_t i = 0; i < data[1]; i++ ) { spi_port->transfer16(data[i]); }
  if ( !command_ack_response(data, sizeof(data) / 2) ) return; // RECEIPT ACK
  for ( uint8_t i = 0; i < 100; i++ ) {
    if ( spi_port->transfer16(0xFFFF) == 0xAA55 ) {
      uint16_t buf[spi_port->transfer16(0xFFFF)]; buf[0] = 0xAA55; buf[1] = sizeof(buf) / 2; checksum = buf[0]; checksum ^= buf[1];
      for ( uint16_t i = 2; i < buf[1]; i++ ) { delayMicroseconds(_transfer_slowdown); buf[i] = spi_port->transfer16(0xFFFF); if ( i < buf[1] - 1 ) checksum ^= buf[i]; }
      if ( checksum == buf[buf[1] - 1] ) {
        spi_port->transfer16(0xD0D0); // send confirmation
        SPI_deassert(); return;
      }
    }
  }
  SPI_deassert(); return;
}
void SPI_MSTransfer::pinToggle(uint8_t pin) {
  if ( _slave_access ) return;
  uint16_t data[5], checksum = 0, data_pos = 0;
  data[data_pos] = 0x9766; checksum ^= data[data_pos]; data_pos++; // HEADER
  data[data_pos] = sizeof(data) / 2; checksum ^= data[data_pos]; data_pos++; // DATA SIZE
  data[data_pos] = 0x0003; checksum ^= data[data_pos]; data_pos++; // SUB SWITCH STATEMENT
  data[data_pos] = pin; checksum ^= data[data_pos]; data_pos++;
  data[data_pos] = checksum;
  SPI_assert();
  for ( uint16_t i = 0; i < data[1]; i++ ) { spi_port->transfer16(data[i]); }
  if ( !command_ack_response(data, sizeof(data) / 2) ) return; // RECEIPT ACK
  for ( uint8_t i = 0; i < 100; i++ ) {
    if ( spi_port->transfer16(0xFFFF) == 0xAA55 ) {
      uint16_t buf[spi_port->transfer16(0xFFFF)]; buf[0] = 0xAA55; buf[1] = sizeof(buf) / 2; checksum = buf[0]; checksum ^= buf[1];
      for ( uint16_t i = 2; i < buf[1]; i++ ) { delayMicroseconds(_transfer_slowdown); buf[i] = spi_port->transfer16(0xFFFF); if ( i < buf[1] - 1 ) checksum ^= buf[i]; }
      if ( checksum == buf[buf[1] - 1] ) {
        spi_port->transfer16(0xD0D0); // send confirmation
        SPI_deassert(); return;
      }
    }
  }
  SPI_deassert(); return;
}
uint16_t SPI_MSTransfer::transfer16(uint16_t *buffer, uint16_t length, uint16_t packetID, bool fire_and_forget) {
  if ( _slave_access ) {
    uint16_t data[6 + length], checksum = 0, data_pos = 0;
    data[data_pos] = 0xAA55; checksum ^= data[data_pos]; data_pos++; // HEADER
    data[data_pos] = sizeof(data) / 2; checksum ^= data[data_pos]; data_pos++; // DATA SIZE
    data[data_pos] = 0x0000; checksum ^= data[data_pos]; data_pos++; // SUB SWITCH STATEMENT
    data[data_pos] = length; checksum ^= data[data_pos]; data_pos++;
    data[data_pos] = packetID; checksum ^= data[data_pos]; data_pos++;
    for ( uint16_t i = 0; i < length; i++ ) { data[data_pos] = buffer[i]; checksum ^= data[data_pos]; data_pos++; }
    data[data_pos] = checksum;
    std::vector<uint16_t> _vector(data, data + data[1]); teensy_stm_queue.push_back(_vector);
    if ( teensy_stm_queue.size() > 14 ) teensy_stm_queue.pop_front();
    return packetID;
  }
  if ( _master_access ) {
    uint16_t data[6 + length], checksum = 0, data_pos = 0;
    data[data_pos] = ( fire_and_forget ) ? 0x9244 : 0x9243; checksum ^= data[data_pos]; data_pos++; // HEADER
    data[data_pos] = sizeof(data) / 2; checksum ^= data[data_pos]; data_pos++; // DATA SIZE
    data[data_pos] = 0x0000; checksum ^= data[data_pos]; data_pos++; // SUB SWITCH STATEMENT
    data[data_pos] = length; checksum ^= data[data_pos]; data_pos++;
    data[data_pos] = packetID; checksum ^= data[data_pos]; data_pos++;
    for ( uint16_t i = 0; i < length; i++ ) { data[data_pos] = buffer[i]; checksum ^= data[data_pos]; data_pos++; }
    data[data_pos] = checksum;
    if ( fire_and_forget ) {
      spi_port->beginTransaction(SPISettings(_spi_bus_speed, MSBFIRST, SPI_MODE0));
      ::digitalWriteFast(chip_select, LOW);
      for ( uint16_t i = 0; i < data[1]; i++ ) { spi_port->transfer16(data[i]); }
      uint32_t timeout = millis();
      while ( spi_port->transfer16(0xFFFF) != 0xBABE && millis() - timeout < 100 );
      spi_port->transfer16(0xD0D0); // send confirmation
      ::digitalWriteFast(chip_select, HIGH);
      spi_port->endTransaction();
      return data[4]; // F&F, RETURN PACKETID
    }
    SPI_assert();
    for ( uint16_t i = 0; i < data[1]; i++ ) { spi_port->transfer16(data[i]); }
    if ( !command_ack_response(data, sizeof(data) / 2) ) return 0; // RECEIPT ACK
    for ( uint8_t i = 0; i < 100; i++ ) {
      if ( spi_port->transfer16(0xFFFF) == 0xAA55 ) {
        uint16_t buf[spi_port->transfer16(0xFFFF)]; buf[0] = 0xAA55; buf[1] = sizeof(buf) / 2; checksum = buf[0]; checksum ^= buf[1];
        for ( uint16_t i = 2; i < buf[1]; i++ ) { delayMicroseconds(_transfer_slowdown); buf[i] = spi_port->transfer16(0xFFFF); if ( i < buf[1] - 1 ) checksum ^= buf[i]; }
        if ( checksum == buf[buf[1] - 1] ) {
          spi_port->transfer16(0xD0D0); // send confirmation
          SPI_deassert(); return buf[2];
        }
      }
    }
    SPI_deassert();
  }
  return 0;
}
void SPI_MSTransfer::begin(uint32_t baudrate) { // set remote serial port and baud
  if ( _slave_access ) return;
  if ( _serial_port_identifier != -1 ) {
    uint16_t data[7], checksum = 0, data_pos = 0;
    data[data_pos] = 0x3235; checksum ^= data[data_pos]; data_pos++; // HEADER
    data[data_pos] = sizeof(data) / 2; checksum ^= data[data_pos]; data_pos++; // DATA SIZE
    data[data_pos] = 0x0000; checksum ^= data[data_pos]; data_pos++; // SUB SWITCH STATEMENT
    data[data_pos] = _serial_port_identifier; checksum ^= data[data_pos]; data_pos++;
    data[data_pos] = baudrate >> 16; checksum ^= data[data_pos]; data_pos++;
    data[data_pos] = baudrate; checksum ^= data[data_pos]; data_pos++;
    data[data_pos] = checksum;
    SPI_assert();
    for ( uint16_t i = 0; i < data[1]; i++ ) { spi_port->transfer16(data[i]); }
    if ( !command_ack_response(data, sizeof(data) / 2) ) return; // RECEIPT ACK
    for ( uint8_t i = 0; i < 100; i++ ) {
      if ( spi_port->transfer16(0xFFFF) == 0xAA55 ) {
        uint16_t buf[spi_port->transfer16(0xFFFF)]; buf[0] = 0xAA55; buf[1] = sizeof(buf) / 2; checksum = buf[0]; checksum ^= buf[1];
        for ( uint16_t i = 2; i < buf[1]; i++ ) { delayMicroseconds(_transfer_slowdown); buf[i] = spi_port->transfer16(0xFFFF); if ( i < buf[1] - 1 ) checksum ^= buf[i]; }
        if ( checksum == buf[buf[1] - 1] ) {
          spi_port->transfer16(0xD0D0); // send confirmation
          SPI_deassert(); return;
        }
      }
    }
    SPI_deassert(); return;
  }
}
int SPI_MSTransfer::read(void) {
  if ( _slave_access ) return -1;
  if ( _serial_port_identifier != -1 ) {
    uint16_t data[5], checksum = 0, data_pos = 0;
    data[data_pos] = 0x3235; checksum ^= data[data_pos]; data_pos++; // HEADER
    data[data_pos] = sizeof(data) / 2; checksum ^= data[data_pos]; data_pos++; // DATA SIZE
    data[data_pos] = 0x0001; checksum ^= data[data_pos]; data_pos++; // SUB SWITCH STATEMENT
    data[data_pos] = _serial_port_identifier; checksum ^= data[data_pos]; data_pos++;
    data[data_pos] = checksum;
    SPI_assert();
    for ( uint16_t i = 0; i < data[1]; i++ ) { spi_port->transfer16(data[i]); }
    if ( !command_ack_response(data, sizeof(data) / 2) ) return -1; // RECEIPT ACK
    for ( uint8_t i = 0; i < 100; i++ ) {
      if ( spi_port->transfer16(0xFFFF) == 0xAA55 ) {
        uint16_t buf[spi_port->transfer16(0xFFFF)]; buf[0] = 0xAA55; buf[1] = sizeof(buf) / 2; checksum = buf[0]; checksum ^= buf[1];
        for ( uint16_t i = 2; i < buf[1]; i++ ) { delayMicroseconds(_transfer_slowdown); buf[i] = spi_port->transfer16(0xFFFF); if ( i < buf[1] - 1 ) checksum ^= buf[i]; }
        if ( checksum == buf[buf[1] - 1] ) {
          spi_port->transfer16(0xD0D0); // send confirmation
          SPI_deassert(); return (int16_t)buf[2];
        }
      }
    }
    SPI_deassert(); return -1;
  }
}
int SPI_MSTransfer::peek() {
  if ( _slave_access ) return -1;
  if ( _serial_port_identifier != -1 ) {
    uint16_t data[5], checksum = 0, data_pos = 0;
    data[data_pos] = 0x3235; checksum ^= data[data_pos]; data_pos++; // HEADER
    data[data_pos] = sizeof(data) / 2; checksum ^= data[data_pos]; data_pos++; // DATA SIZE
    data[data_pos] = 0x0003; checksum ^= data[data_pos]; data_pos++; // SUB SWITCH STATEMENT
    data[data_pos] = _serial_port_identifier; checksum ^= data[data_pos]; data_pos++;
    data[data_pos] = checksum;
    SPI_assert();
    for ( uint16_t i = 0; i < data[1]; i++ ) { spi_port->transfer16(data[i]); }
    if ( !command_ack_response(data, sizeof(data) / 2) ) return -1; // RECEIPT ACK
    for ( uint8_t i = 0; i < 100; i++ ) {
      if ( spi_port->transfer16(0xFFFF) == 0xAA55 ) {
        uint16_t buf[spi_port->transfer16(0xFFFF)]; buf[0] = 0xAA55; buf[1] = sizeof(buf) / 2; checksum = buf[0]; checksum ^= buf[1];
        for ( uint16_t i = 2; i < buf[1]; i++ ) { delayMicroseconds(_transfer_slowdown); buf[i] = spi_port->transfer16(0xFFFF); if ( i < buf[1] - 1 ) checksum ^= buf[i]; }
        if ( checksum == buf[buf[1] - 1] ) {
          spi_port->transfer16(0xD0D0); // send confirmation
          SPI_deassert(); return (int16_t)buf[2];
        }
      }
    }
    SPI_deassert(); return -1;
  }
}
int SPI_MSTransfer::available(void) {
  if ( _slave_access ) return 0;
  if ( _serial_port_identifier != -1 ) {
    uint16_t data[5], checksum = 0, data_pos = 0;
    data[data_pos] = 0x3235; checksum ^= data[data_pos]; data_pos++; // HEADER
    data[data_pos] = sizeof(data) / 2; checksum ^= data[data_pos]; data_pos++; // DATA SIZE
    data[data_pos] = 0x0002; checksum ^= data[data_pos]; data_pos++; // SUB SWITCH STATEMENT
    data[data_pos] = _serial_port_identifier; checksum ^= data[data_pos]; data_pos++;
    data[data_pos] = checksum;
    SPI_assert();
    for ( uint16_t i = 0; i < data[1]; i++ ) { spi_port->transfer16(data[i]); }
    if ( !command_ack_response(data, sizeof(data) / 2) ) return 0; // RECEIPT ACK
    for ( uint8_t i = 0; i < 100; i++ ) {
      if ( spi_port->transfer16(0xFFFF) == 0xAA55 ) {
        uint16_t buf[spi_port->transfer16(0xFFFF)]; buf[0] = 0xAA55; buf[1] = sizeof(buf) / 2; checksum = buf[0]; checksum ^= buf[1];
        for ( uint16_t i = 2; i < buf[1]; i++ ) { delayMicroseconds(_transfer_slowdown); buf[i] = spi_port->transfer16(0xFFFF); if ( i < buf[1] - 1 ) checksum ^= buf[i]; }
        if ( checksum == buf[buf[1] - 1] ) {
          spi_port->transfer16(0xD0D0); // send confirmation
          SPI_deassert(); return buf[2];
        }
      }
    }
    SPI_deassert(); return 0;
  }
}
size_t SPI_MSTransfer::write(const uint8_t *buf, size_t size) {
  if ( _slave_access ) return 0;
  if ( _serial_port_identifier != -1 ) {
    uint16_t data[6 + size], checksum = 0, data_pos = 0;
    data[data_pos] = 0x3235; checksum ^= data[data_pos]; data_pos++; // HEADER
    data[data_pos] = sizeof(data) / 2; checksum ^= data[data_pos]; data_pos++; // DATA SIZE
    data[data_pos] = 0x0004; checksum ^= data[data_pos]; data_pos++; // SUB SWITCH STATEMENT
    data[data_pos] = _serial_port_identifier; checksum ^= data[data_pos]; data_pos++;
    data[data_pos] = size; checksum ^= data[data_pos]; data_pos++;
    for ( uint16_t i = 0; i < size; i++ ) { data[data_pos] = buf[i]; checksum ^= data[data_pos]; data_pos++; }
    data[data_pos] = checksum;
    SPI_assert();
    for ( uint16_t i = 0; i < data[1]; i++ ) { spi_port->transfer16(data[i]); }
    if ( !command_ack_response(data, sizeof(data) / 2) ) return 0; // RECEIPT ACK
    for ( uint8_t i = 0; i < 100; i++ ) {
      if ( spi_port->transfer16(0xFFFF) == 0xAA55 ) {
        uint16_t buf[spi_port->transfer16(0xFFFF)]; buf[0] = 0xAA55; buf[1] = sizeof(buf) / 2; checksum = buf[0]; checksum ^= buf[1];
        for ( uint16_t i = 2; i < buf[1]; i++ ) { delayMicroseconds(_transfer_slowdown); buf[i] = spi_port->transfer16(0xFFFF); if ( i < buf[1] - 1 ) checksum ^= buf[i]; }
        if ( checksum == buf[buf[1] - 1] ) {
          spi_port->transfer16(0xD0D0); // send confirmation
          SPI_deassert(); return buf[2];
        }
      }
    }
    SPI_deassert(); return 0;
  }
}
void SPI_MSTransfer::flush() {
  if ( _slave_access ) return;
  if ( _serial_port_identifier != -1 ) {
    uint16_t data[5], checksum = 0, data_pos = 0;
    data[data_pos] = 0x3235; checksum ^= data[data_pos]; data_pos++; // HEADER
    data[data_pos] = sizeof(data) / 2; checksum ^= data[data_pos]; data_pos++; // DATA SIZE
    data[data_pos] = 0x0005; checksum ^= data[data_pos]; data_pos++; // SUB SWITCH STATEMENT
    data[data_pos] = _serial_port_identifier; checksum ^= data[data_pos]; data_pos++;
    data[data_pos] = checksum;
    SPI_assert();
    for ( uint16_t i = 0; i < data[1]; i++ ) spi_port->transfer16(data[i]);
    if ( !command_ack_response(data, sizeof(data) / 2) ) return; // RECEIPT ACK
    for ( uint8_t i = 0; i < 100; i++ ) {
      if ( spi_port->transfer16(0xFFFF) == 0xAA55 ) {
        uint16_t buf[spi_port->transfer16(0xFFFF)]; buf[0] = 0xAA55; buf[1] = sizeof(buf) / 2; checksum = buf[0]; checksum ^= buf[1];
        for ( uint16_t i = 2; i < buf[1]; i++ ) { delayMicroseconds(_transfer_slowdown); buf[i] = spi_port->transfer16(0xFFFF); if ( i < buf[1] - 1 ) checksum ^= buf[i]; }
        if ( checksum == buf[buf[1] - 1] ) {
          spi_port->transfer16(0xD0D0); // send confirmation
          SPI_deassert(); return;
        }
      }
    }
    SPI_deassert(); return;
  }
}
size_t SPI_MSTransfer::print(const char *p) {
  write(p, strlen(p));
}
size_t SPI_MSTransfer::println(const char *p) {
  char _text[strlen(p) + 1]; for ( uint16_t i = 0; i < strlen(p); i++ ) _text[i] = p[i]; _text[sizeof(_text) - 1] = '\n'; write(_text, sizeof(_text));
}
void SPI_MSTransfer::begin() {
  if ( _slave_access ) {
    SIM_SCGC6 |= SIM_SCGC6_SPI0; // enable slave clock
    SPI0_MCR |= SPI_MCR_HALT | SPI_MCR_MDIS; // stop
    SPI0_CTAR0_SLAVE = SPI_CTAR_FMSZ(15) & SPI0_CTAR0_SLAVE & (~(SPI_CTAR_CPOL | SPI_CTAR_CPHA) | 0x00 << 25);
    SPI0_RSER = 0x00020000;
    CORE_PIN14_CONFIG = PORT_PCR_MUX(2);
    CORE_PIN11_CONFIG = PORT_PCR_DSE | PORT_PCR_MUX(2);
    CORE_PIN12_CONFIG = PORT_PCR_MUX(2);
    CORE_PIN2_CONFIG =  PORT_PCR_PS | PORT_PCR_MUX(2); // this uses pin 2 for the CS so Serial2 can be used instead.
    SPI0_MCR &= ~SPI_MCR_HALT & ~SPI_MCR_MDIS; // start
    NVIC_SET_PRIORITY(IRQ_SPI0, 0); // set priority
    NVIC_ENABLE_IRQ(IRQ_SPI0); // enable CS IRQ
  }
}



































































































uint16_t SPI_MSTransfer::analogRead(uint8_t pin) {
}
void SPI_MSTransfer::analogWrite(uint8_t pin, uint16_t value) {
}
void SPI_MSTransfer::analogReadResolution(uint8_t value) {
}
void SPI_MSTransfer::analogWriteResolution(uint8_t value) {
}
void SPI_MSTransfer::beginTransmission(uint8_t addr) {
}
void SPI_MSTransfer::endTransmission() {
}
void SPI_MSTransfer::requestFrom(uint8_t address, uint8_t bytes) {
}
uint8_t SPI_MSTransfer::transfer(uint8_t data) {
}
uint16_t SPI_MSTransfer::transfer16(uint16_t data) {
}
bool SPI_MSTransfer::online() {
}
void SPI_MSTransfer::software_reset() {
}
uint8_t SPI_MSTransfer::status_update() {
}
void SPI_MSTransfer::beginTransaction(uint32_t baudrate, uint8_t msblsb, uint8_t dataMode) { // set remote serial port and baud
}
void SPI_MSTransfer::endTransaction() { // end remote SPI transaction
}
int SPI_MSTransfer::read(int addr) {
}
uint16_t SPI_MSTransfer::length() {
}
size_t SPI_MSTransfer::write(int addr, uint8_t data) {
}
void SPI_MSTransfer::update(int addr, uint8_t data) {
}
uint32_t SPI_MSTransfer::crc() {
}
//void SPI_MSTransfer::show(uint8_t pin, CRGB *array, uint16_t array_length) { // update Fastled pixels
//}












































































SPI_MSTransfer::SPI_MSTransfer(const char *data, const char *mode) {
  if ( !strcmp(data, "SLAVE") && !strcmp(mode, "STANDALONE") ) {
    debugSerial = nullptr;
    ::pinMode(13, OUTPUT);  // detach pin13 from SPI0 for led use.
    _slave_pointer = this; _slave_access = 1;
  }
}

void spi0_isr(void) {
  static uint16_t data[DATA_BUFFER_MAX];
  uint16_t buffer_pos = 0, len = 0, process_crc = 0;
  while ( !(GPIOD_PDIR & 0x01) ) {
    if ( SPI0_SR & 0xF0 ) {
      SPI0_PUSHR_SLAVE = 0xDEAF; data[buffer_pos] = SPI0_POPR; buffer_pos++;
      if ( data[1] && buffer_pos < data[1] ) process_crc ^= data[buffer_pos-1];
      if ( buffer_pos >= DATA_BUFFER_MAX ) break; // BUFFER LENGTH PROTECTION
    }
    if ( data[1] && buffer_pos >= data[1] ) break;
  }

  if ( data[1] && buffer_pos >= data[1] ) {
    len = data[1];

    /* F&F BLOCK START */
    if ( data[0] == 0x9244 ) {
      std::vector<uint16_t> _vector(data, data + data[1]);
      _slave_pointer->SPI_MSTransfer::teensy_handler_queue.push_back(_vector);
      while ( !(GPIOD_PDIR & 0x01) ) { // wait here until MASTER confirms F&F receipt
        if ( SPI0_SR & 0xF0 ) {
          SPI0_PUSHR_SLAVE = 0xBABE;
          if ( SPI0_POPR == 0xD0D0 ) { SPI0_SR |= SPI_SR_RFDF; return; }
        }
      }
      SPI0_SR |= SPI_SR_RFDF; return;
    }
    /* F&F BLOCK END */


    /* BEGIN PROCESSING */


    while ( 1 ) { // wait here until MASTER confirms ACK receipt
      if ( !(GPIOD_PDIR & 0x01) ) {
        if ( SPI0_SR & 0xF0 ) {
          if ( data[len - 1] != process_crc ) {
            SPI0_PUSHR_SLAVE = 0xBAAD;
            if ( SPI0_POPR == 0xBEEF ) SPI0_SR;
          }
          else {
            SPI0_PUSHR_SLAVE = 0xF00D;
            if ( SPI0_POPR == 0xBEEF ) break;
          }
        }
      }
      else {
        SPI0_SR |= SPI_SR_RFDF; return;
      }
    }

    if ( data[len - 1] != process_crc ) {
      if ( _slave_pointer->SPI_MSTransfer::debugSerial != nullptr ) {
        Serial.print("DEBUG: [CRC FAIL ISR] [DATA] ");
        for ( uint16_t i = 0; i < len; i++ ) {
          Serial.print(data[i], HEX); Serial.print(" ");
        } Serial.println(); Serial.flush();
      }
      SPI0_SR |= SPI_SR_RFDF; return; // CRC FAILED, DON'T PROCESS!
    }

    if ( _slave_pointer->SPI_MSTransfer::debugSerial != nullptr ) {
      Serial.print("DEBUG: [DATA] ");
      for ( uint16_t i = 0; i < len; i++ ) {
        Serial.print(data[i], HEX); Serial.print(" ");
      } Serial.println(); Serial.flush();
    }

    switch ( data[0] ) {
      case 0x9766: {
          switch ( data[2] ) {
            case 0x0000: { // SLAVE DIGITALWRITE(FAST) CONTROL
                ::digitalWriteFast(data[3] >> 8, data[3]);
                uint16_t checksum = 0, buf_pos = 0, buf[3] = { 0xAA55, 3, 0xAA56 };
                while ( !(GPIOD_PDIR & 0x01) ) {
                  if ( SPI0_SR & 0xF0 ) {
                    SPI0_PUSHR_SLAVE = buf[ ( buf_pos > buf[1] ) ? buf_pos = 0 : buf_pos++];
                    if ( SPI0_POPR == 0xD0D0 ) { SPI0_SR |= SPI_SR_RFDF; return; }
                  }
                }
                SPI0_SR |= SPI_SR_RFDF; return;
              }
            case 0x0001: { // SLAVE DIGITALREAD(FAST) CONTROL
                uint16_t checksum = 0, buf_pos = 0, buf[4] = { 0xAA55, 4, 0, 0 };
                if ( ::digitalReadFast(data[3]) ) {
                  buf[2] = 1; buf[3] = 0xAA50;
                }
                else {
                  buf[2] = 0; buf[3] = 0xAA51;
                }
                while ( !(GPIOD_PDIR & 0x01) ) {
                  if ( SPI0_SR & 0xF0 ) {
                    SPI0_PUSHR_SLAVE = buf[ ( buf_pos > buf[1] ) ? buf_pos = 0 : buf_pos++];
                    if ( SPI0_POPR == 0xD0D0 ) { SPI0_SR |= SPI_SR_RFDF; return; }
                  }
                }
                SPI0_SR |= SPI_SR_RFDF; return;
              }
            case 0x0002: { // SLAVE PINMODE CONTROL
                ::pinMode(data[3] >> 8, data[3]);
                uint16_t checksum = 0, buf_pos = 0, buf[] = { 0xAA55, 3, 0xAA56 };
                while ( !(GPIOD_PDIR & 0x01) ) {
                  if ( SPI0_SR & 0xF0 ) {
                    SPI0_PUSHR_SLAVE = buf[ ( buf_pos > buf[1] ) ? buf_pos = 0 : buf_pos++];
                    if ( SPI0_POPR == 0xD0D0 ) { SPI0_SR |= SPI_SR_RFDF; return; }
                  }
                }
                SPI0_SR |= SPI_SR_RFDF; return;
              }
            case 0x0003: { // SLAVE PIN TOGGLE
                uint16_t tPin = data[3];
                if ( LED_BUILTIN == tPin ) GPIOC_PTOR = 32;
                else digitalWrite(tPin, !digitalRead(tPin) );
                uint16_t checksum = 0, buf_pos = 0, buf[] = { 0xAA55, 3, 0xAA56 };
                while ( !(GPIOD_PDIR & 0x01) ) {
                  if ( SPI0_SR & 0xF0 ) {
                    SPI0_PUSHR_SLAVE = buf[ ( buf_pos > buf[1] ) ? buf_pos = 0 : buf_pos++];
                    if ( SPI0_POPR == 0xD0D0 ) { SPI0_SR |= SPI_SR_RFDF; return; }
                  }
                }
                SPI0_SR |= SPI_SR_RFDF; return;
              }
          }
          SPI0_SR |= SPI_SR_RFDF; return;
        }
      case 0x9243: { // MASTER SENDS PACKET TO SLAVE QUEUE WITH CRC ACKNOWLEDGEMENT
          switch ( data[2] ) {
            case 0x0000: {
                std::vector<uint16_t> _vector(data, data + data[1]); SPI_MSTransfer::teensy_handler_queue.push_back(_vector);
                uint16_t checksum = 0xAA51, buf_pos = 0, buf[] = { 0xAA55, 4, data[4], checksum ^= data[4] };
                while ( !(GPIOD_PDIR & 0x01) ) {
                  if ( SPI0_SR & 0xF0 ) {
                    SPI0_PUSHR_SLAVE = buf[ ( buf_pos > buf[1] ) ? buf_pos = 0 : buf_pos++];
                    if ( SPI0_POPR == 0xD0D0 ) { SPI0_SR |= SPI_SR_RFDF; return; }
                  }
                }
                SPI0_SR |= SPI_SR_RFDF; return;
              }
          }
          SPI0_SR |= SPI_SR_RFDF; return;
        }
      case 0x9712: {
          switch ( data[2] ) {
            case 0x0000: {
                if ( _slave_pointer->SPI_MSTransfer::teensy_stm_queue.size() > 0 ) { // IF QUEUES EXIST, ONE WILL BE DEQUEUED TO MASTER
                  uint16_t checksum = 0, _deque_pos = 0;
                  auto _deque = move(_slave_pointer->SPI_MSTransfer::teensy_stm_queue.front());
                  _slave_pointer->SPI_MSTransfer::teensy_stm_queue.pop_front();
                  while ( !(GPIOD_PDIR & 0x01) ) {
                    if ( SPI0_SR & 0xF0 ) {
                      SPI0_PUSHR_SLAVE = _deque[ ( _deque_pos > _deque[1] ) ? _deque_pos = 0 : _deque_pos++];
                      if ( SPI0_POPR == 0xD0D0 ) { SPI0_SR |= SPI_SR_RFDF; return; }
                    }
                  }
                  SPI0_SR |= SPI_SR_RFDF; return;
                }
                else { // OTHERWISE, SEND MINIMAL PACKET FOR NO QUEUES
                  uint16_t checksum = 0, buf_pos = 0, buf[] = { 0xAA55, 4, 0, 0xAA51 };
                  while ( !(GPIOD_PDIR & 0x01) ) {
                    if ( SPI0_SR & 0xF0 ) {
                      SPI0_PUSHR_SLAVE = buf[ ( buf_pos > buf[1] ) ? buf_pos = 0 : buf_pos++];
                      if ( SPI0_POPR == 0xD0D0 ) { SPI0_SR |= SPI_SR_RFDF; return; }
                    }
                  }
                  SPI0_SR |= SPI_SR_RFDF; return;
                }
              }
          }
          SPI0_SR |= SPI_SR_RFDF; return;
        }
      case 0x3235: { // SERIAL PORT BEGIN METHOD
          switch ( data[2] ) {
            case 0x0000: {
                switch ( data[3] ) {
                  case 0x0000: {
                      Serial.begin((uint32_t)data[4] << 16 | data[5]); break;
                    }
                  case 0x0001: {
                      Serial1.begin((uint32_t)data[4] << 16 | data[5]); break;
                    }
                  case 0x0002: {
                      Serial2.begin((uint32_t)data[4] << 16 | data[5]); break;
                    }
                  case 0x0003: {
                      Serial3.begin((uint32_t)data[4] << 16 | data[5]); break;
                    }
                  case 0x0004: {
                      Serial4.begin((uint32_t)data[4] << 16 | data[5]); break;
                    }
                  case 0x0005: {
                      Serial5.begin((uint32_t)data[4] << 16 | data[5]); break;
                    }
                  case 0x0006: {
                      Serial6.begin((uint32_t)data[4] << 16 | data[5]); break;
                    }
                }
                uint16_t checksum = 0, buf_pos = 0, buf[] = { 0xAA55, 3, 0xAA56 };
                while ( !(GPIOD_PDIR & 0x01) ) {
                  if ( SPI0_SR & 0xF0 ) {
                    SPI0_PUSHR_SLAVE = buf[ ( buf_pos > buf[1] ) ? buf_pos = 0 : buf_pos++];
                    if ( SPI0_POPR == 0xD0D0 ) { SPI0_SR |= SPI_SR_RFDF; return; }
                  }
                }
                SPI0_SR |= SPI_SR_RFDF; return;
              }
            case 0x0001: {
                int16_t _read = 0;
                switch ( data[3] ) {
                  case 0x0000: {
                      _read = Serial.read(); break;
                    }
                  case 0x0001: {
                      _read = Serial1.read(); break;
                    }
                  case 0x0002: {
                      _read = Serial2.read(); break;
                    }
                  case 0x0003: {
                      _read = Serial3.read(); break;
                    }
                  case 0x0004: {
                      _read = Serial4.read(); break;
                    }
                  case 0x0005: {
                      _read = Serial5.read(); break;
                    }
                  case 0x0006: {
                      _read = Serial6.read(); break;
                    }
                }
                uint16_t checksum = 0xAA51, buf_pos = 0, buf[] = { 0xAA55, 4, _read, checksum ^= _read };
                while ( !(GPIOD_PDIR & 0x01) ) {
                  if ( SPI0_SR & 0xF0 ) {
                    SPI0_PUSHR_SLAVE = buf[ ( buf_pos > buf[1] ) ? buf_pos = 0 : buf_pos++];
                    if ( SPI0_POPR == 0xD0D0 ) { SPI0_SR |= SPI_SR_RFDF; return; }
                  }
                }
                SPI0_SR |= SPI_SR_RFDF; return;
              }
            case 0x0002: {
                int16_t _available = 0;
                switch ( data[3] ) {
                  case 0x0000: {
                      _available = Serial.available(); break;
                    }
                  case 0x0001: {
                      _available = Serial1.available(); break;
                    }
                  case 0x0002: {
                      _available = Serial2.available(); break;
                    }
                  case 0x0003: {
                      _available = Serial3.available(); break;
                    }
                  case 0x0004: {
                      _available = Serial4.available(); break;
                    }
                  case 0x0005: {
                      _available = Serial5.available(); break;
                    }
                  case 0x0006: {
                      _available = Serial6.available(); break;
                    }
                }
                uint16_t checksum = 0xAA51, buf_pos = 0, buf[] = { 0xAA55, 4, _available, checksum ^= _available };
                while ( !(GPIOD_PDIR & 0x01) ) {
                  if ( SPI0_SR & 0xF0 ) {
                    SPI0_PUSHR_SLAVE = buf[ ( buf_pos > buf[1] ) ? buf_pos = 0 : buf_pos++];
                    if ( SPI0_POPR == 0xD0D0 ) { SPI0_SR |= SPI_SR_RFDF; return; }
                  }
                }
                SPI0_SR |= SPI_SR_RFDF; return;
              }
            case 0x0003: {
                int16_t _peek = 0;
                switch ( data[3] ) {
                  case 0x0000: {
                      _peek = Serial.peek(); break;
                    }
                  case 0x0001: {
                      _peek = Serial1.peek(); break;
                    }
                  case 0x0002: {
                      _peek = Serial2.peek(); break;
                    }
                  case 0x0003: {
                      _peek = Serial3.peek(); break;
                    }
                  case 0x0004: {
                      _peek = Serial4.peek(); break;
                    }
                  case 0x0005: {
                      _peek = Serial5.peek(); break;
                    }
                  case 0x0006: {
                      _peek = Serial6.peek(); break;
                    }
                }
                uint16_t checksum = 0xAA51, buf_pos = 0, buf[] = { 0xAA55, 4, _peek, checksum ^= _peek };
                while ( !(GPIOD_PDIR & 0x01) ) {
                  if ( SPI0_SR & 0xF0 ) {
                    SPI0_PUSHR_SLAVE = buf[ ( buf_pos > buf[1] ) ? buf_pos = 0 : buf_pos++];
                    if ( SPI0_POPR == 0xD0D0 ) { SPI0_SR |= SPI_SR_RFDF; return; }
                  }
                }
                SPI0_SR |= SPI_SR_RFDF; return;
              }
            case 0x0004: {
                uint16_t _written = 0; uint8_t _buf[data[4]];
                for ( uint16_t i = 0; i < data[4]; i++ ) _buf[i] = data[i + 5];
                switch ( data[3] ) {
                  case 0x0000: {
                      _written = Serial.write(_buf, data[4]); break;
                    }
                  case 0x0001: {
                      _written = Serial1.write(_buf, data[4]); break;
                    }
                  case 0x0002: {
                      _written = Serial2.write(_buf, data[4]); break;
                    }
                  case 0x0003: {
                      _written = Serial3.write(_buf, data[4]); break;
                    }
                  case 0x0004: {
                      _written = Serial4.write(_buf, data[4]); break;
                    }
                  case 0x0005: {
                      _written = Serial5.write(_buf, data[4]); break;
                    }
                  case 0x0006: {
                      _written = Serial6.write(_buf, data[4]); break;
                    }
                }
                uint16_t checksum = 0xAA51, buf_pos = 0, buf[] = { 0xAA55, 4, _written, checksum ^= _written };
                while ( !(GPIOD_PDIR & 0x01) ) {
                  if ( SPI0_SR & 0xF0 ) {
                    SPI0_PUSHR_SLAVE = buf[ ( buf_pos > buf[1] ) ? buf_pos = 0 : buf_pos++];
                    if ( SPI0_POPR == 0xD0D0 ) { SPI0_SR |= SPI_SR_RFDF; return; }
                  }
                }
                SPI0_SR |= SPI_SR_RFDF; return;
              }
            case 0x0005: {
                switch ( data[3] ) {
                  case 0x0000: {
                      Serial.flush(); break;
                    }
                  case 0x0001: {
                      Serial1.flush(); break;
                    }
                  case 0x0002: {
                      Serial2.flush(); break;
                    }
                  case 0x0003: {
                      Serial3.flush(); break;
                    }
                  case 0x0004: {
                      Serial4.flush(); break;
                    }
                  case 0x0005: {
                      Serial5.flush(); break;
                    }
                  case 0x0006: {
                      Serial6.flush(); break;
                    }
                }
                uint16_t buf_pos = 0, buf[] = { 0xAA55, 3, 0xAA56 };
                while ( !(GPIOD_PDIR & 0x01) ) {
                  if ( SPI0_SR & 0xF0 ) {
                    SPI0_PUSHR_SLAVE = buf[ ( buf_pos > buf[1] ) ? buf_pos = 0 : buf_pos++];
                    if ( SPI0_POPR == 0xD0D0 ) { SPI0_SR |= SPI_SR_RFDF; return; }
                  }
                }
                SPI0_SR |= SPI_SR_RFDF; return;
              }















          }
          SPI0_SR |= SPI_SR_RFDF; return;
        }








    }




  }
  SPI0_SR |= SPI_SR_RFDF; return;
}









































void SPI_MSTransfer::_detect() {
  if ( _slave_access ) return;
  if ( _serial_port_identifier != -1 ) {
    SPI_assert();
    uint16_t data[5], checksum = 0, data_pos = 0;
    data[data_pos] = 0x3235; checksum ^= data[data_pos]; data_pos++; // HEADER
    data[data_pos] = sizeof(data) / 2; checksum ^= data[data_pos]; data_pos++; // DATA SIZE
    data[data_pos] = 0x0003; checksum ^= data[data_pos]; data_pos++; // SUB SWITCH STATEMENT
    data[data_pos] = _serial_port_identifier; checksum ^= data[data_pos]; data_pos++;
    data[data_pos] = checksum;
    for ( uint16_t i = 0; i < data[1]; i++ ) { spi_port->transfer16(data[i]); }
    if ( !command_ack_response(data, sizeof(data) / 2) ) return; // RECEIPT ACK
    for ( uint8_t i = 0; i < 100; i++ ) {
      if ( spi_port->transfer16(0xFFFF) == 0xAA55 ) {
        uint16_t buf[spi_port->transfer16(0xFFFF)]; buf[0] = 0xAA55; buf[1] = sizeof(buf) / 2; checksum = buf[0]; checksum ^= buf[1];
        for ( uint16_t i = 2; i < buf[1]; i++ ) { delayMicroseconds(_transfer_slowdown); buf[i] = spi_port->transfer16(0xFFFF); if ( i < buf[1] - 1 ) checksum ^= buf[i]; }
        if ( checksum == buf[buf[1] - 1] ) {
          SPI_deassert(); return;
        }
      }
    }
    SPI_deassert(); return;
  }
}



uint16_t SPI_MSTransfer::events() {
  if ( _master_access ) {
    uint16_t data[4], checksum = 0, data_pos = 0;
    data[data_pos] = 0x9712; checksum ^= data[data_pos]; data_pos++; // HEADER
    data[data_pos] = sizeof(data) / 2; checksum ^= data[data_pos]; data_pos++; // DATA SIZE
    data[data_pos] = 0x0000; checksum ^= data[data_pos]; data_pos++; // SUB SWITCH STATEMENT
    data[data_pos] = checksum;
    SPI_assert();
    for ( uint16_t i = 0; i < data[1]; i++ ) { spi_port->transfer16(data[i]); }
    if ( !command_ack_response(data, sizeof(data) / 2) ) return 0; // RECEIPT ACK
    for ( uint8_t i = 0; i < 100; i++ ) {
      if ( spi_port->transfer16(0xFFFF) == 0xAA55 ) {
        uint16_t buf[spi_port->transfer16(0xFFFF)]; buf[0] = 0xAA55; buf[1] = sizeof(buf) / 2; checksum = buf[0]; checksum ^= buf[1];
        for ( uint16_t i = 2; i < buf[1]; i++ ) { delayMicroseconds(_transfer_slowdown); buf[i] = spi_port->transfer16(0xFFFF); if ( i < buf[1] - 1 ) checksum ^= buf[i]; }
        if ( checksum == buf[buf[1] - 1] ) {
          if ( buf[1] > 4 ) {
            uint16_t _slave_array[buf[3]];
            memmove (&_slave_array[0], &buf[5], buf[3] * 2 );
            AsyncMST info; info.packetID = buf[4];
            _master_handler(_slave_array, buf[3], info);
            spi_port->transfer16(0xD0D0); // send confirmation
            SPI_deassert(); return 0;
          }
          spi_port->transfer16(0xD0D0); // send confirmation
          SPI_deassert(); return 0;
        }
      }
    }
    SPI_deassert(); return 0;
  }
  if ( _slave_access ) {
    if ( watchdogEnabled && millis() - watchdogFeedInterval > ( watchdogTimeout / 4 ) ) {
      watchdogFeedInterval = millis();
      __disable_irq(); WDOG_REFRESH = 0xA602; WDOG_REFRESH = 0xB480; __enable_irq();
    }
    if ( _slave_handler == nullptr ) {
      if ( teensy_handler_queue.size() > 0 ) teensy_handler_queue.pop_front();
      return 0;
    }
    if ( teensy_handler_queue.size() > 0 ) {
      auto _deque = move(teensy_handler_queue.front());
      teensy_handler_queue.pop_front();
      uint16_t checksum = 0, _deque_pos = 0, _buf[_deque[3]];
      for ( uint16_t i = 0; i < _deque[1] - 1; i++ ) checksum ^= _deque[i];
      memmove (&_buf[0], &_deque[5], _deque[3] * 2 );
      if ( _slave_handler != nullptr ) {
        AsyncMST info;
        if ( checksum != _deque[_deque[1] - 1] ) info.error = 1;
        info.packetID = _deque[4];
        _slave_handler(_buf, _deque[3], info);
        return 0;
      }
    }
  }
  return 0;
}











namespace std {
void __attribute__((weak)) __throw_bad_alloc() {
  Serial.println("Unable to allocate memory");
}
void __attribute__((weak)) __throw_length_error( char const*e ) {
  Serial.print("Length Error :"); Serial.println(e);
}
}




























void SPI_MSTransfer::watchdog(uint32_t value) {
  if ( !_slave_access ) return;
  watchdogEnabled = 1;
  __disable_irq();
  WDOG_UNLOCK = WDOG_UNLOCK_SEQ1;
  WDOG_UNLOCK = WDOG_UNLOCK_SEQ2;
  __asm__ volatile ("nop");
#if (F_CPU / F_BUS) > 1
  __asm__ volatile ("nop");
#endif
#if (F_CPU / F_BUS) > 2
  __asm__ volatile ("nop");
#endif
#if (F_CPU / F_BUS) > 3
  __asm__ volatile ("nop");
#endif
#if (F_CPU / F_BUS) > 4
  __asm__ volatile ("nop");
#endif
#if (F_CPU / F_BUS) > 5
  __asm__ volatile ("nop");
#endif
#if (F_CPU / F_BUS) > 6
  __asm__ volatile ("nop");
#endif
#if (F_CPU / F_BUS) > 7
  __asm__ volatile ("nop");
#endif
  WDOG_STCTRLH = WDOG_STCTRLH_WDOGEN | WDOG_STCTRLH_ALLOWUPDATE;
  WDOG_TOVALH = value >> 16;
  WDOG_TOVALL = value;
  WDOG_PRESC = 0;
  __enable_irq();
  for (int i = 0; i < 256; i++) {
    __asm__ volatile ("nop");
#if (F_CPU / F_BUS) > 1
    __asm__ volatile ("nop");
#endif
#if (F_CPU / F_BUS) > 2
    __asm__ volatile ("nop");
#endif
#if (F_CPU / F_BUS) > 3
    __asm__ volatile ("nop");
#endif
#if (F_CPU / F_BUS) > 4
    __asm__ volatile ("nop");
#endif
#if (F_CPU / F_BUS) > 5
    __asm__ volatile ("nop");
#endif
#if (F_CPU / F_BUS) > 6
    __asm__ volatile ("nop");
#endif
#if (F_CPU / F_BUS) > 7
    __asm__ volatile ("nop");
#endif
  }
}

