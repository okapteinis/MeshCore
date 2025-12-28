#!/usr/bin/env python3
"""
Portable serial logger for D1L repeater
Uses Python's serial library for reliable serial communication
"""

import argparse
import serial
import sys
import time
from datetime import datetime
from pathlib import Path

DEFAULT_BAUD_RATE = 115200
DEFAULT_LOG_DIR = "./monitoring_logs"

def find_serial_port():
    """
    Try to auto-detect D1L serial port on macOS/Linux.
    Returns None if not found.
    """
    import glob

    # macOS patterns
    patterns = [
        '/dev/cu.usbserial-*',
        '/dev/cu.SLAB_USBtoUART',
        '/dev/cu.usbmodem*',
        # Linux patterns
        '/dev/ttyUSB*',
        '/dev/ttyACM*',
    ]

    for pattern in patterns:
        ports = glob.glob(pattern)
        if ports:
            return ports[0]  # Return first match

    return None

def main():
    parser = argparse.ArgumentParser(
        description='D1L Serial Logger - Portable serial monitor with event logging',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  # With explicit port
  python3 tools/d1l_logger.py --port /dev/cu.usbserial-1110

  # Auto-detect port (macOS/Linux)
  python3 tools/d1l_logger.py

  # Custom log directory
  python3 tools/d1l_logger.py --port /dev/ttyUSB0 --log-dir ./logs

  # Custom baud rate
  python3 tools/d1l_logger.py --port /dev/cu.usbserial-1110 --baud 9600
        '''
    )

    parser.add_argument(
        '--port', '-p',
        type=str,
        help='Serial port (e.g., /dev/cu.usbserial-1110, /dev/ttyUSB0). Auto-detected if not specified.'
    )

    parser.add_argument(
        '--baud', '-b',
        type=int,
        default=DEFAULT_BAUD_RATE,
        help=f'Baud rate (default: {DEFAULT_BAUD_RATE})'
    )

    parser.add_argument(
        '--log-dir', '-d',
        type=str,
        default=DEFAULT_LOG_DIR,
        help=f'Log directory (default: {DEFAULT_LOG_DIR})'
    )

    args = parser.parse_args()

    # Determine serial port
    serial_port = args.port
    if not serial_port:
        print("No port specified, attempting auto-detection...")
        serial_port = find_serial_port()
        if not serial_port:
            print("ERROR: Could not auto-detect serial port.")
            print("Please specify port with --port /dev/cu.usbserial-XXXX")
            print("\nOn macOS, use: ls /dev/cu.usb*")
            print("On Linux, use: ls /dev/ttyUSB* /dev/ttyACM*")
            sys.exit(1)
        print(f"Auto-detected port: {serial_port}")

    # Setup log directory
    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    serial_log = log_dir / f"serial_output_{timestamp}.log"
    event_log = log_dir / f"events_{timestamp}.log"

    print(f"=== D1L Monitor Started: {datetime.now()} ===")
    print(f"Serial port: {serial_port}")
    print(f"Baud rate: {args.baud}")
    print(f"Main log: {serial_log}")
    print(f"Event log: {event_log}")
    print("")

    # Open log files
    with open(serial_log, 'a') as sf, open(event_log, 'a') as ef:
        sf.write(f"=== D1L Monitor Started: {datetime.now()} ===\n")
        sf.write(f"Serial port: {serial_port}\n")
        sf.write(f"Baud rate: {args.baud}\n")
        sf.write(f"Main log: {serial_log}\n")
        sf.write(f"Event log: {event_log}\n\n")
        sf.write(f"[{datetime.now()}] Starting serial capture...\n")
        sf.flush()

        ef.write(f"[{datetime.now()}] Starting serial capture...\n")
        ef.flush()

        # Open serial port
        try:
            ser = serial.Serial(
                port=serial_port,
                baudrate=args.baud,
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
            print(f"\nTroubleshooting:")
            print(f"1. Check port exists: ls {serial_port}")
            print(f"2. Check permissions (Linux): sudo usermod -a -G dialout $USER")
            print(f"3. Check cable supports data (not charge-only)")
            print(f"4. Try different USB port")
            sys.exit(1)

if __name__ == "__main__":
    main()
