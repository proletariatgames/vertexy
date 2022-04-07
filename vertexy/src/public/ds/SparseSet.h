// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"

namespace Vertexy
{
/** "Sparse-set" implementation.
 *  Stores a set of integers, with iteration and O(1) membership check.
 *  Supports efficient O(1) backtracking: store the stamp returned by Add() or Remove(), then call Backtrack() with the stamp to roll back any removals. *
 */
template <typename InElementType, bool bInsertion = true>
class TSparseSet
{
public:
	using ElementType = InElementType;

	inline TSparseSet()
		: m_size(0)
	{
	}

	template <bool bOtherInsertion>
	TSparseSet(const TSparseSet<InElementType, bOtherInsertion>& other)
		: m_dense(other.m_dense)
		, m_map(other.m_map)
		, m_size(other.m_size)
	{
	}

	// Initialize
	explicit TSparseSet(int maxValue)
	{
		for (int i = 0; i <= maxValue; ++i)
		{
			m_dense.push_back(i);
			m_map.push_back(i);
		}
		m_size = m_dense.size();
	}

	explicit TSparseSet(const vector<ElementType>& elements)
	{
		for (int i = 0; i != elements.size(); ++i)
		{
			m_dense.push_back(elements[i]);
			ensureSpaceForValue(elements[i]);
			m_map[elements[i]] = i;
		}
		m_size = m_dense.size();
	}

	explicit TSparseSet(std::initializer_list<InElementType> initList)
	{
		int i = 0;
		for (auto it = initList.begin(); it != initList.end(); ++it)
		{
			auto& val = *it;
			m_dense.push_back(val);
			ensureSpaceForValue(val);
			m_map[val] = i++;
		}
		m_size = m_dense.size();
	}

	TSparseSet& operator=(std::initializer_list<InElementType> initList)
	{
		m_dense.clear();
		m_map.clear();

		int i = 0;
		for (auto it = initList.begin(); it != initList.end(); ++it)
		{
			auto& val = *it;
			m_dense.push_back(val);

			ensureSpaceForValue(val);
			m_map[val] = i++;
		}
		m_size = m_dense.size();

		return *this;
	}

	TSparseSet& operator=(const vector<ElementType>& elements)
	{
		m_dense.clear();
		m_map.clear();

		for (int i = 0; i != elements.size(); ++i)
		{
			m_dense.push_back(elements[i]);

			ensureSpaceForValue(elements[i]);
			m_map[elements[i]] = i;
		}
		m_size = m_dense.size();
		return *this;
	}

	TSparseSet& operator=(const TSparseSet& other)
	{
		m_dense = other.m_dense;
		m_map = other.m_map;
		m_size = other.m_size;
		return *this;
	}

	inline bool isValidIndex(int index) const
	{
		return index >= 0 && index < m_size;
	}

	inline void rangeCheck(int index) const
	{
		vxy_assert_msg((index >= 0) & (index < m_size), "TSparseSet index out of bounds: %i from a set of size %i", index, m_size); // & for one branch
	}

	inline int size() const
	{
		return m_size;
	}

	inline void clear()
	{
		m_size = 0;
		m_map.clear();
		m_dense.clear();
	}

	inline void reserve(int amount)
	{
		m_map.reserve(amount);
		m_dense.reserve(amount);
	}

	inline bool empty() const
	{
		return m_size == 0;
	}

	inline bool contains(const ElementType& value) const
	{
		if (value < 0 || value >= m_map.size())
		{
			return false;
		}
		int mappedPosition = m_map[value];
		return mappedPosition >= 0 && mappedPosition < m_size;
	}

	template <bool bEnable = !bInsertion>
	typename eastl::enable_if<bEnable, int>::type remove(ElementType value)
	{
		int prevSize = m_size;

		vxy_sanity(value >= 0);
		if (value < m_map.size())
		{
			const int index = m_map[value];
			const int end = m_size - 1;
			if (index >= 0 && index <= end)
			{
				swap(m_dense[index], m_dense[end]);
				m_map[m_dense[index]] = index;
				m_map[m_dense[end]] = end;

				--m_size;
			}
		}
		return prevSize;
	}

	template <bool bEnable = bInsertion>
	typename eastl::enable_if<bEnable, int>::type add(ElementType value)
	{
		int prevSize = m_size;
		vxy_sanity(value >= 0);

		ensureSpaceForValue(value);

		int index = m_map[value];
		const int end = m_size - 1;

		if (index < 0)
		{
			index = m_map[value] = m_dense.size();
			m_dense.push_back(value);
		}

		if (index > end)
		{
			swap(m_dense[index], m_dense[m_size]);
			m_map[m_dense[index]] = index;
			m_map[m_dense[m_size]] = m_size;
			++m_size;
		}

		return prevSize;
	}

	void backtrack(int stamp)
	{
		vxy_sanity(m_size <= m_dense.size());
		if (bInsertion)
		{
			vxy_sanity(stamp <= m_size);
		}
		else
		{
			vxy_sanity(stamp >= m_size);
		}
		m_size = stamp;
	}

	inline ElementType& operator[](int index)
	{
		rangeCheck(index);
		return m_dense[index];
	}

	inline const ElementType& operator[](int index) const
	{
		rangeCheck(index);
		return m_dense[index];
	}

	inline ElementType& last(int indexFromTheEnd = 0)
	{
		rangeCheck(m_size - indexFromTheEnd - 1);
		return m_dense[m_size - indexFromTheEnd - 1];
	}

	inline const ElementType& last(int indexFromTheEnd = 0) const
	{
		rangeCheck(m_size - indexFromTheEnd - 1);
		return m_dense[m_size - indexFromTheEnd - 1];
	}

	inline int getIndexForValue(const ElementType& value) const
	{
		vxy_sanity(m_map.isValidIndex(value));
		int index = m_map[value];
		return index >= m_size ? -1 : index;
	}

	wstring toString() const
	{
		wstring str = TEXT("[");
		for (int i = 0; i < size(); ++i)
		{
			str.append_sprintf(TEXT("%d"), m_dense[i]);
			if (i != size() - 1)
			{
				str += TEXT(", ");
			}
		}
		str += TEXT("]");
		return str;
	}

	inline auto begin() { return m_dense.begin(); }
	inline auto begin() const { return m_dense.begin(); }
	inline auto end() { return m_dense.end(); }
	inline auto end() const { return m_dense.end(); }

private:
	inline void ensureSpaceForValue(ElementType value)
	{
		while (value >= m_map.size())
		{
			m_map.push_back(-1);
		}
	}

	// All values. The elements in indices [0, Size) are considered in the set.
	vector<ElementType> m_dense;
	// Maps an element's value to where its index in the set.
	vector<int> m_map;
	// The index one past the last element in the set inside of Dyn.
	int m_size;
};

} // namespace Vertexy