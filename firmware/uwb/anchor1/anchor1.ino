#include <SPI.h>
#include <DW1000.h>
#include <DW1000Ranging.h>

/* -------- VACUS PIN MAPPING -------- */
#define PIN_RST 27
#define PIN_SS  5
#define PIN_IRQ 4

#define ADELAYS 17150

/* -------- UNIQUE ANCHOR ADDRESS -------- */
#define ANCHOR_ADDR "83:17:5B:D5:A9:9A:E2:9C"

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println("Anchor 1 Starting...");

  SPI.begin(18, 19, 23, 5);

  DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ);
  DW1000.setAntennaDelay(ADELAYS);

  DW1000Ranging.startAsAnchor(
    ANCHOR_ADDR,
    DW1000.MODE_LONGDATA_RANGE_LOWPOWER,
    false
  );

  Serial.println("Anchor 1 Ready");
}

void loop() {
  DW1000Ranging.loop();
}