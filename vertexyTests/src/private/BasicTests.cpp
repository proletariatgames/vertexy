// Copyright Proletariat, Inc. All Rights Reserved.
#include "BasicTests.h"

#include <EASTL/hash_map.h>

#include "ConstraintSolver.h"
#include "EATest/EATest.h"
#include "util/SolverDecisionLog.h"
#include "variable/SolverVariableDomain.h"

using namespace VertexyTests;

// Whether to write a decision log as DecisionLog.txt
static constexpr bool WRITE_BREADCRUMB_LOG = false;

int TestSolvers::solveCardinalityBasic(int times, int seed, bool printVerbose)
{
	int nErrorCount = 0;
	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("TestCardinality"), seed);

		vector<VarID> vars;
		vars.push_back(solver.makeVariable(TEXT("X1"), vector{2, 2}));
		vars.push_back(solver.makeVariable(TEXT("X2"), vector{1, 2}));
		vars.push_back(solver.makeVariable(TEXT("X3"), vector{2, 3}));
		vars.push_back(solver.makeVariable(TEXT("X4"), vector{2, 3}));
		vars.push_back(solver.makeVariable(TEXT("X5"), vector{1, 4}));
		vars.push_back(solver.makeVariable(TEXT("X6"), vector{3, 4}));

		hash_map<int, tuple<int, int>> cardinalities;
		cardinalities[1] = make_tuple(1, 3);
		cardinalities[2] = make_tuple(1, 3);
		cardinalities[3] = make_tuple(1, 3);
		cardinalities[4] = make_tuple(2, 3);

		solver.cardinality(vars, cardinalities);

		solver.solve();
		if (printVerbose)
		{
			for (auto varID : vars)
			{
				VERTEXY_LOG("    %s = %d", solver.getVariableName(varID).c_str(), solver.getSolvedValue(varID));
			}
		}

		solver.dumpStats(printVerbose);
		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);

		vector<int> counts;
		counts.resize(5, 0);

		for (auto varID : vars)
		{
			counts[solver.getSolvedValue(varID)]++;
		}
		EATEST_VERIFY(counts[0] == 0);
		EATEST_VERIFY(counts[1] >= 1 && counts[1] <= 3);
		EATEST_VERIFY(counts[2] >= 1 && counts[2] <= 3);
		EATEST_VERIFY(counts[3] >= 1 && counts[3] <= 3);
		EATEST_VERIFY(counts[4] >= 2 && counts[4] <= 3);
	}
	return nErrorCount;
}

int TestSolvers::solveCardinalityShiftProblem(int times, int seed, bool printVerbose)
{
	int nErrorCount = 0;
	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("ShiftProblem"), seed);

		SolverVariableDomain domain(1, 3);
		vector<vector<int>> values = {
			{2, 3},
			{3},
			{1, 2, 3},
			{1, 2, 3},
			{1, 2, 3},
			{1, 2, 3}
		};

		vector<VarID> vars;
		for (int i = 0; i < 6; ++i)
		{
			vars.push_back(solver.makeVariable({wstring::CtorSprintf(), TEXT("X%d"), i}, domain, values[i]));
		}

		hash_map<int, tuple<int, int>> shiftReqs;
		shiftReqs[1] = make_tuple(1, 4);
		shiftReqs[2] = make_tuple(2, 3);
		shiftReqs[3] = make_tuple(2, 2);
		solver.cardinality(vars, shiftReqs);

		solver.solve();
		if (printVerbose)
		{
			for (auto varID : vars)
			{
				VERTEXY_LOG("    %s = %d", solver.getVariableName(varID).c_str(), solver.getSolvedValue(varID));
			}
		}
		solver.dumpStats(printVerbose);
		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);

		int numShift1 = count_if(vars.begin(), vars.end(), [&](VarID V) { return solver.getSolvedValue(V) == 1; });
		int numShift2 = count_if(vars.begin(), vars.end(), [&](VarID V) { return solver.getSolvedValue(V) == 2; });
		int numShift3 = count_if(vars.begin(), vars.end(), [&](VarID V) { return solver.getSolvedValue(V) == 3; });
		EATEST_VERIFY(numShift1 >= 1 && numShift1 <= 4);
		EATEST_VERIFY(numShift2 >= 2 && numShift2 <= 3);
		EATEST_VERIFY(numShift3 == 2);
		EATEST_VERIFY(solver.getSolvedValue(vars[0]) != 1);
		EATEST_VERIFY(solver.getSolvedValue(vars[1]) == 3);
	}
	return nErrorCount;
}

int TestSolvers::solveClauseBasic(int times, int seed, bool printVerbose)
{
	int nErrorCount = 0;
	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("ClauseTest"), seed);

		vector<VarID> vars = {
			solver.makeVariable(TEXT("X0"), vector{3, 4}),
			solver.makeVariable(TEXT("X1"), vector{2, 3, 4, 5}),
			solver.makeVariable(TEXT("X2"), vector{1, 2, 4, 6}),
		};

		SolverVariableDomain domain(0, 6);

		solver.clause({
			SignedClause(vars[0], {3}),
			SignedClause(vars[1], {2, 3, 5}),
			SignedClause(vars[2], EClauseSign::Outside, {2, 4, 6})
		});
		solver.inequality(vars[1], EConstraintOperator::GreaterThan, vars[2]);

		solver.solve();
		if (printVerbose)
		{
			for (auto vi : solver.getSolution())
			{
				VERTEXY_LOG("    %s = %d", vi.second.name.c_str(), vi.second.value);
			}
		}

		solver.dumpStats(printVerbose);
		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);

		EATEST_VERIFY(
			solver.getSolvedValue(vars[0]) == 3 ||
			(solver.getSolvedValue(vars[1]) == 2 || solver.getSolvedValue(vars[1]) == 3 || solver.getSolvedValue(vars[1]) == 5) ||
			(solver.getSolvedValue(vars[2]) != 2 && solver.getSolvedValue(vars[2]) != 4 || solver.getSolvedValue(vars[2]) != 6)
		);
		EATEST_VERIFY(solver.getSolvedValue(vars[1]) > solver.getSolvedValue(vars[2]));
	}
	return nErrorCount;
}

int TestSolvers::solveInequalityBasic(int times, int seed, bool printVerbose)
{
	int nErrorCount = 0;
	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("InequalityTest"), seed);

		SolverVariableDomain domain(0, 10);

		auto x0 = solver.makeVariable(TEXT("X0"), domain);
		auto x1 = solver.makeVariable(TEXT("X1"), domain);
		auto x2 = solver.makeVariable(TEXT("X2"), domain);

		// X2 >= X1 > X0
		solver.inequality(x1, EConstraintOperator::LessThanEq, x2);
		solver.inequality(x1, EConstraintOperator::GreaterThan, x0);

		solver.solve();

		if (printVerbose)
		{
			for (auto vi : solver.getSolution())
			{
				VERTEXY_LOG("    %s = %d", vi.second.name.c_str(), vi.second.value);
			}
		}

		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
		solver.dumpStats(printVerbose);

		EATEST_VERIFY(solver.getSolvedValue(x1) > solver.getSolvedValue(x0));
		EATEST_VERIFY(solver.getSolvedValue(x2) >= solver.getSolvedValue(x1));
	}
	return nErrorCount;
}

int TestSolvers::solveAllDifferentLarge(int times, int seed, bool printVerbose)
{
	int nErrorCount = 0;
	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("AllDifferent_Large"), seed);

		const int numVars = 24;

		SolverVariableDomain domain(0, numVars - 1);
		vector<VarID> vars;
		for (int i = 0; i < numVars; ++i)
		{
			vars.push_back(solver.makeVariable({wstring::CtorSprintf(), TEXT("X%d"), i}, domain));
			if (i > 0)
			{
				solver.inequality(vars[i - 1], EConstraintOperator::LessThanEq, vars.back());
			}
		}

		solver.allDifferent(vars);
		solver.solve();

		if (printVerbose)
		{
			for (auto vi : solver.getSolution())
			{
				VERTEXY_LOG("    %s = %d", vi.second.name.c_str(), vi.second.value);
			}
		}

		solver.dumpStats(printVerbose);
		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
		for (auto vi : solver.getSolution())
		{
			EATEST_VERIFY(vi.first.raw()-1 == vi.second.value);
		}
	}
	return nErrorCount;
}

int TestSolvers::solveAllDifferentSmall(int times, int seed, bool printVerbose)
{
	int nErrorCount = 0;
	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("AllDifferent_Small"), seed);

		SolverVariableDomain domain(0, 6);
		vector<VarID> vars = {
			solver.makeVariable(TEXT("X1"), vector{3, 4}),
			solver.makeVariable(TEXT("X2"), vector{2, 3, 4}),
			solver.makeVariable(TEXT("X3"), vector{3, 4}),
			solver.makeVariable(TEXT("X4"), vector{2, 3, 4, 5}),
			solver.makeVariable(TEXT("X5"), vector{3, 4, 5, 6}),
			solver.makeVariable(TEXT("X6"), vector{1, 2, 3, 4, 5, 6})
		};

		solver.allDifferent(vars);
		solver.solve();

		if (printVerbose)
		{
			for (auto vi : solver.getSolution())
			{
				VERTEXY_LOG("    %s = %d", vi.second.name.c_str(), vi.second.value);
			}
		}

		solver.dumpStats(printVerbose);
		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);

		hash_set<int> values;
		for (int i = 0; i < vars.size(); ++i)
		{
			EATEST_VERIFY(values.count(solver.getSolvedValue(vars[i])) == 0);
			values.insert(solver.getSolvedValue(vars[i]));
		}
	}
	return nErrorCount;
}

int TestSolvers::solveSumBasic(int times, int seed, bool printVerbose)
{
	int nErrorCount = 0;

	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("Sum_Basic"), seed);

		SolverVariableDomain domain(0, 10);
		VarID sum = solver.makeVariable(TEXT("Sum"), domain);
		vector<VarID> vars = {
			solver.makeVariable(TEXT("X1"), vector{5, 10}),
			solver.makeVariable(TEXT("X2"), vector{1, 17}),
			solver.makeVariable(TEXT("X3"), domain),
			solver.makeVariable(TEXT("X4"), domain)
		};

		shared_ptr<SolverDecisionLog> outputLog;
		if constexpr (WRITE_BREADCRUMB_LOG)
		{
			outputLog = make_shared<SolverDecisionLog>();
		}

		if (outputLog != nullptr)
		{
			solver.setOutputLog(outputLog);
		}

		solver.sum(sum, vars);
		solver.solve();

		if (printVerbose)
		{
			for (auto vi : solver.getSolution())
			{
				VERTEXY_LOG("    %s = %d", vi.second.name.c_str(), vi.second.value);
			}
		}

		solver.dumpStats(printVerbose);
		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);

		int summedVars = 0;
		for (auto var : vars)
		{
			summedVars += solver.getSolvedValue(var);
		}
		EATEST_VERIFY(solver.getSolvedValue(sum) == summedVars);

		if (outputLog != nullptr)
		{
			outputLog->writeBreadcrumbs(solver, TEXT("SumDecisionLog.txt"));
			outputLog->write(TEXT("SumOutput.txt"));
		}

	}

	return nErrorCount;
}

int TestSolvers::solveRules_basicChoice(int seed, bool printVerbose)
{
	int nErrorCount = 0;

	ConstraintSolver solver(TEXT("Rules:BasicChoice"), seed);

	auto a = solver.makeVariable(TEXT("a"), vector{0,1});
	auto b = solver.makeVariable(TEXT("b"), vector{0,1});
	auto c = solver.makeVariable(TEXT("c"), vector{0,1});

	// {b}. {c}. a <- b, c.
	solver.getRuleDB().addFact(SignedClause{b, {1}}, ERuleHeadType::Choice);
	solver.getRuleDB().addFact(SignedClause{c, {1}}, ERuleHeadType::Choice);
	solver.getRuleDB().addRule(SignedClause{a, {1}}, vector{SignedClause{b, {1}}, SignedClause{c, {1}}});

	int numResults = 0;
	while (true)
	{
		auto result = solver.solve();
		if (result != EConstraintSolverResult::Solved)
		{
			break;
		}

		++numResults;
		if (printVerbose)
		{
			VERTEXY_LOG("a=%d b=%d c=%d", solver.getSolvedValue(a), solver.getSolvedValue(b), solver.getSolvedValue(c));
		}
		EATEST_VERIFY(solver.getSolvedValue(a) == (solver.getSolvedValue(b) && solver.getSolvedValue(c)));
	}

	// a=0 b=0 c=0
	// a=0 b=1 c=0
	// a=0 b=0 c=1
	// a=1 b=1 c=1
	EATEST_VERIFY(numResults == 4);
	EATEST_VERIFY(solver.getRuleDB().isTight());

	return nErrorCount;
}

int TestSolvers::solveRules_basicDisjunction(int seed, bool printVerbose)
{
	int nErrorCount = 0;

	ConstraintSolver solver(TEXT("Rules:BasicDisjunction"), seed);

	auto a = solver.makeVariable(TEXT("a"), vector{0,1});
	auto b = solver.makeVariable(TEXT("b"), vector{0,1});
	auto c = solver.makeVariable(TEXT("c"), vector{0,1});

	// a | b <- c. {c}.
	solver.getRuleDB().addRule(
		TRuleHead{vector{SignedClause{a, {1}}, SignedClause{b, {1}}}, ERuleHeadType::Disjunction},
		SignedClause{c, {1}}
	);
	solver.getRuleDB().addFact(SignedClause{c, {1}}, ERuleHeadType::Choice);

	int numResults = 0;
	while (true)
	{
		auto result = solver.solve();
		if (result != EConstraintSolverResult::Solved)
		{
			break;
		}

		++numResults;
		if (printVerbose)
		{
			VERTEXY_LOG("a=%d b=%d c=%d", solver.getSolvedValue(a), solver.getSolvedValue(b), solver.getSolvedValue(c));
		}
		EATEST_VERIFY(!solver.getSolvedValue(c) || (solver.getSolvedValue(a) != solver.getSolvedValue(b)));
		EATEST_VERIFY((solver.getSolvedValue(a) || solver.getSolvedValue(b)) == solver.getSolvedValue(c));
	}

	// a=0 b=0 c=0
	// a=1 b=0 c=1
	// a=0 b=1 c=1
	EATEST_VERIFY(numResults == 3);
	EATEST_VERIFY(solver.getRuleDB().isTight());

	return nErrorCount;
}

int TestSolvers::solveRules_basicCycle(int seed, bool printVerbose)
{
	int nErrorCount = 0;

	ConstraintSolver solver(TEXT("Rules:BasicDisjunction"), seed);
	VERTEXY_LOG("Rules:BasicCycle(%d)", solver.getSeed());

	auto a = solver.makeVariable(TEXT("a"), vector{0,1});
	auto b = solver.makeVariable(TEXT("b"), vector{0,1});
	auto c = solver.makeVariable(TEXT("c"), vector{0,1});

	// a <- b. b <- a. b <- c. {c}.
	solver.getRuleDB().addRule(SignedClause{a, {1}}, SignedClause{b, {1}});
	solver.getRuleDB().addRule(SignedClause{b, {1}}, SignedClause{a, {1}});
	solver.getRuleDB().addRule(SignedClause{b, {1}}, SignedClause{c, {1}});
	solver.getRuleDB().addFact(SignedClause{c, {1}}, ERuleHeadType::Choice);

	int numResults = 0;
	while (true)
	{
		auto result = solver.solve();
		if (result != EConstraintSolverResult::Solved)
		{
			break;
		}

		++numResults;
		if (printVerbose)
		{
			VERTEXY_LOG("a=%d b=%d c=%d", solver.getSolvedValue(a), solver.getSolvedValue(b), solver.getSolvedValue(c));
		}
		EATEST_VERIFY(!solver.getSolvedValue(c) || (solver.getSolvedValue(a) && solver.getSolvedValue(b)));
		EATEST_VERIFY(solver.getSolvedValue(c) || (!solver.getSolvedValue(a) && !solver.getSolvedValue(b)));
	}

	// a=1 b=1 c=1
	// a=0 b=0 c=0
	EATEST_VERIFY(numResults == 2);
	EATEST_VERIFY(!solver.getRuleDB().isTight());

	return nErrorCount;
}

int TestSolvers::solveRules_incompleteCycle(int seed, bool printVerbose)
{
	int nErrorCount = 0;

	ConstraintSolver solver(TEXT("Rules:BasicDisjunction"), seed);
	VERTEXY_LOG("Rules:BasicCycle(%d)", solver.getSeed());

	vector domain = {0,1,2,3};
	auto a = solver.makeVariable(TEXT("a"), domain);
	auto b = solver.makeVariable(TEXT("b"), domain);
	auto c = solver.makeVariable(TEXT("c"), domain);

	vector pos = {1,2,3};

	// a <- b. b <- a. b <- c. {c}.
	solver.getRuleDB().addRule(SignedClause{a, pos}, SignedClause{b, pos});
	solver.getRuleDB().addRule(SignedClause{b, pos}, SignedClause{a, pos});
	solver.getRuleDB().addRule(SignedClause{b, pos}, SignedClause{c, pos});
	solver.getRuleDB().addFact(SignedClause{c, pos}, ERuleHeadType::Choice);

	int numResults = 0;
	while (true)
	{
		auto result = solver.solve();
		if (result != EConstraintSolverResult::Solved)
		{
			break;
		}

		++numResults;
		if (printVerbose)
		{
			VERTEXY_LOG("a=%d b=%d c=%d", solver.getSolvedValue(a), solver.getSolvedValue(b), solver.getSolvedValue(c));
		}
		EATEST_VERIFY(!solver.getSolvedValue(c) || (solver.getSolvedValue(a) && solver.getSolvedValue(b)));
		EATEST_VERIFY(solver.getSolvedValue(c) || (!solver.getSolvedValue(a) && !solver.getSolvedValue(b)));
	}

	// a!=0 b!=0 c!=0
	// a=0 b=0 c=0
	EATEST_VERIFY(numResults == 28);
	EATEST_VERIFY(!solver.getRuleDB().isTight());

	return nErrorCount;
}