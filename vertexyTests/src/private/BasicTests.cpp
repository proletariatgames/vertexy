// Copyright Proletariat, Inc. All Rights Reserved.
#include "BasicTests.h"

#include <EASTL/hash_map.h>
#include <EASTL/set.h>

#include "ConstraintSolver.h"
#include "ds/ESTree.h"
#include "EATest/EATest.h"
#include "program/ProgramDSL.h"
#include "rules/RuleDatabase.h"
#include "topology/GridTopology.h"
#include "topology/IPlanarTopology.h"
#include "util/SolverDecisionLog.h"
#include "variable/SolverVariableDomain.h"

using namespace VertexyTests;

// Whether to write a decision log as DecisionLog.txt
static constexpr bool WRITE_BREADCRUMB_LOG = false;

int TestSolvers::bitsetTests()
{
	int nErrorCount = 0;
	using vbs = TValueBitset<>;

	{
		vbs a;
		EATEST_VERIFY(a.size() == 0);

		a.pad(33, false);
		EATEST_VERIFY(a.size() == 33);

		EATEST_VERIFY(a.indexOf(true) < 0);
		EATEST_VERIFY(a.indexOf(false) == 0);
		EATEST_VERIFY(a.lastIndexOf(true) < 0);
		EATEST_VERIFY(a.lastIndexOf(false) == 32);

		a.pad(31, false);
		EATEST_VERIFY(a.size() == 33);
	}


	{
		vbs a;
		a.pad(33, true);
		EATEST_VERIFY(a.size() == 33);
		EATEST_VERIFY(a.indexOf(false) < 0);
		EATEST_VERIFY(a.indexOf(true) == 0);
		EATEST_VERIFY(a.lastIndexOf(false) < 0);
		EATEST_VERIFY(a.lastIndexOf(true) == 32);
	}
	{
		vbs a;
		a.pad(48, false);
		a[31] = true;
		EATEST_VERIFY(a.indexOf(true) == 31);
		EATEST_VERIFY(a.lastIndexOf(true) == 31);

		a[32] = true;
		EATEST_VERIFY(a.indexOf(true) == 31);
		EATEST_VERIFY(a.lastIndexOf(true) == 32);

		a[47] = true;
		EATEST_VERIFY(a.indexOf(true) == 31);
		EATEST_VERIFY(a.lastIndexOf(true) == 47);
	}

	{
		vbs a;
		a.pad(48, false);

		a.setRange(5, 10, true);
		EATEST_VERIFY(a[5]);
		EATEST_VERIFY(a[6]);
		EATEST_VERIFY(a[7]);
		EATEST_VERIFY(a[8]);
		EATEST_VERIFY(a[9]);
		EATEST_VERIFY(!a[10]);

		a.setRange(30, 35, true);
		EATEST_VERIFY(!a[29]);
		EATEST_VERIFY(a[30]);
		EATEST_VERIFY(a[31]);
		EATEST_VERIFY(a[32]);
		EATEST_VERIFY(a[33]);
		EATEST_VERIFY(a[34]);
		EATEST_VERIFY(!a[35]);
	}

	{
		vbs a(48, false);
		vbs b;

		a.setRange(30, 35, true);
		b = a;
		EATEST_VERIFY(b.size() == a.size());
		EATEST_VERIFY(b.indexOf(true) == 30);

		vbs c(a);
		EATEST_VERIFY(c.size() == a.size());
		EATEST_VERIFY(c.indexOf(true) == 30);

		vbs d = move(a);
		EATEST_VERIFY(d.size() == b.size());
		EATEST_VERIFY(d.indexOf(true) == 30);
	}

	{
		vbs a(48, false);
		a.setRange(30, 35, true);

		a.pad(900, false);
		EATEST_VERIFY(a[30]);
		EATEST_VERIFY(a[31]);
		EATEST_VERIFY(a[32]);
		EATEST_VERIFY(a[33]);
		EATEST_VERIFY(a[34]);
		EATEST_VERIFY(a.indexOf(true) == 30);
		EATEST_VERIFY(a.lastIndexOf(true) == 34);
	}

	{
		vbs a(48, false);
		a[0] = true;
		a[1] = true;
		a[20] = true;
		a[32] = true;
		a[45] = true;
		a[46] = true;
		a[47] = true;

		set<int32_t> Found;
		for (auto it = a.beginSetBits(); it != a.endSetBits(); ++it)
		{
			Found.insert(*it);
		}

		EATEST_VERIFY(Found.find(0) != Found.end());
		EATEST_VERIFY(Found.find(1) != Found.end());
		EATEST_VERIFY(Found.find(20) != Found.end());
		EATEST_VERIFY(Found.find(32) != Found.end());
		EATEST_VERIFY(Found.find(45) != Found.end());
		EATEST_VERIFY(Found.find(46) != Found.end());
		EATEST_VERIFY(Found.find(47) != Found.end());
		EATEST_VERIFY(Found.size() == 7);
	}

	{
		vbs a(64, false);
		a[30] = true;

		vbs b(64, false);
		b[58] = true;

		vbs c = a.including(b);
		EATEST_VERIFY(c[30]);
		EATEST_VERIFY(c[58]);

		a[58] = true;
		c = a.excluding(b);
		EATEST_VERIFY(c[30]);
		EATEST_VERIFY(!c[58]);

		a[58] = false;
		b[30] = true;
		c = a.intersecting(b);
		EATEST_VERIFY(c[30]);
		EATEST_VERIFY(!c[58]);

		c = a.xoring(b);
		EATEST_VERIFY(!c[30]);
		EATEST_VERIFY(c[58]);

		a[58] = true;
		c.init(64, false);
		c[30] = true;
		EATEST_VERIFY(!a.isSubsetOf(c));
		c[58] = true;
		EATEST_VERIFY(a.isSubsetOf(c));
		c[59] = true;
		EATEST_VERIFY(a.isSubsetOf(c));
	}

	return nErrorCount;
}

int TestSolvers::digraphTests()
{
	int nErrorCount = 0;

	shared_ptr<DigraphTopology> graph = make_shared<DigraphTopology>();
	vector<int> nodes = {
		graph->addVertex(),
		graph->addVertex(),
		graph->addVertex(),
		graph->addVertex(),
		graph->addVertex(),
		graph->addVertex()
	};

	graph->addEdge(nodes[0], nodes[2]);
	for (int i = 0; i < nodes.size() - 1; ++i)
	{
		graph->addEdge(nodes[i], nodes[i + 1]);
	}

	ESTree<> tree(graph);
	tree.initialize(nodes[0]);
	EATEST_VERIFY(!containsPredicate(nodes.begin(), nodes.end(), [&](int Node) { return !tree.isReachable(Node); }));

	graph->removeEdge(nodes[0], nodes[1]);
	EATEST_VERIFY(!tree.isReachable(nodes[1]));
	EATEST_VERIFY(!containsPredicate(nodes.begin(), nodes.end(), [&](int Node) { return Node != nodes[1] && !tree.isReachable(Node); }));

	graph->removeEdge(nodes[0], nodes[2]);
	EATEST_VERIFY(!containsPredicate(nodes.begin(), nodes.end(), [&](int Node) { return Node != nodes[0] && tree.isReachable(Node); }));

	return nErrorCount;
}

int TestSolvers::ruleSCCTests()
{
	int nErrorCount = 0;

	ConstraintSolver solver;
	auto& rdb = solver.getRuleDB();
	auto a = rdb.createAtom(TEXT("a"));
	auto b = rdb.createAtom(TEXT("b"));
	auto c = rdb.createAtom(TEXT("c"));
	auto d = rdb.createAtom(TEXT("d"));
	auto e = rdb.createAtom(TEXT("e"));

	auto pos = [&](AtomID atom) { return AtomLiteral(atom, true, ValueSet(1, true)); };
	auto neg = [&](AtomID atom) { return AtomLiteral(atom, false, ValueSet(1, true)); };
	
	rdb.addRule(pos(a), false, vector{neg(b)});
	rdb.addRule(pos(b), false, vector{neg(a)});
	rdb.addRule(pos(c), false, vector{pos(a)});
	rdb.addRule(pos(c), false, vector{pos(b), pos(d)});
	rdb.addRule(pos(d), false, vector{pos(b), pos(c)});
	rdb.addRule(pos(d), false, vector{pos(e)});
	rdb.addRule(pos(e), false, vector{pos(b), neg(a)});
	rdb.addRule(pos(e), false, vector{pos(c), pos(d)});
	
	rdb.finalize();
	
	EATEST_VERIFY(rdb.getAtom(a)->asConcrete()->scc < 0);
	EATEST_VERIFY(rdb.getAtom(b)->asConcrete()->scc < 0);
	EATEST_VERIFY(rdb.getAtom(c)->asConcrete()->scc >= 0);
	EATEST_VERIFY(rdb.getAtom(d)->asConcrete()->scc == rdb.getAtom(c)->asConcrete()->scc);
	EATEST_VERIFY(rdb.getAtom(e)->asConcrete()->scc == rdb.getAtom(c)->asConcrete()->scc);
	return nErrorCount;
}

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
			for (auto var : vars)
			{
				VERTEXY_LOG("    %s = %d", solver.getVariableName(var).c_str(), solver.getSolvedValue(var));
			}
		}

		solver.dumpStats(printVerbose);
		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
		for (int i = 0; i < vars.size(); ++i)
		{
			VarID var = vars[i];
			EATEST_VERIFY(solver.getSolvedValue(var) == i);
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
	
	auto var_a = solver.makeBoolean(TEXT("a"));
	auto var_b = solver.makeBoolean(TEXT("b"));
	auto var_c = solver.makeBoolean(TEXT("c"));

	struct Result
	{
		FormulaResult<0> a, b, c;
	};
	
	auto prg = Program::define([]()
	{
		VXY_FORMULA(a, 0);
		VXY_FORMULA(b, 0);
		VXY_FORMULA(c, 0);

		b().choice();
		c().choice();
		a() <<= b() && c();

		return Result{a, b, c};
	});

	auto inst = prg();
	inst->getResult().a.bind(var_a);
	inst->getResult().b.bind(var_b);
	inst->getResult().c.bind(var_c);

	solver.addProgram(inst);
		
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
			VERTEXY_LOG("a=%d b=%d c=%d", solver.getSolvedValue(var_a), solver.getSolvedValue(var_b), solver.getSolvedValue(var_c));
		}
		EATEST_VERIFY(solver.getSolvedValue(var_a) == (solver.getSolvedValue(var_b) && solver.getSolvedValue(var_c)));
	}
	
	// a=0 b=0 c=0
	// a=0 b=1 c=0
	// a=0 b=0 c=1
	// a=1 b=1 c=1
	EATEST_VERIFY(numResults == 4);
	EATEST_VERIFY(!solver.getStats().nonTightRules);

	return nErrorCount;
}

int TestSolvers::solveRules_basicDisjunction(int seed, bool printVerbose)
{
	int nErrorCount = 0;

	ConstraintSolver solver(TEXT("Rules:BasicDisjunction"), seed);
	
	auto var_a = solver.makeBoolean(TEXT("a"));
	auto var_b = solver.makeBoolean(TEXT("b"));
	auto var_c = solver.makeBoolean(TEXT("c"));

	struct Result
	{
		FormulaResult<0> a, b, c;
	};
	
	auto prg = Program::define([]()
	{
		VXY_FORMULA(a, 0);
		VXY_FORMULA(b, 0);
		VXY_FORMULA(c, 0);

		(a() | b()) <<= c();
		c().choice();
		return Result{a, b, c};
	});

	auto inst = prg();
	inst->getResult().a.bind(var_a);
	inst->getResult().b.bind(var_b);
	inst->getResult().c.bind(var_c);

	solver.addProgram(inst);
	
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
			VERTEXY_LOG("a=%d b=%d c=%d", solver.getSolvedValue(var_a), solver.getSolvedValue(var_b), solver.getSolvedValue(var_c));
		}
		EATEST_VERIFY(!solver.getSolvedValue(var_c) || (solver.getSolvedValue(var_a) != solver.getSolvedValue(var_b)));
		EATEST_VERIFY((solver.getSolvedValue(var_a) || solver.getSolvedValue(var_b)) == solver.getSolvedValue(var_c));
	}
	
	// a=0 b=0 c=0
	// a=1 b=0 c=1
	// a=0 b=1 c=1
	EATEST_VERIFY(numResults == 3);
	EATEST_VERIFY(!solver.getStats().nonTightRules);

	return nErrorCount;
}

int TestSolvers::solveRules_basicCycle(int seed, bool printVerbose)
{
	int nErrorCount = 0;

	ConstraintSolver solver(TEXT("Rules:BasicDisjunction"), seed);
	VERTEXY_LOG("Rules:BasicCycle(%d)", solver.getSeed());

	auto var_a = solver.makeBoolean(TEXT("a"));
	auto var_b = solver.makeBoolean(TEXT("b"));
	auto var_c = solver.makeBoolean(TEXT("c"));

	struct Result
	{
		FormulaResult<0> a, b, c;
	};
	
	auto prg = Program::define([]()
	{
		VXY_FORMULA(a, 0);
		VXY_FORMULA(b, 0);
		VXY_FORMULA(c, 0);

		a() <<= b();
		b() <<= a();
		b() <<= c();
		c().choice();
		return Result{a, b, c};
	});

	auto inst = prg();
	inst->getResult().a.bind(var_a);
	inst->getResult().b.bind(var_b);
	inst->getResult().c.bind(var_c);

	solver.addProgram(inst);
	
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
			VERTEXY_LOG("a=%d b=%d c=%d", solver.getSolvedValue(var_a), solver.getSolvedValue(var_b), solver.getSolvedValue(var_c));
		}
		EATEST_VERIFY(!solver.getSolvedValue(var_c) || (solver.getSolvedValue(var_a) && solver.getSolvedValue(var_b)));
		EATEST_VERIFY(solver.getSolvedValue(var_c) || (!solver.getSolvedValue(var_a) && !solver.getSolvedValue(var_b)));
	}
	
	// a=1 b=1 c=1
	// a=0 b=0 c=0
	EATEST_VERIFY(numResults == 2);
	EATEST_VERIFY(solver.getStats().nonTightRules);

	return nErrorCount;
}

// Simple test of graph relations and negative (~) literals
int TestSolvers::solveProgram_graphTests(int seed, bool printVerbose)
{
	int nErrorCount = 0;

	static constexpr int WIDTH = 5;
	auto topology = make_shared<PlanarGridTopology>(WIDTH, 1);

	struct Output
	{
		FormulaResult<2> graphEdgeTest;
		FormulaResult<1> rightTest;
		FormulaResult<1> negRightTest;
	};
	
	auto prog = Program::define([](ProgramVertex vertex)
	{
		VXY_VARIABLE(X);
		VXY_VARIABLE(Y);
		
		VXY_FORMULA(graphEdgeTest, 2);
		graphEdgeTest(vertex, Y) <<= Program::graphEdge(vertex, Y);
		
		auto right = Program::graphLink(PlanarGridTopology::moveRight());
		
		VXY_FORMULA(rightTest, 1);
		rightTest(vertex) <<= right(vertex, X);

		VXY_FORMULA(negRightTest, 1);
		negRightTest(vertex) <<= ~rightTest(vertex);

		return Output {graphEdgeTest, rightTest, negRightTest};
	});

	ConstraintSolver solver(TEXT("graphTests"), seed);

	auto inst = prog(ITopology::adapt(topology));

	struct GraphEdgeVars { VarID left, right; };
	vector<GraphEdgeVars> graphEdgeTestVars;
	graphEdgeTestVars.resize(WIDTH, {VarID::INVALID, VarID::INVALID});
	vector<VarID> rightTestVars;
	rightTestVars.resize(WIDTH, VarID::INVALID);
	vector<VarID> negRightTestVars;
	negRightTestVars.resize(WIDTH, VarID::INVALID);
	
	inst->getResult().graphEdgeTest.bind([&](const ProgramSymbol& _x, const ProgramSymbol& _y)
	{
		int x = _x.getInt(), y = _y.getInt();
		VarID* dest = y < x ? &graphEdgeTestVars[x].left : &graphEdgeTestVars[x].right;
		vxy_assert(!dest->isValid());
		*dest = solver.makeBoolean(inst->getResult().graphEdgeTest.toString(x,y));
		return *dest;
	});
	inst->getResult().rightTest.bind([&](const ProgramSymbol& _x)
	{
		int x = _x.getInt();
		vxy_assert(!rightTestVars[x].isValid());
		rightTestVars[x] = solver.makeBoolean(inst->getResult().rightTest.toString(x));
		return rightTestVars[x];
	});
	inst->getResult().negRightTest.bind([&](const ProgramSymbol& _x)
	{
		int x = _x.getInt();
		vxy_assert(!negRightTestVars[x].isValid());
		negRightTestVars[x] = solver.makeBoolean(inst->getResult().negRightTest.toString(x));
		return negRightTestVars[x];
	});
	
	solver.addProgram(inst);
	solver.solve();
	EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);

	for (int i = 0; i < WIDTH; ++i)
	{
		EATEST_VERIFY(rightTestVars[i].isValid());
		VarID lvar = graphEdgeTestVars[i].left;
		VarID rvar = graphEdgeTestVars[i].right;
		EATEST_VERIFY(lvar.isValid() || i == 0);
		EATEST_VERIFY(rvar.isValid() || i == WIDTH-1);
		
		int gtL = lvar.isValid() ? solver.getSolvedValue(graphEdgeTestVars[i].left) : 1;
		int gtR = rvar.isValid() ? solver.getSolvedValue(graphEdgeTestVars[i].right) : 1;
		EATEST_VERIFY(gtL == 1);
		EATEST_VERIFY(gtR == 1);

		int sr = solver.getSolvedValue(rightTestVars[i]);
		int nsr = solver.getSolvedValue(negRightTestVars[i]);
		EATEST_VERIFY((i < WIDTH-1 && sr == 1) || (i==WIDTH-1 && sr == 0));
		EATEST_VERIFY(sr != nsr);
		if (printVerbose)
		{
			VERTEXY_LOG("graphEdgeTestVars(%d)   = %d, %d", i, gtL, gtR);
			VERTEXY_LOG("rightTestVars(%d)       = %d", i, sr);
			VERTEXY_LOG("negRightTestVars(%d)    = %d", i, nsr);
		}
	}
	
	solver.dumpStats(printVerbose);
	return nErrorCount;
}

int TestSolvers::solveProgram_hamiltonian(int seed, bool printVerbose)
{
	int nErrorCount = 0;

	// Define the output of the program, so that we can bind instances of the returned formulas to named variables.
	struct HamiltonianOutput
	{
		FormulaResult<2> path;
	};

	// Create a program definition using the rule-definition language.
	// This program finds a hamiltonian cycle: given a graph, find a path that traverses
	// through every node exactly once, and is circular.
	auto hamiltonian = Program::define([]()
	{
		// Define a formula called "node" that has 1 parameter.
		VXY_FORMULA(node, 1);
		// Encode facts about which nodes exist.
		node(0);
		node(1);
		node(2);
		node(3);

		// Define a formula called "edge" that has 2 parameters.
		VXY_FORMULA(edge, 2);
		// Encode facts about which edges exist.
		edge(0, 1);
		edge(0, 2);
		edge(1, 2);
		edge(1, 3);
		edge(2, 0);
		edge(2, 3);
		edge(3, 0);

		// Specify a start point for the cycle
		VXY_FORMULA(start, 1);
		start(0);

		// Declare variables that will be used in rule definitions.
		// These will be expanded during compilation into all possible values.
		VXY_VARIABLE(X);
		VXY_VARIABLE(Y);
		VXY_VARIABLE(Z);

		// Define path(X,Y) and omit(X,Y).
		VXY_FORMULA(path, 2);
		VXY_FORMULA(omit, 2);

		// Circular rules: a given path(X,Y) exists if there is an edge(X,Y) and an omit(X,Y) doesn't exist.
		// Likewise, a given omit(X,Y) exists if there is an edge(X,Y) and a path(X,Y) doesn't exist.
		//
		// There are two other synonymous ways of saying this:
		//
		//		(path(X,Y) | omit(X,Y)) <<= edge(X,Y);
		//		Says **either** a path(X,Y) or emit(X,Y) exists if for any edge(X,Y) that exists.
		//  or
		//		path(X,Y).choice() <<= edge(X,Y);
		//		Says there **can be** a path(X,Y) for any edge(X,Y) that exists.
		//		(Note that "omit" is not needed in this case)
		path(X,Y) <<= ~omit(X,Y) && edge(X,Y);
		omit(X,Y) <<= ~path(X,Y) && edge(X,Y);

		VXY_VARIABLE(X1);
		VXY_VARIABLE(Y1);
		// Program::disallow prevents any solution where the statement is true.
		// Specify that there can't be two paths ending at the same node.
		Program::disallow(path(X,Y) && path(X1, Y) && X < X1);
		// Specify that there can't be two paths starting at the same node.
		Program::disallow(path(X,Y) && path(X, Y1) && Y < Y1);

		VXY_FORMULA(on_path, 1);
		// on_path(X) is only true if there is a path reaching node X and a path leaving node X.
		on_path(Y) <<= path(X, Y) && path(Y, Z);
		// Disallow any node that is not on the path. (~ is logical NOT, i.e. "on_path(X) does not exist")
		Program::disallow(node(X) && ~on_path(X));

		// Ensure that every node is reached.
		VXY_FORMULA(reach, 1);
		// We reach a node if it is the start node.
		reach(X) <<= start(X);
		// We reach a node Y if we reached a node X and a path between X,Y was created.
		reach(Y) <<= reach(X) && path(X, Y);
		Program::disallow(node(X) && ~reach(X));

		// Return the path formula so we can bind named variables to it.
		return HamiltonianOutput{path};
	});

	// create the solver that will generate the solution.
	ConstraintSolver solver(TEXT("hamiltonianProgram"), seed);

	// Instantiate the program. Programs can take arguments and be instantiated multiple times.
	// Note that the formulas are NOT shared between multiple instances of the same program.
	auto inst = hamiltonian();

	//
	// Bind all possible instances of path(X,Y) to named variables.
	// The callback will be called during solve() for every *potential* path(X,Y) that may occur.
	// Note that since there are only 4 nodes in the program, we know the bounds of X and Y for path(X,Y).
	//
	// NOTE: some variables will not be bound if it is determined they cannot possibly occur.
	// E.g. in our example path(0,3) cannot exist because there is no edge(0,3).
	// Therefore pathVars[0][3] will be invalid.
	//

	VarID pathVars[4][4];
	inst->getResult().path.bind([&](const ProgramSymbol& _x, const ProgramSymbol& _y)
	{
		int x = _x.getInt(), y = _y.getInt();
		vxy_assert(!pathVars[x][y].isValid());

		// Create a boolean solver variable to hold the result of this path(x,y).
		wstring varName = inst->getResult().path.toString(x,y);
		VarID var = solver.makeBoolean(varName);

		// Store it and return it as the variable to bind to.
		pathVars[x][y] = var;
		return pathVars[x][y];
	});

	// Add the program to the solver. You can add more than one program; the solver will only
	// report a solution once all added programs have been solved.
	solver.addProgram(inst);
	solver.solve();
	EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);

	// Print out all path(X,Y) that are true, and count the number of times each node was visited.
	int timesVisited[4][4] = {0};

	for (int x = 0; x < 4; ++x)
	{
		for (int y = 0; y < 4; ++y)
		{
			// if a pathVar is invalid, it means it wasn't possibly part of any solution,
			// meaning we can safely treat it as false here.
			if (pathVars[x][y].isValid() && solver.getSolvedValue(pathVars[x][y]) != 0)
			{
				if (printVerbose) { VERTEXY_LOG("%s", solver.getVariableName(pathVars[x][y]).c_str()); }
				timesVisited[x][y]++;
				// check each node was only visited once.
				EATEST_VERIFY(timesVisited[x][y] == 1);
			}
		}
	}

	solver.dumpStats(printVerbose);
	return nErrorCount;
}

int TestSolvers::solveProgram_hamiltonianGraph(int seed, bool printVerbose)
{
	int nErrorCount = 0;

	struct HamiltonianOutput
	{
		FormulaResult<2> path;
	};

	auto topology = make_shared<DigraphTopology>();
	for (int i = 0; i < 4; ++i) topology->addVertex();

	topology->addEdge(0, 1);
	topology->addEdge(0, 2);
	topology->addEdge(1, 2);
	topology->addEdge(1, 3);
	topology->addEdge(2, 0);
	topology->addEdge(2, 3);
	topology->addEdge(3, 0);

	auto hamiltonian = Program::define([](ProgramVertex vertex)
	{
		VXY_VARIABLE(X);
		VXY_VARIABLE(Y);
		VXY_VARIABLE(Z);

		VXY_FORMULA(path, 2);

		path(vertex, X).choice() <<= Program::graphEdge(vertex, X);
		path(X, vertex).choice() <<= Program::graphEdge(X, vertex);

		Program::disallow(path(X, vertex) && path(Y, vertex) && X != Y);
		Program::disallow(path(vertex, X) && path(vertex, Y) && X != Y);

		VXY_FORMULA(on_path, 1);
		on_path(vertex) <<= path(X, vertex) && path(vertex, Y);
		Program::disallow(~on_path(vertex));

		VXY_FORMULA(reach, 1);
		reach(0);
		reach(vertex) <<= reach(X) && path(X, vertex);
		Program::disallow(~reach(vertex));

		return HamiltonianOutput{path};
	});

	ConstraintSolver solver(TEXT("hamiltonianProgram-Graph"), seed);

	auto inst = hamiltonian(ITopology::adapt(topology));

	VarID pathVars[4][4];
	inst->getResult().path.bind([&](const ProgramSymbol& _x, const ProgramSymbol& _y)
	{
		int x = _x.getInt(), y = _y.getInt();
		vxy_assert(!pathVars[x][y].isValid());
		
		// Create a boolean solver variable to hold the result of this path(x,y).
		wstring varName = inst->getResult().path.toString(x,y);
		VarID var = solver.makeBoolean(varName);

		// Store it and return it as the variable to bind to.
		pathVars[x][y] = var;
		return pathVars[x][y];
	});

	solver.addProgram(inst);
	solver.solve();
	EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);

	int timesVisited[4][4] = {0};
	for (int x = 0; x < 4; ++x)
	{
		for (int y = 0; y < 4; ++y)
		{
			if (pathVars[x][y].isValid() && solver.getSolvedValue(pathVars[x][y]) != 0)
			{
				if (printVerbose) { VERTEXY_LOG("%s", solver.getVariableName(pathVars[x][y]).c_str()); }
				timesVisited[x][y]++;
				EATEST_VERIFY(timesVisited[x][y] == 1);
			}
		}
	}

	solver.dumpStats(printVerbose);
	return nErrorCount;
}
