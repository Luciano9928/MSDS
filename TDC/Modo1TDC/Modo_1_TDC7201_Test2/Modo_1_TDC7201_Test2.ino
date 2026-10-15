#include <SPI.h>

// --- Definición de Pines XIAO ESP32S3 ---
const int ENABLE_PIN = D0;  // Habilitación general
const int CSB1_PIN   = D2;  // Chip Select Núcleo 1
const int TRIGG1_PIN = D3;  // Confirmación de TDC listo (TRIGG1)
const int INTB1_PIN  = D5;  // Interrupción del Núcleo 1 (Prioridad máxima)

// Frecuencia del reloj externo ASFLMB (8MHz)
const float CLOCK_FREQ = 8000000.0; 
const float CLOCK_PERIOD = 1.0 / CLOCK_FREQ;

// --- Direcciones de Registros del TDC7201 ---
const uint8_t TDC_CONFIG1       = 0x00;
const uint8_t TDC_CONFIG2       = 0x01;
const uint8_t TDC_INT_STATUS    = 0x02;
const uint8_t TDC_TIME1         = 0x10;
const uint8_t TDC_CALIBRATION1  = 0x1B;
const uint8_t TDC_CALIBRATION2  = 0x1C;

// Timeout (5 segundos sin pulsos)
unsigned long ultimoPulsoTiempo = 0;
const unsigned long TIMEOUT_PULSO = 5000;

// Banderas volátiles para interrupciones en RAM
volatile bool medicionLista = false;
volatile bool tdcArmadoConfirmado = false;

// ISR Prioritaria: Se activa cuando INTB1 cae a LOW (Medición terminada)
void IRAM_ATTR isrINTB1() {
  medicionLista = true;
}

// ISR de Confirmación: Se activa cuando TRIGG1 sube a HIGH (TDC listo para medir)
void IRAM_ATTR isrTRIGG1() {
  tdcArmadoConfirmado = true;
}

// Función para armar el TDC y confirmar físicamente con TRIGG1
bool armarYConfirmarTDC() {
  tdcArmadoConfirmado = false;
  
  // Iniciar medición en Modo 1 (MEAS_MODE=00, START_MEAS=1, TRIGG_EDGE=0)
  writeRegister(CSB1_PIN, TDC_CONFIG1, 0x01);

  // Esperar hasta 100 microsegundos a que TRIGG1 suba a HIGH
  unsigned long tInicio = micros();
  while (micros() - tInicio < 100) {
    if (tdcArmadoConfirmado || digitalRead(TRIGG1_PIN) == HIGH || medicionLista || digitalRead(INTB1_PIN) == LOW) {
      return true; // TRIGG1 subió a HIGH (o ya entró un pulso ultrarrápido)
    }
  }
  return false; // TRIGG1 nunca subió (posible falta de reloj CLOCK de 8MHz)
}

void procesarMedicion() {
  medicionLista = false;
  ultimoPulsoTiempo = millis();

  // 1. Limpiar el registro de estado de interrupción
  writeRegister(CSB1_PIN, TDC_INT_STATUS, 0x1F);

  // 2. Leer los registros de resultados de 24 bits
  uint32_t time1 = readRegister24(CSB1_PIN, TDC_TIME1);
  uint32_t cal1  = readRegister24(CSB1_PIN, TDC_CALIBRATION1);
  uint32_t cal2  = readRegister24(CSB1_PIN, TDC_CALIBRATION2);

  // 3. Rearmar inmediatamente el TDC7201 antes de imprimir por Serial
  bool listo = armarYConfirmarTDC();

  // 4. Cálculos matemáticos del Modo 1
  float calCount = (float)(cal2 - cal1) / (10.0 - 1.0);
  
  if (calCount > 0) {
    float normLSB = CLOCK_PERIOD / calCount;
    float tof_segundos = (float)time1 * normLSB; 
    float tof_nanosegundos = tof_segundos * 1e9; 

    Serial.print("TIME1 (Raw): "); Serial.print(time1);
    Serial.print("\t| TOF: "); Serial.print(tof_nanosegundos, 3);
    Serial.print(" ns\t| Rearmado (TRIGG1): ");
    Serial.println(listo ? "LISTO (HIGH)" : "FALLO (LOW)");
  } else {
    Serial.println("Error de calibración (calCount = 0).");
  }
}

void configurarTDC() {
  bool comunicacionExitosa = false;
  
  while (!comunicacionExitosa) {
    Serial.println("Configurar el TDC7201 en MODO 1...");

    // Secuencia de reinicio por hardware
    digitalWrite(ENABLE_PIN, LOW);
    delay(10);
    digitalWrite(ENABLE_PIN, HIGH);
    delay(5); // Estabilización del LDO interno

    // Configurar 10 periodos de calibración y limpiar interrupciones
    writeRegister(CSB1_PIN, TDC_CONFIG2, 0x40);
    writeRegister(CSB1_PIN, TDC_INT_STATUS, 0x1F);

    // Armar el TDC y verificar mediante TRIGG1
    bool triggOk = armarYConfirmarTDC();

    // Verificación de registros por SPI
    uint8_t checkConfig1 = readRegister8(CSB1_PIN, TDC_CONFIG1);
    uint8_t checkConfig2 = readRegister8(CSB1_PIN, TDC_CONFIG2);

    if (checkConfig1 == 0x01 && checkConfig2 == 0x40 && triggOk) {
      Serial.println("ÉXITO: SPI OK y pin TRIGG1 en HIGH (TDC 100% listo para medir).");
      comunicacionExitosa = true;
    } else {
      Serial.print("Fallo en verificación -> CFG1: 0x"); Serial.print(checkConfig1, HEX);
      Serial.print(" | CFG2: 0x"); Serial.print(checkConfig2, HEX);
      Serial.print(" | Pin TRIGG1 (D3): ");
      Serial.println(digitalRead(TRIGG1_PIN) == HIGH ? "HIGH (Listo)" : "LOW (No listo - Verifica el reloj de 8MHz)");
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); } 
  
  Serial.println("TDC7201 Modo 1 - Confirmación con TRIGG1 y Prioridad INTB1");

  pinMode(CSB1_PIN, OUTPUT);
  digitalWrite(CSB1_PIN, HIGH);
  
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(INTB1_PIN, INPUT_PULLUP);
  pinMode(TRIGG1_PIN, INPUT); // Entrada para leer el estado de TRIGG1

  SPI.begin();
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));

  // Interrupciones por hardware
  attachInterrupt(digitalPinToInterrupt(INTB1_PIN), isrINTB1, FALLING);
  attachInterrupt(digitalPinToInterrupt(TRIGG1_PIN), isrTRIGG1, RISING);

  configurarTDC();

  Serial.println("Esperando pulsos...");
  ultimoPulsoTiempo = millis();
}

void loop() {
  // PRIORIDAD 1: Atender inmediatamente la medición cuando INTB1 cae a LOW
  if (medicionLista || digitalRead(INTB1_PIN) == LOW) {
    procesarMedicion();
    return;
  }

  // PRIORIDAD 2: Diagnóstico cada 5 segundos si no se completan mediciones
  if (millis() - ultimoPulsoTiempo > TIMEOUT_PULSO) {
    if (medicionLista || digitalRead(INTB1_PIN) == LOW) return;

    uint8_t checkConfig1 = readRegister8(CSB1_PIN, TDC_CONFIG1);
    bool estadoTrigg1    = digitalRead(TRIGG1_PIN);

    if (medicionLista || digitalRead(INTB1_PIN) == LOW) return;

    Serial.print("[Timeout 5s] SPI CFG1: 0x"); Serial.print(checkConfig1, HEX);
    Serial.print(" | TRIGG1 (D3): ");

    if (estadoTrigg1 == HIGH) {
      Serial.println("HIGH -> El TDC está LISTO y esperando el pulso START1.");
    } else if (checkConfig1 == 0x01 && estadoTrigg1 == LOW) {
      Serial.println("LOW -> ¡START1 fue detectado! (TRIGG1 bajó), pero no llegó STOP1.");
    } else {
      Serial.println("LOW -> TDC desarmado. Reconfigurando...");
      configurarTDC();
    }
    
    ultimoPulsoTiempo = millis();
  }
}

// === Funciones Auxiliares SPI ===

void writeRegister(int csPin, uint8_t reg, uint8_t data) {
  uint8_t cmd = 0x40 | (reg & 0x3F);
  digitalWrite(csPin, LOW);
  SPI.transfer(cmd);
  SPI.transfer(data);
  digitalWrite(csPin, HIGH);
}

uint8_t readRegister8(int csPin, uint8_t reg) {
  uint8_t cmd = reg & 0x3F;
  digitalWrite(csPin, LOW);
  SPI.transfer(cmd);
  uint8_t value = SPI.transfer(0x00);
  digitalWrite(csPin, HIGH);
  return value;
}

uint32_t readRegister24(int csPin, uint8_t reg) {
  uint8_t cmd = reg & 0x3F;
  uint32_t result = 0;
  digitalWrite(csPin, LOW);
  SPI.transfer(cmd);
  result |= ((uint32_t)SPI.transfer(0x00) << 16);
  result |= ((uint32_t)SPI.transfer(0x00) << 8); 
  result |= ((uint32_t)SPI.transfer(0x00));      
  digitalWrite(csPin, HIGH);
  return result;
}