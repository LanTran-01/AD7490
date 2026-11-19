/**
 * @file        test_sequencer.c
 * @brief       Unit tests of ad7490_sequencer_start / _read_scan / _stop
 *              against the fake AD7490 model.
 */

#include <string.h>
#include "test_common.h"
#include "ad7490_driver.h"
#include "fake_ad7490.h"

#define SCAN_MASK_3CH               (0x8005U) /* VIN0, VIN2, VIN15 */
#define SHADOW_3CH                  (0xA001U) /* Same, VIN0 = MSB  */

static ad7490_config_t make_cfg(void);
static void start_ready_device(ad7490_dev_t * p_dev, uint32_t latency);
static uint32_t code_to_mv(uint16_t code);
static void test_start_and_scan(uint32_t latency);
static void test_scan_mismatch(void);
static void test_bus_errors(void);
static void test_refused_calls(void);
static void test_corrupted_handle(void);
static void test_scrub_repairs_coding(void);


int main(void)
{
    test_start_and_scan(0U);
    test_start_and_scan(1U);
    test_scan_mismatch();
    test_bus_errors();
    test_refused_calls();
    test_corrupted_handle();
    test_scrub_repairs_coding();

    return test_summary("test_sequencer");
}


static ad7490_config_t make_cfg(void)
{
    ad7490_config_t cfg = {
        .qspi_cfg = {
            .base_addr = 0x80000000U,
            .clock_mode = AD7490_QSPI_CLOCK_MODE,
            .poll_timeout_us = 10U
        },
        .vref_mv = 2500U,
        .ctrl_cfg = {
            .power_mode = AD7490_POWER_NORMAL,
            .range_mode = AD7490_RANGE_1X_VREF,
            .coding = AD7490_CODING_STRAIGHT_BINARY,
            .dout_mode = AD7490_DOUT_TRI_STATE
        }
    };

    return cfg;
}


/* Model reset, channel n returns n * 200 + 7, device initialised */
static void start_ready_device(ad7490_dev_t * p_dev, uint32_t latency)
{
    ad7490_config_t cfg = make_cfg();
    uint32_t chan_idx = 0U;

    fake_reset(FAKE_MODE_ADC);
    fake_adc_set_start_latency(latency);

    for (chan_idx = 0U; chan_idx < AD7490_CHANNEL_COUNT; chan_idx++)
    {
        fake_adc_set_code(chan_idx,
                          (uint16_t)((chan_idx * 200U) + 7U));
    }

    CHECK(AD7490_OK == ad7490_init(p_dev, &cfg));
    fake_clear_frame_count();
}


/* 1x REFIN 2500 mV, straight binary, rounded */
static uint32_t code_to_mv(uint16_t code)
{
    return (((uint32_t)code * 2500U) + 2048U) / 4096U;
}


static void test_start_and_scan(uint32_t latency)
{
    ad7490_dev_t dev;
    ad7490_sample_t samples[AD7490_CHANNEL_COUNT];
    uint32_t pass = 0U;

    start_ready_device(&dev, latency);

    /* CR load-shadow (1x, SB) = 0x83B, then Shadow word, then priming */
    CHECK(AD7490_OK == ad7490_sequencer_start(&dev, SCAN_MASK_3CH));
    CHECK(3U == fake_frame_count());
    CHECK(0x83B0U == fake_frame_din(1U));
    CHECK(SHADOW_3CH == fake_frame_din(2U));
    CHECK(AD7490_STATE_SEQUENCING == dev.state);

    for (pass = 0U; pass < 5U; pass++)
    {
        (void)memset(samples, 0xAA, sizeof(samples));
        fake_clear_frame_count();

        CHECK(AD7490_OK == ad7490_sequencer_read_scan(&dev,
                                                      samples,
                                                      AD7490_CHANNEL_COUNT));
        CHECK(3U == fake_frame_count());

        /* First frame scrubs the CR (SEQ=1, SHADOW=0): 0xC33 */
        CHECK(0xC330U == fake_frame_din(1U));
        CHECK(0x0000U == fake_frame_din(2U));
        CHECK(0x0000U == fake_frame_din(3U));

        CHECK(0U == samples[0].channel_id);
        CHECK(7U == samples[0].raw_lsb);
        CHECK(code_to_mv(7U) == samples[0].voltage_mv);
        CHECK(2U == samples[2].channel_id);
        CHECK(407U == samples[2].raw_lsb);
        CHECK(15U == samples[15].channel_id);
        CHECK(3007U == samples[15].raw_lsb);
        CHECK(code_to_mv(3007U) == samples[15].voltage_mv);
    }

    /* Stop: back to READY, single reads work again */
    CHECK(AD7490_OK == ad7490_sequencer_stop(&dev));
    CHECK(AD7490_STATE_READY == dev.state);
    CHECK(0U == dev.channel_mask);
    CHECK(AD7490_OK == ad7490_read_channel(&dev, 9U, &samples[0]));
    CHECK(9U == samples[0].channel_id);
}


static void test_scan_mismatch(void)
{
    ad7490_dev_t dev;
    ad7490_sample_t samples[AD7490_CHANNEL_COUNT];

    /* A channel seen twice in one pass (lost sync) */
    start_ready_device(&dev, 0U);
    CHECK(AD7490_OK == ad7490_sequencer_start(&dev, 0x0003U));
    fake_clear_frame_count();
    fake_bad_echo_frame(2U);
    CHECK(AD7490_ERR_CHAN_MISMATCH == ad7490_sequencer_read_scan(
                                          &dev, samples,
                                          AD7490_CHANNEL_COUNT));
    CHECK(AD7490_STATE_FAULT == dev.state);

    /* A channel outside the mask (mask VIN0, echo VIN1) */
    start_ready_device(&dev, 0U);
    CHECK(AD7490_OK == ad7490_sequencer_start(&dev, 0x0001U));
    fake_clear_frame_count();
    fake_bad_echo_frame(1U);
    CHECK(AD7490_ERR_CHAN_MISMATCH == ad7490_sequencer_read_scan(
                                          &dev, samples,
                                          AD7490_CHANNEL_COUNT));
}


static void test_bus_errors(void)
{
    ad7490_dev_t dev;
    ad7490_sample_t samples[AD7490_CHANNEL_COUNT];
    uint32_t frame_no = 0U;

    /* Timeout on each of the 3 start frames -> FAULT */
    for (frame_no = 1U; frame_no <= 3U; frame_no++)
    {
        start_ready_device(&dev, 0U);
        fake_fail_frame(frame_no);
        CHECK(AD7490_ERR_TIMEOUT == ad7490_sequencer_start(&dev, 0x0003U));
        CHECK(AD7490_STATE_FAULT == dev.state);
    }

    /* Timeout during a scan -> FAULT */
    start_ready_device(&dev, 0U);
    CHECK(AD7490_OK == ad7490_sequencer_start(&dev, 0x0003U));
    fake_clear_frame_count();
    fake_fail_frame(2U);
    CHECK(AD7490_ERR_TIMEOUT == ad7490_sequencer_read_scan(
                                    &dev, samples, AD7490_CHANNEL_COUNT));
    CHECK(AD7490_STATE_FAULT == dev.state);

    /* Timeout on the stop frame -> FAULT */
    start_ready_device(&dev, 0U);
    CHECK(AD7490_OK == ad7490_sequencer_start(&dev, 0x0001U));
    fake_clear_frame_count();
    fake_fail_frame(1U);
    CHECK(AD7490_ERR_TIMEOUT == ad7490_sequencer_stop(&dev));
    CHECK(AD7490_STATE_FAULT == dev.state);
}


static void test_refused_calls(void)
{
    ad7490_dev_t dev;
    ad7490_sample_t samples[AD7490_CHANNEL_COUNT];

    start_ready_device(&dev, 0U);

    CHECK(AD7490_ERR_PARAM == ad7490_sequencer_start(NULL, 1U));
    CHECK(AD7490_ERR_PARAM == ad7490_sequencer_start(&dev, 0U));
    CHECK(AD7490_ERR_STATE == ad7490_sequencer_read_scan(
                                  &dev, samples, AD7490_CHANNEL_COUNT));
    CHECK(AD7490_ERR_STATE == ad7490_sequencer_stop(&dev));
    CHECK(AD7490_ERR_PARAM == ad7490_sequencer_stop(NULL));

    CHECK(AD7490_OK == ad7490_sequencer_start(&dev, 0x0003U));
    CHECK(AD7490_ERR_PARAM == ad7490_sequencer_read_scan(
                                  NULL, samples, AD7490_CHANNEL_COUNT));
    CHECK(AD7490_ERR_PARAM == ad7490_sequencer_read_scan(
                                  &dev, NULL, AD7490_CHANNEL_COUNT));
    CHECK(AD7490_ERR_PARAM == ad7490_sequencer_read_scan(
                                  &dev, samples, AD7490_CHANNEL_COUNT - 1U));

    /* Single reads and second start refused while sequencing */
    CHECK(AD7490_ERR_STATE == ad7490_read_channel(&dev, 0U, &samples[0]));
    CHECK(AD7490_ERR_STATE == ad7490_sequencer_start(&dev, 0x0003U));

    /* AUTO power mode: sequencing refused */
    start_ready_device(&dev, 0U);
    CHECK(AD7490_OK == ad7490_set_power_mode(&dev,
                                             AD7490_POWER_AUTO_STANDBY));
    CHECK(AD7490_ERR_STATE == ad7490_sequencer_start(&dev, 0x0003U));
}


static void test_corrupted_handle(void)
{
    ad7490_dev_t dev;
    ad7490_sample_t samples[AD7490_CHANNEL_COUNT];

    /* SEQUENCING with an empty mask */
    start_ready_device(&dev, 0U);
    CHECK(AD7490_OK == ad7490_sequencer_start(&dev, 0x0001U));
    dev.channel_mask = 0U;
    CHECK(AD7490_ERR_STATE == ad7490_sequencer_read_scan(
                                  &dev, samples, AD7490_CHANNEL_COUNT));

    /* Bad enum: start, scan (scrub build) and stop refuse */
    start_ready_device(&dev, 0U);
    dev.cfg.ctrl_cfg.coding = (ad7490_coding_t)9U;
    CHECK(AD7490_ERR_PARAM == ad7490_sequencer_start(&dev, 1U));

    start_ready_device(&dev, 0U);
    CHECK(AD7490_OK == ad7490_sequencer_start(&dev, 1U));
    dev.cfg.ctrl_cfg.coding = (ad7490_coding_t)9U;
    CHECK(AD7490_ERR_PARAM == ad7490_sequencer_read_scan(
                                  &dev, samples, AD7490_CHANNEL_COUNT));

    start_ready_device(&dev, 0U);
    CHECK(AD7490_OK == ad7490_sequencer_start(&dev, 1U));
    dev.cfg.ctrl_cfg.coding = (ad7490_coding_t)9U;
    CHECK(AD7490_ERR_PARAM == ad7490_sequencer_stop(&dev));

    /* Bad REFIN: conversion refuses */
    start_ready_device(&dev, 0U);
    CHECK(AD7490_OK == ad7490_sequencer_start(&dev, 1U));
    dev.cfg.vref_mv = 1U;
    CHECK(AD7490_ERR_PARAM == ad7490_sequencer_read_scan(
                                  &dev, samples, AD7490_CHANNEL_COUNT));
}


static void test_scrub_repairs_coding(void)
{
    ad7490_dev_t dev;
    ad7490_sample_t samples[AD7490_CHANNEL_COUNT];

    start_ready_device(&dev, 0U);
    CHECK(AD7490_OK == ad7490_sequencer_start(&dev, 0x0003U));

    /* CODING bit flipped inside the ADC: frames still look valid */
    fake_adc_upset_coding();
    CHECK(0U == fake_adc_coding_bit());
    CHECK(AD7490_OK == ad7490_sequencer_read_scan(&dev, samples,
                                                  AD7490_CHANNEL_COUNT));
    CHECK(1U == fake_adc_coding_bit());

    /* Next pass is fully correct again */
    CHECK(AD7490_OK == ad7490_sequencer_read_scan(&dev, samples,
                                                  AD7490_CHANNEL_COUNT));
    CHECK(7U == samples[0].raw_lsb);
    CHECK(207U == samples[1].raw_lsb);
}
