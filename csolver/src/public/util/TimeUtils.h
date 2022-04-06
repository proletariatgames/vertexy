// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"

namespace csolver
{

class TimeUtils
{
	TimeUtils()
	{
	}

	static inline double m_secondsPerCycle = 0;
public:
	static uint32_t getCycles();
	static double getSeconds();
};

} // namespace csolver