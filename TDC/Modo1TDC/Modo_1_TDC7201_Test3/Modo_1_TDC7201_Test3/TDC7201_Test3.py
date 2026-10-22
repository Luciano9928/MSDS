import serial
import serial.tools.list_ports
import csv
import time
from datetime import datetime

# --- Configuración de la Prueba ---
PUERTO_MANUAL = None         
BAUDIOS = 115200
DURACION_MINUTOS = 30      
DURACION_SEGUNDOS = DURACION_MINUTOS * 60

def encontrar_puerto():
    if PUERTO_MANUAL:
        return PUERTO_MANUAL
    puertos = serial.tools.list_ports.comports()
    for p in puertos:
        if "USB" in p.description or "Serial" in p.description or p.vid == 0x303A:
            return p.device
    return puertos[0].device if puertos else None

def main():
    puerto = encontrar_puerto()
    if not puerto:
        print("[ERROR] No se detectó el XIAO ESP32S3.")
        return

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    nombre_csv = f"TDC7201_Modo1_MultiStop_{timestamp}.csv"

    print(f"Conectando a {puerto} a {BAUDIOS} baudios...")
    
    with serial.Serial(puerto, BAUDIOS, timeout=1) as ser, open(nombre_csv, mode="w", newline="", encoding="utf-8") as archivo:
        escritor = csv.writer(archivo)
        
       
        escritor.writerow(["Hora", "TOF_1_ns", "TOF_2_ns", "TOF_3_ns", "TOF_4_ns", "TOF_5_ns"])

        # Reiniciar el TDC7201
        ser.reset_input_buffer()
        ser.write(b'R')

        print(f"Capturando ráfagas de 5 pulsos durante {DURACION_MINUTOS} minutos.")
        print(f"Guardando en: {nombre_csv}\n" + "-"*65)

        inicio = time.time()
        contador = 0

        try:
            while (time.time() - inicio) < DURACION_SEGUNDOS:
                if ser.in_waiting > 0:
                    linea = ser.readline().decode("utf-8", errors="ignore").strip()
                    
                    if not linea or "," not in linea:
                        continue

                 
                    partes = linea.split(",")
                    if len(partes) == 5:
                        hora = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                        
                        texto_pantalla = (f"{hora} -> TOF1: {partes[0]}ns | "
                                          f"TOF2: {partes[1]}ns | TOF3: {partes[2]}ns | "
                                          f"TOF4: {partes[3]}ns | TOF5: {partes[4]}ns")
                        print(texto_pantalla)

            
                        escritor.writerow([hora, partes[0], partes[1], partes[2], partes[3], partes[4]])
                        archivo.flush()
                        contador += 1

        except KeyboardInterrupt:
            print("\n[!] Captura detenida manualmente.")

        print("-" * 65)
        print(f"Prueba finalizada. Total de ráfagas capturadas: {contador}")

if __name__ == "__main__":
    main()