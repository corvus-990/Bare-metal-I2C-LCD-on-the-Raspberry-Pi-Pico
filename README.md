# Bare-metal I2C LCD on the Raspberry Pi Pico

A C program for the Raspberry Pi Pico (RP2040) that writes a character to a 16x2 HD44780 LCD over I2C, without the Pico SDK. Every peripheral is driven by writing to its memory-mapped registers directly, working from the RP2040 and HD44780U datasheets.

Building it taught me a lot about the I2C protocol and about working from datasheets.

<img width="3024" height="2570" alt="IMG_3099" src="https://github.com/user-attachments/assets/43a5e6ee-0838-4d9b-b21a-e018b9f44036" />

## What it does

- Takes the GPIO, pad and I2C0 peripherals out of reset and sets up GPIO20 and GPIO21 for I2C
- Configures the RP2040's I2C controller as a master in standard mode
- Initialises the LCD in 4-bit mode, following the sequence in the HD44780U datasheet
- Prints the letter `A`
- Turns on the onboard LED and stops if an I2C transfer is aborted
- Uses its own delay function, built on the hardware timer without interrupts

The whole firmware image, second-stage bootloader included, is 1,380 bytes.

## Hardware

- Raspberry Pi Pico (the original board; on the Pico W the onboard LED is not on GPIO25)
- LCD1602 module with an I2C backpack (HD44780U controller, I2C address `0x27`)

| Pico | Function | Connects to |
| --- | --- | --- |
| GPIO20 (pin 26) | I2C0 SDA | SDA on the backpack |
| GPIO21 (pin 27) | I2C0 SCL | SCL on the backpack |
| GPIO25 | Onboard LED | Nothing; it is the error indicator |

The LCD module also needs power and ground. The RP2040's internal pull-ups are enabled on both I2C lines.

## Build and flash

The build needs the Arm GNU toolchain, a host C++ compiler, `make`, `git` and Python 3. On Debian or Ubuntu:

```sh
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi g++ make git python3
```

Then, from the project folder:

```sh
make
```

This produces `main.uf2`. The first build downloads three third-party repositories into `deps/`, each pinned to an exact commit (see [Credits](#credits-and-references)), so it needs an internet connection.

To flash it, hold the BOOTSEL button while plugging the Pico in, then copy `main.uf2` onto the `RPI-RP2` drive that appears. The Pico restarts and the LCD shows `A`.

`make clean` removes the build output. `make distclean` also removes the downloaded dependencies.

## How it works

### Boot

The RP2040's boot ROM loads the first 256 bytes of flash, checks their CRC32 and runs them. Those 256 bytes are the second-stage bootloader, which here comes from the guide credited below. It configures the flash chip for execute-in-place and jumps to address `0x10000100`. The linker script places `main` first in the `.text` section, so that is where `main` sits.

There is no vector table and no startup code. The program uses no interrupts and no global variables, so `main` can run as it is.

### Peripheral setup

1. **Reset.** Peripherals are held in reset after power-up. `main` clears the IO_BANK0, PADS_BANK0 and I2C0 bits in the RESETS block and waits until all three report ready.
2. **Pins.** GPIO20 and GPIO21 are switched to their I2C function and their pull-ups are enabled, because I2C lines are open-drain. GPIO25 is made a software-controlled output for the LED.
3. **I2C.** With the controller disabled, it is set to master mode and standard speed, the SCL timing is written, and the target address is set to `0x27`. Then it is enabled.

### Talking to the LCD

The backpack is an I2C I/O expander: each byte written to it sets its eight output pins. The upper four are wired to the LCD's data lines D7 to D4, and the lower four to backlight, EN, RW and RS.

The LCD reads the data lines on the falling edge of EN, so `i2cWrite()` sends every 4-bit nibble twice: once with EN high, then again with EN low. A full command or character is two nibbles, which makes four I2C writes.

The initialisation follows "Initializing by Instruction" for the 4-bit interface in the HD44780U datasheet: `0x3` three times with the required pauses, `0x2` to switch to 4-bit mode, then function set (`0x28`), display off (`0x08`), clear (`0x01`), entry mode (`0x06`) and display on (`0x0C`). After that, `A` is sent as `0x41` with RS high.

### Delays without interrupts

The LCD needs pauses between commands, and writing the delay function was the hardest part. There is no SDK, so no `sleep_us()`, and without a vector table I could not use a timer alarm interrupt. Instead, `wait()` polls the RP2040's 64-bit timer:

1. Read `TIMELR` (the low 32 bits), then `TIMEHR` (the high 32 bits). The order matters: reading `TIMELR` latches the high half, so both values belong to the same instant even if the low half rolls over between the two reads.
2. Join them into one 64-bit timestamp and add the requested delay to get a deadline.
3. Loop, reading the raw timer registers `TIMERAWH` and `TIMERAWL`, until the deadline has passed.

The boot ROM takes the timer out of reset, so `main` can read it without any setup.

### Error handling

After every I2C write, the program checks the `TX_ABRT` flag in `IC_RAW_INTR_STAT`, which is set when a transfer is aborted, for example because no device acknowledged. If it is set, the onboard LED is turned on and the program stops.

## Project layout

| File | Purpose | Origin |
| --- | --- | --- |
| `main.c` | The program | Written by me |
| `link.ld` | Linker script: memory layout, boot stage 2 and checksum placement | Adapted from the guide |
| `Makefile` | Build: boot stage 2, checksum, firmware, UF2 | Adapted from the guide |

The linker script and Makefile were adapted from step `02_xipAndFlash` of the guide with AI assistance.

## Limitations and next steps

- It prints one hard-coded character. There are no functions yet for strings or cursor position.
- The program does not configure the clocks or the timer tick, so the length of the delays depends on the state the boot ROM leaves the chip in.
- Delays are busy-waits.

What I plan to add next:

- A vector table and reset handler (steps 03 and 04 of the guide)
- A timer alarm interrupt to replace the busy-wait
- Clock and timer tick setup, so that delays are exact

## Credits and references

- [Bare Metal RP2040: A Step-By-Step Guide](https://github.com/vxj9800/bareMetalRP2040) by vxj9800. The register-access approach, the build flow and the second-stage bootloader come from this guide. Its `bootStage2QuadIO.c` and `compCrc32.cpp` are downloaded at build time and are not part of this repository.
- [CRC++](https://github.com/d-bahr/CRCpp) by Daniel Bahr (BSD 3-Clause), used to compute the bootloader's checksum.
- [Microsoft UF2](https://github.com/microsoft/uf2) (MIT). Its `uf2conv.py` converts the binary to the UF2 format.
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) (BSD 3-Clause). Its `boot2_w25q080.S` is the official second-stage bootloader for the Pico's flash chip and a useful reference. It is not used in this build.
- [RP2040 datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf), for every register address and bit used in `main.c`.
- [HD44780U datasheet](https://cdn.sparkfun.com/assets/9/5/f/7/b/HD44780.pdf), for the initialisation sequence and commands.
