#ifndef ANALYSIS_H
#define ANALYSIS_H
#include <stdbool.h>
#include <stdint.h>

enum { RATE = 1600, WINDOW = 1024, HOP = 512, FEATURES = 10, TRAINING = 64 };
typedef struct { int16_t axis[3]; } Sample;
typedef struct { float value[FEATURES]; } Features;
typedef enum { LEARNING, NORMAL, WARNING, CRITICAL, ANOMALY,
               SENSOR_FAULT, ACQUISITION_FAULT } Condition;
typedef struct {
    unsigned count, raised, severe, shape, quiet;
    float mean[4], sumsq[4];
    Condition condition;
} Detector;

bool analysis_init(void);
Features analyze(const Sample samples[WINDOW], unsigned axis);
Condition classify(Detector *detector, Features features, float *score);
#endif
