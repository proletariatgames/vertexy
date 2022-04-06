// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"

namespace csolver
{
/**
 *  Represents the range of integer values that a variable can be assigned to.
 *  FSolverVariable takes a domain as input.
 */
class SolverVariableDomain
{
public:
	explicit SolverVariableDomain(int minValue, int maxValue)
		: m_minValue(minValue)
		, m_maxValue(maxValue)
	{
		cs_assert(m_maxValue >= m_minValue);
	}

	inline bool operator==(const SolverVariableDomain& rhs) const
	{
		return m_minValue == rhs.m_minValue && m_maxValue == rhs.m_maxValue;
	}

	inline bool operator!=(const SolverVariableDomain& rhs) const
	{
		return m_minValue != rhs.m_minValue || m_maxValue != rhs.m_maxValue;
	}

	inline int getMin() const { return m_minValue; }
	inline int getMax() const { return m_maxValue; }
	inline int getDomainSize() const { return m_maxValue - m_minValue + 1; }

	/** Clamps the value to be within the domain. I.e. if less than MinValue, return MinValue. If greater than MaxValue, return MaxValue. */
	inline int clampValueToDomain(int value) const
	{
		return max(m_minValue, min(m_maxValue, value));
	}

	/** Given a value within the domain, return an index 0..n where N is the size of the domain */
	inline int getIndexForValue(int value) const
	{
		cs_assert(isValueWithinDomain(value));
		return value - m_minValue;
	}

	/** Version of above that will return false if the value isn't in the domain instead of asserting */
	inline bool getIndexForValue(int value, int& index) const
	{
		if (!isValueWithinDomain(value))
		{
			return false;
		}
		index = value - m_minValue;
		return true;
	}

	/** Given an index 0...n where N is the size of the domain, return the value associated with that index */
	inline int getValueForIndex(int index) const
	{
		cs_sanity(isValidIndex(index));
		return m_minValue + index;
	}

	inline bool isValidIndex(int index) const
	{
		return index >= 0 && index < getDomainSize();
	}

	inline bool isValueWithinDomain(int value) const
	{
		return value >= m_minValue && value <= m_maxValue;
	}

	template <typename AllocatorType>
	inline void getBitset(TValueBitset<AllocatorType>& out, bool defaultValue = false) const
	{
		out.clear();
		out.pad(getDomainSize(), defaultValue);
	}

	template <typename AllocatorType>
	inline void getBitsetForValue(int value, TValueBitset<AllocatorType>& out) const
	{
		out.clear();
		out.pad(getDomainSize(), false);
		out[getIndexForValue(value)] = true;
	}

	template <typename AllocatorType>
	inline void getBitsetForInverseValue(int value, TValueBitset<AllocatorType>& out) const
	{
		out.clear();
		out.pad(getDomainSize(), true);
		out[getIndexForValue(value)] = false;
	}

	// Translate a bit array from another domain into a bit array in this domain.
	template <typename AllocatorType1, typename AllocatorType2>
	void translateToDomain(const TValueBitset<AllocatorType1>& in, const SolverVariableDomain& otherDomain, TValueBitset<AllocatorType2>& out) const
	{
		getBitset(out, false);

		int si = max(0, m_minValue - otherDomain.m_minValue);
		int sj = max(0, otherDomain.m_minValue - m_minValue);
		int ej = min(getDomainSize(), m_maxValue - otherDomain.m_maxValue);

		for (int i = si, j = sj; j != ej; ++i, ++j)
		{
			out[j] = in[i];
		}
	}

private:
	int m_minValue;
	int m_maxValue;
};

} // namespace csolver