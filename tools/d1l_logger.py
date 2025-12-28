#!/usr/bin/env python3
"""
macOS-compatible serial logger for D1L repeater
Uses Python's serial library for reliable serial communication
"""

import serial
import sys
import time
from datetime import datetime

SERIAL_PORT = "/dev/cu.usbserial-1110"
BAUD_RATE = 115200
LOG_DIR = "/Users/ojarskapteinis/Documents/Kods/MeshCore/monitoring_logs"

def main():
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    serial_log = f"{LOG_DIR}/serial_output_{timestamp}.log"
    event_log = f"{LOG_DIR}/events_{timestamp}.log"

    print(f"=== D1L Monitor Started: {datetime.now()} ===")
    print(f"Serial port: {SERIAL_PORT}")
    print(f"Main log: {serial_log}")
    print(f"Event log: {event_log}")
    print("")

    # Open log files
    with open(serial_log, 'a') as sf, open(event_log, 'a') as ef:
        sf.write(f"=== D1L Monitor Started: {datetime.now()} ===\n")
        sf.write(f"Serial port: {SERIAL_PORT}\n")
        sf.write(f"Main log: {serial_log}\n")
        sf.write(f"Event log: {event_log}\n\n")
        sf.write(f"[{datetime.now()}] Starting serial capture...\n")
        sf.flush()

        ef.write(f"[{datetime.now()}] Starting serial capture...\n")
        ef.flush()

        # Open serial port
        try:
            ser = serial.Serial(
                port=SERIAL_PORT,
                baudrate=BAUD_RATE,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=1,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False
            )

            # Clear any stale data
            ser.reset_input_buffer()

            print("Serial port opened successfully")
            print("Monitoring... (Ctrl+C to stop)")

            last_heartbeat = time.time()
            heartbeat_interval = 60  # seconds

            while True:
                try:
                    # Heartbeat - write every 60 seconds to detect silent failures
                    current_time = time.time()
                    if current_time - last_heartbeat >= heartbeat_interval:
                        heartbeat_msg = f"[HEARTBEAT] Logger alive at {datetime.now()}"
                        sf.write(heartbeat_msg + "\n")
                        sf.flush()
                        last_heartbeat = current_time

                    if ser.in_waiting > 0:
                        line = ser.readline().decode('utf-8', errors='replace').rstrip()

                        # Timestamp and log
                        now_time = datetime.now().strftime("%H:%M:%S")
                        timestamped = f"[{now_time}] {line}"
                        sf.write(timestamped + "\n")
                        sf.flush()

                        # Check for important events
                        line_lower = line.lower()
                        now_full = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

                        if any(x in line_lower for x in ['boot', 'reset']):
                            ef.write(f"[{now_full}] [BOOT/RESET] {line}\n")
                            ef.flush()
                        elif any(x in line for x in ['ADV', '📡', 'Avotiela']):
                            ef.write(f"[{now_full}] [ADVERT] {line}\n")
                            ef.flush()
                        elif any(x in line_lower for x in ['error', 'fail', 'timeout']):
                            ef.write(f"[{now_full}] [ERROR] {line}\n")
                            ef.flush()
                        elif any(x in line for x in ['Kapteinis', '1e9f1fc4']):
                            ef.write(f"[{now_full}] [COMPANION] {line}\n")
                            ef.flush()
                        elif 'free heap' in line_lower:
                            ef.write(f"[{now_full}] [MEMORY] {line}\n")
                            ef.flush()
                        elif 'repeater id' in line_lower:
                            ef.write(f"[{now_full}] [INFO] {line}\n")
                            ef.flush()
                    else:
                        time.sleep(0.1)

                except KeyboardInterrupt:
                    print("\nStopping...")
                    sf.write(f"[{datetime.now()}] Logger stopped by user\n")
                    sf.flush()
                    break
                except Exception as e:
                    error_msg = f"[{datetime.now()}] Error reading serial: {e}"
                    print(error_msg)
                    sf.write(error_msg + "\n")
                    sf.flush()
                    ef.write(f"[{datetime.now()}] [LOGGER ERROR] {e}\n")
                    ef.flush()
                    time.sleep(1)

            ser.close()

        except Exception as e:
            print(f"Failed to open serial port: {e}")
            sys.exit(1)

if __name__ == "__main__":
    main()
