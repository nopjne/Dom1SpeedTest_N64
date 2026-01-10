# N64 Domain 1 Speed Test ROM

This is a test ROM for Nintendo 64 that tests Domain 1 (cartridge ROM) read speeds with cartridge hotswap capability.

## Features

* **Cartridge Hotswap Support**: Enables cartridge swapping without resetting the console
* **Open Bus Detection**: Detects cartridge presence/absence using open bus value patterns
* **Comprehensive Speed Testing**: Tests all 65,536 possible LAT/PWD combinations (256×256) to find the fastest working speed

## How It Works

The test ROM operates in 4 states:

1. **Initialization**: Sets up display, console, and hotswap support
2. **Safe to Remove**: Displays "Safe to remove cartridge" and waits for cartridge removal
3. **Cartridge Detection**: Monitors for cartridge insertion using open bus detection
4. **Speed Testing**: When a cartridge is detected:
   - Reads the cartridge name from ROM header
   - Performs reference data read at slowest speed (LAT=0xFF, PWD=0xFF)
   - Tests all speed combinations from slowest to fastest
   - Displays the fastest working speed level and returns to detection mode

## Open Bus Detection

The N64 cartridge bus is 16-bit wide while addresses are 32-bit. When no cartridge is present (open bus), reading from any address returns the lower 16 bits of that address. The test uses this pattern to detect cartridge presence/absence.

## Speed Testing

The test reads 128 addresses (every 64KB) across an 8MB span starting at 0x10000000. It:
1. Reads reference data at the slowest speed (LAT=0xFF, PWD=0xFF)
2. Tests all LAT/PWD combinations from (0xFF, 0xFF) down to (0x00, 0x00)
3. Compares each read against the reference data
4. Finds the fastest combination that still returns correct data
5. Displays the result with the appropriate speed level name

## Build the ROM

1. [Install LibDragon](https://github.com/DragonMinded/libdragon) and make sure you export `N64_INST` as the path to your N64 compiler toolchain.
2. Run `make` to produce `dom1speedtest.z64`

## Usage

1. Load the ROM on your Nintendo 64 console
2. Wait for "Safe to remove cartridge" message
3. Remove the cartridge (if one is inserted)
4. Insert a cartridge to test
5. The test will automatically detect the cartridge, read its name, and run the speed test
6. Results will show the fastest working speed level name and LAT/PWD values
7. After testing, you can remove the cartridge and insert another one to test
