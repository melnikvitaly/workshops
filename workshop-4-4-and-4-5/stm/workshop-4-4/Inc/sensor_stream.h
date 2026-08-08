#ifndef SENSOR_STREAM_H
#define SENSOR_STREAM_H

// Sensor acquisition: the single place every measurement in this firmware is
// taken, and the single place the rest of the code reads them back from.
//
//  * Light: ADC1 (PA0) sampled by TIM2 at LIGHT_SAMPLE_RATE_HZ into a circular
//    DMA buffer. Each half of the buffer, when full, is averaged into one
//    reading. The DMA half/full interrupts only set a flag; the averaging
//    happens in SensorStream_Poll() so nothing but the main loop ever touches
//    the published values.
//  * Time: the DS1307 RTC read on a software timer (RTC_SAMPLE_INTERVAL_MS).
//  * Environment: the BME280 read on a software timer (ENV_SAMPLE_INTERVAL_MS)
//    for temperature, humidity and pressure. The chip runs in NORMAL mode, so a
//    read is one I2C exchange against an already-finished measurement — there is
//    no wait-for-conversion anywhere in the loop.
//
// Consumers (display_ui for the OLED frame, telemetry_link for the SPI frame)
// only ever read the latest published value; they never talk to a sensor
// themselves. That keeps every I2C exchange on one caller and makes "is this
// number fresh?" a single flag rather than a per-consumer question.
//
// Uses the ADC/I2C peripherals brought up in main.c (hadc1, hi2c1) and owns TIM2
// and the ADC's DMA stream (DMA2 Stream0) plus their glue as weak overrides.

#include "stm32f4xx_hal.h"
#include <stdint.h>

#include "bme280.h"   // Bme280Reading
#include "ds1307.h"   // RtcTime

// Start the light ADC (timer-triggered circular DMA) and bring up the BME280.
// Call once after MX_ADC1_Init()/MX_I2C1_Init(). A BME280 that is absent or
// silent at this point is not an error — SensorStream_Poll() keeps retrying, so
// the sensor can also be plugged in while the board runs.
void SensorStream_Init(void);

// Drain whatever the ISRs flagged and service the RTC/BME280 timers. Call every
// main-loop iteration; each sensor does work only when its own period is due.
void SensorStream_Poll(void);

// Most recent averaged light level (0..100 %) — this is the "light level
// percentage" the telemetry frame carries, and the percent shown on the OLED.
uint8_t SensorStream_LatestLightPercent(void);

// Most recent averaged light level in raw ADC counts (0..4095). A percent is
// only 100 buckets over 0..4095, so one step is ~41 counts and ordinary light
// changes quantise away; keep the raw value for anything that shows detail.
uint16_t SensorStream_LatestLightRaw(void);

// Latest RTC reading. Returns 1 and fills *out when the last DS1307 exchange
// succeeded, 0 when it did not (in which case *out is left untouched, so the
// caller keeps showing/sending the previous time rather than a zeroed one).
int SensorStream_LatestTime(RtcTime *out);

// Latest BME280 reading (°C*100, %RH*100, Pa), same contract as above.
int SensorStream_LatestEnv(Bme280Reading *out);

#endif // SENSOR_STREAM_H
