// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include <EASTL/bonus/ring_buffer.h>

namespace csolver
{
/** Fixed size queue that keeps a running sum of elements inside it
 *  Elements are removed from the queue in FIFO order
 */
template <typename T>
class TAveragingBoundedQueue
{
	using BufferType = ring_buffer<T>;

public:
	TAveragingBoundedQueue(int size)
		: m_buffer(size)
		, m_currentSum(0)
	{
	}

	// Not yet implemented
	TAveragingBoundedQueue(const TAveragingBoundedQueue& rhs) = delete;
	TAveragingBoundedQueue(TAveragingBoundedQueue&& rhs) = delete;
	TAveragingBoundedQueue& operator=(const TAveragingBoundedQueue& rhs) = delete;
	TAveragingBoundedQueue& operator=(const TAveragingBoundedQueue&& rhs) = delete;
	bool operator==(const TAveragingBoundedQueue& rhs) const = delete;

	/** Maximum size of the queue.
	 *  If new items are added to the end of the queue after capacity is reached, items at the front
	 *  of the queue are popped off to make room.
	 */
	int getCapacity() const
	{
		return m_buffer.capacity();
	}

	/** Number of items currently in the queue */
	int size() const
	{
		return m_buffer.size();
	}

	/** Returns whether we're at capacity */
	bool atCapacity() const { return size() == getCapacity(); }

	/** Empties the queue without deallocating memory */
	void clear()
	{
		m_buffer.clear();
		m_currentSum = 0;
	}

	/** Push a new element onto the back of the queue, potentially popping off an element
	 *  from the front of the queue to make room.
	 */
	void pushBack(const T& element)
	{
		if (atCapacity())
		{
			popFront();
		}
		m_currentSum += element;
		m_buffer.push_back(element);
	}

	/** Removes an item from the front of the queue */
	T popFront()
	{
		T popped = m_buffer.front();
		m_buffer.pop_front();

		m_currentSum -= popped;
		return popped;
	}

	/** Gets the sum of all elements currently in the queue */
	T getSum() const
	{
		return m_currentSum;
	}

	/** Returns the average of all elements currently in the queue */
	T getAverage() const
	{
		return m_currentSum / T(size());
	}

	/** Returns the average, explicitly as floating point */
	float getFloatAverage() const
	{
		return float(m_currentSum) / float(size());
	}

protected:
	BufferType m_buffer;
	T m_currentSum;
};

} // namespace csolver