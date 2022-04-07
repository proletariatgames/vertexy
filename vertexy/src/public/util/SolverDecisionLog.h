// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "ConstraintTypes.h"

namespace Vertexy
{
// Handles writing/reading a log file of solver choices.
class SolverDecisionLog
{
public:
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

	int getNumDecisions() const { return m_decisions.size(); }

protected:
	vector<DecisionRecord> m_decisions;
};

} // namespace Vertexy