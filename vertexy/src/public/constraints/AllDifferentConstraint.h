// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "topology/BipartiteGraph.h"
#include "ds/HallIntervalPropagation.h"
#include "IConstraint.h"
#include "MaxOccurrenceExplainer.h"
#include "topology/algo/tarjan.h"

namespace Vertexy
{

class AllDifferentConstraint : public IConstraint
{
public:
	AllDifferentConstraint(const ConstraintFactoryParams& params, const vector<VarID>& variables, bool bWeakPropagation = false);

	struct AllDifferentFactory
	{
		static AllDifferentConstraint* construct(const ConstraintFactoryParams& params, const vector<VarID>& variables, bool useWeakPropagation = false);
	};

	using Factory = AllDifferentFactory;

	virtual EConstraintType getConstraintType() const override { return EConstraintType::AllDifferent; }
	virtual vector<VarID> getConstrainingVariables() const override { return m_variables; }
	virtual bool initialize(IVariableDatabase* db) override;
	virtual void reset(IVariableDatabase* db) override;
	virtual bool onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& previousValue, bool& removeWatch) override;
	virtual bool propagate(IVariableDatabase* db) override;
	virtual bool checkConflicting(IVariableDatabase* db) const override;
	virtual void explain(const NarrowingExplanationParams& params, vector<Literal>& outExplanation) const override;

protected:
	using Interval = HallIntervalPropagation::Interval;

	// Given a solved variable, exclude that the variable's value from all others.
	bool excludeSolvedValue(IVariableDatabase* db, VarID solvedVar);

	void calculateBounds(const IVariableDatabase* db, const vector<VarID>& unsolvedVariables, vector<Interval>& outBounds, vector<Interval>& outInvBounds) const;
	bool checkBoundsConsistency(IVariableDatabase* db, const vector<VarID>& unsolvedVariables);

	vector<VarID> m_variables;
	vector<WatcherHandle> m_watcherHandles;

	unique_ptr<const HallIntervalPropagation> m_hallIntervalPropagator;

	mutable vector<int> m_sortedBoundaries;
	mutable vector<int> m_intervalMinRank;
	mutable vector<int> m_intervalMaxRank;

	vector<Interval> m_bounds;
	vector<Interval> m_invBounds;

	// Maximum domain size of all variables in the constraint
	int m_maxDomainSize;
	// If true, we will only propagate when a variable is solved, excluding its value from all other variables.
	// Otherwise we will also perform bounds propagation using hall intervals.
	bool m_useWeakPropagation;

	mutable MaxOccurrenceExplainer m_explainer;
};

} // namespace Vertexy