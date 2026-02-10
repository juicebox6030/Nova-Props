#include "core/stepper.h"

#include "core/config.h"
static int32_t stepCurrent = 0;
static int32_t stepTarget  = 0;
static uint8_t halfStepIdx = 0;
static uint32_t nextStepDueUs = 0;

static constexpr uint8_t HALFSEQ[8][4] = {
  {1,0,0,0},
  {1,1,0,0},
  {0,1,0,0},
  {0,1,1,0},
  {0,0,1,0},
  {0,0,1,1},
  {0,0,0,1},
  {1,0,0,1},
};

static void stepperWritePhase(uint8_t idx) {
  digitalWrite(cfg.hardware.stepper.in1, HALFSEQ[idx][0] ? HIGH : LOW);
  digitalWrite(cfg.hardware.stepper.in2, HALFSEQ[idx][1] ? HIGH : LOW);
  digitalWrite(cfg.hardware.stepper.in3, HALFSEQ[idx][2] ? HIGH : LOW);
  digitalWrite(cfg.hardware.stepper.in4, HALFSEQ[idx][3] ? HIGH : LOW);
}

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static int32_t degToSteps(float deg) {
  float d = deg;
  if (cfg.limitsEnabled) d = clampf(d, cfg.minDeg, cfg.maxDeg);
  float stepsPerDeg = (float)cfg.stepsPerRev / 360.0f;
  int32_t steps = (int32_t)lroundf(d * stepsPerDeg);
  return steps + cfg.homeOffsetSteps;
}

float stepsToDeg(int32_t steps) {
  float stepsPerDeg = (float)cfg.stepsPerRev / 360.0f;
  return (float)(steps - cfg.homeOffsetSteps) / stepsPerDeg;
}

void initStepper() {
  pinMode(cfg.hardware.stepper.in1, OUTPUT);
  pinMode(cfg.hardware.stepper.in2, OUTPUT);
  pinMode(cfg.hardware.stepper.in3, OUTPUT);
  pinMode(cfg.hardware.stepper.in4, OUTPUT);
  stepperWritePhase(0);
}

void stepperSetTargetDeg(float deg) {
  stepTarget = degToSteps(deg);
}

void stepperTick() {
  if (stepCurrent == stepTarget) return;

  uint32_t nowUs = micros();
  if ((int32_t)(nowUs - nextStepDueUs) < 0) return;

  // compute step interval based on maxDegPerSec
  float stepsPerDeg = (float)cfg.stepsPerRev / 360.0f;
  float maxStepsPerSec = cfg.maxDegPerSec * stepsPerDeg;
  if (maxStepsPerSec < 1.0f) maxStepsPerSec = 1.0f;

  uint32_t intervalUs = (uint32_t)(1000000.0f / maxStepsPerSec);
  if (intervalUs < 300) intervalUs = 300;

  if (stepTarget > stepCurrent) {
    stepCurrent++;
    halfStepIdx = (halfStepIdx + 1) & 0x07;
  } else {
    stepCurrent--;
    halfStepIdx = (halfStepIdx + 7) & 0x07;
  }

  stepperWritePhase(halfStepIdx);
  nextStepDueUs = nowUs + intervalUs;
}

int32_t stepperCurrentPosition() {
  return stepCurrent;
}
