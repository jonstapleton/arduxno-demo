/* arduxno-demo
   A proof-of-concept port of uxn to the STM32duino environment
   Minor modifications to uxncli.c, copyright/permission notice below
   Cass Smith, October 2021
   Written for the STM32F411-based WeAct BlackPill v3 board
*/

/*
  Copyright (c) 2021 Devine Lu Linvega

  Permission to use, copy, modify, and distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE.
*/

/* Weird stuff:
   - No distinct stdout and stderr. All output, normal and error, is directed over Serial
   - No time device. The F411 does have a built-in RTC, so this could be added
   - No screen, audio, controller, or mouse devices
*/

//#include "uxn.h"
#include "uxn.c"  // :/

#include <SPI.h>
#include <SD.h>
#include <Bounce2.h>

#include <Hardware.h>

#define DBG 1
#define DEBUG(s) if(DBG){Serial.println(s);}

// TODO: Add a rom selection menu or something
char ROMNAME[] = "test.rom";

static Device *devsystem, *devconsole, *devctrl;

static int error(char *msg, const char *err) {
  //fprintf(stderr, "Error %s: %s\n", msg, err);
  Serial.printf("Error %s: %s\n", msg, err);
  return 0;
}

static void inspect(Stack *s, char *name) {
  Uint8 x, y;
  //fprintf(stderr, "\n%s\n", name);
  Serial.printf("\n%s\n", name);
  for (y = 0; y < 0x04; ++y) {
    for (x = 0; x < 0x08; ++x) {
      Uint8 p = y * 0x08 + x;
      //fprintf(stderr, p == s->ptr ? "[%02x]" : " %02x ", s->dat[p]);
      Serial.printf(p == s->ptr ? "[%02x]" : " %02x ", s->dat[p]);
    }
    //fprintf(stderr, "\n");
    Serial.printf("\n");
  }
}

// Controller stuff


static int system_talk(Device *d, Uint8 b0, Uint8 w) {
  if (!w) { /* read */
    switch (b0) {
      case 0x2: d->dat[0x2] = d->u->wst.ptr; break;
      case 0x3: d->dat[0x3] = d->u->rst.ptr; break;
    }
  } else { /* write */
    switch (b0) {
      case 0x2: d->u->wst.ptr = d->dat[0x2]; break;
      case 0x3: d->u->rst.ptr = d->dat[0x3]; break;
      case 0xe:
        inspect(&d->u->wst, "Working-stack");
        inspect(&d->u->rst, "Return-stack");
        break;
      case 0xf: return 0;
    }
  }
  return 1;
}

static int console_talk(Device *d, Uint8 b0, Uint8 w) {
  if (b0 == 0x1)
    d->vector = peek16(d->dat, 0x0);
  if (w && b0 > 0x7)
    //write(b0 - 0x7, (char *)&d->dat[b0], 1);
    Serial.write((char *)&d->dat[b0], 1); // All writes are directed to Serial
  return 1;
}



// TODO: Modify this to access files on the SD card
static int file_talk(Device *d, Uint8 b0, Uint8 w) {
  Uint8 read = b0 == 0xd;
  if (w && (read || b0 == 0xf)) {
    char *name = (char *)&d->mem[peek16(d->dat, 0x8)];
    Uint16 result = 0, length = peek16(d->dat, 0xa);
    long offset = (peek16(d->dat, 0x4) << 16) + peek16(d->dat, 0x6);
    Uint16 addr = peek16(d->dat, b0 - 1);
    //FILE *f = fopen(name, read ? "rb" : (offset ? "ab" : "wb"));
    File f = SD.open(name, read ? FILE_READ : FILE_WRITE);
    if (f) {
      //if(fseek(f, offset, SEEK_SET) != -1)
      if (f.seek(offset)) {
        //result = read ? fread(&d->mem[addr], 1, length, f) : fwrite(&d->mem[addr], 1, length, f);
        result = read ? f.read(&d->mem[addr], length) : f.write(&d->mem[addr], length);
      }
      //fclose(f);
      f.close();
    }
    poke16(d->dat, 0x2, result);
  }
  return 1;
}

static int doctrl(Pin_Event *e, int z) {
  // push the hardware snapshot to the controller device
  devctrl->dat[2] = e->snapshot; // I think this sets the button memory port
  Serial.println("Got controller input");
  return 1;
}


// TODO: RTC support?
//static int datetime_talk(Device *d, Uint8 b0, Uint8 w) {
//  time_t seconds = time(NULL);
//  struct tm *t = localtime(&seconds);
//  t->tm_year += 1900;
//  poke16(d->dat, 0x0, t->tm_year);
//  d->dat[0x2] = t->tm_mon;
//  d->dat[0x3] = t->tm_mday;
//  d->dat[0x4] = t->tm_hour;
//  d->dat[0x5] = t->tm_min;
//  d->dat[0x6] = t->tm_sec;
//  d->dat[0x7] = t->tm_wday;
//  poke16(d->dat, 0x08, t->tm_yday);
//  d->dat[0xa] = t->tm_isdst;
//  (void)b0;
//  (void)w;
//  return 1;
//}

static int nil_talk(Device *d, Uint8 b0, Uint8 w) {
  (void)d;
  (void)b0;
  (void)w;
  return 1;
}

static const char *errors[] = {"underflow", "overflow", "division by zero"};

int uxn_halt(Uxn *u, Uint8 error, char *name, int id) {
  //fprintf(stderr, "Halted: %s %s#%04x, at 0x%04x\n", name, errors[error - 1], id, u->ram.ptr);
  Serial.printf("Halted: %s %s#%04x, at 0x%04x\n", name, errors[error - 1], id, u->ram.ptr);
  return 0;
}

static int console_input(Uxn *u, char c) {
  devconsole->dat[0x2] = c;
  return uxn_eval(u, devconsole->vector);
}

static int run(Uxn *u) {
  
    Uint16 vec;
  //while((!u->dev[0].dat[0xf]) && (read(0, &devconsole->dat[0x2], 1) > 0)) {
//   while ((!u->dev[0].dat[0xf]) && ((*(&devconsole->dat[0x2]) = Serial.read()) != -1)) { // Hope this works :P
    while(!devsystem->dat[0xf]) {
        vec = peek16(devconsole->dat, 0);
        if (!vec) vec = u->ram.ptr; /* continue after last BRK */
        uxn_eval(u, vec);

        Hardware h;
        while(check_hardware(&h)) {
          Serial.println(h.state);
        }
        // hardware event poll -- *any* hardware interaction
        // while(is_event(&event) != 0) { // while there is an event in the queue...
        //   switch(event.type) {
        //     case QUIT:
        //       return error("Run", "Quit");
        //     case PINUP:
        //     case PINDOWN:
        //       doctrl(&event, event.type == PINDOWN);
        //       uxn_eval(u, devctrl->vector);
        //       devctrl->dat[3] = 0; // no idea what this does
        //       break;
        //   }
        // }
        // b.update();
        // if(b.changed()) {
        //   Serial.println("Got button change");
        // }
    }
}

static int load(Uxn *u, char *filepath) {
  //FILE *f;
  File f;
  //if(!(f = fopen(filepath, "rb")))
  if (!(f = SD.open(filepath)))
    return 0;
  //fread(u->ram.dat + PAGE_PROGRAM, sizeof(u->ram.dat) - PAGE_PROGRAM, 1, f);
  f.read(u->ram.dat + PAGE_PROGRAM, sizeof(u->ram.dat) - PAGE_PROGRAM);
  //fprintf(stderr, "Loaded %s\n", filepath);
  Serial.printf("Loaded %s\n", filepath);
  f.close();  // Close rom file
  return 1;
}

Uxn u;

#if defined(BUILTIN_SDCARD)
  const int chipSelect = BUILTIN_SDCARD; // use the builtin Teensy 4.1 SD card
#else
  const int chipSelect = 10; // This is the usual pin for Arduino boards, use 53 for Arduino Mega
#endif

void setup() {
  Serial.begin(115200);
  while (!Serial) {};

  // Set up hardware
  // DEBUG("Pins INIT...");
  // if(!setup_hardware()){
  //   DEBUG("Pins INIT FAILED! Halting...");
  //   while(1);
  // }
  // DEBUG("Pins INIT SUCCEEDED");

  setup_hardware();

  // Set up SD card
  DEBUG("SD card INIT...");
  if (!SD.begin(chipSelect)) {
    DEBUG("SD card INIT FAILED! Halting");
    while (1);
  }
  DEBUG("SD card INIT SUCCEEDED");

  if (!uxn_boot(&u)) {
    error("Boot", "Failed");
    while (1);
  }

  /* system   */ devsystem = uxn_port(&u, 0x0, system_talk);
  /* console  */ devconsole = uxn_port(&u, 0x1, console_talk);
  /* empty    */ uxn_port(&u, 0x2, nil_talk);
  /* empty    */ uxn_port(&u, 0x3, nil_talk);
  /* empty    */ uxn_port(&u, 0x4, nil_talk);
  /* empty    */ uxn_port(&u, 0x5, nil_talk);
  /* empty    */ uxn_port(&u, 0x6, nil_talk);
  /* empty    */ uxn_port(&u, 0x7, nil_talk);
  /* empty    */ uxn_port(&u, 0x8, nil_talk);
  /* empty    */ uxn_port(&u, 0x9, nil_talk);
  /* file     */ uxn_port(&u, 0xa, file_talk);
  /* datetime */ //uxn_port(&u, 0xb, datetime_talk);
  /* empty    */ uxn_port(&u, 0xb, nil_talk); // No datetime yet
  /* empty    */ uxn_port(&u, 0xc, nil_talk);
  /* empty    */ uxn_port(&u, 0xd, nil_talk);
  /* empty    */ uxn_port(&u, 0xe, nil_talk);
  /* empty    */ uxn_port(&u, 0xf, nil_talk);

  if (!load(&u, ROMNAME)) {
    error("Load", "Failed");
    while (1);
  }
  if (!uxn_eval(&u, PAGE_PROGRAM)) {
    error("Init", "Failed");
    while (1);
  }

  run(&u);
  // quit() // TODO: write quit function
}

void loop() {} // This is empty because run() is a loop, we never get here.
