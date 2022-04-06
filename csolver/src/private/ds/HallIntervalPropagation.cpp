// Copyright Proletariat, Inc. All Rights Reserved.
#include "ds/HallIntervalPropagation.h"
#include "ConstraintTypes.h"
#include <EASTL/sort.h>

using namespace csolver;

#define SANITY_CHECK CS_SANITY_CHECKS

constexpr int TailSentinelOffset = 4;

HallIntervalPropagation::HallIntervalPropagation(int maxValue)
{
	m_maxValue = maxValue;
	m_minValue = -m_maxValue - 1;
	m_capacityPartialSums.resize(m_maxValue + 2);

	m_capacityPartialSums[0] = 0;
	for (int i = 1; i < m_capacityPartialSums.size(); ++i)
	{
		m_capacityPartialSums[i] = i;
	}
}

HallIntervalPropagation::HallIntervalPropagation(const vector<int>& capacities)
{
	m_maxValue = capacities.size() - 1;
	m_minValue = -m_maxValue - 1;
	m_capacityPartialSums.resize(m_maxValue + 2);

	int total = 0;
	m_capacityPartialSums[0] = 0;
	for (int i = 1; i < m_capacityPartialSums.size(); ++i)
	{
		total += capacities[i - 1];
		m_capacityPartialSums[i] = total;
	}
}

// Search forward from X -> A[X] -> A[A[X] ..., stopping when A[X] stops increasing in value.
// Returns the largest value found.
static int arrayTree_FollowPath(const vector<int>& a, int x)
{
	int nextX = a[x];
	while (nextX > x)
	{
		x = nextX;
		nextX = a[x];
	}
	return x;
}

// With I=StartIndex, set A[I], A[A[I], ... to AssignValue, until I == EndIndex.
static void arrayTree_SetPath(vector<int>& a, int startIndex, int endIndex, int assignValue)
{
	int index = startIndex;
	while (index != endIndex)
	{
		int nextIndex = a[index];
		a[index] = assignValue;
		index = nextIndex;
	}
}

// See "A Fast and Simple Algorithm for Bounds Consistency of the AllDifferent Constraint" Lopez-Ortiz et. al.
// This is a performance improvement over the Puget algorithm in the typical case.
// https://cs.uwaterloo.ca/~vanbeek/Publications/ijcai03.pdf
bool HallIntervalPropagation::checkAndPrune(vector<Interval>& intervals, const function<bool(int, int)>& callback) const
{
	const int numVar = intervals.size();

	//
	// Sort all intervals by increasing max
	//

	quick_sort(intervals.begin(), intervals.end(), [&](auto& lhs, auto& rhs)
	{
		return lhs.maxValue < rhs.maxValue;
	});

	// Update SortedEdges/IntervalMinRank/IntervalMaxRank
	bool inverted = createUniqueEdges(intervals);

	vector<int> predecessors;
	predecessors.resize(m_sortedEdges.size());
	vector<int> capacities;
	capacities.resize(m_sortedEdges.size());
	vector<int> hallIntervalIndices;
	hallIntervalIndices.resize(m_sortedEdges.size());

	capacities[0] = 0;
	predecessors[0] = 0;
	hallIntervalIndices[0] = 0;
	for (int i = 1; i < m_sortedEdges.size(); ++i)
	{
		predecessors[i] = i - 1;
		hallIntervalIndices[i] = i - 1;

		if (i == 1)
		{
			// handle head sentinel
			if (!inverted)
			{
				capacities[i] = getCapacityForInterval(0, m_sortedEdges[i]) - m_sortedEdges[0] - 1;
			}
			else
			{
				capacities[i] = (-m_sortedEdges[i - 1] - m_maxValue - 1) + getCapacityForInterval(-m_sortedEdges[i], m_maxValue);
			}
		}
		else if (i == m_sortedEdges.size() - 1)
		{
			// handle tail sentinel
			if (!inverted)
			{
				if (m_sortedEdges[i - 1] <= m_maxValue)
				{
					capacities[i] = (m_sortedEdges[i] - m_maxValue - 1) + getCapacityForInterval(m_sortedEdges[i - 1], m_maxValue);
				}
				else
				{
					capacities[i] = m_sortedEdges[i] - m_sortedEdges[i - 1];
				}
			}
			else
			{
				if (m_sortedEdges[i - 1] <= 0)
				{
					capacities[i] = m_sortedEdges[i] - 1 + getCapacityForInterval(0, -m_sortedEdges[i - 1]);
				}
				else
				{
					capacities[i] = m_sortedEdges[i] - m_sortedEdges[i - 1];
				}
			}
		}
		else
		{
			capacities[i] = getCapacityForInterval(m_sortedEdges[i - 1], m_sortedEdges[i] - 1);
		}
	}

	for (int i = 0; i < numVar; ++i)
	{
		// x,y respectively point to the min/max for this interval within the SortedEdges array.
		const int x = m_intervalMinRank[i];
		const int y = m_intervalMaxRank[i];

		// Find the critical set the min bound lies in
		int z = arrayTree_FollowPath(predecessors, x + 1);
		const int j = predecessors[z];
		// reduce the capacity of the set
		capacities[z]--;

		// If we're out of capacity
		if (capacities[z] == 0)
		{
			// later interval dominated by earlier interval
			predecessors[z] = z + 1;
			z = arrayTree_FollowPath(predecessors, predecessors[z]);
			predecessors[z] = j;
		}

		const int boundaryWidth = m_sortedEdges[z] - m_sortedEdges[y];

		// Path compression (just point everything from [x+1 - z] to point to z)
		arrayTree_SetPath(predecessors, x + 1, z, z);

		if (capacities[z] < boundaryWidth)
		{
			// over capacity in this interval.
			return false;
		}

		if (hallIntervalIndices[x] > x)
		{
			//
			// This is part of a hall interval; we need to exclude values inside the interval.
			//

			// Find the beginning of the hall interval
			const int w = arrayTree_FollowPath(hallIntervalIndices, hallIntervalIndices[x]);
			if (!callback(intervals[i].key, m_sortedEdges[w]))
			{
				return false;
			}
			// path compression
			arrayTree_SetPath(hallIntervalIndices, x, w, w);
		}

		if (capacities[z] == boundaryWidth)
		{
			// New Hall Interval
			arrayTree_SetPath(hallIntervalIndices, hallIntervalIndices[y], j - 1, y);
			hallIntervalIndices[y] = j - 1;
		}
	}

	return true;
}

bool HallIntervalPropagation::createUniqueEdges(const vector<Interval>& intervals) const
{
	const int numVar = intervals.size();

	//
	// Sort the indices of intervals by increasing min. The input Intervals is already sorted by increasing max.
	//

	fixed_vector<int, 8> minSortedIndices;
	minSortedIndices.reserve(numVar);
	for (int i = 0; i < numVar; ++i)
	{
		minSortedIndices.push_back(i);
	}

	quick_sort(minSortedIndices.begin(), minSortedIndices.end(), [&](int lhs, int rhs)
	{
		return intervals[lhs].minValue < intervals[rhs].minValue;
	});

	//
	// Create a sorted set of boundaries (the set of all mins and maxs for each variable)
	//
	m_sortedEdges.clear();

	m_intervalMinRank.resize(numVar);
	m_intervalMaxRank.resize(numVar);

	// Add head sentinel.
	m_sortedEdges.push_back(m_minValue);

	// Add first value.
	bool inverted = intervals[minSortedIndices[0]].minValue < 0;
	m_sortedEdges.push_back(intervals[minSortedIndices[0]].minValue);

	// Create the sorted list. Since we have intervals sorted by both their min value and their max value,
	// we keep a pointer to the current one of each type and add the smaller value each iteration.
	int i = 0, j = 0;
	while (i < numVar && j < numVar)
	{
		const int iv = intervals[minSortedIndices[i]].minValue;
		const int jv = intervals[j].maxValue + 1;
		cs_assert(iv >= m_minValue);
		cs_assert(iv <= m_maxValue);

		if (iv == jv)
		{
			if (m_sortedEdges.back() != iv)
			{
				cs_assert(m_sortedEdges.back() < iv);
				m_sortedEdges.push_back(iv);
			}
			m_intervalMinRank[minSortedIndices[i]] = m_sortedEdges.size() - 1;
			m_intervalMaxRank[j] = m_sortedEdges.size() - 1;

			++i;
			++j;
		}
		else if (iv < jv)
		{
			if (m_sortedEdges.back() != iv)
			{
				cs_assert(m_sortedEdges.back() < iv);
				m_sortedEdges.push_back(iv);
			}
			m_intervalMinRank[minSortedIndices[i]] = m_sortedEdges.size() - 1;
			++i;
		}
		else
		{
			cs_assert(jv < iv);
			if (m_sortedEdges.back() != jv)
			{
				cs_assert(m_sortedEdges.back() < jv);
				m_sortedEdges.push_back(jv);
			}
			m_intervalMaxRank[j] = m_sortedEdges.size() - 1;
			++j;
		}
	}

	// add any left over
	while (i < numVar)
	{
		const int iv = intervals[minSortedIndices[i]].minValue;
		if (m_sortedEdges.back() != iv)
		{
			cs_assert(m_sortedEdges.back() < iv);
			m_sortedEdges.push_back(iv);
		}
		m_intervalMinRank[i] = m_sortedEdges.size() - 1;
		++i;
	}

	while (j < numVar)
	{
		const int jv = intervals[j].maxValue + 1;
		if (m_sortedEdges.back() != jv)
		{
			cs_assert(m_sortedEdges.back() < jv);
			m_sortedEdges.push_back(jv);
		}
		m_intervalMaxRank[j] = m_sortedEdges.size() - 1;
		++j;
	}

	// add tail sentinel
	m_sortedEdges.push_back(m_maxValue + TailSentinelOffset);

	#if (SANITY_CHECK)
	for (auto& interval : intervals)
	{
		cs_assert(contains(m_sortedEdges.begin(), m_sortedEdges.end(), interval.minValue));
		cs_assert(contains(m_sortedEdges.begin(), m_sortedEdges.end(), interval.maxValue+1));
	}

	for (int k = 1; k < m_sortedEdges.size(); ++k)
	{
		cs_assert(m_sortedEdges[k] > m_sortedEdges[k-1]);
	}
	#endif

	return inverted;
}

#undef SANITY_CHECK
