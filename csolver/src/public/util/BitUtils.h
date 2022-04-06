// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

// !!FIXME!! Windows dependency
#include "stdint.h"
#include <intrin.h>

namespace csolver
{
class BitUtils
{
	BitUtils()
	{
	}

public:
	template <typename WORD_TYPE>
	static WORD_TYPE computeMask(int domainSize);

	template <typename WORD_TYPE>
	static WORD_TYPE countTrailingZeros(WORD_TYPE in);

	template <typename WORD_TYPE>
	static WORD_TYPE countLeadingZeros(WORD_TYPE in);

	template <typename WORD_TYPE>
	static WORD_TYPE countBits(WORD_TYPE in);
};

template <>
inline uint32_t BitUtils::computeMask<uint32_t>(int domainSize)
{
	constexpr int bitsPerWord = sizeof(uint32_t) * 8;
	const uint32_t unusedBits = (bitsPerWord - static_cast<uint32_t>(domainSize) % bitsPerWord) % bitsPerWord;
	return ~0U >> unusedBits;
}

template <>
inline uint64_t BitUtils::computeMask<uint64_t>(int domainSize)
{
	constexpr int bitsPerWord = sizeof(uint64_t) * 8;
	const uint64_t unusedBits = (bitsPerWord - static_cast<uint64_t>(domainSize) % bitsPerWord) % bitsPerWord;
	return ~0ULL >> unusedBits;
}


template <>
inline uint32_t BitUtils::countTrailingZeros<uint32_t>(uint32_t value)
{
	if (value == 0)
	{
		return 32;
	}
	unsigned long bitIndex; // 0-based, where the LSB is 0 and MSB is 31
	_BitScanForward(&bitIndex, value); // Scans from LSB to MSB
	return bitIndex;
}

template <>
inline uint64_t BitUtils::countTrailingZeros<uint64_t>(uint64_t value)
{
	if (value == 0)
	{
		return 64;
	}
	unsigned long bitIndex; // 0-based, where the LSB is 0 and MSB is 31
	_BitScanForward64(&bitIndex, value); // Scans from LSB to MSB
	return bitIndex;
}

template <>
inline uint32_t BitUtils::countLeadingZeros<uint32_t>(uint32_t value)
{
	unsigned long log2;
	uint8_t mask = -long(_BitScanReverse(&log2, value) != 0);
	return ((31 - log2) & mask) | (32 & ~mask);
}

template <>
inline uint64_t BitUtils::countLeadingZeros<uint64_t>(uint64_t value)
{
	unsigned long log2;
	uint8_t mask = -long(_BitScanReverse64(&log2, value) != 0);
	return ((63 - log2) & mask) | (64 & ~mask);
}

template <typename WORD_TYPE>
WORD_TYPE BitUtils::countBits(WORD_TYPE bits)
{
	bits -= (bits >> 1) & 0x5555555555555555ull;
	bits = (bits & 0x3333333333333333ull) + ((bits >> 2) & 0x3333333333333333ull);
	bits = (bits + (bits >> 4)) & 0x0f0f0f0f0f0f0f0full;
	return (bits * 0x0101010101010101) >> 56;
}

} // namespace csolver