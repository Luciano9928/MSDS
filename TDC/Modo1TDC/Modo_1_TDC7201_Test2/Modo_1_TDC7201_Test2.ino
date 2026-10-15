#include <SPI.h>

// --- Definición de Pines XIAO ESP32S3 ---
const int ENABLE_PIN = D0;  // Habilitación general
const int CSB1_PIN   = D2;  // Chip Select Núcleo 1
const int INTB1_PIN  = D5;  // Interrupción del Núcleo 1

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
const unsigned long TIMEOUT_PULSO = 5000; // 5000 ms = 5 segundos

void configurarTDC() {
  bool comunicacionExitosa = false;
  
  while (!comunicacionExitosa) {
    Serial.println("Configurar el TDC7201 en MODO 1...");

    // Secuencia de reinicio por hardware
    digitalWrite(ENABLE_PIN, LOW);
    delay(10);
    digitalWrite(ENABLE_PIN, HIGH);
    delay(5); // Estabilización del LDO interno

    // Configuración para el MODO 1
    
    writeRegister(CSB1_PIN, TDC_CONFIG2, 0x40); // 10 periodos de calibración
      // Modo 1 (MEAS_MODE=00) e Iniciar medición (Bit 0 = 1) -> Valor: 0x01
    writeRegister(CSB1_PIN, TDC_CONFIG1, 0x01); 

    // Verificación de configuración por SPI
    uint8_t checkConfig1 = readRegister8(CSB1_PIN, TDC_CONFIG1);
    uint8_t checkConfig2 = readRegister8(CSB1_PIN, TDC_CONFIG2);

    // Comprobamos contra 0x01, que es el valor de inicio del Modo 1
    if (checkConfig1 == 0x01 && checkConfig2 == 0x40) {
      Serial.println("Hay comunicacion y TDC configurado en Modo 1.");
      comunicacionExitosa = true;
    } else {
      Serial.println("No se pudo verificar la configuración...");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); } 
  
  Serial.println("TDC7201 Modo 1");

  // Configuración de pines físicos
  pinMode(CSB1_PIN, OUTPUT);
  digitalWrite(CSB1_PIN, HIGH);
  
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(INTB1_PIN, INPUT_PULLUP);

  // Inicializar bus SPI (10 MHz por seguridad en prototipos)
  SPI.begin();
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));

  // Ejecutar configuración inicial con reintentos automáticos
  configurarTDC();

  Serial.println("Esperando pulsos...");
  ultimoPulsoTiempo = millis(); // Inicializar temporizador de timeout
}

void loop() {
  // 1. Verificar si han pasado 5 segundos sin recibir un pulso (Timeout)
  if (millis() - ultimoPulsoTiempo > TIMEOUT_PULSO) {
    Serial.println("No se ha recibido ningún pulso en 5 segundos.");
    Serial.println("Verificando comunicación SPI...");
    
    // Comprobar si los registros siguen respondiendo al Modo 1 (0x01)
    uint8_t checkConfig1 = readRegister8(CSB1_PIN, TDC_CONFIG1);
    if (checkConfig1 != 0x01) {
      Serial.println("Se perdió la comunicación. Reconfigurando...");
      configurarTDC();
    } else {
      Serial.println("OK");
    }
    
    // Reiniciar el temporizador para el siguiente ciclo de 5s
    ultimoPulsoTiempo = millis();
  }

  // 2. Esperar la interrupción (INTB1 baja cuando la medición termina)
  if (digitalRead(INTB1_PIN) == LOW) {
    
    // Reiniciar el contador de timeout ya que acabamos de recibir un pulso válido
    ultimoPulsoTiempo = millis();

    // Leer el registro de estado para limpiar la interrupción
    writeRegister(CSB1_PIN, TDC_INT_STATUS, 0x1F);

    // 3. Leer los registros de resultados de 24 bits
    uint32_t time1 = readRegister24(CSB1_PIN, TDC_TIME1);
    uint32_t cal1  = readRegister24(CSB1_PIN, TDC_CALIBRATION1);
    uint32_t cal2  = readRegister24(CSB1_PIN, TDC_CALIBRATION2);

    // 4. Cálculos matemáticos del Modo 1
    float calCount = (float)(cal2 - cal1) / (10.0 - 1.0);
    
    if (calCount > 0) {
      float normLSB = CLOCK_PERIOD / calCount;
      
      // Aplicar fórmula oficial del Modo 1: TOF = TIME1 * normLSB
      float tof_segundos = (float)time1 * normLSB; 
      float tof_nanosegundos = tof_segundos * 1e9; 

      // Mostrar resultados
      Serial.print("TIME1 (Raw): "); Serial.print(time1);
      Serial.print("\t| TOF: ");
      Serial.print(tof_nanosegundos, 3);
      Serial.println(" ns");
    } else {
      Serial.println("Error de calibración (calCount = 0).");
    }

    // 5. Lanzar una nueva medición en MODO 1
    writeRegister(CSB1_PIN, TDC_CONFIG1, 0x01);
  }
}

// === Funciones Auxiliares SPI ===

void writeRegister(int csPin, uint8_t reg, uint8_t data) {
  uint8_t cmd = 0x40 | (reg & 0x3F); // Bit 6 en 1 para Escritura
  digitalWrite(csPin, LOW);
  SPI.transfer(cmd);
  SPI.transfer(data);
  digitalWrite(csPin, HIGH);
}

uint8_t readRegister8(int csPin, uint8_t reg) {
  uint8_t cmd = reg & 0x3F; // Bit 6 en 0 para Lectura
  digitalWrite(csPin, LOW);
  SPI.transfer(cmd);
  uint8_t value = SPI.transfer(0x00);
  digitalWrite(csPin, HIGH);
  return value;
}

uint32_t readRegister24(int csPin, uint8_t reg) {
  uint8_t cmd = reg & 0x3F; // Bit 6 en 0 para Lectura
  uint32_t result = 0;
  digitalWrite(csPin, LOW);
  SPI.transfer(cmd);
  result |= ((uint32_t)SPI.transfer(0x00) << 16);
  result |= ((uint32_t)SPI.transfer(0x00) << 8); 
  result |= ((uint32_t)SPI.transfer(0x00));      
  digitalWrite(csPin, HIGH);
  return result;
}