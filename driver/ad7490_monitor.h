/**
 * @file        ad7490_monitor.h
 * @brief       Periodic AD7490 acquisition: sequenced scan every period,
 *              a 16-sample moving average per monitored channel, and
 *              bounded re-configuration (max 3 attempts) after faults.
 * @req         LLR-AD7490-001
 * @design      SDD-AD7490-7.0
 *
 * Typical use:
 *   static ad7490_monitor_t g_adc_mon;      (static: ~1.5 KB)
 *   ad7490_monitor_init(&g_adc_mon, &adc_cfg, 0x0003U);
 *   1 ms task:  ad7490_monitor_step(&g_adc_mon);
 *               ad7490_monitor_get_avg(&g_adc_mon, 0U, &vin0_mv, &n);
 *   On AD7490_ERR_DEVICE_FAILED: treat the sensor data as invalid and
 *   report it (maintenance / degraded mode).
 */

#ifndef AD7490_MONITOR_H
#define AD7490_MONITOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ad7490_driver.h"

/* Samples per averaging window (project requirement, 16 x 1 ms).
 * Defined before the types because ad7490_avg_t uses it. */
#define AD7490_AVG_WINDOW_SIZE      (16U)

/**
 * @brief       Moving-average window (FIFO) of one channel.
 * @req         LLR-AD7490-064
 * @design      SDD-AD7490-6.5
 * @note        Private to the monitor (public only because it is a
 *              member of ad7490_monitor_t). Each push stores the newest
 *              sample over the oldest one. The average covers only the
 *              samples actually stored, so during the first 15 pushes it
 *              is the mean of 1..15 real samples, never diluted by empty
 *              slots. No running sum is stored: the sum is rebuilt from
 *              the window on every read, so a corrupted value leaves the
 *              window after at most 16 pushes.
 */
typedef struct
{
    uint32_t window_mv[AD7490_AVG_WINDOW_SIZE]; /* Ring buffer, mV, 0..5050 */
    size_t   next_idx;    /* Slot the next push overwrites, 0..15      */
    size_t   fill_count;  /* Samples stored since reset, 0..16          */
} ad7490_avg_t;

/**
 * @brief       State of one monitored AD7490, owned by the caller.
 * @req         LLR-AD7490-076
 * @design      SDD-AD7490-7.7
 * @note        Fields are private: use ad7490_monitor_* only. Too large
 *              for the stack (rule §8): give it static storage.
 */
typedef struct
{
    ad7490_config_t cfg;                        /* Used to (re)init      */
    uint16_t        channel_mask;               /* bit n = VINn, 0 = off */
    ad7490_dev_t    dev;                        /* Device handle         */
    ad7490_sample_t scan[AD7490_CHANNEL_COUNT]; /* Last pass, by channel */
    ad7490_avg_t    avg[AD7490_CHANNEL_COUNT];  /* Window per channel    */
    uint32_t        health;            /* AD7490_MON_HEALTH_* pattern    */
    uint32_t        reconfig_attempts; /* Attempts not yet cleared, 0..3 */
    uint32_t        reconfig_total;    /* Attempts since init, saturates */
    uint32_t        good_scan_count;   /* Consecutive good scans, 0..16  */
} ad7490_monitor_t;

/**
 * @brief       Health and re-configuration counters of a monitor.
 * @req         LLR-AD7490-077
 * @design      SDD-AD7490-7.8
 */
typedef struct
{
    bool     is_failed;         /* true: limit hit, data invalid        */
    uint32_t reconfig_attempts; /* Attempts in the current fault episode */
    uint32_t reconfig_total;    /* Attempts since init (diagnostics)    */
} ad7490_mon_diag_t;

/* Re-config attempts allowed per fault episode before the ADC is
 * declared failed (project requirement). */
#define AD7490_MON_MAX_RECONFIG     (3U)

/* Consecutive good scans (1 per ms = one full window, 16 ms) that close
 * a fault episode and clear reconfig_attempts. A fault that comes back
 * sooner counts towards the same 3-attempt limit. */
#define AD7490_MON_STABLE_SCANS     (16U)

/* Monitor health patterns: Hamming distance >= 12 to each other, to the
 * AD7490_STATE_* patterns and to 0x00000000 / 0xFFFFFFFF (rule §9). */
#define AD7490_MON_HEALTH_ACTIVE    (0x96E15A3CU)
#define AD7490_MON_HEALTH_FAILED    (0x691EA5C3U)

/**
 * @brief       Configure the monitor, initialise the ADC, start sequencing.
 * @req         LLR-AD7490-070
 * @design      SDD-AD7490-7.1
 * @param[out]  p_mon         Monitor state, must not be NULL.
 * @param[in]   p_cfg         ADC configuration (see ad7490_init), copied;
 *                            ctrl_cfg.power_mode must be NORMAL.
 * @param[in]   channel_mask  Monitored channels, bit n = VINn,
 *                            0x0001..0xFFFF.
 * @return      AD7490_OK, AD7490_ERR_PARAM, AD7490_ERR_STATE,
 *              AD7490_ERR_SPI, AD7490_ERR_TIMEOUT.
 * @note        Not ISR-safe, blocking, WCET: QSPI init + 6 frames.
 *              Clears every counter and a FAILED latch (explicit reset
 *              by the system). On a bus error the monitor stays
 *              configured and ad7490_monitor_step() re-configures; on
 *              AD7490_ERR_PARAM / AD7490_ERR_STATE it is disabled.
 */
ad7490_status_t ad7490_monitor_init(ad7490_monitor_t * p_mon,
                                    ad7490_config_t const * p_cfg,
                                    uint16_t channel_mask);

/**
 * @brief       One acquisition period: scan, update averages, or recover.
 * @req         LLR-AD7490-071
 * @design      SDD-AD7490-7.2
 * @param[in,out] p_mon  Configured monitor, must not be NULL.
 * @return      AD7490_OK, AD7490_ERR_PARAM, AD7490_ERR_STATE,
 *              AD7490_ERR_SPI, AD7490_ERR_TIMEOUT,
 *              AD7490_ERR_CHAN_MISMATCH, AD7490_ERR_DEVICE_FAILED.
 * @note        Call every 1 ms from a periodic task, NOT from an ISR.
 *              WCET: 16 frames, or QSPI init + 22 frames in a
 *              re-configuration cycle. If the previous cycle failed,
 *              this call makes ONE re-configuration attempt (see
 *              ad7490_monitor_reconfig). Any error empties all windows.
 *              Not reentrant: step, reconfig and get_avg must run in the
 *              same task or be serialised by the caller (§9).
 */
ad7490_status_t ad7490_monitor_step(ad7490_monitor_t * p_mon);

/**
 * @brief       One verified re-configuration attempt of the ADC.
 * @req         LLR-AD7490-078
 * @design      SDD-AD7490-7.9
 * @param[in,out] p_mon  Configured monitor, must not be NULL.
 * @return      AD7490_OK (ADC re-configured and a verified scan stored),
 *              AD7490_ERR_PARAM, AD7490_ERR_STATE, AD7490_ERR_SPI,
 *              AD7490_ERR_TIMEOUT, AD7490_ERR_CHAN_MISMATCH,
 *              AD7490_ERR_DEVICE_FAILED.
 * @note        Not ISR-safe, blocking, WCET: QSPI init + 22 frames.
 *              Attempt = empty windows, reset QSPI IP, AD7490 power-up
 *              sequence, reload Control and Shadow registers, then one
 *              full scan whose channel echoes must all be correct.
 *              At most AD7490_MON_MAX_RECONFIG attempts per fault
 *              episode; the call that would exceed it, or the failure
 *              of the last attempt, latches FAILED: every later call
 *              returns AD7490_ERR_DEVICE_FAILED until
 *              ad7490_monitor_init(). Also callable by the application
 *              when its own plausibility check finds a wrong value.
 */
ad7490_status_t ad7490_monitor_reconfig(ad7490_monitor_t * p_mon);

/**
 * @brief       Moving average of one monitored channel.
 * @req         LLR-AD7490-072
 * @design      SDD-AD7490-7.3
 * @param[in]   p_mon         Monitor, must not be NULL.
 * @param[in]   channel_id    Monitored channel, 0..15.
 * @param[out]  p_avg_mv      Mean voltage at VINx, mV, 0..5050, not NULL.
 * @param[out]  p_fill_count  Samples in the mean, 1..16, not NULL.
 * @return      AD7490_OK, AD7490_ERR_PARAM, AD7490_ERR_STATE,
 *              AD7490_ERR_DEVICE_FAILED.
 * @note        AD7490_ERR_PARAM: channel not monitored. AD7490_ERR_STATE:
 *              no sample since the last init or fault.
 *              AD7490_ERR_DEVICE_FAILED: ADC declared failed.
 */
ad7490_status_t ad7490_monitor_get_avg(ad7490_monitor_t const * p_mon,
                                       uint8_t channel_id,
                                       uint32_t * p_avg_mv,
                                       size_t * p_fill_count);

/**
 * @brief       Read the health flag and re-configuration counters.
 * @req         LLR-AD7490-079
 * @design      SDD-AD7490-7.10
 * @param[in]   p_mon   Monitor, must not be NULL.
 * @param[out]  p_diag  Diagnostics, must not be NULL.
 * @return      AD7490_OK, AD7490_ERR_PARAM.
 * @note        ISR-safe only if the caller prevents a concurrent step.
 *              Any health value other than ACTIVE reads as failed.
 */
ad7490_status_t ad7490_monitor_get_diag(ad7490_monitor_t const * p_mon,
                                        ad7490_mon_diag_t * p_diag);

#endif /* AD7490_MONITOR_H */
