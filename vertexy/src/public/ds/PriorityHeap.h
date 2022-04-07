// ADAPTED from Minisat Heap.h, copyright follows:
/******************************************************************************************[Heap.h]
Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
Copyright (c) 2007-2010, Niklas Sorensson

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**************************************************************************************************/

#pragma once

#include "ConstraintTypes.h"

namespace Vertexy
{

//=================================================================================================
// A heap implementation with support for decrease/increase key.


template <class K, class Comp>
class TPriorityHeap
{
	vector<K> m_heap; // Heap of Keys
	vector<int> m_indices; // Each Key's position (index) in the Heap
	Comp m_comp; // The heap is a minimum-heap with respect to this comparator

	// Index "traversal" functions
	static inline int left(int i) { return i * 2 + 1; }
	static inline int right(int i) { return (i + 1) * 2; }
	static inline int parent(int i) { return (i - 1) >> 1; }

	void percolateUp(int i)
	{
		K x = m_heap[i];
		int p = parent(i);

		while (i != 0 && m_comp(x, m_heap[p]))
		{
			m_heap[i] = m_heap[p];
			m_indices[m_heap[p]] = i;
			i = p;
			p = parent(p);
		}
		m_heap[i] = x;
		m_indices[x] = i;
	}


	void percolateDown(int i)
	{
		K x = m_heap[i];
		while (left(i) < m_heap.size())
		{
			int child = right(i) < m_heap.size() && m_comp(m_heap[right(i)], m_heap[left(i)]) ? right(i) : left(i);
			if (!m_comp(m_heap[child], x))
			{
				break;
			}
			m_heap[i] = m_heap[child];
			m_indices[m_heap[i]] = i;
			i = child;
		}
		m_heap[i] = x;
		m_indices[x] = i;
	}


public:
	TPriorityHeap(const Comp& c)
		: m_comp(c)
	{
	}

	int size() const { return m_heap.size(); }
	bool empty() const { return m_heap.size() == 0; }
	bool inHeap(K k) const { return k < m_indices.size() && m_indices[k] >= 0; }

	const K& operator[](int index) const
	{
		vxy_assert(index < m_heap.size());
		return m_heap[index];
	}

	K peek() const { return m_heap[0]; }

	void decrease(K k)
	{
		vxy_assert(inHeap(k));
		percolateUp(m_indices[k]);
	}

	void increase(K k)
	{
		vxy_assert(inHeap(k));
		percolateDown(m_indices[k]);
	}

	void reserve(int n)
	{
		m_indices.resize(n, -1);
	}

	// Safe variant of insert/decrease/increase:
	void update(K k)
	{
		if (!inHeap(k))
		{
			insert(k);
		}
		else
		{
			percolateUp(m_indices[k]);
			percolateDown(m_indices[k]);
		}
	}


	void insert(K k)
	{
		if (k >= m_indices.size())
		{
			m_indices.resize(k + 1, -1);
		}
		vxy_assert(!inHeap(k));

		m_indices[k] = m_heap.size();
		m_heap.push_back(k);
		percolateUp(m_indices[k]);
	}


	void remove(K k)
	{
		vxy_assert(inHeap(k));

		int kPos = m_indices[k];
		m_indices[k] = -1;

		if (kPos < m_heap.size() - 1)
		{
			m_heap[kPos] = m_heap.back();
			m_indices[m_heap[kPos]] = kPos;
			m_heap.pop_back();
			percolateDown(kPos);
		}
		else
		{
			m_heap.pop_back();
		}
	}


	K removeMin()
	{
		K x = m_heap[0];
		m_heap[0] = m_heap.back();
		m_indices[m_heap[0]] = 0;
		m_indices[x] = -1;
		m_heap.pop_back();
		if (m_heap.size() > 1)
		{
			percolateDown(0);
		}
		return x;
	}


	// Rebuild the heap from scratch, using the elements in 'ns':
	void build(const vector<K>& ns)
	{
		for (int i = 0; i < m_heap.size(); i++)
		{
			m_indices[m_heap[i]] = -1;
		}
		m_heap.clear();

		for (int i = 0; i < ns.size(); i++)
		{
			m_indices[ns[i]] = i;
			m_heap.push_back(ns[i]);
		}

		for (int i = m_heap.size() / 2 - 1; i >= 0; i--)
		{
			percolateDown(i);
		}
	}

	void clear()
	{
		// TODO: shouldn't the 'indices' map also be dispose-cleared?
		for (int i = 0; i < m_heap.size(); i++)
		{
			m_indices[m_heap[i]] = -1;
		}
		m_heap.clear();
	}
};

} // namespace Vertexy