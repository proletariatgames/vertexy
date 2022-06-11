// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "IConstraint.h"
#include "SignedClause.h"
#include "variable/IVariableDatabase.h"

namespace Vertexy
{

class ConstraintSolver;

#define CLAUSE_DEBUG_INFO VERTEXY_SANITY_CHECKS

enum class ENoGood : uint8_t
{
	NoGood
};

/** Constraint of clauses, where at least one clause needs to hold true.
 *  Each clause is a statement "variable X is (not) in D" where D is some set of values.
 */
class ClauseConstraint : public IConstraint
{
public:
	// NOTE: Do not call directly. Not enough memory will be allocated.
	ClauseConstraint(const ConstraintFactoryParams& params, const vector<Literal>& literals, bool isLearned = false);
	
	struct ClauseConstraintFactory
	{
		static ClauseConstraint* construct(const ConstraintFactoryParams& params, const vector<SignedClause>& clauses);
		static ClauseConstraint* construct(const ConstraintFactoryParams& params, ENoGood noGood, const vector<SignedClause>& clauses);
		static ClauseConstraint* construct(const ConstraintFactoryParams& params, const vector<Literal>& lits, bool isLearned = false);

		template <typename... ArgsType,
			typename = enable_if_t< (is_same_v<decay_t<ArgsType>, SignedClause> && ...) >>
		static ClauseConstraint* construct(const ConstraintFactoryParams& params, ENoGood noGood, ArgsType&&... args)
		{
			vector<SignedClause> argsArray = {args...};
			return construct(params, noGood, argsArray);
		}

		template <typename... ArgsType,
			typename = enable_if_t< (is_same_v<decay_t<ArgsType>, SignedClause> && ...) >>
		static ClauseConstraint* construct(const ConstraintFactoryParams& params, ArgsType&&... args)
		{
			vector<SignedClause> argsArray = {args...};
			return construct(params, argsArray);
		}
	};

	using Factory = ClauseConstraintFactory;

	virtual EConstraintType getConstraintType() const override { return EConstraintType::Clause; }
	virtual vector<VarID> getConstrainingVariables() const override;
	virtual bool initialize(IVariableDatabase* db) override { return initialize(db, nullptr); }
	virtual bool initialize(IVariableDatabase* db, IConstraint* outerConstraint) override;
	virtual void reset(IVariableDatabase* db) override;
	virtual bool onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& previousValue, bool& removeWatch) override;
	virtual vector<Literal> explain(const NarrowingExplanationParams& params) const override { return getLiteralsCopy(); }
	virtual bool checkConflicting(IVariableDatabase* db) const override;

	virtual ClauseConstraint* asClauseConstraint() override
	{
		return this;
	}

	inline bool isLocked() const { return m_extendedInfo.get() && m_extendedInfo->lockCount > 0; }

	inline void lock()
	{
		vxy_assert(m_extendedInfo.get());
		vxy_assert(m_extendedInfo->isLearned);
		vxy_assert(m_extendedInfo->lockCount < 0xFFFF);
		++m_extendedInfo->lockCount;
	}

	inline void unlock()
	{
		vxy_assert(m_extendedInfo.get());
		vxy_assert(m_extendedInfo->isLearned);
		vxy_assert(m_extendedInfo->lockCount > 0);
		--m_extendedInfo->lockCount;
	}

	bool makeUnit(IVariableDatabase* db, int literalIndex);

	inline bool isLearned() const { return m_extendedInfo.get() && m_extendedInfo->isLearned; }

	void computeLbd(const class SolverVariableDatabase& db);

	inline int getLBD() const
	{
		vxy_assert(isLearned());
		return m_extendedInfo->LBD;
	}

	inline int getNumLiterals() const { return m_numLiterals; }

	inline const Literal& getLiteral(int num) const
	{
		vxy_assert(num >= 0);
		vxy_assert(num < m_numLiterals);
		return m_literals[num];
	}

	inline const Literal* getLiteralForVariable(VarID variable) const
	{
		for (int i = 0; i < m_numLiterals; ++i)
		{
			if (m_literals[i].variable == variable)
			{
				return &m_literals[i];
			}
		}
		return nullptr;
	}

	bool propagateAndStrengthen(IVariableDatabase* db, vector<Literal>& outLitsRemoved);
	void removeLiteralAt(IVariableDatabase* db, int litIndex);

	void getLiterals(vector<Literal>& outLiterals) const;
	const Literal* getLiterals() const { return m_literals; }
	vector<Literal> getLiteralsCopy() const;

	inline float getActivity() const
	{
		vxy_assert(isLearned());
		// promotion source handles activity on behalf of promoted constraints.
		if (m_extendedInfo->promotionSource != nullptr)
		{
			return m_extendedInfo->promotionSource->getActivity();
		}
		return m_extendedInfo->activity;
	}

	inline float incrementActivity(float increment)
	{
		vxy_assert(isLearned());
		// promotion source handles activity on behalf of promoted constraints.
		if (m_extendedInfo->promotionSource != nullptr)
		{
			return m_extendedInfo->promotionSource->incrementActivity(increment);
		}
		m_extendedInfo->activity += increment;
		return m_extendedInfo->activity;
	}

	inline void scaleActivity(float activityScale)
	{
		vxy_assert(isLearned());
		// When activity is scaled, it is scaled for all constraints. So we
		// don't want to delegate to promotionSource here, otherwise it will be scaled multiple times.
		if (m_extendedInfo->promotionSource == nullptr)
		{
			m_extendedInfo->activity *= activityScale;
		}
	}

	inline bool isPermanent() const { return m_extendedInfo != nullptr ? m_extendedInfo->isPermanent : true; }

	inline void setPermanent()
	{
		vxy_assert(isLearned());
		m_extendedInfo->isPermanent = true;
	}

	inline bool isPromotableToGraph() const
	{
		return m_graphRelationInfo != nullptr && m_graphRelationInfo->getGraph() != nullptr &&
			   m_extendedInfo != nullptr && !m_extendedInfo->isPromoted && !isPromotedFromGraph();
	}

	inline void setPromotedToGraph()
	{
		vxy_assert(isLearned());
		vxy_assert(m_extendedInfo != nullptr);
		m_extendedInfo->isPromoted = true;
	}

	inline bool isPromotedFromGraph() const
	{
		if (m_extendedInfo == nullptr)
		{
			return false;
		}
		return m_extendedInfo->promotionSource != nullptr;
	}

	inline void setPromotionSource(ClauseConstraint* inSource)
	{
		vxy_assert(isLearned());
		vxy_assert(isPromotableToGraph());
		m_extendedInfo->promotionSource = inSource;
	}

	inline void setStepLearned(int step)
	{
		#if CLAUSE_DEBUG_INFO
		vxy_assert(isLearned());
		m_extendedInfo->stepLearned_ForDebugging = step;
		#endif
	}

	using const_iterator = const Literal*;
	const_iterator beginLiterals() const { return m_literals; }
	const_iterator endLiterals() const { return m_literals+m_numLiterals; }

	// not exposed: not really safe to mutate literals after construction.
	// using iterator = Literal*;
	//iterator beginLiterals() { return m_literals; }
	//iterator endLiterals() { return m_literals+m_numLiterals; }

protected:
	// NOTE: We attempt to pack data tightly here. Be careful increasing the size of this class, as it can
	// have drastic effect on cache performance!
	// Stuff that doesn't need to be accessed during hotpath (i.e. propagation) can be put in ExtendedInfo.

	struct ExtendedInfo
	{
		// Our activity indicator. Incremented whenever this clause is involved in a conflict
		float activity = 0;
		// The number of times this clause is the reason for narrowing a variable in the assignment stack.
		// Only valid for learned clauses.
		uint16_t lockCount = 0;
		// The Literal Blocks Distance. Only valid for learned clauses
		// See "Predicting Learnt Clauses Quality in Modern SAT Solvers"
		// https://www.ijcai.org/Proceedings/09/Papers/074.pdf
		uint8_t LBD = 0xFF;

		// Whether this was a learned clause, or an original clause.
		unsigned isLearned : 1;
		// Whether this clause is permanent - will never be purged. Always true if not learned.
		unsigned isPermanent : 1;
		// Whether this clause was promoted to a graph
		unsigned isPromoted : 1;
		// If we were created via a graph promotion, the constraint that we were promoted from.
		ClauseConstraint* promotionSource = nullptr;

		#if CLAUSE_DEBUG_INFO
		int stepLearned_ForDebugging = -1;
		#endif
	};

	// Watchers for first and second clause
	WatcherHandle m_watches[2];
	// Total number of literals. Literals are appended to the end of this class.
	uint16_t m_numLiterals;
	// Extended (cold) information
	unique_ptr<ExtendedInfo> m_extendedInfo;
	// Pointer to beginning of clauses. Construct function allocates enough memory for them.
	Literal m_literals[0];
};

} // namespace Vertexy