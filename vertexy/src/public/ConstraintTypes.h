// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include <EASTL/vector.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/hash_set.h>
#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/functional.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/shared_ptr.h>
#include <EAAssert/eaassert.h>

#include "ds/ValueBitset.h"
#include "util/Logging.h"
#include "util/Asserts.h"

#ifndef TEXT
#define TEXT(s) L ## s
#endif

namespace Vertexy
{

using namespace eastl;

class ConstraintSolver;
class IConstraint;
class IVariableDatabase;

enum class EConstraintType : uint8_t
{
	Clause,
	AllDifferent,
	Cardinality,
	Disjunction,
	Iff,
	Inequality,
	Offset,
	Table,
	Reachability,
	Sum
};

// If set, VariableDBs will cache the state of each variable (solved/unsolved/contradiction), only updating when
// the variable changes. Otherwise it will be recalculated each query.
#define CONSTRAINT_USE_CACHED_STATES 1

// Reference to a variable. Top bit encodes whether that variable is part of a graph in the context it's being used.
class VarID
{
	friend struct eastl::hash<VarID>;
public:
	VarID()
		: m_value(0)
	{
	}

	static const VarID INVALID;

	explicit VarID(uint32_t inValue)
	{
		vxy_assert(inValue > 0);
		m_value = inValue;
	}

	inline VarID(const VarID& other)
	{
		m_value = other.m_value;
	}

	inline VarID(VarID&& other) noexcept
	{
		m_value = other.m_value;
	}

	void reset()
	{
		m_value = 0;
	}

	inline bool isValid() const { return m_value > 0; }
	inline uint32_t raw() const { return m_value; }

	// Note equivalency does not check if variable is in graph!
	inline bool operator==(const VarID& other) const
	{
		return raw() == other.raw();
	}

	inline bool operator!=(const VarID& other) const
	{
		return raw() != other.raw();
	}

	VarID& operator=(const VarID& other) = default;

private:
	uint32_t m_value;
};

static_assert(sizeof(VarID) == sizeof(uint32_t), "Expected FVarID to be 32 bits");

// Reference to a graph constraint (actually an array of constraints)
class GraphConstraintID
{
public:
	GraphConstraintID()
		: m_value(0)
	{
	}

	static const GraphConstraintID INVALID;

	explicit GraphConstraintID(uint32_t inValue)
	{
		vxy_assert(inValue > 0);
		m_value = inValue;
	}

	inline GraphConstraintID(const GraphConstraintID& other)
	{
		m_value = other.m_value;
	}

	inline GraphConstraintID(GraphConstraintID&& other) noexcept
	{
		m_value = other.m_value;
	}

	void reset()
	{
		m_value = 0;
	}

	inline bool isValid() const { return m_value > 0; }
	inline uint32_t raw() const { return m_value; }

	// Note equivalency does not check if variable is in graph!
	inline bool operator==(const GraphConstraintID& other) const
	{
		return raw() == other.raw();
	}

	inline bool operator!=(const GraphConstraintID& other) const
	{
		return raw() != other.raw();
	}

	GraphConstraintID& operator=(const GraphConstraintID& other) = default;

private:
	uint32_t m_value;
};

using SolverDecisionLevel = int32_t;
using SolverTimestamp = int32_t;
using ValueSet = TValueBitset<>;

// Represents a variable and value combination
struct Literal
{
	Literal()
		: variable(VarID::INVALID)
	{
	}

	Literal(const Literal& other) = default;
	Literal(Literal&& other) = default;

	template <typename Allocator>
	explicit Literal(VarID varID, const TValueBitset<Allocator>& values)
		: variable(varID)
		, values(values)
	{
	}

	template <typename Allocator>
	explicit Literal(VarID varID, TValueBitset<Allocator>&& values)
		: variable(varID)
		, values(forward<TValueBitset<Allocator>>(values))
	{
	}

	Literal& operator=(const Literal& rhs)
	{
		variable = rhs.variable;
		values = rhs.values;
		return *this;
	}

	Literal& operator=(Literal&& rhs) noexcept
	{
		variable = rhs.variable;
		values = move(rhs.values);
		return *this;
	}

	inline bool operator==(const Literal& rhs) const
	{
		return variable == rhs.variable && values == rhs.values;
	}

	Literal inverted() const
	{
		return Literal(variable, values.inverted());
	}

	VarID variable;
	ValueSet values;
};

// Parameters passed to constraint explanation functions
struct NarrowingExplanationParams
{
	NarrowingExplanationParams() = delete;

	NarrowingExplanationParams(const ConstraintSolver* inSolver, const IVariableDatabase* inDB, const IConstraint* inConstraint, VarID inVar, const ValueSet& inValues, SolverTimestamp inTimestamp)
		: solver(inSolver)
		, database(inDB)
		, constraint(inConstraint)
		, propagatedVariable(inVar)
		, propagatedValues(inValues)
		, timestamp(inTimestamp)
	{
	}

	const ConstraintSolver* solver;
	const IVariableDatabase* database;
	const IConstraint* constraint;
	VarID propagatedVariable;
	const ValueSet& propagatedValues;
	SolverTimestamp timestamp;
};

// Types of modifications that can be watched on a variable
enum class EVariableWatchType : uint8_t
{
	WatchModification = 0,
	// Trigger for any modification to the variable
	WatchUpperBoundChange = 1,
	// Trigger any time the variable's maximum potential value changes
	WatchLowerBoundChange = 2,
	// Trigger any time the variable's minimum potential value changes
	WatchSolved = 3,
	// Trigger any time the variable becomes solved
	NUM_WATCH_TYPES
};


using ExplainerFunction = function<vector<Literal>(const NarrowingExplanationParams&)>;

using WatcherHandle = uint32_t;
constexpr WatcherHandle INVALID_WATCHER_HANDLE = -1;

template <typename T>
class ValueGuard
{
public:
	ValueGuard() = delete;
	ValueGuard(const ValueGuard&) = delete;
	ValueGuard& operator=(const ValueGuard&) = delete;

	ValueGuard(T& destination, const T& newValue)
		: m_dest(destination)
		, m_oldVal(destination)
	{
		destination = newValue;
	}

	~ValueGuard()
	{
		m_dest = m_oldVal;
	}

private:
	T& m_dest;
	T m_oldVal;
};

///
/// Various STL-style helper functions
///

template <typename InputIterator, typename T>
inline bool contains(InputIterator first, InputIterator last, const T& value)
{
	return eastl::find(first, last, value) != last;
}

template <typename InputIterator, typename Predicate>
inline bool containsPredicate(InputIterator first, InputIterator last, Predicate predicate)
{
	return eastl::find_if(first, last, predicate) != last;
}

template <typename InputIterator, typename T>
int indexOf(InputIterator first, InputIterator last, const T& value)
{
	int idx = 0;
	auto it = first;
	for (; it != last; ++it, ++idx)
	{
		if (value == (*it))
		{
			break;
		}
	}
	return it == last ? -1 : idx;
}

template <typename InputIterator, typename Predicate>
inline int indexOfPredicate(InputIterator first, InputIterator last, Predicate predicate)
{
	int idx = 0;
	auto it = first;
	for (; it != last; ++it, ++idx)
	{
		if (predicate(*it))
		{
			break;
		}
	}
	return it == last ? -1 : idx;
}

} // namespace Vertexy


///
///
/// Add support for hashing tuple<> to EASTL
///
///

namespace eastl
{

inline uint32_t combineHashes(uint32_t a, uint32_t c)
{
	uint32_t b = 0x9e3779b9;
	a += b;
	a -= b;
	a -= c;
	a ^= (c >> 13);
	b -= c;
	b -= a;
	b ^= (a << 8);
	c -= a;
	c -= b;
	c ^= (b >> 13);
	a -= b;
	a -= c;
	a ^= (c >> 12);
	b -= c;
	b -= a;
	b ^= (a << 16);
	c -= a;
	c -= b;
	c ^= (b >> 5);
	a -= b;
	a -= c;
	a ^= (c >> 3);
	b -= c;
	b -= a;
	b ^= (a << 10);
	c -= a;
	c -= b;
	c ^= (b >> 15);
	return c;
}

template <size_t ARG, size_t COUNT>
struct _tuple_hash_helper
{
	template <typename T>
	inline static size_t fold(size_t hash, const T& t)
	{
		eastl::hash<remove_cvref<decltype(get<ARG>(t))>::type> hasher;
		return _tuple_hash_helper<ARG + 1, COUNT>::fold(combineHashes(hash, hasher(get<ARG>(t))), t);
	}
};

template <size_t ARG>
struct _tuple_hash_helper<ARG, ARG>
{
	template <typename T>
	inline static size_t fold(size_t hash, const T& t)
	{
		return hash;
	}
};

// Add hashing for tuple<>
template <typename... Types>
struct hash<tuple<Types...>>
{
	inline size_t operator()(const tuple<Types...>& val) const
	{
		eastl::hash<remove_cvref<decltype(get<0>(val))>::type> hasher;
		size_t first = hasher.operator()(get<0>(val));
		return _tuple_hash_helper<1u, sizeof...(Types)>::fold(first, val);
	}
};

// Hashing for FVarID
template <>
struct hash<Vertexy::VarID>
{
	inline size_t operator()(Vertexy::VarID val) const
	{
		eastl::hash<uint32_t> hasher;
		return hasher(val.m_value);
	}
};

// Hashing for Literal
template<>
struct hash<Vertexy::Literal>
{
	inline size_t operator()(const Vertexy::Literal& lit) const
	{
		hash<Vertexy::VarID> varHash;
		hash<Vertexy::ValueSet> valHash;
		return varHash(lit.variable) | valHash(lit.values);
	}
};

} // names