#include <Hardware.h>
#include <Arduino.h>
#include <Bounce2.h>

// define the pins for each bit of the controller byte
#define PIN_1 0
#define PIN_2 1
#define PIN_3 2
#define PIN_4 3
#define PIN_5 4
#define PIN_6 5
#define PIN_7 6
#define PIN_8 7

int previous = 0x00;
Hardware hardware;

Bounce pins[8];

int setup_hardware() {
    for(int i=0;i<8;i++) {
        pins[i] = Bounce();
        pins[i].attach(i, INPUT_PULLUP);
    }
    // set LED as output
    pinMode(13, OUTPUT);

    // set all pins to "off" initially in memory
    hardware.state = 0x00;
}

// Checks the state of the MCU pins and updates *h accordingly
// returns a 1 if the state of the hardware is different than the last check
// returns a 0 if the state of the hardware is unchanged since the last check
int check_hardware(Hardware *h) {
    int bitfield = 0x00;
    for(int i=0; i<8; /* always 8 -- it has to iterate over a byte! */ i++) {
        pins[i].update();
        bitfield <<= 1;
        bitfield += 1 - pins[i].read(); // Flip HIGH to LOW & vice-versa--pins are pulled down
    }

    // *h is from the emulator -- this will probably be replaced with a HardwareEvent struct to match the Desktop emulator
    h->state = bitfield; // update the Hardware struct with new pin readings
    if(hardware.state != bitfield) {
    hardware.state = bitfield; // `hardware` is persistent across cycles
        return 1; // state has changed
    } else {
        return 0; // state is unchanged
    }
}