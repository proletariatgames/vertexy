#include "util/TimeUtils.h"
#include "ConstraintTypes.h"

#undef TEXT // redefined in Windows.h
#include <Windows.h>

using namespace csolver;

uint32_t TimeUtils::getCycles()
{
	LARGE_INTEGER cycles;
	QueryPerformanceCounter(&cycles);
	return (uint32_t)cycles.QuadPart;
}

double TimeUtils::getSeconds()
{
	if (m_secondsPerCycle == 0)
	{
		LARGE_INTEGER frequency;
		cs_verify(QueryPerformanceFrequency(&frequency));
		m_secondsPerCycle = 1.0 / frequency.QuadPart;
	}

	LARGE_INTEGER cycles;
	QueryPerformanceCounter(&cycles);

	// add big number to make bugs apparent where return value is being passed to float
	return cycles.QuadPart * m_secondsPerCycle;
}
