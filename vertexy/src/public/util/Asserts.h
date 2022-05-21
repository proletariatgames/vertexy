// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "EAAssert/eaassert.h"

#define vxy_assert(v) EA_ASSERT(v)
#define vxy_assert_msg(v, msg, ...) EA_ASSERT_FORMATTED(v, (msg, ##__VA_ARGS__))
#define vxy_verify(v) { bool __b = (v); EA_ASSERT(__b); __b; } 0
#define vxy_verify_msg(v, msg, ...) { bool __b = (v); EA_ASSERT_FORMATTED(__b, (msg, ##__VA_ARGS__)); }
#define vxy_fail() EA_FAIL()
#define vxy_fail_msg(msg, ...) EA_ASSERT_FORMATTED(false, (msg, ##__VA_ARGS__))

#if VERTEXY_SANITY_CHECKS
    #define vxy_sanity(v) EA_ASSERT(v)
    #define vxy_sanity_msg(v, msg, ...) EA_ASSERT_FORMATTED(v, (msg, ##__VA_ARGS__))
#else
    #define vxy_sanity(v)
    #define vxy_sanity_msg(v, msg, ...)
#endif
