#include <Arduino.h>
#include <SPI.h>

// ---------------- PINLER ESP32-S3-MASTER ------------------

static const int PIN_SS = 10;
static const int PIN_MOSI = 11;
static const int PIN_SCLK = 12;
static const int PIN_MISO = 13;

// SPI settings: 4MHz, MSB first, SPI mode 0

SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

// Frame Length: 32 bits

static const int FRAME_LEN = 32;

//Basic CRC8 (0X07) polynomial -- Single byte CRC calculation
// This function calculates the CRC8 checksum for a given data buffer.
// It iterates through each byte of the data, processes each bit, and updates the CRC
// value accordingly. The final CRC value is returned as an 8-bit unsigned integer.
// The CRC8 algorithm is commonly used for error detection in communication protocols.
// The polynomial used is 0x07, which is a common choice for CRC8 calculations
// and is suitable for many applications.
// The function takes a pointer to the data and its length as parameters.
// The CRC8 value is initialized to 0x00 and updated based on the input data
// using bitwise operations. The final CRC value is returned after processing all bits.
// Example usage: uint8_t crc = crc8(data, length);
// This function can be used to verify the integrity of data transmitted over SPI or other protocols.
// It can be useful in applications where data integrity is critical, such as in communication systems,
// storage devices, or any system where data corruption may occur during transmission.
// The CRC8 function can be used to validate data received from a slave device in an SPI
// communication setup, ensuring that the data has not been altered or corrupted during transmission.
// The function is designed to be efficient and straightforward, making it suitable for embedded systems

uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0x00;

  for(size_t i = 0; i < len; i++) {
    uint8_t in = data[i];
    for (int b = 0; b < 8; b++) {
      uint8_t mix = (crc ^ in) & 0x80;
      crc <<= 1;
      if (mix) crc ^= 0x07;
      in <<= 1 ;
    }
  }

return crc;
}


void build_request(uint8_t* frame, uint8_t seq, const char* msg){

  memset(frame, 0, FRAME_LEN);   //Sets unused bits to 0.

  frame[0] = 0xA5;   //Header
  frame[1] = seq;   //Message identification number

  size_t payload_len = min((size_t )28, strlen(msg));          
  frame[2] = (uint8_t)payload_len;  //Meaningful Message Length
  memcpy(&frame[3], msg, payload_len); //Copy the msg from frame[3] to frame[3 + payload]
  frame[31] = crc8(frame, 31); // Used for CRC8
}

bool parse_response(const uint8_t* frame, uint8_t expected_seq) {
  if (frame[0] != 0xA5) return false;                           //Header Kontrolu
  if (frame[1] != expected_seq) return false;                   //Compare the message number with the expected message number

  uint8_t plen = frame[2];                                     //Get the length of the meaningful message
  if (plen > 28) return false;

  uint8_t crc = crc8(frame,31);
  if (crc != frame[31]) return false;                           // CRC8 Check

  return true;
}

void spi_write_32(const uint8_t* tx) {
  uint8_t rx_dummy[FRAME_LEN];
  digitalWrite(PIN_SS, LOW);
  SPI.beginTransaction(spiSettings);
  SPI.transferBytes((uint8_t*)tx, rx_dummy, FRAME_LEN);
  SPI.endTransaction();
  digitalWrite(PIN_SS, HIGH);
}

void spi_read_32(uint8_t* rx){
  uint8_t tx_dummy[FRAME_LEN];
  memset(tx_dummy, 0x00, FRAME_LEN);
  digitalWrite(PIN_SS, LOW);
  SPI.beginTransaction(spiSettings);
  SPI.transferBytes(tx_dummy, rx, FRAME_LEN);
  SPI.endTransaction();
  digitalWrite(PIN_SS, HIGH);
}

void setup() {
  
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nESP32-S3 SPI MASTER HAS BEEN STARTED");

  pinMode(PIN_SS, OUTPUT);
  digitalWrite(PIN_SS, HIGH); //SPI works active high in SS (Chip Select Pin) to prevent fault messages keep SPI inactive

  //For initializing SPI with your described pins
  SPI.begin(PIN_SCLK, PIN_MISO, PIN_MOSI, PIN_SS);
 
}

void loop() {
  
  static uint8_t seq = 0;
  uint8_t tx[FRAME_LEN];  //Create 32 Byte transmission buffer
  uint8_t rx[FRAME_LEN];  //Create 32 byte receive buffer

  //Send a message
  String msg = String("Hello from S3") + String(seq);
  build_request(tx, seq, msg.c_str());
  spi_write_32(tx);
  Serial.printf("Master TX (seq=%u): %s\n", seq, msg.c_str());

  //Wait for preparing answer from slave
  delay(2);

  //Answer: read the message that comes for the previous message
  spi_read_32(rx);

  if (parse_response(rx, seq)) {
    uint8_t plen = rx[2];
    char payload[29] = {0};
    memcpy(payload, &rx[3], plen);
    Serial.printf("Master RX OK (seq=%u): '%s'\n", rx[1], payload);
  } else {
    Serial.prinitln("Master RX FAIL (CRC/HEADER/SEQ)");
  }

  seq++;

  delay(500);

}
