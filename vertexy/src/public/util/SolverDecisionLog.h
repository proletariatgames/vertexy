// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "ConstraintTypes.h"

namespace Vertexy
{
// Handles writing/reading a log file of solver choices.
class SolverDecisionLog
{
public:
	struct SolverRecord
	{
		SolverDecisionLevel level;
		wstring variableName;
		int constraintID;
		ValueSet newBits;

		wstring toString() const
		{
			return { wstring::CtorSprintf(), TEXT("%d,%s,%d,%s"), level, variableName.c_str(), constraintID, newBits.toString().c_str() };
		}
	};

	struct DecisionRecord
	{
		SolverDecisionLevel level;
		VarID variable;
		int valueIndex;

		wstring toString() const
		{
			return {wstring::CtorSprintf(), TEXT("%d %d %d"), level, variable.raw(), valueIndex};
		}
	};

	SolverDecisionLog();

	void write(const wchar_t* outputFile) const;
	void writeBreadcrumbs(const ConstraintSolver& solver, const wchar_t* outputFile) const;
	bool read(const wchar_t* inFile);

	void addDecision(int level, VarID variable, int valueIndex);
	const DecisionRecord& getDecision(int i) const { return m_decisions[i]; }

	void addSolverRecord(int decisionLevel, wstring variableName, int constraintID, ValueSet newValues);

	int getNumDecisions() const { return m_decisions.size(); }

protected:
	vector<DecisionRecord> m_decisions;
	vector<SolverRecord> m_solverRecords;
};

} // namespace Vertexy