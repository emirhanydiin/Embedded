#include <HardwareSerial.h>

#define S3_RXD1 6
#define S3_TXD1 5
#define UART_BAUD 115200

HardwareSerial U1(1);
uint32_t counter = 0;

void setup() {

  Serial.begin(115200);           // USB CDC for logs
  delay(1500);                    // let USB enumerate
  Serial.println("S3: USB Serial ready");

  U1.begin(UART_BAUD, SERIAL_8N1, S3_RXD1, S3_TXD1);
  Serial.println("S3: UART1 started (GPIO5 TX, GPIO6 RX)");
}

void loop() {
  U1.print("CNT=");
  U1.println(counter++);          // sends "...\\r\\n"
  Serial.println("S3: sent one line on UART1");
  delay(1000);
}
