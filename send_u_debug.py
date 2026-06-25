import serial, struct, sys, time, os

BAUD = 115200

def load_env():
    env = {}
    try:
        with open(os.path.join(os.path.dirname(__file__), '.env')) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith('#') and '=' in line:
                    k, v = line.split('=', 1)
                    env[k.strip()] = v.strip().strip('"').strip("'")
    except FileNotFoundError:
        pass
    return env

env = load_env()

# BIN: CLI arg takes priority, then .env
if len(sys.argv) >= 2:
    bin_path = sys.argv[1]
else:
    bin_path = env.get('BIN', '')
    if not bin_path:
        print("Error: no BIN path given. Pass as CLI arg or set BIN= in .env")
        sys.exit(1)

# PORT: .env takes priority over default
PORT = env.get('PORT', 'COM9')

firmware = open(bin_path, 'rb').read()
print(f"[PC] Firmware: {len(firmware)} bytes")

ser = serial.Serial(PORT, BAUD, timeout=1)

# ── background reader: print everything the bootloader sends back ──────────────

print("[PC] Reset the board now — you have a moment. Sending 'U' in 2s...")
time.sleep(2)

while (1):
    print("\n[PC] >>> sending 'U'")
    ser.write(b'U')
    #wait for message "U received"
    response = ser.read_until(b'\n')
    if (response != b"U received\n"):
        print("\n [PC] U not received")
    else:
        print("\n[PC] >>> connection established")
        break
    
while (1):
    print(f"\n[PC] >>> sending size = {len(firmware)}")
    time.sleep(0.05)     
    ser.write(struct.pack('<I', len(firmware)))

    #wait for "size received"
    response = ser.read_until(b'\n')
    if (response != b"size received\n"):
        print("size not received")
    else:
        print("size received\n")
        break

#read "receiving and writing X bytes" (informational, no retry possible)
response = ser.read_until(b'\n')
print(f"\n[PC] >>> {response.decode().strip()}")

print(f"\n[PC] waiting for erase...")
while (1):
    #wait for "erased flash"
    response = ser.read_until(b'\n')
    if (response == b"erased flash\n"):
        print("\n[PC] >>> flash erased")
        break
    else:
        print("\n [PC] flash not erased")
        
time.sleep(1)
print("\n[PC] >>> streaming firmware")
ser.write(firmware) #sending bytes from .bin

while (1):
    #wait for "written to flash, entering main"
    response = ser.read_until(b'\n')
    if (response == b"written to flash, entering main\n"):
        print("\n[PC] >>> written to flash, entering main")
        break
    else:
        print("\n [PC] not written to flash, not entering main")

print("\n[PC] done — watching for app output for 3s...")
time.sleep(3)


time.sleep(0.3)
ser.close()
print("\n[PC] closed.")
