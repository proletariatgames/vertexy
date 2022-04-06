// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/AllDifferentConstraint.h"

#include "topology/BipartiteGraph.h"
#include "variable/IVariableDatabase.h"
#include "topology/algo/tarjan.h"

using namespace csolver;

AllDifferentConstraint* AllDifferentConstraint::AllDifferentFactory::construct(const ConstraintFactoryParams& params, const vector<VarID>& variables, bool useWeakPropagation)
{
	return new AllDifferentConstraint(params, params.unifyVariableDomains(variables), useWeakPropagation);
}

AllDifferentConstraint::AllDifferentConstraint(const ConstraintFactoryParams& params, const vector<VarID>& inVariables, bool useWeakPropagation)
	: ISolverConstraint(params)
	, m_variables(inVariables)
	, m_maxDomainSize(0)
	, m_useWeakPropagation(useWeakPropagation)
{
}

bool AllDifferentConstraint::initialize(IVariableDatabase* db)
{
	//
	// Ensure initial arc consistency: if any variables are solved, then exclude that value
	// from all others. If this causes any variable to be solved, exclude its value from all
	// others, and so on.
	//

	vector<VarID> solvedVariables;
	vector<VarID> unsolvedVariables = m_variables;

	m_maxDomainSize = -INT_MAX;
	for (VarID var : m_variables)
	{
		m_maxDomainSize = max(m_maxDomainSize, db->getDomainSize(var));

		if (db->isSolved(var))
		{
			solvedVariables.push_back(var);
			unsolvedVariables.erase_first_unsorted(var);
		}

		if (m_useWeakPropagation)
		{
			m_watcherHandles.push_back(db->addVariableWatch(var, EVariableWatchType::WatchSolved, this));
		}
		else
		{
			m_watcherHandles.push_back(db->addVariableWatch(var, EVariableWatchType::WatchLowerBoundChange, this));
			m_watcherHandles.push_back(db->addVariableWatch(var, EVariableWatchType::WatchUpperBoundChange, this));
		}
	}

	for (VarID solvedVar : solvedVariables)
	{
		excludeSolvedValue(db, solvedVar);
	}

	if (!m_useWeakPropagation)
	{
		m_hallIntervalPropagtor = make_unique<HallIntervalPropagation>(m_maxDomainSize);
		if (unsolvedVariables.size() > 0 && !checkBoundsConsistency(db, unsolvedVariables))
		{
			return false;
		}
	}

	vector<int> maxs;
	maxs.reserve(m_maxDomainSize);
	for (int i = 0; i < m_maxDomainSize; ++i)	{ maxs.push_back(1); }

	m_explainer.initialize(*db, m_variables, 0, m_maxDomainSize-1, maxs, /*useBoundsConsistency=*/true);

	return true;
}

void AllDifferentConstraint::reset(IVariableDatabase* db)
{
	if (!m_useWeakPropagation)
	{
		// Two watchers per variable
		for (int i = 0; i < m_watcherHandles.size(); ++i)
		{
			db->removeVariableWatch(m_variables[i >> 1], m_watcherHandles[i], this);
		}
	}
	else
	{
		for (int i = 0; i < m_watcherHandles.size(); ++i)
		{
			db->removeVariableWatch(m_variables[i], m_watcherHandles[i], this);
		}
	}
	m_watcherHandles.clear();
}

bool AllDifferentConstraint::onVariableNarrowed(IVariableDatabase* db, VarID narrowedVar, const ValueSet&, bool&)
{
	if (db->isSolved(narrowedVar))
	{
		if (!excludeSolvedValue(db, narrowedVar))
		{
			return false;
		}
	}

	if (!m_useWeakPropagation)
	{
		db->queueConstraintPropagation(this);
	}
	return true;
}

bool AllDifferentConstraint::propagate(IVariableDatabase* db)
{
	cs_assert(!m_useWeakPropagation);
	vector<VarID> unsolvedVariables;
	unsolvedVariables.reserve(m_variables.size());

	for (VarID var : m_variables)
	{
		if (!db->isSolved(var))
		{
			unsolvedVariables.push_back(var);
		}
	}

	if (unsolvedVariables.size() > 0)
	{
		if (!checkBoundsConsistency(db, unsolvedVariables))
		{
			return false;
		}
	}

	return true;
}

bool AllDifferentConstraint::checkConflicting(IVariableDatabase* db) const
{
	if (!m_useWeakPropagation)
	{
		vector<Interval> tempBounds, tempInvBounds;
		calculateBounds(db, m_variables, tempBounds, tempInvBounds);

		if (!m_hallIntervalPropagtor->checkAndPrune(tempBounds, [&](int, int) { return true; }))
		{
			return false;
		}

		if (!m_hallIntervalPropagtor->checkAndPrune(tempInvBounds, [&](int, int) { return true; }))
		{
			return false;
		}
	}

	return false;
}

bool AllDifferentConstraint::excludeSolvedValue(IVariableDatabase* db, VarID solvedVar)
{
	cs_assert(db->isSolved(solvedVar));

	// Ensure we maintain arc consistency by excluding the var's value, and if that causes
	// another variable to be solved, exclude that value from other vars, etc.

	auto explainerFn = [&](const NarrowingExplanationParams& params)
	{
		return explainVariable(params);
	};

	int solvedValue = db->getSolvedValue(solvedVar);
	for (VarID var : m_variables)
	{
		if (var != solvedVar && db->isPossible(var, solvedValue))
		{
			if (!db->excludeValue(var, solvedValue, this, explainerFn))
			{
				return false;
			}
		}
	}

	return true;
}

//
// Ensure bounds consistency. This uses the notion of Hall Intervals. A Hall Interval is a continuous range of
// values where there exists some subset of variables with potential values ONLY within that range, where
// the number of variables is equal to the size of the range.
//
// Example:
// X0: {3,4}
// X1: {3,4}
// X2: {3,4,5}
//
//  {3,4} is a Hall Interval, since X0 and X1 both fit within it, and the size of the interval (2) is equal to
//  the number of variables contained within it.
//
// Since each variable has to have a unique value, that means variables within a Hall Interval will necessarily
// have to take all the values, leaving none remaining for other variables. In the example above, removing 3 and 4
// from X2 will make it bounds-consistent.
//
// For more precise mathematical definition of Hall Intervals and relation to AllDifferent constraints, see
// "A fast algorithm for the bound consistency of alldiff constraints" Puget
// https://www.aaai.org/Papers/AAAI/1998/AAAI98-051.pdf
//
bool AllDifferentConstraint::checkBoundsConsistency(IVariableDatabase* db, const vector<VarID>& unsolvedVariables)
{
	cs_assert(m_hallIntervalPropagtor.get());
	calculateBounds(db, unsolvedVariables, m_bounds, m_invBounds);

	auto explainerFn = [&](const NarrowingExplanationParams& params)
	{
		return explainVariable(params);
	};

	fixed_vector<VarID, 8> solvedVars;

	auto excludeLessThan = [&](int varIndex, int boundary)
	{
		if (!db->excludeValuesLessThan(unsolvedVariables[varIndex], boundary, this, explainerFn))
		{
			return false;
		}
		if (db->isSolved(unsolvedVariables[varIndex]))
		{
			solvedVars.push_back(unsolvedVariables[varIndex]);
		}
		return true;
	};

	auto excludeGreaterThan = [&](int varIndex, int boundary)
	{
		if (!db->excludeValuesGreaterThan(unsolvedVariables[varIndex], -boundary, this, explainerFn))
		{
			return false;
		}
		if (db->isSolved(unsolvedVariables[varIndex]))
		{
			solvedVars.push_back(unsolvedVariables[varIndex]);
		}
		return true;
	};

	if (!m_hallIntervalPropagtor->checkAndPrune(m_bounds, excludeLessThan))
	{
		return false;
	}

	if (!m_hallIntervalPropagtor->checkAndPrune(m_invBounds, excludeGreaterThan))
	{
		return false;
	}

	for (VarID solved : solvedVars)
	{
		cs_assert(db->isSolved(solved));
		if (!excludeSolvedValue(db, solved))
		{
			return false;
		}
	}

	return true;
}

void AllDifferentConstraint::calculateBounds(const IVariableDatabase* db, const vector<VarID>& unsolvedVariables, vector<Interval>& outBounds, vector<Interval>& outInvBounds) const
{
	// Grab the min/max value for each variable.
	outBounds.clear();
	outInvBounds.clear();

	for (int i = 0; i < unsolvedVariables.size(); ++i)
	{
		int minVal = db->getMinimumPossibleValue(unsolvedVariables[i]);
		int maxVal = db->getMaximumPossibleValue(unsolvedVariables[i]);

		outBounds.emplace_back(minVal, maxVal, i);
		outInvBounds.emplace_back(-maxVal, -minVal, i);
	}
}

vector<Literal> AllDifferentConstraint::explainVariable(const NarrowingExplanationParams& params) const
{
	ValueSet removedValues = params.database->getPotentialValues(params.propagatedVariable).excluding(params.propagatedValues);
	return m_explainer.getExplanation(*params.database, params.propagatedVariable, removedValues);
}

bool AllDifferentConstraint::explainConflict(const IVariableDatabase* db, vector<Literal>& outClauses) const
{
	outClauses = m_explainer.getExplanation(*db, VarID::INVALID, {});
	return true;
}
