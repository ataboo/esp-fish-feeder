// github.com/ataboo/esp-fish-feeder
// See https://www.printables.com/model/594102-automatic-fish-feeder for hardware.
// This uses the bare minimum functionality to extend a bucket every 24 hours.
// Manually load and push the buckets in, plug in the board, and it will despense one bucket every 24 hours after that.
// Tested with esp32 v3.0.3

const int BUCKET_COUNT = 10;
// Number of steps to take when extending the first bucket
const int STEPS_FIRST_BUCKET = 430;
// Number of steps to take when extending all other buckets
const int STEPS_PER_BUCKET = 340;

// 24 hours between feedings
const int FEED_DELAY = 1000 * 60 * 60 * 24;

// Delay between each step in microseconds
const int STEP_PERIOD_US = 5000;

const int STEP_COUNT = 4;
const bool STEPS[STEP_COUNT][4] = {
    {0, 0, 0, 1},
    // {0, 0, 1, 1},
    {0, 0, 1, 0},
    // {0, 1, 1, 0},
    {0, 1, 0, 0},
    // {1, 1, 0, 0},
    {1, 0, 0, 0},
    // {1, 0, 0, 1},
};

// This order pairs with the STEPS sequence.
const int STEPPER_PINS[4] = {33, 25, 27, 14};

int step_idx = 0;
unsigned long last_step_time = 0;
int extendedBucketCount = 0;

/* 
* Move the stepper motor a number of steps.
* Positive steps for one direction, negative for the other.
* This is very similar to the Arduino Stepper implementation only stepping one pin at a time rather than pairs.
*/
static void step(int steps) {
  int steps_left = abs(steps);
  bool reversed = steps < 0;

  while(steps_left > 0) {
    unsigned long now = micros();

    if(now - last_step_time >= STEP_PERIOD_US) {
      digitalWrite(STEPPER_PINS[0], STEPS[step_idx][0]);
      digitalWrite(STEPPER_PINS[1], STEPS[step_idx][1]);
      digitalWrite(STEPPER_PINS[2], STEPS[step_idx][2]);
      digitalWrite(STEPPER_PINS[3], STEPS[step_idx][3]);

      steps_left--;

      int next_step_idx = (reversed ? (step_idx + STEP_COUNT-1) : step_idx + 1) % STEP_COUNT;
      step_idx = next_step_idx;
      last_step_time = now;
    } else {
      yield();
    }
  }

  digitalWrite(STEPPER_PINS[0], LOW);
  digitalWrite(STEPPER_PINS[1], LOW);
  digitalWrite(STEPPER_PINS[2], LOW);
  digitalWrite(STEPPER_PINS[3], LOW);
}

void setup() {
  Serial.begin(9600);

  pinMode(STEPPER_PINS[0], OUTPUT);
  pinMode(STEPPER_PINS[1], OUTPUT);
  pinMode(STEPPER_PINS[2], OUTPUT);
  pinMode(STEPPER_PINS[3], OUTPUT);

  Serial.println("Setup complete");
}

void loop() {
  Serial.printf("Waiting for %dms\n", FEED_DELAY);
  delay(FEED_DELAY);

  if(extendedBucketCount < BUCKET_COUNT) {
    int stepCount = (extendedBucketCount == 0 ? STEPS_FIRST_BUCKET : STEPS_PER_BUCKET);
    step(stepCount);

    extendedBucketCount++;
    Serial.printf("Extended bucket %d\n", extendedBucketCount);
  } else {
    Serial.println("All buckets extended");
  }
}
