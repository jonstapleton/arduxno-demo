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
    pinMode(13, OUTPUT);
    hardware.state = 0x00;
}

int check_hardware(Hardware *h) { // TODO: add debounce

    int bitfield = 0x00;
    for(int i=0; i<8; /* always 8 -- it has to iterate over a byte! */ i++) {
        pins[i].update();
        bitfield <<= 1;
        // TODO: actually load in multiple buttons
        bitfield += 1 - pins[i].read(); // Flip HIGH to LOW & vice-versa
    }

    h->state = bitfield;
    if(hardware.state != bitfield) {
        hardware.state = bitfield;
        return 1;
    } else {
        return 0;
    }
}