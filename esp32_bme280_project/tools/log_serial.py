"""Save the ESP32 serial output to a CSV file.   pip install pyserial
Usage:  python log_serial.py COM5 fixed.csv        (Linux/macOS: /dev/ttyUSB0)
Start this BEFORE powering/resetting the board: opening a port can reset the ESP32."""
import sys
import serial

port, out = sys.argv[1], sys.argv[2]
with serial.Serial(port, 115200, timeout=1) as ser, open(out, "w", encoding="utf-8") as f:
    print(f"Logging {port} -> {out}   (Ctrl+C to stop)")
    try:
        while True:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if line and (line[0].isdigit() or line.startswith("time_s")):
                f.write(line + "\n"); f.flush(); print(line)
    except KeyboardInterrupt:
        print("Stopped.")
