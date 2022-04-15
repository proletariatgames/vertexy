// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include <EASTL/sort.h>

namespace Vertexy
{

template<typename T, typename IndexType>
struct DefaultLookupSetToIndex
{
	 IndexType operator()(const T& val) { return val; }
};

/** Set for integral elements that supports fast lookup via table */
template <typename T, bool bExpectValidIndexSize = false, typename IndexType=uint32_t, typename ToIndexType=DefaultLookupSetToIndex<T, IndexType>>
class TFastLookupSet
{
public:
	TFastLookupSet(int indexReserve = 0, const ToIndexType& indexer = {}) : m_indexer(indexer)
	{
		m_inSet.resize(indexReserve, 0);
	}

	void reserve(int amt)
	{
		m_elements.reserve(amt);
	}

	void setIndexSize(int amt)
	{
		m_inSet.clear();
		m_inSet.resize(amt, 0);
		m_curStamp = 1;
	}

	void clear()
	{
		m_elements.clear();

		if (m_curStamp < INT_MAX)
		{
			// Fast case: increment the stamp.
			++m_curStamp;
		}
		else
		{
			// Wrap around the stamp. In this case we need to memset the InSet array.
			// Otherwise there might be stale stamps causing false positives.
			m_curStamp = 1;

			int num = m_inSet.size();
			m_inSet.clear();
			m_inSet.resize(num, 0);
		}
	}

	inline bool empty() const { return m_elements.empty(); }

	inline bool contains(const T& val) const
	{
		IndexType ival = m_indexer(val);
		vxy_sanity(ival >= 0);
		if constexpr (bExpectValidIndexSize)
		{
			return m_inSet[ival] == m_curStamp;
		}
		else
		{
			return ival < m_inSet.size() ? m_inSet[ival] == m_curStamp : false;
		}
	}

	template <class PREDICATE_CLASS>
	inline void sort(const PREDICATE_CLASS& predicate)
	{
		quick_sort(m_elements.begin(), m_elements.end(), predicate);
	}

	void add(T val)
	{
		if (!contains(val))
		{
			markContained(val);
			m_elements.push_back(val);
		}
	}

	void remove(T val)
	{
		if (contains(val))
		{
			m_inSet[m_indexer(val)] = 0;
			m_elements.erase_first_unsorted(val);
		}
	}

	inline int size() const { return m_elements.size(); }

	inline auto begin() { return m_elements.begin(); }
	inline auto begin() const { return m_elements.begin(); }
	inline auto end() { return m_elements.end(); }
	inline auto end() const { return m_elements.end(); }

	inline T& operator[](int i) { return m_elements[i]; }
	inline T operator[](int i) const { return m_elements[i]; }

protected:
	inline void markContained(T val)
	{
		IndexType ival = m_indexer(val);
		vxy_sanity(ival >= 0);
		if constexpr (!bExpectValidIndexSize)
		{
			m_inSet.reserve(ival + 1);
			while (ival >= m_inSet.size())
			{
				m_inSet.push_back(m_curStamp - 1);
			}
		}
		m_inSet[ival] = m_curStamp;
	}

	vector<uint32_t> m_inSet;
	vector<int> m_elements;
	uint32_t m_curStamp = 1;
	mutable ToIndexType m_indexer;
};

} // namespace Vertexy