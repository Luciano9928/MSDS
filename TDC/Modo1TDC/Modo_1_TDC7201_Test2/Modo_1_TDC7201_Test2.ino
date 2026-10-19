#include <SPI.h>

// --- Definición de Pines XIAO ESP32S3 ---
const int ENABLE_PIN = D0;  // Habilitación general
const int CSB1_PIN   = D2;  // Chip Select Núcleo 1
const int INTB1_PIN  = D5;  // Interrupción del Núcleo 1

// Frecuencia del reloj externo ASFLMB (8 MHz)
const float CLOCK_FREQ   = 8000000.0; 
const float CLOCK_PERIOD = 1.0 / CLOCK_FREQ;

// --- Direcciones de Registros del TDC7201 ---
const uint8_t TDC_CONFIG1      = 0x00;
const uint8_t TDC_CONFIG2      = 0x01;
const uint8_t TDC_INT_STATUS   = 0x02;
const uint8_t TDC_TIME1        = 0x10;
const uint8_t TDC_CALIBRATION1 = 0x1B;
const uint8_t TDC_CALIBRATION2 = 0x1C;

volatile bool medicionLista = false;

void IRAM_ATTR isrINTB1() {
  medicionLista = true;
}

void iniciarModo1() {
  digitalWrite(ENABLE_PIN, LOW);
  delay(10);
  digitalWrite(ENABLE_PIN, HIGH);
  delay(5);

  writeRegister(CSB1_PIN, TDC_CONFIG2, 0x40);    // 10 periodos de calibración
  writeRegister(CSB1_PIN, TDC_INT_STATUS, 0x1F); // Limpiar banderas
  writeRegister(CSB1_PIN, TDC_CONFIG1, 0x01);    // Armar Modo 1
}

void setup() {
  Serial.begin(115200);

  pinMode(CSB1_PIN, OUTPUT);
  digitalWrite(CSB1_PIN, HIGH);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(INTB1_PIN, INPUT_PULLUP);

  SPI.begin();
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));

  attachInterrupt(digitalPinToInterrupt(INTB1_PIN), isrINTB1, FALLING);

  iniciarModo1();
}

void loop() {
  // Prioridad 1: Atender inmediatamente la medición cuando INTB1 cae a LOW
  if (medicionLista || digitalRead(INTB1_PIN) == LOW) {
    medicionLista = false;

    writeRegister(CSB1_PIN, TDC_INT_STATUS, 0x1F);
    uint32_t time1 = readRegister24(CSB1_PIN, TDC_TIME1);
    uint32_t cal1  = readRegister24(CSB1_PIN, TDC_CALIBRATION1);
    uint32_t cal2  = readRegister24(CSB1_PIN, TDC_CALIBRATION2);

    // Rearmar de inmediato el TDC7201
    writeRegister(CSB1_PIN, TDC_CONFIG1, 0x01);

    float calCount = (float)(cal2 - cal1) / 9.0;
    if (calCount > 0) {
      float normLSB = CLOCK_PERIOD / calCount;
      float tof_ns  = (float)time1 * normLSB * 1e9;

      // Trama limpia para Python: TIME1_Raw,TOF_ns
      Serial.print(time1);
      Serial.print(",");
      Serial.println(tof_ns, 3);
    }
    return;
  }

  // Opcional: Si Python envía una 'R', reinicia y rearma el TDC7201
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'R' || cmd == 'r') {
      iniciarModo1();
    }
  }
}

void writeRegister(int csPin, uint8_t reg, uint8_t data) {
  digitalWrite(csPin, LOW);
  SPI.transfer(0x40 | (reg & 0x3F));
  SPI.transfer(data);
  digitalWrite(csPin, HIGH);
}

uint32_t readRegister24(int csPin, uint8_t reg) {
  uint32_t result = 0;
  digitalWrite(csPin, LOW);
  SPI.transfer(reg & 0x3F);
  result |= ((uint32_t)SPI.transfer(0x00) << 16);
  result |= ((uint32_t)SPI.transfer(0x00) << 8);
  result |= ((uint32_t)SPI.transfer(0x00));
  digitalWrite(csPin, HIGH);
  return result;
}