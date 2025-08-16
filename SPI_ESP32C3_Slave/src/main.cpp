#include <Arduino.h>
extern "C" {
  #include "driver/spi_slave.h"
  #include "driver/gpio.h"
}

// -------- Pinler (ESP32-C3 Slave) --------
static const int PIN_SCLK = 6;   // Slave->Master Clock
static const int PIN_MOSI = 7;   // Master->Slave
static const int PIN_MISO = 2;   // Slave->Master
static const int PIN_SS   = 10;  // CS/SS

static const size_t FRAME_LEN = 32; //Frame Length: 32 bytes (1 header + 1 sequence number of the message + 1 payload (meaningful message length) + 28 meaningful message + 1 CRC)

// Basic CRC8 (0x07)
uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    uint8_t in = data[i];
    for (int b = 0; b < 8; b++) {
      uint8_t mix = (crc ^ in) & 0x80;
      crc <<= 1;
      if (mix) crc ^= 0x07;
      in <<= 1;
    }
  }
  return crc;
}

// Build a response frame
// Frame format: [0xA5][seq][payload_len][payload...][CRC]
// - seq: sequence number of the request
// - payload_len: length of the payload (max 28 bytes)
// - payload: the actual message (up to 28 bytes)
// - CRC: 1 byte CRC of the first 31 bytes
// - The frame is 32 bytes long in total.
// - If the payload is longer than 28 bytes, it will be truncated to fit.
// - If the payload is shorter than 28 bytes, it will be padded with zeros.
// - The CRC is calculated over the first 31 bytes of the frame.
void build_response(uint8_t* frame, uint8_t seq, const char* msg) {
  memset(frame, 0, FRAME_LEN);
  frame[0] = 0xA5;
  frame[1] = seq;
  size_t payload_len = min((size_t)28, strlen(msg));
  frame[2] = (uint8_t)payload_len;
  memcpy(&frame[3], msg, payload_len);
  frame[31] = crc8(frame, 31);
}

// Parse a request frame
bool parse_request(const uint8_t* frame, uint8_t& seq, char* out_msg) {
  if (frame[0] != 0xA5) return false; // - Checks if the frame starts with 0xA5
  uint8_t plen = frame[2];
  if (plen > 28) return false;  // - Checks if the length is valid (<= 28 bytes for payload)
  uint8_t crc = crc8(frame, 31);
  if (crc != frame[31]) return false; // - Checks if the CRC is valid
  seq = frame[1]; // - If valid, extracts the sequence number and payload
  memcpy(out_msg, &frame[3], plen); // - If valid, out_msg will contain the payload and will be null-terminated
  out_msg[plen] = '\0';
  return true;  // - Returns true if the frame is valid, false otherwise //  If invalid, out_msg will be empty
}

void setup() {

  // Serial port for debugging
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nESP32-C3 SPI Slave basladi");

  // To avoid floating inputs, set pull-up resistors
  gpio_set_pull_mode((gpio_num_t)PIN_MOSI, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode((gpio_num_t)PIN_SCLK, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode((gpio_num_t)PIN_SS,   GPIO_PULLUP_ONLY);

  // SPI bus config (Slave)
  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = PIN_MOSI;
  buscfg.miso_io_num = PIN_MISO;
  buscfg.sclk_io_num = PIN_SCLK;
  buscfg.quadwp_io_num = -1; //GPIO pin for WP (Write Protect) signal, or -1 if not used.
  buscfg.quadhd_io_num = -1; //GPIO pin for HD (Hold) signal, or -1 if not used.
  buscfg.max_transfer_sz = FRAME_LEN; // Maximum transfer size, in bytes. Defaults to 4092

  // SPI slave interface config (Mode0)
  spi_slave_interface_config_t slvcfg = {};
  slvcfg.mode = 0;  // SPI mode, representing a pair of (CPOL, CPHA) configuration: CPOL defines the clock's first movement (0 is rising, 1 is falling), CPHA defines when data is sampled (0 is rising, 1 is falling) and shifted out.
  //0: [Data is sampled at the leading rising edge of the clock] (0, 0), 
  //1: [Data is sampled at the leading falling edge od the clock] (0, 1), 
  //2: [Data is sampled at the trailing falling edge of the clock] (1, 0), 
  //3: [Data is sampled at the trailing rising edge of the clock] (1, 1)
  slvcfg.spics_io_num = PIN_SS; // CS (Chip Select) GPIO pin for this device
  slvcfg.queue_size = 3; // Number of stored transactions (already prepared before transaction)
  slvcfg.flags = 0; // Feature bits (rarely needed). Leaving 0 means default behavior: MSB-first, normal 4-wire SPI, no special quirks.
  slvcfg.post_setup_cb = NULL;
  slvcfg.post_trans_cb = NULL;

  // DMA kanalını otomatik seç
  esp_err_t ret;
  ret = spi_slave_initialize(SPI2_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    Serial.printf("spi_slave_initialize FAIL: %d\n", ret);
    for(;;) delay(1000);
  }
}

void loop() {
  static uint8_t next_tx[FRAME_LEN] = {0}; // Answer for next transaction
  static bool have_response = false;

  uint8_t rxbuf[FRAME_LEN] = {0};
  uint8_t txbuf[FRAME_LEN] = {0};

  if (have_response) {
    memcpy(txbuf, next_tx, FRAME_LEN);
  } else {
    memset(txbuf, 0x00, FRAME_LEN);
  }

  spi_slave_transaction_t t = {};
  t.length   = FRAME_LEN * 8;   // bit type length
  t.tx_buffer = txbuf;
  t.rx_buffer = rxbuf;

  // error control and transmission: do the transaction and checks if the transaction is valid
  esp_err_t ret = spi_slave_transmit(SPI2_HOST, &t, portMAX_DELAY);
  if (ret != ESP_OK) {
    Serial.printf("spi_slave_transmit FAIL: %d\n", ret);
    delay(100);
    return;
  }


  uint8_t seq;
  char msg[29];
  if (rxbuf[0] == 0xA5) {
    if (parse_request(rxbuf, seq, msg)) { // If header is valid (0xA5) parse the message
      Serial.printf("Slave RX OK (seq=%u): '%s'\n", seq, msg);

      // Prepare the response for next transaction
      String reply = String("ack_from_C3_") + String(seq);
      build_response(next_tx, seq, reply.c_str());
      have_response = true;
    } else {
      Serial.println("Slave RX FAIL (CRC/len)");
      have_response = false; 
    }
  } else {
  }
}
