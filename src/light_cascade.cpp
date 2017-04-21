/*
light-cascade light controller
Copyright (C) 2017  Eliot Blennerhassett eliot@blennerhassett.gen.nz

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <Arduino.h>
#include <EEPROM.h>
#include <IRReadOnlyRemote.h>
#include "magic_remote.h"
#include "apple_remote.h"

#define DEBUG 1
#define Debug if (DEBUG) Serial

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

/********** Keymapping *********/

static unsigned long last_press = 0;
static unsigned long keycode_down = 0;

/** Has the 'keyboard' been idle for at least given time
*/
static bool key_idle(unsigned long time)
{
	return (millis() - last_press) >= time;
}

static unsigned long map_key(unsigned long keycode)
{
	last_press = millis();

	if (keycode == MAGIC_release)
		return keycode_down;
	keycode_down = keycode;
	return 0;
}

/********** Settings object in EEPROM *********/

struct Settings {
    int magic;  ///< magic number indicating valid eeprom layout
    int on_time;  ///< milliseconds
    int gap_time;  ///< milliseconds -ve = overlap, +ve
};


struct Settings settings  = {1000, 0};

static const int EEPROM_MAGIC = 0xBEEF;
static bool eeprom_needs_update = true;

static void eeprom_print()
{
	Serial.print("EEPROM Interval=");
	Serial.print(settings.on_time);
	Serial.print(" Gap=");
	Serial.println(settings.gap_time);
}

static void eeprom_update()
{
	if (!eeprom_needs_update)
		return;

	if (!key_idle(5000))
		return;

	eeprom_needs_update = false;
	EEPROM.put(0, settings);

	if (DEBUG)
		eeprom_print();
}

static void eeprom_init(void)
{
	EEPROM.get(0, settings);
	if (settings.magic != EEPROM_MAGIC) {
		settings.magic = EEPROM_MAGIC;
		settings.on_time = 1000;
		settings.gap_time = 0;
		eeprom_needs_update = true;
		Debug.print("Initialize ");
	}
	eeprom_print();
}

/********** Lights object *********/
struct Lights {
    const uint8_t *pins;
    int num_pins;
    int current_idx;
};

// pins 6-13 in some order
static const uint8_t pins[] = {9, 7, 13, 8, 11, 6, 12, 10};
struct Lights lights = {pins, ARRAY_SIZE(pins), 0};

static void light_on(int light)
{
  digitalWrite(light, HIGH);
  Debug.print(light);
  Debug.println(" ON");
}

static void light_off(int light)
{
  digitalWrite(light, LOW);
  Debug.print(light);
  Debug.println(" OFF");
}

static void light_init()
{
    int i;
    /* set all light pins as outputs */
    for (i = 0; i < lights.num_pins; i++) {
        light_off(lights.pins[i]);
        pinMode(lights.pins[i], OUTPUT);
    }
}

static uint8_t current_pin()
{
    return lights.pins[lights.current_idx];
}

static uint8_t next_pin()
{
    return lights.pins[(lights.current_idx + 1) % lights.num_pins];
}

static void increment_pin()
{
    lights.current_idx = (lights.current_idx + 1) % lights.num_pins;
}

/********** Timer object **********/
struct Timer {
    unsigned long start_millis;
    unsigned long delay;
};

void timer_init(Timer& t)
{
    t.delay = 0;
}

void timer_start(Timer& t, unsigned int delay_ms)
{
    t.start_millis = millis();
    t.delay = delay_ms;
    //Debug.print("Timer start ");
    //Debug.println(delay);
};

bool timer_expired(Timer &t)
{
    // Debug.print(millis());
    // Debug.print(" - ");
    // Debug.println(start_millis);
    return (t.delay == 0) || ((millis() - t.start_millis) >= t.delay);
};

Timer timer;


/********** State machine **********/

static enum state {on, off} current_state;

static void state_machine_init()
{
    current_state = on;
}

static void state_machine_run()
{
    if (not timer_expired(timer))
        return;

    if (settings.gap_time >= 0) {
        switch (current_state) {
        case off:
            light_off(current_pin());
            increment_pin();
            current_state = on;
            if (settings.gap_time)
                timer_start(timer, settings.gap_time);
                break;
            // else fall through
        case on:
            light_on(current_pin());
            timer_start(timer, settings.on_time);
            current_state = off;
            break;
        }
    } else {  // gap_time < 0 -> overlap
        switch (current_state) {
        case on:
            light_on(next_pin());
            timer_start(timer, -settings.gap_time);
            current_state = off;
            break;
        case off:
            light_off(current_pin());
            timer_start(timer, settings.on_time + settings.gap_time);
            increment_pin();
            current_state = on;
            break;
        }
    }
}

/********** Remote control **********/

#define IR_PIN 2  // must be interrupt capable pin (2 or 3)

#if NEC_ONLY
static IRReadOnlyRemote remote(IR_PIN);		// NEC
#else
static IRReadOnlyRemote remote(2, 564, 16, 8, 4, 1, 1, 3, 32);		// NEC
#endif

static void remote_control(void)
{
	unsigned long keycode, mapped_keycode;
	keycode = remote.read();
	if (keycode) {
		mapped_keycode = map_key(keycode);
	} else if (Serial.available() > 0) {
		mapped_keycode = Serial.read();
		last_press = millis();
	} else {
		mapped_keycode = 0;
	}

    switch(mapped_keycode) {
    	case APPLE_up:
    	case MAGIC_flash:
	case 'q':
		settings.on_time += 100;
		eeprom_needs_update = true;
		break;
	case APPLE_down:
	case MAGIC_strobe:
	case 'a':
		settings.on_time -= 100;
		eeprom_needs_update = true;
		break;
	case APPLE_right:
	case MAGIC_fade:
	case 'w':
		settings.gap_time += 5;
		eeprom_needs_update = true;
		break;
	case APPLE_left:
	case MAGIC_smooth:
	case 's':
		settings.gap_time -= 5;
		eeprom_needs_update = true;
		break;
	case 0:
		return;
	default:
		Debug.print("Unhandled key ");
		Debug.println(mapped_keycode, HEX);
		return;
    }
	Serial.print("Interval=");
	Serial.print(settings.on_time);
	Serial.print(" Gap=");
	Serial.println(settings.gap_time);
}

/********** Interface to Arduino framework **********/

void setup()
{
    eeprom_init();
    light_init();
    state_machine_init();

    Serial.begin(115200);
    Serial.println("Light cascade, qa=on, ws=off");
}

void loop()
{
    eeprom_update();
    state_machine_run();
    remote_control();
}
