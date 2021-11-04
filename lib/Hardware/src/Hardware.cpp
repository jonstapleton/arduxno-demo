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

Bounce b = Bounce();

// Bounce b = Bounce();

int is_event(Pin_Event *e) {
    // check to see if there are new events
    get_pins(&hardware);
    if(hardware.state != previous) {
        e->snapshot = hardware.state;
        e->type = PINDOWN;
        previous = hardware.state;
        return 1;
    }
    return 0; // no new events
}

// int setup_hardware() {
//     // store pin definitions in hardware struct
//     h.pins[0] = PIN_1;
//     h.pins[1] = PIN_2;
//     h.pins[2] = PIN_3;
//     h.pins[3] = PIN_4;
//     h.pins[4] = PIN_5;
//     h.pins[5] = PIN_6;
//     h.pins[6] = PIN_7;
//     h.pins[7] = PIN_8;

//     // iterate over the pins and set them up as inputs
//     for(int i=0;i<8;i++) {
//         pinMode(h.pins[i], INPUT_PULLUP);
//     }
//     pinMode(13, OUTPUT);

//     // set the initial bitfield based on pin state
//     get_pins(&h);
//     return 1;
// }

int setup_hardware() {
    b.attach(0, INPUT_PULLUP);
    b.interval(25);
    pinMode(13, OUTPUT);
    hardware.state = 0x00;
}

int get_pins(Hardware *h) {
    unsigned char bitfield = 0x00;
    for(int i=0; i<8; /* always 8 -- it has to iterate over a byte! */ i++) {
        bitfield <<= 1;
        bitfield += ~digitalRead(h->pins[i]);
    }
    h->state = bitfield; // push the bitfield to the hardware struct
    return 1; // success
}

int check_hardware(Hardware *h) { // TODO: add debounce

    b.update();

    int bitfield = 0x00;
    for(int i=0; i<8; /* always 8 -- it has to iterate over a byte! */ i++) {
        bitfield <<= 1;
        // TODO: actually load in multiple buttons
        bitfield += 1 - b.read(); // Flip HIGH to LOW & vice-versa
    }

    h->state = bitfield;
    if(hardware.state != bitfield) {
        hardware.state = bitfield;
        return 1;
    } else {
        return 0;
    }
}