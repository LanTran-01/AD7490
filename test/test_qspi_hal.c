/**
 * @file        test_qspi_hal.c
 * @brief       Host unit tests for axi_qspi_hal.c against a simulated
 *              32-bit register window in host memory.
 */

#include <string.h>
#include "test_common.h"
#include "axi_qspi_hal.c"

/* 32 words = 128 bytes, covering offsets 0x00..0x7C */
#define FAKE_MMIO_WORDS             (32U)
#define WORD_SHIFT                  (2U)

static uint32_t g_mmio_regs[FAKE_MMIO_WORDS];

static uintptr_t mmio_base(void);
static void mmio_reset(void);
static void mmio_set_reg(uint32_t offset_bytes, uint32_t reg_word);
static uint32_t mmio_get_reg(uint32_t offset_bytes);
static qspi_hal_config_t make_hal_cfg(void);
static void test_init_params(void);
static void test_init_sequence_and_faults(void);
static void test_transceive_bytes(void);
static void test_write_bytes(void);
static void test_read_bytes(void);
static void test_drain_rx_fault(void);


int main(void)
{
    test_init_params();
    test_init_sequence_and_faults();
    test_transceive_bytes();
    test_write_bytes();
    test_read_bytes();
    test_drain_rx_fault();

    return test_summary("test_qspi_hal");
}


static uintptr_t mmio_base(void)
{
    return (uintptr_t)g_mmio_regs;
}


static void mmio_reset(void)
{
    size_t i = 0U;

    for (i = 0U; i < FAKE_MMIO_WORDS; i++)
    {
        g_mmio_regs[i] = 0U;
    }

    /* Default PG153 post-reset state: SPICR = 0x180, SS0 deasserted */
    mmio_set_reg(QSPI_REG_SPICR_OFFSET, QSPI_SPICR_RESET_WORD);
    mmio_set_reg(QSPI_REG_SPISSR_OFFSET, QSPI_SPISSR_DEASSERT_ALL);
}


static void mmio_set_reg(uint32_t offset_bytes, uint32_t reg_word)
{
    size_t word_idx = (size_t)(offset_bytes >> WORD_SHIFT);

    if (word_idx < FAKE_MMIO_WORDS)
    {
        g_mmio_regs[word_idx] = reg_word;
    }
    else
    {
        /* Out of range offset */
    }
}


static uint32_t mmio_get_reg(uint32_t offset_bytes)
{
    uint32_t reg_word = 0U;
    size_t word_idx = (size_t)(offset_bytes >> WORD_SHIFT);

    if (word_idx < FAKE_MMIO_WORDS)
    {
        reg_word = g_mmio_regs[word_idx];
    }
    else
    {
        reg_word = 0U;
    }

    return reg_word;
}


static qspi_hal_config_t make_hal_cfg(void)
{
    qspi_hal_config_t cfg = {
        .base_addr = mmio_base(),
        .clock_mode = QSPI_CLOCK_MODE_2,
        .poll_timeout_us = 1U
    };

    return cfg;
}


static void test_init_params(void)
{
    qspi_hal_config_t cfg = make_hal_cfg();
    qspi_hal_config_t bad = cfg;

    mmio_reset();

    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_init(NULL));

    bad.base_addr = 0U;
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_init(&bad));

    bad = cfg;
    bad.base_addr = mmio_base() + 1U;
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_init(&bad));

    bad = cfg;
    bad.base_addr = mmio_base() + 2U;
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_init(&bad));

    bad = cfg;
    bad.base_addr = mmio_base() + 3U;
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_init(&bad));

    bad = cfg;
    bad.clock_mode = (qspi_clock_mode_t)QSPI_CLOCK_MODE_COUNT;
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_init(&bad));

    bad = cfg;
    bad.poll_timeout_us = 0U;
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_init(&bad));

    bad = cfg;
    bad.poll_timeout_us = QSPI_HAL_MAX_TIMEOUT_US + 1U;
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_init(&bad));
}


static void test_init_sequence_and_faults(void)
{
    qspi_hal_config_t cfg = make_hal_cfg();
    uint32_t expected_base_ctrl = QSPI_SPICR_SPE_MASK
                                  | QSPI_SPICR_MASTER_MASK
                                  | QSPI_SPICR_MANUAL_SS_MASK
                                  | QSPI_SPICR_INHIBIT_MASK;

    /* Normal init across all 4 SPI clock modes */
    mmio_reset();
    cfg.clock_mode = QSPI_CLOCK_MODE_0;
    CHECK(QSPI_HAL_OK == qspi_hal_init(&cfg));
    CHECK(QSPI_SRR_RESET_KEY == mmio_get_reg(QSPI_REG_SRR_OFFSET));
    CHECK(0U == mmio_get_reg(QSPI_REG_GIER_OFFSET));
    CHECK(QSPI_SPISSR_DEASSERT_ALL == mmio_get_reg(QSPI_REG_SPISSR_OFFSET));
    CHECK(expected_base_ctrl == mmio_get_reg(QSPI_REG_SPICR_OFFSET));

    mmio_reset();
    cfg.clock_mode = QSPI_CLOCK_MODE_1;
    CHECK(QSPI_HAL_OK == qspi_hal_init(&cfg));
    CHECK((expected_base_ctrl | QSPI_SPICR_CPHA_MASK)
          == mmio_get_reg(QSPI_REG_SPICR_OFFSET));

    mmio_reset();
    cfg.clock_mode = QSPI_CLOCK_MODE_2;
    CHECK(QSPI_HAL_OK == qspi_hal_init(&cfg));
    CHECK((expected_base_ctrl | QSPI_SPICR_CPOL_MASK)
          == mmio_get_reg(QSPI_REG_SPICR_OFFSET));

    mmio_reset();
    cfg.clock_mode = QSPI_CLOCK_MODE_3;
    CHECK(QSPI_HAL_OK == qspi_hal_init(&cfg));
    CHECK((expected_base_ctrl | QSPI_SPICR_CPOL_MASK | QSPI_SPICR_CPHA_MASK)
          == mmio_get_reg(QSPI_REG_SPICR_OFFSET));

    /* Defensive branch in build_control_word when clock_mode >= 4 */
    CHECK(expected_base_ctrl
          == build_control_word((qspi_clock_mode_t)QSPI_CLOCK_MODE_COUNT));

    /* Core fails to return to PG153 reset state */
    mmio_reset();
    mmio_set_reg(QSPI_REG_SPICR_OFFSET, 0U);
    CHECK(QSPI_HAL_ERR_HW == qspi_hal_init(&cfg));

    /* Mode fault (MODF) flagged in SPISR after init */
    mmio_reset();
    mmio_set_reg(QSPI_REG_SPISR_OFFSET, QSPI_SPISR_MODF_MASK);
    CHECK(QSPI_HAL_ERR_HW == qspi_hal_init(&cfg));
}


static void test_transceive_bytes(void)
{
    qspi_hal_config_t cfg = make_hal_cfg();
    uint8_t tx_bytes[2U] = { 0x97U, 0x10U };
    uint8_t rx_bytes[2U] = { 0U, 0U };

    mmio_reset();

    /* Parameter boundary checks */
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_transceive_bytes(NULL,
                                                          tx_bytes,
                                                          rx_bytes,
                                                          2U));
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_transceive_bytes(&cfg,
                                                          tx_bytes,
                                                          rx_bytes,
                                                          0U));
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_transceive_bytes(
                                    &cfg,
                                    tx_bytes,
                                    rx_bytes,
                                    QSPI_HAL_FIFO_DEPTH_BYTES + 1U));
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_transceive_bytes(&cfg,
                                                          NULL,
                                                          rx_bytes,
                                                          2U));
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_transceive_bytes(&cfg,
                                                          tx_bytes,
                                                          NULL,
                                                          2U));

    /* Successful 2-byte transfer: RX not empty, occupancy = 2 - 1 = 1 */
    mmio_reset();
    mmio_set_reg(QSPI_REG_SPISR_OFFSET, 0U);
    mmio_set_reg(QSPI_REG_RXOCY_OFFSET, 1U);
    mmio_set_reg(QSPI_REG_SPIDRR_OFFSET, 0x5AU);
    CHECK(QSPI_HAL_OK == qspi_hal_transceive_bytes(&cfg,
                                                   tx_bytes,
                                                   rx_bytes,
                                                   2U));
    CHECK(0x10U == mmio_get_reg(QSPI_REG_SPIDTR_OFFSET));
    CHECK(0x5AU == rx_bytes[0U]);
    CHECK(0x5AU == rx_bytes[1U]);
    CHECK(QSPI_SPISSR_DEASSERT_ALL == mmio_get_reg(QSPI_REG_SPISSR_OFFSET));

    /* Timeout when RX FIFO stays empty; output buffer untouched */
    rx_bytes[0U] = 0xCCU;
    rx_bytes[1U] = 0xDDU;
    mmio_reset();
    mmio_set_reg(QSPI_REG_SPISR_OFFSET, QSPI_SPISR_RX_EMPTY_MASK);
    CHECK(QSPI_HAL_ERR_TIMEOUT == qspi_hal_transceive_bytes(&cfg,
                                                            tx_bytes,
                                                            rx_bytes,
                                                            2U));
    CHECK(0xCCU == rx_bytes[0U]);
    CHECK(0xDDU == rx_bytes[1U]);

    /* Hardware mode fault (MODF) */
    mmio_reset();
    mmio_set_reg(QSPI_REG_SPISR_OFFSET, QSPI_SPISR_MODF_MASK);
    mmio_set_reg(QSPI_REG_RXOCY_OFFSET, 1U);
    CHECK(QSPI_HAL_ERR_HW == qspi_hal_transceive_bytes(&cfg,
                                                       tx_bytes,
                                                       rx_bytes,
                                                       2U));
    CHECK(0xCCU == rx_bytes[0U]);
}


static void test_write_bytes(void)
{
    qspi_hal_config_t cfg = make_hal_cfg();
    uint8_t tx_bytes[2U] = { 0x83U, 0x10U };

    mmio_reset();

    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_write_bytes(NULL, tx_bytes, 2U));
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_write_bytes(&cfg, tx_bytes, 0U));
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_write_bytes(
                                    &cfg,
                                    tx_bytes,
                                    QSPI_HAL_FIFO_DEPTH_BYTES + 1U));
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_write_bytes(&cfg, NULL, 2U));

    /* Successful write */
    mmio_reset();
    mmio_set_reg(QSPI_REG_SPISR_OFFSET, 0U);
    mmio_set_reg(QSPI_REG_RXOCY_OFFSET, 1U);
    CHECK(QSPI_HAL_OK == qspi_hal_write_bytes(&cfg, tx_bytes, 2U));
    CHECK(0x10U == mmio_get_reg(QSPI_REG_SPIDTR_OFFSET));
    CHECK(QSPI_SPISSR_DEASSERT_ALL == mmio_get_reg(QSPI_REG_SPISSR_OFFSET));

    /* Timeout */
    mmio_reset();
    mmio_set_reg(QSPI_REG_SPISR_OFFSET, QSPI_SPISR_RX_EMPTY_MASK);
    CHECK(QSPI_HAL_ERR_TIMEOUT == qspi_hal_write_bytes(&cfg, tx_bytes, 2U));

    /* Mode fault */
    mmio_reset();
    mmio_set_reg(QSPI_REG_SPISR_OFFSET, QSPI_SPISR_MODF_MASK);
    mmio_set_reg(QSPI_REG_RXOCY_OFFSET, 1U);
    CHECK(QSPI_HAL_ERR_HW == qspi_hal_write_bytes(&cfg, tx_bytes, 2U));
}


static void test_read_bytes(void)
{
    qspi_hal_config_t cfg = make_hal_cfg();
    uint8_t rx_bytes[2U] = { 0U, 0U };

    mmio_reset();

    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_read_bytes(NULL,
                                                    0x00U,
                                                    rx_bytes,
                                                    2U));
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_read_bytes(&cfg,
                                                    0x00U,
                                                    rx_bytes,
                                                    0U));
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_read_bytes(
                                    &cfg,
                                    0x00U,
                                    rx_bytes,
                                    QSPI_HAL_FIFO_DEPTH_BYTES + 1U));
    CHECK(QSPI_HAL_ERR_PARAM == qspi_hal_read_bytes(&cfg,
                                                    0x00U,
                                                    NULL,
                                                    2U));

    /* Successful read with dummy byte 0xAA */
    mmio_reset();
    mmio_set_reg(QSPI_REG_SPISR_OFFSET, 0U);
    mmio_set_reg(QSPI_REG_RXOCY_OFFSET, 1U);
    mmio_set_reg(QSPI_REG_SPIDRR_OFFSET, 0x3CU);
    CHECK(QSPI_HAL_OK == qspi_hal_read_bytes(&cfg, 0xAAU, rx_bytes, 2U));
    CHECK(0xAAU == mmio_get_reg(QSPI_REG_SPIDTR_OFFSET));
    CHECK(0x3CU == rx_bytes[0U]);
    CHECK(0x3CU == rx_bytes[1U]);

    /* Timeout leaves rx_bytes untouched */
    rx_bytes[0U] = 0x11U;
    mmio_reset();
    mmio_set_reg(QSPI_REG_SPISR_OFFSET, QSPI_SPISR_RX_EMPTY_MASK);
    CHECK(QSPI_HAL_ERR_TIMEOUT == qspi_hal_read_bytes(&cfg,
                                                      0x00U,
                                                      rx_bytes,
                                                      2U));
    CHECK(0x11U == rx_bytes[0U]);
}


static void test_drain_rx_fault(void)
{
    uint8_t rx_bytes[2U] = { 0x77U, 0x88U };

    /* Direct test of drain_rx_bytes when RX_EMPTY asserts unexpectedly */
    mmio_reset();
    mmio_set_reg(QSPI_REG_SPISR_OFFSET, QSPI_SPISR_RX_EMPTY_MASK);
    CHECK(QSPI_HAL_ERR_HW == drain_rx_bytes(mmio_base(), rx_bytes, 2U));
    CHECK(0x77U == rx_bytes[0U]);
    CHECK(0x88U == rx_bytes[1U]);
}
