#include <SPI.h>
 
const int ENABLE_PIN = D0;  
const int CSB1_PIN   = D2;  
const int INTB1_PIN  = D5;  

// Frecuencia del reloj externo ASFLMB (8 MHz) 
const float CLOCK_FREQ   = 8000000.0;  
const float CLOCK_PERIOD = 1.0 / CLOCK_FREQ;

// --- Direcciones de Registros del TDC7201 --- 
const uint8_t TDC_CONFIG1      = 0x00; 
const uint8_t TDC_CONFIG2      = 0x01; 
const uint8_t TDC_INT_STATUS   = 0x02; 
const uint8_t TDC_TIME1        = 0x10; 
const uint8_t TDC_TIME2        = 0x12; 
const uint8_t TDC_TIME3        = 0x14; 
const uint8_t TDC_TIME4        = 0x16; 
const uint8_t TDC_TIME5        = 0x18; 
const uint8_t TDC_CALIBRATION1 = 0x1B; 
const uint8_t TDC_CALIBRATION2 = 0x1C;

volatile bool medicionLista = false;

void IRAM_ATTR isrINTB1() {  
  medicionLista = true; 
}

void iniciarModo1_MultiStop() {  
  digitalWrite(ENABLE_PIN, LOW);  
  delay(10);  
  digitalWrite(ENABLE_PIN, HIGH);  
  delay(5);

  // CONFIG2: 10 periodos de calibración (0x40) + NUM_STOP=5 (0x04) = 0x44
  writeRegister(CSB1_PIN, TDC_CONFIG2, 0x44);    
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

  iniciarModo1_MultiStop(); 
}

void loop() {  
 
  if (medicionLista || digitalRead(INTB1_PIN) == LOW) {  
    medicionLista = false;

    writeRegister(CSB1_PIN, TDC_INT_STATUS, 0x1F);  

    // Leer los 5 registros de tiempo
    uint32_t t1 = readRegister24(CSB1_PIN, TDC_TIME1);  
    uint32_t t2 = readRegister24(CSB1_PIN, TDC_TIME2);
    uint32_t t3 = readRegister24(CSB1_PIN, TDC_TIME3);
    uint32_t t4 = readRegister24(CSB1_PIN, TDC_TIME4);
    uint32_t t5 = readRegister24(CSB1_PIN, TDC_TIME5);

    uint32_t cal1 = readRegister24(CSB1_PIN, TDC_CALIBRATION1);  
    uint32_t cal2 = readRegister24(CSB1_PIN, TDC_CALIBRATION2);

    // Rearmar de inmediato el TDC7201  
    writeRegister(CSB1_PIN, TDC_CONFIG1, 0x01);

    float calCount = (float)(cal2 - cal1) / 9.0;  
    if (calCount > 0) {  
      float normLSB = CLOCK_PERIOD / calCount;  

      // Calcular los 5 Tiempos de Vuelo en nanosegundos
      float tof1 = (float)t1 * normLSB * 1e9;
      float tof2 = (float)t2 * normLSB * 1e9;
      float tof3 = (float)t3 * normLSB * 1e9;
      float tof4 = (float)t4 * normLSB * 1e9;
      float tof5 = (float)t5 * normLSB * 1e9;

    
      Serial.print(tof1, 3); Serial.print(",");
      Serial.print(tof2, 3); Serial.print(",");
      Serial.print(tof3, 3); Serial.print(",");
      Serial.print(tof4, 3); Serial.print(",");
      Serial.println(tof5, 3);
    }  
    return;  
  }

  // Opcional: Si Python envía una 'R', reinicia y rearma el TDC7201  
  if (Serial.available() > 0) {  
    char cmd = Serial.read();  
    if (cmd == 'R' || cmd == 'r') {  
      iniciarModo1_MultiStop();  
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