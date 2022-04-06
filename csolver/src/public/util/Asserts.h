// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "EAAssert/eaassert.h"

#define cs_assert(v) EA_ASSERT(v)
#define cs_assert_msg(v, msg, ...) EA_ASSERT_FORMATTED(v, (msg, ##__VA_ARGS__))
#define cs_verify(v) { bool __b = (v); EA_ASSERT(__b); }
#define cs_verify_msg(v, msg, ...) { bool __b = (v); EA_ASSERT_FORMATTED(__b, (msg, ##__VA_ARGS__)); }
#define cs_fail() EA_FAIL()
#define cs_fail_msg(msg, ...) EA_ASSERT_FORMATTED(false, (msg, ##__VA_ARGS__))

#if CS_SANITY_CHECKS
#define cs_sanity(v) EA_ASSERT(v)
#define cs_sanity_msg(v, msg, ...) EA_ASSERT_FORMATTED(v, (msg, ##__VA_ARGS__))
#else
#define cs_sanity(v)
#define cs_sanity_msg(v, msg, ...)
#endif
