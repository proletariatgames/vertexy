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

	TFastLookupSet(TFastLookupSet&& other) noexcept
		: m_inSet(move(other.m_inSet))
		, m_elements(move(other.m_elements))
		, m_curStamp(other.m_curStamp)
		, m_indexer(move(other.m_indexer))
	{
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

	TFastLookupSet& operator=(const TFastLookupSet& rhs)
	{
		m_inSet = rhs.m_inSet;
		m_elements = rhs.m_elements;
		m_curStamp = rhs.m_curStamp;
		m_indexer = rhs.m_indexer;

		return *this;
	}

	TFastLookupSet& operator=(TFastLookupSet&& rhs) noexcept
	{
		m_inSet = move(rhs.m_inSet);
		m_elements = move(rhs.m_elements);
		m_curStamp = rhs.m_curStamp;
		m_indexer = move(rhs.m_indexer);

		return *this;
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

	void add(const T& val)
	{
		if (!contains(val))
		{
			markContained(val);
			m_elements.push_back(val);
		}
	}

	void remove(const T& val)
	{
		if (contains(val))
		{
			m_inSet[m_indexer(val)] = 0;
			m_elements.erase_first_unsorted(val);
		}
	}

	template<typename Pred>
	void removeIf(Pred&& pred)
	{
		auto insert = m_elements.begin();
		for (auto it = insert, itEnd = m_elements.end(); it != itEnd; ++it)
		{
			if (!pred(*it))
			{
				swap(*insert, *it);
				++insert;
			}
			else
			{
				m_inSet[m_indexer(*it)] = 0;
			}
		}
		m_elements.erase(insert, m_elements.end());
	}

	void removeAt(int index)
	{
		vxy_sanity(m_inSet[m_indexer(m_elements[index])]);
		m_inSet[m_indexer(m_elements[index])] = 0;
		m_elements.erase_unsorted(m_elements.begin()+index);
	}

	T pop()
	{
		T val = m_elements.back();
		vxy_sanity(m_inSet[m_indexer(val)]);
		m_inSet[m_indexer(val)] = 0;
		m_elements.pop_back();

		return val;
	}

	inline T& back() { return m_elements.back(); }
	inline const T& back() const { return m_elements.back(); }

	inline int size() const { return m_elements.size(); }

	inline auto begin() { return m_elements.begin(); }
	inline auto begin() const { return m_elements.begin(); }
	inline auto end() { return m_elements.end(); }
	inline auto end() const { return m_elements.end(); }

	inline T& operator[](int i) { return m_elements[i]; }
	inline const T& operator[](int i) const { return m_elements[i]; }

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
	vector<T> m_elements;
	uint32_t m_curStamp = 1;
	mutable ToIndexType m_indexer;
};

} // namespace Vertexy