#pragma once
#include "ConstraintTypes.h"
#include "EASTL/vector.h"
#include "util/Asserts.h"

namespace Vertexy
{
template<typename InElementType>
struct TTimestampAndValue
{
	InElementType value;
	SolverTimestamp timestamp;
};

template<typename InElementType>
class TBacktrackableValue
{
public:
	TBacktrackableValue() {}

	void set(SolverTimestamp timestamp, InElementType&& value)
	{
		vxy_assert(this->timesAndValues.size() == 0 || this->timesAndValues.back().timestamp <= timestamp);
		this->timesAndValues.push_back({ move(value), timestamp });
	}

	void set(SolverTimestamp timestamp, const InElementType& value)
	{
		vxy_assert(this->timesAndValues.size() == 0 || this->timesAndValues.back().timestamp <= timestamp);
		this->timesAndValues.push_back({ value, timestamp });
	}

	bool hasValue()
	{
		return this->timesAndValues.size() > 0;
	}

	const InElementType& get() const
	{
		return this->timesAndValues.back().value;
	}

	int getIndexBefore(SolverTimestamp timestamp) const
	{
		auto size = this->timesAndValues.size();
		while (size > 0 && this->timesAndValues[size - 1].timestamp >= timestamp)
		{
			size--;
		}
		return size - 1;
	}

	const InElementType& getValueForIndex(int index) const
	{
		return this->timesAndValues[index].value;
	}

	const InElementType& getTimestampForIndex(int index) const
	{
		return this->timesAndValues[index].timestamp;
	}

	const SolverTimestamp getTimestamp()
	{
		return this->timesAndValues.back().timestamp;
	}

	void backtrackUntil(SolverTimestamp timestamp)
	{
		auto size = this->timesAndValues.size();
		while (size > 0 && this->timesAndValues[size - 1].timestamp > timestamp)
		{
			size--;
			this->timesAndValues.pop_back();
		}
	}

private:
	vector<TTimestampAndValue<InElementType>> timesAndValues;
};
}
