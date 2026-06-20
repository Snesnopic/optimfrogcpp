#ifndef OPTIMFROG_TABLES_H
#define OPTIMFROG_TABLES_H

#include <stdint.h>

// Shift multipliers for Predictor
static const int16_t DAT_00326238[8] = { 32, 64, 128, 256, 512, 1024, 2048, 0 };

// Predictor param 1 (used when index < 31)
static const int16_t DAT_00326220[32] = { 12, 24, 36, 48, 64, 96, 128, 192, 256, 32, 64, 128, 32, 64, 128, 256, 512, 1024, 2048, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// Predictor param 2 (used when index < 31)
static const int16_t DAT_00326200[32] = { 4, 8, 12, 16, 20, 32, 40, 64, 80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

#endif
