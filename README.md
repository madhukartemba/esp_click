# esp_click
This is a deep sleep ESP32 C6 push button device which transmits clicks via ESPNOW protocol. It uses an ESP32C6 microcontroller.

## Firmware build: broken RISC-V toolchain (PlatformIO)

ESP32-C6 uses the RISC-V toolchain (`riscv32-esp-elf-g++`). If the build fails with:

`sh: riscv32-esp-elf-g++: command not found` (exit 127)

the compiler package is missing, incomplete, or PlatformIO’s environment did not pick it up. From the firmware directory (`esp_click_firmware`), remove the toolchain and project build cache, then rebuild so PlatformIO re-downloads the toolchain:

```bash
cd esp_click_firmware
rm -rf .pio
rm -rf ~/.platformio/packages/toolchain-riscv32-esp
rm -rf ~/.platformio/tools/toolchain-riscv32-esp
pio run
```

Optional: remove all installed compiler packages (they reinstall on demand):

```bash
rm -rf ~/.platformio/packages/toolchain-*
```

Optional: prune unused PlatformIO packages:

```bash
pio system prune
```
