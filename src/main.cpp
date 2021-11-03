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

// Screen stuff
extern "C" {
    #include "devices/ppu.h"
}
#include <TFT_eSPI.h> // the ESP32 port uses this library, which is also compatible with STM32 processors. Not sure if it works with Teensy

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);
const int hor = 40, ver = 30;

static Ppu *ppu;
static Uint8 reqdraw = 0;

static Uint8 uxn_font[][8] = {	// there is already a variable named "font" in TFT_eSPI.h
	{0x00, 0x7c, 0x82, 0x82, 0x82, 0x82, 0x82, 0x7c},
	{0x00, 0x30, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
	{0x00, 0x7c, 0x82, 0x02, 0x7c, 0x80, 0x80, 0xfe},
	{0x00, 0x7c, 0x82, 0x02, 0x1c, 0x02, 0x82, 0x7c},
	{0x00, 0x0c, 0x14, 0x24, 0x44, 0x84, 0xfe, 0x04},
	{0x00, 0xfe, 0x80, 0x80, 0x7c, 0x02, 0x82, 0x7c},
	{0x00, 0x7c, 0x82, 0x80, 0xfc, 0x82, 0x82, 0x7c},
	{0x00, 0x7c, 0x82, 0x02, 0x1e, 0x02, 0x02, 0x02},
	{0x00, 0x7c, 0x82, 0x82, 0x7c, 0x82, 0x82, 0x7c},
	{0x00, 0x7c, 0x82, 0x82, 0x7e, 0x02, 0x82, 0x7c},
	{0x00, 0x7c, 0x82, 0x02, 0x7e, 0x82, 0x82, 0x7e},
	{0x00, 0xfc, 0x82, 0x82, 0xfc, 0x82, 0x82, 0xfc},
	{0x00, 0x7c, 0x82, 0x80, 0x80, 0x80, 0x82, 0x7c},
	{0x00, 0xfc, 0x82, 0x82, 0x82, 0x82, 0x82, 0xfc},
	{0x00, 0x7c, 0x82, 0x80, 0xf0, 0x80, 0x82, 0x7c},
	{0x00, 0x7c, 0x82, 0x80, 0xf0, 0x80, 0x80, 0x80}};

#define DBG 1
#define DEBUG(s) if(DBG){Serial.println(s);}

// TODO: Add a rom selection menu or something
char ROMNAME[] = "uxn-test.rom";

static Device *devsystem, *devconsole, *devscreen;

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

// More screen stuff adapted from https://github.com/max22-/uxn-esp32/blob/esp32/src/main.cpp
static void
inspect(Ppu *p, Uint8 *stack, Uint8 wptr, Uint8 rptr, Uint8 *memory)
{
	Uint8 i, x, y, b;
	for(i = 0; i < 0x20; ++i) { /* stack */
		x = ((i % 8) * 3 + 1) * 8, y = (i / 8 + 1) * 8, b = stack[i];
		ppu_1bpp(p, 1, x, y, uxn_font[(b >> 4) & 0xf], 1 + (wptr == i) * 0x7, 0, 0);
		ppu_1bpp(p, 1, x + 8, y, uxn_font[b & 0xf], 1 + (wptr == i) * 0x7, 0, 0);
	}
	/* return pointer */
	ppu_1bpp(p, 1, 0x8, y + 0x10, uxn_font[(rptr >> 4) & 0xf], 0x2, 0, 0);
	ppu_1bpp(p, 1, 0x10, y + 0x10, uxn_font[rptr & 0xf], 0x2, 0, 0);
	for(i = 0; i < 0x20; ++i) { /* memory */
		x = ((i % 8) * 3 + 1) * 8, y = 0x38 + (i / 8 + 1) * 8, b = memory[i];
		ppu_1bpp(p, 1, x, y, uxn_font[(b >> 4) & 0xf], 3, 0, 0);
		ppu_1bpp(p, 1, x + 8, y, uxn_font[b & 0xf], 3, 0, 0);
	}
	for(x = 0; x < 0x10; ++x) { /* guides */
		ppu_pixel(p, 1, x, p->height / 2, 2);
		ppu_pixel(p, 1, p->width - x, p->height / 2, 2);
		ppu_pixel(p, 1, p->width / 2, p->height - x, 2);
		ppu_pixel(p, 1, p->width / 2, x, 2);
		ppu_pixel(p, 1, p->width / 2 - 0x10 / 2 + x, p->height / 2, 2);
		ppu_pixel(p, 1, p->width / 2, p->height / 2 - 0x10 / 2 + x, 2);
	}
}

void
redraw(Uxn* u)
{
	if(devsystem->dat[0xe])
		inspect(ppu, u->wst.dat, u->wst.ptr, u->rst.ptr, u->ram.dat);
	spr.pushSprite(0, 0);
	reqdraw = 0;
}

static int
screen_talk(Device *d, Uint8 b0, Uint8 w)
{
	if(w && b0 == 0xe) {
		Uint16 x = peek16(d->dat, 0x8);
		Uint16 y = peek16(d->dat, 0xa);
		Uint8 layer = d->dat[0xe] >> 4 & 0x1;
		ppu_pixel(ppu, layer, x, y, d->dat[0xe] & 0x3);
		reqdraw = 1;
	} else if(w && b0 == 0xf) {
		Uint16 x = peek16(d->dat, 0x8);
		Uint16 y = peek16(d->dat, 0xa);
		Uint8 layer = d->dat[0xf] >> 0x6 & 0x1;
		Uint8 *addr = &d->mem[peek16(d->dat, 0xc)];
		if(d->dat[0xf] >> 0x7 & 0x1)
			ppu_2bpp(ppu, layer, x, y, addr, d->dat[0xf] & 0xf, d->dat[0xf] >> 0x4 & 0x1, d->dat[0xf] >> 0x5 & 0x1);
		else
			ppu_1bpp(ppu, layer, x, y, addr, d->dat[0xf] & 0xf, d->dat[0xf] >> 0x4 & 0x1, d->dat[0xf] >> 0x5 & 0x1);
		reqdraw = 1;
	}
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

static void run(Uxn *u) {
    Uint16 vec;

    redraw(u);
    //while((!u->dev[0].dat[0xf]) && (read(0, &devconsole->dat[0x2], 1) > 0)) {
    while ((!u->dev[0].dat[0xf]) && ((*(&devconsole->dat[0x2]) = Serial.read()) != -1)) { // Hope this works :P
        vec = peek16(devconsole->dat, 0);
        if (!vec) vec = u->ram.ptr; /* continue after last BRK */
        uxn_eval(u, vec);

        // update the screen
        if(reqdraw)
			redraw(u);
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

    // Set up tft screen
    tft.init();
	tft.setRotation(3);
	tft.fillScreen(TFT_BLACK);
	tft.setCursor(0, 0);
	tft.setTextColor(TFT_GREEN);
	spr.setColorDepth(4);
	if(spr.createSprite(8 * hor, 8 * ver) == nullptr) {
		error("tTFT_eSPI", "Cannot create sprite");
	}
  while (!Serial) {};

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
  /* screen   */ devscreen = uxn_port(&u, 0x2, screen_talk);
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

    /* Write screen size to dev/screen */
    poke16(devscreen->dat, 2, ppu->width);
	poke16(devscreen->dat, 4, ppu->height);
	tft.println("Starting Uxn in 2 seconds");
	delay(2000);

  run(&u);
}

void loop() {} // This is empty because run() is a loop, we never get here.
