#include <HardwareSerial.h>

#define C3_RXD0 20     // U0RXD
#define C3_TXD0 21     // U0TXD
#define UART_BAUD 115200

HardwareSerial U0(0);            // UART0 (pins 20/21)
String line;
char c;
void setup() {
  Serial.begin(115200);          // USB‑CDC for monitor
  delay(1500);
  Serial.println("C3: USB Serial ready");

  U0.begin(UART_BAUD, SERIAL_8N1, C3_RXD0, C3_TXD0);
  Serial.println("C3: UART0 started (GPIO20 RX, GPIO21 TX)");
}

void loop() {
  while (U0.available() > 0) {
    int c = U0.read();
    if (c == '\n') {
      line.trim();               // drop CR/spaces
      Serial.print("C3 got: ");  // show on USB monitor
      Serial.println(line);
      // Optional: reply back to S3
      U0.println("Reply Back:" + line);
      line = "";
    } else if (c != '\r') {
      line += c;
    }
  }
}
