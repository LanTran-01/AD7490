/**
 * @file        test_common.h
 * @brief       Minimal host unit-test support: checks, ASSERT hook and
 *              result summary.
 *
 * Test code is a verification artefact, not flight code. It follows the
 * formatting rules (§0) but may use <stdio.h>, <math.h> and double,
 * which rule §8 bans only from production code.
 *
 * Every test build defines ASSERT(expr) as test_assert(...), so a driver
 * contract violated during a test is counted as a failure.
 */

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

/* Record one test check; prints the line number when it fails */
#define CHECK(expr)                 test_check((expr), (uint32_t)__LINE__)

void test_check(bool is_ok, uint32_t line);
void test_assert(bool is_ok, uint32_t line);
int test_summary(char const * p_name);

#endif /* TEST_COMMON_H */
