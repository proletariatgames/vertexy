// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "constraints/ISolverConstraint.h"

namespace Vertexy
{
class ConstraintSolver;
class ISolverConstraint;
class IVariableWatchSink;

/** Abstract class for access/modification of variables. */
class IVariableDatabase
{
	//
	// Overridables
	//

	protected:
	/** Optionally override to support adding a variable to the database. */
	virtual VarID addVariableImpl(const wstring& name, int domainSize, const vector<int>& potentialValues)
	{
		vxy_fail();
		return VarID::INVALID;
	}

	/** Override to return a writeable value set of current domain of the variable */
	virtual ValueSet& lockVariableImpl(VarID varID) = 0;

	/** Override to respond when a locked variable is unlocked. If the value was actually changed,
	 *  bChanged will be set to true and ChangeExplainer will be a functor that can explain why
	 *  values were removed.
	 */
	virtual void unlockVariableImpl(VarID varID, bool wasChanged, ISolverConstraint* constraint, ExplainerFunction explainerFn) = 0;

	/** Optional override to receive notification when a variable contradiction occurred (i.e potential values reduced to empty set) */
	virtual void onContradiction(VarID varID, ISolverConstraint* constraint, const ExplainerFunction& explainer)
	{
	}

public:
	/** Override to return the current decision level of the solver. */
	virtual SolverDecisionLevel getDecisionLevel() const = 0;

	/** Override to return the current timestamp of the solver. */
	virtual SolverTimestamp getTimestamp() const = 0;

	/** Override to return the decision level of a given variable (i.e. which level chose this variable to decide on).
	 *  Return 0 if the variable has not been used in a decision yet.
	 */
	virtual SolverDecisionLevel getDecisionLevelForVariable(VarID varID) const = 0;

	/** Return the timestamp of the last time this variable was modified */
	virtual SolverTimestamp getLastModificationTimestamp(VarID variable) const = 0;

	/** Return the decision level corresponding to the timestamp */
	virtual SolverDecisionLevel getDecisionLevelForTimestamp(SolverTimestamp timestamp) const = 0;

	/** Override to return the current (read-only) potential values for the given variable */
	virtual const ValueSet& getPotentialValues(VarID varID) const = 0;

	/** Override to return the initial potential values for the given variable */
	virtual const ValueSet& getInitialValues(VarID varID) const = 0;

	/** Add a constraint to the constraint propagation queue */
	virtual void queueConstraintPropagation(ISolverConstraint* constraint) = 0;

	/** Called when a constraint has indicated it is fully satisfied. This should only be called
	 *  when no further narrowing of variables would cause the constraint to become unsatisfied.
	 *  However, a fully satisfied constraint can become unsatisfied upon backtracking.
	 */
	virtual void markConstraintFullySatisfied(ISolverConstraint* constraint)
	{
	}

	/** Add a watcher for a variable */
	virtual WatcherHandle addVariableWatch(VarID varID, EVariableWatchType watchType, IVariableWatchSink* sink) = 0;

	/** Add a watcher for when a variable is no longer any of the specified values */
	virtual WatcherHandle addVariableValueWatch(VarID varID, const ValueSet& values, IVariableWatchSink* sink) = 0;

	/** Mark a given variable-value watch to be added if we every backtrack the current decision level */
	virtual void disableWatcherUntilBacktrack(WatcherHandle handle, VarID varID, IVariableWatchSink* sink) = 0;

	/** Remove a watcher for a variable */
	virtual void removeVariableWatch(VarID varID, WatcherHandle handle, IVariableWatchSink* sink) = 0;

	/** Get the value of the variable (and optional modification timestamp) before the given timestamp */
	virtual const ValueSet& getValueBefore(VarID variable, SolverTimestamp timestamp, SolverTimestamp* outTimestamp = nullptr) const = 0;
	/** Get the value of the variable at or after the specified timestamp. */
	virtual const ValueSet& getValueAfter(VarID variable, SolverTimestamp timestamp) const = 0;
	/** Given a variable and timestamp, return the most recent time it was modified prior to that timestamp. */
	virtual SolverTimestamp getModificationTimePriorTo(VarID variable, SolverTimestamp timestamp) const = 0;
	/** Get the solver associated with this database. */
	virtual const ConstraintSolver* getSolver() const = 0;

	/** Optional override to indicate whether initial arc consistency has been established yet. Only the main
	*  variable db should need to override this.
	*/
	virtual bool hasFinishedInitialArcConsistency() const
	{
		return true;
	}

	//
	// Built-in functionality
	//

	IVariableDatabase()
	{
		// Add dummy for 0 (invalid) variable
		m_states.push_back(EVariableState::Contradiction);
	}

	virtual ~IVariableDatabase()
	{
	}

	virtual VarID addVariable(wstring name, int domainSize, const vector<int>& potentialValues)
	{
		VarID varId = addVariableImpl(name, domainSize, potentialValues);
		#if CONSTRAINT_USE_CACHED_STATES
		vxy_assert(varId.raw() == m_states.size());
		m_states.push_back(EVariableState::Unknown);
		#endif
		++m_numVariables;
		return varId;
	}

	inline int getNumVariables() const
	{
		return m_numVariables;
	}

	inline bool isInContradiction(VarID varID) const
	{
		vxy_assert(varID.isValid());
		return maybeUpdateState(varID) == EVariableState::Contradiction;
	}

	inline bool isSolved(VarID varID) const
	{
		vxy_assert(varID.isValid());
		return maybeUpdateState(varID) == EVariableState::Solved;
	}

	inline bool isSolved(VarID varID, int& solvedValue) const
	{
		vxy_assert(varID.isValid());
		return getPotentialValues(varID).isSingleton(solvedValue);
	}

	inline int getSolvedValue(VarID varID) const
	{
		vxy_assert(isSolved(varID));
		return getPotentialValues(varID).indexOf(true);
	}

	inline int getDomainSize(VarID varID) const
	{
		vxy_assert(varID.isValid());
		return getPotentialValues(varID).size();
	}

	inline bool isPossible(VarID varID, int value) const
	{
		vxy_assert(varID.isValid());
		auto& values = getPotentialValues(varID);
		return value >= 0 && value < values.size() ? values[value] : false;
	}

	template <typename T, int N>
	inline bool anyPossible(VarID varID, const TValueBitset<T, N>& values) const
	{
		vxy_assert(varID.isValid());
		return getPotentialValues(varID).anyPossible(values);
	}

	inline int getMinimumPossibleValue(VarID varID) const
	{
		vxy_assert(varID.isValid());
		return getPotentialValues(varID).indexOf(true);
	}

	inline int getMaximumPossibleValue(VarID varID) const
	{
		vxy_assert(varID.isValid());
		return getPotentialValues(varID).lastIndexOf(true);
	}

	// Default explanation for propagation. The explanation is guaranteed to be assertive (i.e. will cause backtracking) but
	// is not necessarily the tightest explanation possible. (It is generally rather poor.)
	static vector<Literal> defaultExplainer(const NarrowingExplanationParams& params);

	template <typename T, int N>
	bool excludeValues(VarID varID, const TValueBitset<T, N>& valuesToExclude, ISolverConstraint* origin, ExplainerFunction explainer = defaultExplainer)
	{
		vxy_assert(varID.isValid());
		bool removed = lockVariable(varID).excludeCheck(valuesToExclude);
		unlockVariable(varID, removed, origin, move(explainer));
		return checkContradiction(varID, origin, explainer);
	}

	bool excludeValue(VarID varID, int value, ISolverConstraint* origin, ExplainerFunction explainer = defaultExplainer)
	{
		vxy_assert(varID.isValid());
		ValueSet& values = lockVariable(varID);
		bool removed = false;
		if (value < values.size() && values[value] == true)
		{
			values[value] = false;
			removed = true;
		}

		unlockVariable(varID, removed, origin, move(explainer));
		return checkContradiction(varID, origin, explainer);
	}

	template <typename T, int N>
	bool constrainToValues(VarID varID, const TValueBitset<T, N>& constrainedValues, ISolverConstraint* origin, ExplainerFunction explainer = defaultExplainer)
	{
		vxy_assert(varID.isValid());
		bool removed = lockVariable(varID).intersectCheck(constrainedValues);
		unlockVariable(varID, removed, origin, move(explainer));
		return checkContradiction(varID, origin, explainer);
	}

	bool constrainToValue(VarID varID, int value, ISolverConstraint* origin, ExplainerFunction explainer = defaultExplainer)
	{
		vxy_assert(varID.isValid());
		bool removed = false;
		ValueSet& values = lockVariable(varID);

		if (value >= values.size())
		{
			values.setZeroed();
			removed = true;
		}
		else
		{
			ValueSet newValueSet;
			newValueSet.pad(getDomainSize(varID), false);
			newValueSet[value] = true;

			removed = values.intersectCheck(newValueSet);
		}

		unlockVariable(varID, removed, origin, move(explainer));
		return checkContradiction(varID, origin, explainer);
	}

	bool excludeValuesLessThan(VarID varID, int value, ISolverConstraint* origin, ExplainerFunction explainer = defaultExplainer)
	{
		vxy_assert(varID.isValid());
		ValueSet& values = lockVariable(varID);

		bool removed = false;
		for (int i = 0; i < value; ++i)
		{
			if (values[i] == true)
			{
				removed = true;
				values[i] = false;
			}
		}

		unlockVariable(varID, removed, origin, move(explainer));
		return checkContradiction(varID, origin, explainer);
	}

	bool excludeValuesGreaterThan(VarID varID, int value, ISolverConstraint* origin, ExplainerFunction explainer = defaultExplainer)
	{
		vxy_assert(varID.isValid());
		ValueSet& values = lockVariable(varID);
		bool removed = false;
		for (int i = value + 1; i < values.size(); ++i)
		{
			if (values[i] == true)
			{
				removed = true;
				values[i] = false;
			}
		}

		unlockVariable(varID, removed, origin, move(explainer));
		return checkContradiction(varID, origin, explainer);
	}

protected:
	enum class EVariableState : uint8_t
	{
		Unknown,
		Solved,
		Unsolved,
		Contradiction
	};

	inline ValueSet& lockVariable(VarID varID)
	{
		vxy_assert(varID.isValid());
		return lockVariableImpl(varID);
	}

	inline void unlockVariable(VarID varID, bool wasChanged, ISolverConstraint* constraint, ExplainerFunction changeExplainer)
	{
		vxy_assert(varID.isValid());
		#if CONSTRAINT_USE_CACHED_STATES
		if (wasChanged)
		{
			m_states[varID.raw()] = EVariableState::Unknown;
		}
		#endif
		return unlockVariableImpl(varID, wasChanged, constraint, move(changeExplainer));
	}

	inline void resetVariableState(VarID varID)
	{
		vxy_assert(varID.isValid());
		#if CONSTRAINT_USE_CACHED_STATES
		m_states[varID.raw()] = EVariableState::Unknown;
		#endif
	}

	inline bool checkContradiction(VarID varID, ISolverConstraint* origin, const ExplainerFunction& explainer)
	{
		vxy_assert(varID.isValid());
		if (isInContradiction(varID))
		{
			onContradiction(varID, origin, explainer);
			return false;
		}

		return true;
	}

	inline EVariableState maybeUpdateState(VarID varID) const
	{
		vxy_assert(varID.isValid());
		#if CONSTRAINT_USE_CACHED_STATES
		if (m_states[varID.raw()] == EVariableState::Unknown)
		{
			m_states[varID.raw()] = determineState(varID);
		}
		return m_states[varID.raw()];
		#else
		return determineState(ID);
		#endif
	}

	inline EVariableState determineState(VarID varID) const
	{
		vxy_assert(varID.isValid());

		const ValueSet& values = getPotentialValues(varID);
		const int firstBit = values.indexOf(true);
		if (firstBit < 0)
		{
			return EVariableState::Contradiction;
		}

		const int lastBit = values.lastIndexOf(true);
		if (firstBit == lastBit)
		{
			return EVariableState::Solved;
		}
		else
		{
			return EVariableState::Unsolved;
		}
	}

	int m_numVariables = 0;
	#if CONSTRAINT_USE_CACHED_STATES
	mutable vector<EVariableState> m_states;
	#endif
};

} // namespace Vertexy