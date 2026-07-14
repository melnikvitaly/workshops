#ifndef SENSOR_STREAM_H
#define SENSOR_STREAM_H

// Sensor acquisition that streams into the SPI log (see log_emission.h).
//
//  * Light: ADC1 (PA0) sampled by TIM2 at LIGHT_SAMPLE_RATE_HZ into a circular
//    DMA buffer. Each half of the buffer, when full, is averaged into one
//    "LGHT" DataEntry. The DMA half/full interrupts only set a flag; the
//    averaging and the LogEmission enqueue happen in SensorStream_Poll() so the
//    log producer stays single-writer.
//  * Time: the DS1307 RTC is read on a software timer (RTC_STREAM_INTERVAL_MS)
//    and pushed as a "TIME" DataEntry — the RTC is just another sensor.
//
// Uses the ADC/I2C peripherals brought up in main.c (hadc1, hi2c1) and owns TIM2
// and the ADC's DMA stream (DMA2 Stream0) plus their glue as weak overrides.

#include "stm32f4xx_hal.h"
#include <stdint.h>

// Reconfigure ADC1 for timer-triggered circular DMA, start TIM2 and the DMA.
// Call once after MX_ADC1_Init()/MX_I2C1_Init() and LogEmission_Init().
void SensorStream_Init(void);

// Drain whatever the ISRs flagged: average any ready ADC half into a "LGHT"
// entry, and on the RTC tick read the DS1307 into a "TIME" entry. Call every
// main-loop iteration.
void SensorStream_Poll(void);

// Most recent averaged light level (0..100 %), e.g. for the EEPROM logger.
uint8_t SensorStream_LatestLightPercent(void);

#endif // SENSOR_STREAM_H
