import serial
import serial.tools.list_ports
import csv
import time
from datetime import datetime

# --- Configuración de la Prueba ---
PUERTO_MANUAL = None          # Déjalo en None para autodetección, o pon ej. "COM5"
BAUDIOS = 115200
DURACION_MINUTOS = 30         # Duración automática de la captura
DURACION_SEGUNDOS = DURACION_MINUTOS * 60

def encontrar_puerto():
    if PUERTO_MANUAL:
        return PUERTO_MANUAL
    puertos = serial.tools.list_ports.comports()
    for p in puertos:
        # Identificar el XIAO ESP32S3 por descripción o VID de Espressif
        if "USB" in p.description or "Serial" in p.description or p.vid == 0x303A:
            return p.device
    return puertos[0].device if puertos else None

def main():
    puerto = encontrar_puerto()
    if not puerto:
        print("[ERROR] No se detectó ningún XIAO ESP32S3 conectado por USB.")
        return

    timestamp_archivo = datetime.now().strftime("%Y%m%d_%H%M%S")
    nombre_csv = f"TDC7201_Modo1_{timestamp_archivo}.csv"

    print(f"Conectando a {puerto} a {BAUDIOS} baudios...")
    
    with serial.Serial(puerto, BAUDIOS, timeout=1) as ser, open(nombre_csv, mode="w", newline="", encoding="utf-8") as archivo:
        escritor_csv = csv.writer(archivo)
        
        # Encabezados de columnas para Excel / Pandas
        escritor_csv.writerow(["Hora", "TIME1_Raw", "TOF_ns", "Formato_Reporte"])

        # Limpiar buffer y enviar 'R' para reiniciar el TDC7201 en la placa
        ser.reset_input_buffer()
        ser.write(b'R')

        print(f"Iniciando captura de {DURACION_MINUTOS} minutos.")
        print(f"Guardando en tiempo real en: {nombre_csv}")
        print("-" * 65)

        inicio_prueba = time.time()
        contador_pulsos = 0

        try:
            while (time.time() - inicio_prueba) < DURACION_SEGUNDOS:
                if ser.in_waiting > 0:
                    linea = ser.readline().decode("utf-8", errors="ignore").strip()
                    
                    # Ignorar mensajes de texto iniciales del setup de la placa (como "Configurando Modo1...")
                    if not linea or "," not in linea:
                        if linea:
                            print(f"[ESP32] {linea}")
                        continue

                    partes = linea.split(",")
                    if len(partes) == 2:
                        time1_raw, tof_ns = partes[0], partes[1]
                        hora_actual = datetime.now().strftime("%H:%M:%S.%f")[:-3]

                        # Formato exacto solicitado
                        texto_formateado = f"{hora_actual} -> TIME1 (Raw): {time1_raw}\t| TOF: {tof_ns} ns"
                        
                        # 1. Mostrar en la consola de Python
                        print(texto_formateado)

                        # 2. Guardar en el archivo CSV
                        escritor_csv.writerow([hora_actual, time1_raw, tof_ns, texto_formateado])
                        archivo.flush()
                        contador_pulsos += 1

        except KeyboardInterrupt:
            print("\n[!] Prueba detenida manualmente por el usuario.")

        print("-" * 65)
        print(f"¡Prueba finalizada! Total de pulsos capturados: {contador_pulsos}")
        print(f"Archivo guardado exitosamente en: {nombre_csv}")

if __name__ == "__main__":
    main()