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


enum class EUnaryOperatorType
{
	Negate
};

enum class EBinaryOperatorType
{
	LessThan,
	LessThanEq,
	GreaterThan,
	GreaterThanEq,
	Equality,
	Inequality,
	Multiply,
	Divide,
	Add,
	Subtract
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

	VarID(const VarID& other) = default;
	VarID(VarID&& other) = default;

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
	VarID& operator=(VarID&& other) = default;

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
		if (this == &rhs) return true;
		return variable == rhs.variable && values == rhs.values;
	}
	inline bool operator!=(const Literal& rhs) const { return !(operator==(rhs)); }

	Literal inverted() const
	{
		return Literal(variable, values.inverted());
	}

	bool isValid() const { return variable.isValid(); }

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

// Interface for various classes that can provide the domain for a variable.
class IVariableDomainProvider
{
public:
	virtual ~IVariableDomainProvider() {}
	virtual const class SolverVariableDomain& getDomain(VarID varID) const = 0;
};

using ExplainerFunction = function<vector<Literal>(const NarrowingExplanationParams&)>;

using WatcherHandle = uint32_t;
constexpr WatcherHandle INVALID_WATCHER_HANDLE = -1;

//
// Utility class to change a variable value, restoring it to its original value once this object
// has left scope. E.g.
//	TValueGuard<bool> guard(insideCriticalArea, true);
template <typename T>
class TValueGuard
{
public:
	TValueGuard() = delete;
	TValueGuard(const TValueGuard&) = delete;
	TValueGuard(TValueGuard&&) = delete;
	TValueGuard& operator=(const TValueGuard&) = delete;
	TValueGuard& operator=(TValueGuard&&) = delete;

	TValueGuard(T& destination, const T& newValue)
		: m_dest(destination)
		, m_oldVal(destination)
	{
		destination = newValue;
	}

	~TValueGuard()
	{
		m_dest = m_oldVal;
	}

private:
	T& m_dest;
	T m_oldVal;
};

//
// Utility class that calls a function when it leaves scope.
//
template<typename Fun>
class TScopeExitCallback
{
public:
	TScopeExitCallback() = delete;
	TScopeExitCallback(const TScopeExitCallback&) = delete;
	TScopeExitCallback(TScopeExitCallback&&) = delete;
	TScopeExitCallback& operator=(const TScopeExitCallback&) = delete;
	TScopeExitCallback& operator=(TScopeExitCallback&&) = delete;
	
	explicit TScopeExitCallback(Fun&& fun) : m_fun(forward<Fun>(fun)) {}

	~TScopeExitCallback()
	{
		m_fun();
	}

protected:
	Fun m_fun;
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

// Utility function to combine two hashes values into one.
// Can be chained together, e.g.
//		int h = hash_combine(a, b);
//		h = hash_combine(h, c);
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

// Used for hash_map/etc where the key is a pointer.
// Pass this as the hash function to hash the thing pointed at, rather than the pointer itself.
template<typename T, typename NEXT=eastl::hash<T>>
struct pointer_value_hash
{
	template<typename PTR_TYPE>
	size_t operator()(const PTR_TYPE& val) const
	{
		return NEXT()(*val);
	}
};

// Used for hash_map/etc where the key type has a "size_t hash() const" function.
struct call_hash
{
	template<typename T>
	size_t operator()(const T& val) const
	{
		return val.hash();
	}
};

// Used for equality of containers of pointers, where you want to call operator== on the
// object being pointed at, rather than comparing the pointer values themselves.
struct pointer_value_equality
{
	template<typename PTR_TYPE_1, typename PTR_TYPE_2>
	bool operator()(const PTR_TYPE_1& a, const PTR_TYPE_2& b) const
	{
		return *a == *b;
	}
};

//
// When an argument of this type if passed into ConstraintSolver::makeGraphConstraint, any arguments
// that do not resolve for a particular vertex are removed from the array for that vertex's constraint.
//
// !!NOTE!! Currently, using GraphCulledVector disables graph-based learning for any
// individual constraints where elements are culled from the vector. 
//
template<typename T>
class GraphCulledVector
{
	using ElementVec = vector<pair<T,bool>>;
public:
	GraphCulledVector() {}
	GraphCulledVector(const GraphCulledVector& rhs)
		: m_internal(rhs.m_internal)
	{
	}
	GraphCulledVector(GraphCulledVector&& rhs) noexcept
		: m_internal(move(rhs.m_internal))
	{		
	}
	explicit GraphCulledVector(const ElementVec& vec)
		: m_internal(vec)
	{		
	}
	explicit GraphCulledVector(ElementVec&& vec)
		: m_internal(move(vec))
	{
	}
	
	~GraphCulledVector() {}

	template<typename Iterator>
	static GraphCulledVector allOptional(Iterator beginIt, Iterator endIt)
	{
		GraphCulledVector out;
		for (Iterator it = beginIt; it != endIt; ++it)
		{
			out.push_back(make_pair(*it, false));
		}
		return out;
	}

	static GraphCulledVector allOptional(std::initializer_list<T> initList)
	{
		GraphCulledVector out;
		for (auto i = initList.begin(); i != initList.end(); ++i)
		{
			out.push_back(make_pair(*i, false));
		}
		return out;
	}
	
	GraphCulledVector& operator=(const GraphCulledVector& rhs)
	{
		m_internal = rhs.m_internal;
		return *this;
	}
	GraphCulledVector& operator=(GraphCulledVector&& rhs) noexcept
	{
		m_internal = move(rhs.m_internal);
		return *this;
	}

	GraphCulledVector& operator=(const ElementVec& rhs)
	{
		m_internal = rhs;
		return *this;
	}
	GraphCulledVector& operator=(ElementVec&& rhs) noexcept
	{
		m_internal = move(rhs);
		return *this;
	}
	
	const ElementVec& getInternal() const { return m_internal; }

	void push_back_optional(const T& optional) { push_back(make_pair(optional, false)); }
	void push_back_optional(T&& optional) { push_back(make_pair(move(optional), false)); }
	void push_back_required(const T& required) { push_back(make_pair(required, true)); }
	void push_back_required(T&& required) { push_back(make_pair(move(required), true)); }
	
	void push_back(const pair<T, bool>& element) { m_internal.push_back(element); }
	void push_back(pair<T,bool>&& element) { m_internal.push_back(move(element)); }

	typename ElementVec::iterator begin() { return m_internal.begin(); }
	typename ElementVec::iterator end() { return m_internal.end(); }
	typename ElementVec::const_iterator begin() const { return m_internal.cbegin(); }
	typename ElementVec::const_iterator end() const { return m_internal.cend(); }
	typename ElementVec::const_iterator cbegin() const { return m_internal.cbegin(); }
	typename ElementVec::const_iterator cend() const { return m_internal.cend(); }
	
protected:
	ElementVec m_internal;
};

} // namespace Vertexy


///
///
/// Add support for hashing tuple<> to EASTL
///
///

namespace eastl
{

template <size_t ARG, size_t COUNT>
struct _tuple_hash_helper
{
	template <typename T>
	inline static size_t fold(size_t hash, const T& t)
	{
		eastl::hash<remove_cvref<decltype(get<ARG>(t))>::type> hasher;
		return _tuple_hash_helper<ARG + 1, COUNT>::fold(Vertexy::combineHashes(hash, hasher(get<ARG>(t))), t);
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

// to_wstring for Literal
inline wstring to_wstring(const Vertexy::Literal& lit)
{
	return {wstring::CtorSprintf(), TEXT("%d=%s"), lit.variable.raw(), lit.values.toString().c_str()};
}

// to_wstring for vectors
template<typename T>
inline wstring to_wstring(const vector<T>& vec)
{
	wstring out = TEXT("[");
	for (int i = 0; i < vec.size(); ++i)
	{
		if (i > 0) out += TEXT(", ");
		out += eastl::to_wstring(vec[i]);
	}
	out += TEXT("]");
	return out;
}

} // names