// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include <EASTL/functional.h>

namespace Vertexy
{

// Algorithm that given a set of intervals I within domain D and a capacity per value v in D Capacity(v), check for
// bounds satisfiability of the intervals and prune intervals that are disallowed.
// See https://cs.uwaterloo.ca/~vanbeek/Publications/ijcai03.pdf
// Extended by allowing capacity(v) > 1, per https://cs.uwaterloo.ca/~vanbeek/Publications/gcc_TR.pdf
class HallIntervalPropagation
{
public:
	struct Interval
	{
		Interval()
			: key(-1)
			, minValue(0)
			, maxValue(0)
		{
		}

		Interval(int minValue, int maxValue, int key = 0)
			: key(key)
			, minValue(minValue)
			, maxValue(maxValue)
		{
		}

		// A caller-specified key
		int key;
		// Bottom of the interval
		int minValue;
		// Top of the interval
		int maxValue;
	};

	// Initializes for propagation where the capacity of each value is set to 1.
	HallIntervalPropagation(int maxValue);
	// Initialize for propagation where the domain size is the size of input array, with varying capacity per value.
	HallIntervalPropagation(const vector<int>& capacities);

	// Main API. Given a list of intervals, check for lower-bounds consistency. Returns false if not bounds-consistent
	// E.g. there are more intervals within a given range than that range can support.
	//
	// The callback is called to prune the lower-bounds: the first parameter returns the key of the interval to prune,
	// and the second parameter is the new lower bound for the interval. The function returns immediately if the callback
	// return false.
	//
	// Note that this only enforces lower-bound consistency. To enforce upper-bound consistency, you can call the function
	// with inverted intervals i.e. Inverted(I) = {-I.max, -I.min}.
	bool checkAndPrune(vector<Interval>& intervals, const function<bool(int, int)>& callback) const;

protected:
	int getCapacityForInterval(int min, int max) const
	{
		int maxI = abs(max) + 1;
		int minI = abs(min) + 1;

		int start, end;
		if (maxI > minI)
		{
			start = minI;
			end = maxI;
		}
		else
		{
			start = maxI;
			end = minI;
		}

		return m_capacityPartialSums[end] - m_capacityPartialSums[start - 1];
	}

	// Takes the set of intervals and returns the unique set of (min,max+1) values that appear, sorted increasing
	// Returns whether the intervals are inverted (i.e. intervals start < 0)
	bool createUniqueEdges(const vector<Interval>& intervals) const;

	int m_minValue;
	int m_maxValue;
	// For every value v in domain D, stores CapacityPartialSums[i] = capacity(v[i]) + capacity(v[i-1]) + ...
	vector<int> m_capacityPartialSums;

	// Intermediate data structures. Memory remains allocated for lifetime of this class.
	mutable vector<int> m_sortedEdges;
	mutable vector<int> m_intervalMinRank;
	mutable vector<int> m_intervalMaxRank;
};

} // namespace Vertexy
