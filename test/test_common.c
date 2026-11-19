/**
 * @file        test_common.c
 * @brief       Minimal host unit-test support (see test_common.h).
 */

#include "test_common.h"

static uint32_t g_check_count = 0U;
static uint32_t g_fail_count = 0U;
static uint32_t g_assert_fail_count = 0U;


void test_check(bool is_ok, uint32_t line)
{
    g_check_count++;

    if (!is_ok)
    {
        g_fail_count++;
        (void)printf("  CHECK FAILED at test line %u\n", (unsigned)line);
    }
    else
    {
        /* Check passed */
    }
}


void test_assert(bool is_ok, uint32_t line)
{
    if (!is_ok)
    {
        g_assert_fail_count++;
        (void)printf("  ASSERT FAILED at source line %u\n", (unsigned)line);
    }
    else
    {
        /* Contract held */
    }
}


int test_summary(char const * p_name)
{
    int exit_code = 0;

    if ((0U == g_fail_count) && (0U == g_assert_fail_count))
    {
        (void)printf("%-22s PASS  %u checks\n",
                     p_name,
                     (unsigned)g_check_count);
    }
    else
    {
        (void)printf("%-22s FAIL  %u/%u checks failed, %u ASSERTs\n",
                     p_name,
                     (unsigned)g_fail_count,
                     (unsigned)g_check_count,
                     (unsigned)g_assert_fail_count);
        exit_code = 1;
    }

    return exit_code;
}
