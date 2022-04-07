#include "ds/DisjointSet.h"

using namespace Vertexy;

DisjointSet::DisjointSet(int numItems)
{
	reset(numItems);
}

void DisjointSet::reset(int numItems)
{
	m_parents.clear();
	m_parents.resize(numItems);
	for (int i = 0; i < numItems; ++i)
	{
		m_parents[i] = i;
	}
}

int DisjointSet::find(int val) const
{
	if (m_parents[val] != val)
	{
		m_parents[val] = find(m_parents[val]);
	}
	return m_parents[val];
}

bool DisjointSet::check(int value, int set) const
{
	return checkRecurse(value, value, set);
}

bool DisjointSet::checkRecurse(int value, int cur, int set) const
{
	if (cur == set)
	{
		return true;
	}
	else if (m_parents[cur] == cur)
	{
		return false;
	}
	return checkRecurse(value, m_parents[value], set);
}

void DisjointSet::makeUnion(int setX, int setY)
{
	int xRoot = find(setX);
	int yRoot = find(setY);

	if (xRoot == yRoot)
	{
		return;
	}

	if (xRoot < yRoot)
	{
		m_parents[yRoot] = xRoot;
	}
	else
	{
		m_parents[xRoot] = yRoot;
	}
}
