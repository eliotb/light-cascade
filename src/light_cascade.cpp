#include <Arduino.h>
#include "EEPROM.h"
#include "IRReadOnlyRemote.h"
#include "magic_remote.h"

#define IR_PIN 2  // must be interrupt capable pin (2 or 3)

#define DEBUG 1
#define Debug if (DEBUG) Serial

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

static unsigned long last_press = 0;
static unsigned long keycode_down = 0;

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


struct Lights {
    const uint8_t *pins;
    int num_pins;
    int current_idx;
};

struct Settings {
    int magic;
    int on_time;
    int gap_time;  // -ve = overlap, +ve = gap_time milliseconds
};


struct Settings settings  = {1000, 0};

static const int EEPROM_MAGIC = 0xBEEF;
static bool eeprom_needs_update = true;

static void eeprom_update()
{
	if (!eeprom_needs_update)
		return;

	if (!key_idle(5000))
		return;

	eeprom_needs_update = false;
	EEPROM.put(0, settings);

	Debug.print("EEPROM Interval=");
	Debug.print(settings.on_time);
	Debug.print(" Gap=");
	Debug.println(settings.gap_time);
}

static void eeprom_init(void)
{
	EEPROM.get(0, settings);
	if (settings.magic != EEPROM_MAGIC) {
		settings.magic = EEPROM_MAGIC;
		settings.on_time = 1000;
		settings.gap_time = 0;
		Debug.print("Initialize ");
	}
	eeprom_update();
}


static const uint8_t pins[] = {13, 12, 11, 10, 9, 8, 7, 6};
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


class Timer {
    unsigned long start_millis;
    unsigned long delay;
public:
    Timer() {delay = 0;};
    void start(unsigned int delay_ms)
    {
        start_millis = millis();
        delay = delay_ms;
        //Debug.print("Timer start ");
        //Debug.println(delay);
    };
    bool expired()
    {
        // Debug.print(millis());
        // Debug.print(" - ");
        // Debug.println(start_millis);
        return (delay == 0) || ((millis() - start_millis) >= delay);
    };
};

Timer timer;

static enum state {on, off} current_state;

static void state_machine_init()
{
    current_state = on;
}

static void state_machine_run()
{
    if (not timer.expired())
        return;

    if (settings.gap_time >= 0) {
        switch (current_state) {
        case off:
            light_off(current_pin());
            increment_pin();
            current_state = on;
            if (settings.gap_time)
                timer.start(settings.gap_time);
                break;
            // else fall through
        case on:
            light_on(current_pin());
            timer.start(settings.on_time);
            current_state = off;
            break;
        }
    } else {  // gap_time < 0 -> overlap
        switch (current_state) {
        case on:
            light_on(next_pin());
            timer.start(-settings.gap_time);
            current_state = off;
            break;
        case off:
            light_off(current_pin());
            timer.start(settings.on_time + settings.gap_time);
            increment_pin();
            current_state = on;
            break;
        }
    }
}

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
		Debug.print(mapped_keycode, HEX);
		Debug.print("   ");
		Debug.println(keycode, HEX);
        switch(mapped_keycode) {
            case MAGIC_flash:
    			settings.on_time += 100;
    			eeprom_needs_update = true;
    			break;
    		case MAGIC_strobe:
    			settings.on_time -= 100;
    			eeprom_needs_update = true;
    			break;
    		case MAGIC_fade:
    			settings.gap_time += 1;
    			eeprom_needs_update = true;
    			break;
    		case MAGIC_smooth:
    			settings.gap_time -= 1;
    			eeprom_needs_update = true;
    			break;

        }
	}
}

void setup()
{
    eeprom_init();
    light_init();
    state_machine_init();

    Debug.begin(115200);
    Debug.println("Light cascade start");
}

void loop()
{
    eeprom_update();
    state_machine_run();
    remote_control();
}
