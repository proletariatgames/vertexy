// Copyright Proletariat, Inc. All Rights Reserved.
#include "util/SolverDecisionLog.h"
#include <iostream>
#include <fstream>
#include <string>

#include "ConstraintSolver.h"

using namespace csolver;

SolverDecisionLog::SolverDecisionLog()
{
}

void SolverDecisionLog::write(const wchar_t* outputFile) const
{
	std::basic_ofstream<wchar_t> file(outputFile);
	cs_verify(file.is_open());
	for (const DecisionRecord& decision : m_decisions)
	{
		file << decision.toString().c_str() << TEXT("\n");
	}
	file.close();
}

void SolverDecisionLog::writeBreadcrumbs(const ConstraintSolver& solver, const wchar_t* outputFile) const
{
	auto sanitizeVariableName = [](const wstring& varName)
	{
		wstring out = varName;
		int n;
		while (n = out.find(TEXT(">>>")), n < out.size() )
		{
			out.replace(n, 3, TEXT("___"));
		}
		return out;
	};

	std::basic_ofstream<wchar_t> file(outputFile);
	cs_verify(file.is_open());

	int leafNum = 0;

	int i = 0;
	vector<VarID> stack;
	while (i < m_decisions.size())
	{
		cs_sanity(!contains(stack.begin(), stack.end(), m_decisions[i].variable));
		stack.push_back(m_decisions[i].variable);

		// add to stack until decision level decreases
		int j = i+1;
		while (j < m_decisions.size() && m_decisions[j].level > m_decisions[j-1].level)
		{
			cs_sanity(!contains(stack.begin(), stack.end(), m_decisions[j].variable));
			stack.push_back(m_decisions[j].variable);
			++j;
		}

		// write out this path
		wstring breadcrumb;
		for (int k = 0; k < stack.size(); ++k)
		{
			breadcrumb.append(sanitizeVariableName(solver.getVariableName(stack[k])));
			if (k != stack.size()-1)
			{
				breadcrumb.append(TEXT(">>>"));
			}
		}
		file << breadcrumb.c_str() << TEXT(",") << leafNum << TEXT("\n");
		++leafNum;

		// pop off stack to backtracked level
		if (j < m_decisions.size())
		{
			while (stack.size() >= m_decisions[j].level)
			{
				stack.pop_back();
			}
		}
		i = j;
	}

	file.close();
}

bool SolverDecisionLog::read(const wchar_t* inFile)
{
	m_decisions.clear();

	std::basic_ifstream<wchar_t> file(inFile);
	if (!file.is_open())
	{
		return false;
	}
	std::wstring line;
	while (std::getline(file, line))
	{
		DecisionRecord decision;
		int var;
		if (3 != swscanf_s(line.c_str(), TEXT("%d %d %d"), &decision.level, &var, &decision.valueIndex))
		{
			return false;
		}
		decision.variable = VarID(var);
		m_decisions.push_back(decision);
	}

	file.close();
	return true;
}

void SolverDecisionLog::addDecision(int level, VarID variable, int valueIndex)
{
	m_decisions.push_back({level, variable, valueIndex});
}
