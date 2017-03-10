#include <Arduino.h>


#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

static int interval = 1000;
static int gap = -25;  // -ve = overlap, +ve = gap milliseconds

static const uint8_t pins[] = {13, 12, 11, 10, 9, 8, 7, 6};
static const int8_t num_pins = ARRAY_SIZE(pins);


static void lightOn(int light)
{
  digitalWrite(light, HIGH);
}


static void lightOff(int light)
{
  digitalWrite(light, LOW);
}


static void cycle_gap(int interval, int gap)
{
  int i;
  for (i = 0; i < num_pins; i++) {
    lightOn(pins[i]);
    delay(interval - gap);
    lightOff(pins[i]);
    delay(gap);
  }
}


static void cycle_overlap(int interval, int overlap)
{
  int i;
  for (i = 0; i < num_pins; i++) {
    int next = (i + 1) % num_pins;
    lightOn(pins[next]);
    delay(overlap);
    lightOff(pins[i]);
    delay(interval - overlap);
  }
}


static void cycle(int interval, int gap)
{
  if (gap < 0)
    cycle_overlap(interval, -gap);
  else
    cycle_gap(interval, gap);
}


void setup()
{
  int8_t i;
  /* set all light pins as outputs */
  for (i = 0; i < num_pins; i++) {
    pinMode(pins[i], OUTPUT);
    lightOff(pins[i]);
  }

  Serial.begin(115200);
  Serial.println("Light cascade start");
}


void loop()
{
  cycle(interval, gap);
}
