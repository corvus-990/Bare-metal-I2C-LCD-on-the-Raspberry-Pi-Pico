// Bare-metal LCD demo for the Raspberry Pi Pico (RP2040).
//
// Writes the character 'A' to a 16x2 HD44780 LCD through its I2C backpack,
// using only memory-mapped registers (no Pico SDK).
//
// Wiring:
//   GPIO20 -> SDA (I2C0)
//   GPIO21 -> SCL (I2C0)
//   GPIO25 -> onboard LED, turned on if an I2C write fails
//   LCD backpack I2C address: 0x27
//
// References:
//   RP2040 datasheet   - register addresses and bit positions
//   HD44780U datasheet - 4-bit initialisation sequence and commands

#include <stdbool.h>
#include <stdint.h>

// Memory-mapped registers. All addresses come from the RP2040 datasheet.

// RESETS block: a peripheral is held in reset while its bit in RESET is 1.
#define RESET *(volatile uint32_t*) (0x4000C000)
// RESET_DONE: a bit reads 1 once that peripheral is out of reset and ready.
#define RESET_DONE *(volatile uint32_t*)(0x4000C000 + 0x08)
// IO_BANK0, GPIOx_CTRL: selects which function is connected to pin X.
#define GPIO_CTRL(X) *(volatile uint32_t*)(0x40014000 + 0x04 + 8*X)
// I2C0 register at offset X.
#define I2C_OFS(X) *(volatile uint32_t*)(0x40044000 + X)
// PADS_BANK0: electrical settings (pull-up, pull-down, ...) of pin X.
#define PADS_OFS(X) *(volatile uint32_t*)(0x4001C000 + 0x004 + 0x004*X)
// TIMER register at offset X. The timer is a 64-bit counter of microseconds.
#define Timer_OFS(X) *(volatile uint32_t*)(0x40054000 + X)
// SIO register at offset X: direct software control of the GPIO outputs.
#define SIO_OFS(X) *(volatile uint32_t*)(0xD0000000 + X)

// Busy-wait for `time` microseconds (same idea as the SDK's sleep_us()).
// It polls the timer instead of using an alarm interrupt.
void wait(uint64_t time){
	// Read the current time. TIMELR (0x0c) has to be read first: reading it latches
	// the upper 32 bits into TIMEHR (0x08), so both halves belong to the same instant
	// even if the low half rolls over in between.
	uint32_t timerLow = Timer_OFS(0x0c);
	uint32_t timerHigh = Timer_OFS(0x08);
	// Join the two halves into one 64-bit timestamp, then add the delay to get the deadline.
	uint64_t timer = ((uint64_t)timerHigh << 32) | (timerLow);
	timer += time;

	// Spin until the timer passes the deadline. TIMERAWH (0x24) and TIMERAWL (0x28)
	// read the same counter without the latching side effect.
	while(( (uint64_t)Timer_OFS(0x24) << 32 | Timer_OFS(0x28)) <= timer);
}

// Called when an I2C write fails: turn on the onboard LED and stop here.
// `s` points to the value that was being sent; it is not used.
void error(uint32_t *s){
	// SIO GPIO_OUT_SET (0x014): drive GPIO25 high.
	SIO_OFS(0x014) = (1<<25);
	while(1);
}

// Send one 4-bit nibble to the LCD.
//   info: the nibble (ends up on the LCD's D7..D4 lines)
//   RS:   0 = the nibble belongs to a command, 1 = it belongs to a character
//
// The backpack is an I2C I/O expander. Each byte written to it sets its 8 output
// pins, which are wired to the LCD:
//   bits 7..4 -> D7..D4 (data lines)
//   bit 3     -> backlight
//   bit 2     -> EN (enable)
//   bit 1     -> RW (left at 0 = write)
//   bit 0     -> RS (register select)
//
// The LCD reads the data lines on the falling edge of EN, so every nibble is sent
// twice: once with EN high, then again with EN low.
//
// In the value written to IC_DATA_CMD (0x10), bits 7..0 are the byte to send and
// bit 9 (STOP) ends the I2C transfer after that byte.
void i2cWrite(uint8_t info, bool RS){
	// Command nibble (RS = 0)
	if(!RS){
		// nibble | backlight on | EN high | STOP
		uint32_t set = ((uint32_t)info << 4) | (1<<3) | (1<<2) | (1<<9) ;
		I2C_OFS(0x10) = set;
		// Wait while the I2C master is busy: IC_STATUS (0x70) bit 5 = MST_ACTIVITY
		while(I2C_OFS(0x70) & (1<<5));

		// IC_RAW_INTR_STAT (0x34) bit 6 = TX_ABRT: the transfer was aborted,
		// for example because no device acknowledged
		if(I2C_OFS(0x34) & (1<<6)){
			error(&set);
		}

		// Same nibble with EN low: this falling edge makes the LCD take the nibble
		set = ((uint32_t)info << 4) | (1<<3) | (1<<9);
		I2C_OFS(0x10) = set;
		while(I2C_OFS(0x70) & (1<<5));

		if(I2C_OFS(0x34) & (1<<6)){
			error(&set);
		}

	}else{
		// Data nibble (RS = 1): the same two writes, with bit 0 set
		uint32_t set = ((uint32_t)info << 4) | (1<<3) | (1<<2) | (1<<9) | (1<<0) ;
		I2C_OFS(0x10) = set;
		while(I2C_OFS(0x70) & (1<<5));

		if(I2C_OFS(0x34) & (1<<6)){
			error(&set);
		}

		set = ((uint32_t)info << 4) | (1<<3) | (1<<9) | (1<<0);
		I2C_OFS(0x10) = set;
		while(I2C_OFS(0x70) & (1<<5));

		if(I2C_OFS(0x34) & (1<<6)){
			error(&set);
		}
	}
}

// main is placed in its own section (.text.main) so the linker script can position it.
__attribute__((section(".text.main"))) int main(){
	// 1. Take the peripherals we need out of reset by clearing their bits in RESET:
	//    bit 5 = IO_BANK0, bit 3 = I2C0, bit 8 = PADS_BANK0
	//PADS + BANK0 + I2C0
	RESET &= ~((1<<5) | (1<<3) | (1<<8));
	// Wait until all three report ready in RESET_DONE
	while(!(RESET_DONE & (1<<5)) || !(RESET_DONE & (1<<3)) || !(RESET_DONE & (1<<8)));

	// 2. Pin setup
	// GPIO25 (onboard LED): function 5 = SIO, i.e. controlled by software.
	// Then enable it as an output with SIO GPIO_OE_SET (0x024).
	//gpio25
	GPIO_CTRL(25) = 5;
	SIO_OFS(0x024) = (1<<25);
	// GPIO21 and GPIO20: function 3 = I2C
	//SCL
	GPIO_CTRL(21) = 3;
	//SDA
	GPIO_CTRL(20) = 3;

	// I2C lines are open-drain, so they need pull-ups to sit high when idle.
	// In each pad register: clear bit 2 (PDE, pull-down enable), set bit 3 (PUE, pull-up enable).
	//set both pins to pull-ups
	PADS_OFS(21) &= ~(1<<2);
	PADS_OFS(21) |= (1<<3);
	PADS_OFS(20) &= ~(1<<2);
	PADS_OFS(20) |= (1<<3);

	// 3. I2C0 setup. The controller has to be disabled while its timing is configured.
	//turn off IC_ENABLE[0]
	I2C_OFS(0x6c) &= ~(1<<0);

	// IC_CON (0x00): bit 0 = MASTER_MODE, bit 6 = IC_SLAVE_DISABLE
	//set to master
	I2C_OFS(0x00) |= (1<<0) | (1<<6);

	// IC_CON bits 2:1 = SPEED. 01 = standard mode (up to 100 kbit/s)
	//set speed to standard mode
	I2C_OFS(0x00) &= ~(1<<2);
	I2C_OFS(0x00) |= (1<<1);

	// SCL timing: IC_SS_SCL_HCNT (0x14) and IC_SS_SCL_LCNT (0x18) are the lengths of the
	// high and low parts of each SCL pulse, counted in system clock cycles.
	// IC_FS_SPKLEN (0xA0) is the length of the spike filter.
	//set clock
	I2C_OFS(0x14) = 486;
	I2C_OFS(0x18) = 587;
	I2C_OFS(0xA0) = 0x07;

	// IC_TAR (0x04): address of the device to talk to. 0x27 is the LCD backpack.
	//set slave address
	I2C_OFS(0x04) = 0x27;

	//turn on IC_ENABLE[0]
	I2C_OFS(0x6c) |= (1<<0);

	// 4. LCD initialisation, following "Initializing by Instruction" (4-bit interface)
	//    in the HD44780U datasheet. All waits are in microseconds.
	//
	// The LCD powers up in 8-bit mode. Sending 0x3 three times, with the delays the
	// datasheet asks for, resets the interface whatever state it was in.
	// Then 0x2 switches it to 4-bit mode.
	//setting LCD to 4 bits mode:
	wait(30000);
	i2cWrite(0x03, 0);
	wait(5000);
	i2cWrite(0x03, 0);
	wait(200);
	i2cWrite(0x03, 0);
	wait(200);
	i2cWrite(0x02, 0);
	wait(80);

	// From here on, every command is 8 bits sent as two nibbles: upper first, then lower.
	// 0x28 = function set: 4-bit interface, 2 lines, 5x8 dot font
	i2cWrite(0x2, 0);
	i2cWrite(0x8, 0);
	wait(80);

	// 0x08 = display off
	i2cWrite(0x0, 0);
	i2cWrite(0x8, 0);
	wait(80);

	// 0x01 = clear display (a slow command, hence the longer wait)
	i2cWrite(0x0, 0);
	i2cWrite(0x1, 0);
	wait(2000);

	// 0x06 = entry mode: cursor moves right after each character, display does not shift
	i2cWrite(0x0, 0);
	i2cWrite(0x6, 0);
	wait(80);

	// 0x0C = display on, cursor off, blink off
	i2cWrite(0x0, 0);
	i2cWrite(0xc, 0);
	wait(80);
	//LCD SETUP END.

	// 5. Write one character
	//Data transfer start, character 'A'
	// 'A' is 0x41 in ASCII. RS = 1 marks it as data, not a command.
	//upper nibble:
	i2cWrite(0x4, 1);
	//lower nibble:
	i2cWrite(0x1, 1);
	wait(80);

}
