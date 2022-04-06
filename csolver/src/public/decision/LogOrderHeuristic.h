// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "ConstraintTypes.h"
#include "ISolverDecisionHeuristic.h"

namespace csolver
{

class SolverDecisionLog;

/** Heuristic that reads decisions from a log file of a previous run. Will assert if the decision is not possible. */
class LogOrderHeuristic : public ISolverDecisionHeuristic
{
public:
	LogOrderHeuristic(ConstraintSolver& solver, const shared_ptr<const SolverDecisionLog>& log)
		: m_solver(solver)
		, m_log(log)
	{
	}

	virtual void initialize() override;
	virtual bool getNextDecision(SolverDecisionLevel level, VarID& var, ValueSet& chosenValues) override;

	virtual void onVariableAssignment(VarID var, const ValueSet& prevValues, const ValueSet& newValues) override
	{
	}

	virtual void onVariableUnassignment(VarID var, const ValueSet& beforeBacktrack, const ValueSet& afterBacktrack) override
	{
	}

	virtual double getPriority(VarID id, int value) const override { return 0; }

protected:
	int m_nextDecision = 0;
	int m_levelOffset = 0;
	int m_totalDecisions = 0;
	ConstraintSolver& m_solver;
	shared_ptr<const SolverDecisionLog> m_log;
	vector<int> m_loggedSolution;
};

} // namespace csolver