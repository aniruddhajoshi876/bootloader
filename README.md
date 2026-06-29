# STM32G491 UART Bootloader

A minimal serial bootloader for the **STM32G491RETx** (512 KB flash, 112 KB RAM).
It lives in the first 32 KB of flash, receives a new application image over UART,
programs it, and jumps to it. It can be triggered either by a serial command at
boot or by a "reboot-to-bootloader" flag set by the running application.

---

## Quick Start

### 1. Flash the bootloader (once)

1. Clone this repo and open the `bootloader` folder as a project in **STM32CubeIDE 1.19+**
2. Build (Ctrl+B) and flash via ST-LINK: **Run → Debug** (or use STM32CubeProgrammer targeting `0x08000000`)
3. The bootloader is now resident. You don't need to flash it again — future app updates go over UART.

### 2. Build your application at `0x08008000`

Your app project needs two changes before it will work with this bootloader (details in [What your application must include](#what-your-application-must-include)):

- Set `FLASH ORIGIN = 0x08008000` in the app linker script
- Set `SCB->VTOR = 0x08008000` early in `main()`

Build the app → STM32CubeIDE produces `Debug/your_app.bin`.

### 3. Send the `.bin` over UART

Install dependencies once:

```bash
pip install -r requirements.txt
```

Create a `.env` file (see `.env.example`) with the path to your `.bin`, then run:

```bash
python send_u_debug.py
# or pass the path directly:
python send_u_debug.py "C:\path\to\your_app\Debug\your_app.bin"
```

Reset the board when prompted. The script sends `'U'`, waits for ACKs, streams the binary, and confirms the jump to app.

---

## Project layout

```text
bootloader/
├── Core/Src/main.c          # All bootloader logic (erase, receive, write, jump)
├── Core/Src/usart.c         # USART1 peripheral init (HAL-generated)
├── STM32G491RETX_FLASH.ld   # Linker script — bootloader lives at 0x08000000, 32 KB
├── bootloader.ioc           # STM32CubeMX config (clock, pinout)
├── send_u_debug.py          # PC-side sender script (Python/pyserial)
├── requirements.txt         # Python dependencies for send_u_debug.py
└── .env.example             # Template for local config (BIN path, COM port)
```

---

## Memory map

| Region             | Address       | Notes                                                |
|--------------------|---------------|------------------------------------------------------|
| Bootloader         | `0x08000000`  | This project. Reset vector lands here.               |
| **Application**    | `0x08008000`  | Your app must be linked to start here (`APP_START`). |
| Flash end          | `0x08080000`  | 512 KB total.                                        |
| **Boot flag**      | `0x2001BFFC`  | Last 4 bytes of RAM, no-init. Magic `0xDEADBEEF`.    |
| RAM staging buffer | (bootloader)  | Max firmware image size **64 KB** (`FW_RAM_MAX`).    |

- Flash page size: **2 KB**. The bootloader erases from `APP_START` to the end of flash before writing.
- The application area is `0x08008000`–`0x08080000` = **480 KB**, but a single update is currently capped at **64 KB** because the whole image is staged in RAM first.

## Serial port

| Setting      | Value                           |
|--------------|---------------------------------|
| Peripheral   | USART1                          |
| TX pin       | PC4                             |
| RX pin       | PC5                             |
| Baud         | 115200                          |
| Framing      | 8 data, no parity, 1 stop (8N1) |
| Flow control | none                            |

---

## How the bootloader decides what to do

On reset the bootloader runs this logic:

1. **Reboot-to-bootloader flag.** If the magic value `0xDEADBEEF` is present at
   `0x2001BFFC`, it clears the flag and immediately enters **update mode** (no
   serial trigger needed). This is how your application requests a firmware update.
2. **Serial trigger.** Otherwise it waits up to ~9 s for a single byte on USART1.
   If it receives `'U'` (0x55), it enters **update mode**.
3. **Boot the app.** If no update is requested and a valid image is present at
   `0x08008000` (its initial stack pointer points into RAM,
   `0x20000000 < SP <= 0x2001C000`), it sets `VTOR`, loads the MSP, and jumps to
   the application's reset handler.
4. If no valid app and no update, it stays in the bootloader waiting for a byte.

## Update protocol

Once in update mode, send the following over USART1 (115200 8N1):

1. **Trigger** — one byte `'U'` (skip this if you entered via the reboot flag).
2. **Size** — 4 bytes, the image length in bytes, **little-endian** (`uint32_t`). Must be `> 0` and `<= 65536`.
3. **Image** — exactly *size* raw bytes of the application binary (`.bin`).

The bootloader erases the application region, writes the image (padded to an
8-byte boundary with `0xFF`), then jumps straight into the new app.

Progress/status strings are printed back on TX (`U received`, `size received`,
`erased flash`, `written to flash, entering main`).

### Example: send an image from a PC

```bash
# configure port
stty -F /dev/ttyUSB0 115200 raw -echo

# trigger + size + image
printf 'U' > /dev/ttyUSB0
SIZE=$(stat -c%s app.bin)
printf "$(printf '%08x' "$SIZE" | sed 's/\(..\)\(..\)\(..\)\(..\)/\\x\4\\x\3\\x\2\\x\1/')" > /dev/ttyUSB0
cat app.bin > /dev/ttyUSB0
```

Or use `send_u_debug.py` on Windows/Mac/Linux — see [Quick Start](#quick-start).

---

## send_u_debug.py

The script handles the full handshake with ACK checking at each step:

| Step | PC sends          | Board replies                      |
|------|-------------------|------------------------------------|
| 1    | `'U'`             | `U received\n`                     |
| 2    | 4-byte size (LE)  | `size received\n`                  |
| 3    | *(waits)*         | `erased flash\n`                   |
| 4    | raw `.bin` bytes  | `written to flash, entering main\n`|

**Configuration** — in order of precedence:

1. CLI argument: `python send_u_debug.py path/to/app.bin`
2. `.env` file in the same directory (`BIN=...`, optional `PORT=...`) — see `.env.example`

**Hardcoded defaults** you may want to change in the script:

| Variable | Default  | Meaning                              |
|----------|----------|--------------------------------------|
| `PORT`   | `COM9`   | Serial port (`/dev/ttyUSB0` on Linux)|
| `BAUD`   | `115200` | Must match bootloader                |

---

## What your application must include

Because the app does not start at the normal `0x08000000`, **two changes are
required** in your main application project, plus one optional helper to request
an update at runtime.

### 1. Link the app to start at `0x08008000`

In your application's linker script (`STM32G491RETX_FLASH.ld`), change the FLASH
origin so it begins where the bootloader expects:

```ld
FLASH (rx) : ORIGIN = 0x08008000, LENGTH = 480K
```

### 2. Relocate the vector table

The bootloader sets `VTOR`, but you should also set it in the app so the app is
correct after its own resets. Add this early in `main()` (or `SystemInit()`),
before enabling interrupts:

```c
#define APP_START 0x08008000U
SCB->VTOR = APP_START;   /* point the vector table at the relocated app */
```

### 3. The reset flag — request a firmware update from your app

To reboot into the bootloader's update mode from the running application, write
the magic value to the boot-flag address and do a system reset. **The flag must
live in a no-init RAM location at `0x2001BFFC` so it survives the reset.**

```c
/* Same address and magic the bootloader checks. */
#define BOOT_FLAG (*((volatile uint32_t *)0x2001BFFC))
#define BOOT_FLAG_MAGIC 0xDEADBEEFU

void reboot_to_bootloader(void)
{
    BOOT_FLAG = BOOT_FLAG_MAGIC;
    __DSB();
    NVIC_SystemReset();
}
```

**Reserve those 4 bytes** so your application's `.bss`/`.data` never overwrite
them. Shrink RAM by 4 bytes in the app linker script exactly as the bootloader
does:

```ld
RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 112K - 4
NOINIT (xrw) : ORIGIN = 0x2001BFFC, LENGTH = 4
```

After `reboot_to_bootloader()` runs, the device resets into the bootloader,
which finds `0xDEADBEEF`, clears it, and waits for the 4-byte size + image as
described in the protocol above (no `'U'` byte needed in this path).
*the boot_flag reserving 4 bytes in memoery is optional. The app should just do a warm reset after receiving 'U' through UART
---

## Building and flashing the bootloader

Open the `bootloader` project in **STM32CubeIDE 1.19+**, build, and flash it to
`0x08000000` via ST-LINK (Run/Debug, or STM32CubeProgrammer). The bootloader
must be flashed once; afterwards application updates go over UART.

## Limitations / Safety

- **No authentication or integrity check** — any binary sent over UART is accepted and written.
- **No CRC or checksum** — a corrupted transfer will write bad data silently.
- **No rollback** — if the transfer fails mid-way or power is lost during erase, the app slot is left blank and the bootloader will wait indefinitely.
- **64 KB image cap** — images larger than `FW_RAM_MAX` (64 KB) are rejected.

## Constants quick reference

| Macro        | Value         | Meaning                           |
|--------------|---------------|-----------------------------------|
| `APP_START`  | `0x08008000`  | Application base address          |
| `BOOT_FLAG`  | `*0x2001BFFC` | Reboot-to-bootloader flag address |
| magic        | `0xDEADBEEF`  | Value that triggers update mode   |
| trigger byte | `'U'` (0x55)  | Serial command to enter update    |
| `FW_RAM_MAX` | `0x10000`     | Max image size per update (64 KB) |
| baud         | `115200`      | USART1, 8N1, PC4/PC5              |

---

## License

MIT — see [LICENSE](LICENSE). STM32 HAL and CMSIS driver files retain their original ST Microelectronics licenses.
