// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "ConstraintTypes.h"

namespace Vertexy
{

// Disjoint Set: Stores a collection of non-overlapping sets of integers
// https://en.wikipedia.org/wiki/Disjoint-set_data_structure
class DisjointSet
{
public:
	DisjointSet()
	{
	}

	explicit DisjointSet(int numItems);

	// Reset to initial state
	void reset(int numItems);

	// Return the key for the set the input value is in
	int find(int val) const;
	// Merge two sets
	void makeUnion(int set1, int set2);
	// Check if the given value is within the given (sub)set
	bool check(int value, int set) const;

	inline int size() const { return m_parents.size(); }

private:
	bool checkRecurse(int value, int cur, int set) const;

	mutable vector<int> m_parents;
};

} // namespace Vertexy