// SolverTest.cpp : Defines the entry point for the application.
//

#include "SolverTest.h"

#include <BasicTests.h>
#include <NQueens.h>
#include <EATest/EATest.h>
#include <EASTL/set.h>
#include <Sudoku.h>
#include <TowersOfHanoi.h>
#include <PrefabTest.h>

#include "ConstraintSolver.h"
#include "KnightTourSolver.h"
#include "util/Asserts.h"
#include "ds/ValueBitset.h"
#include "Maze.h"
#include "ds/ESTree.h"
#include "program/ProgramCompiler.h"
#include "program/ProgramDSL.h"
#include "topology/DigraphTopology.h"
#include "topology/GridTopology.h"

using namespace Vertexy;

int test_ValueBitset()
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

int test_Digraph()
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

int test_ruleSCCs()
{
	int nErrorCount = 0;

	ConstraintSolver solver;
	auto& rdb = solver.getRuleDB();
	auto a = rdb.createAtom(TEXT("a"));
	auto b = rdb.createAtom(TEXT("b"));
	auto c = rdb.createAtom(TEXT("c"));
	auto d = rdb.createAtom(TEXT("d"));
	auto e = rdb.createAtom(TEXT("e"));

	rdb.addRule(a, b.neg());
	rdb.addRule(b, a.neg());
	rdb.addRule(c, a.pos());
	rdb.addRule(c, vector{b.pos(), d.pos()});
	rdb.addRule(d, vector{b.pos(), c.pos()});
	rdb.addRule(d, e.pos());
	rdb.addRule(e, vector{b.pos(), a.neg()});
	rdb.addRule(e, vector{c.pos(), d.pos()});

	rdb.finalize();

	EATEST_VERIFY(rdb.getAtom(a)->scc < 0);
	EATEST_VERIFY(rdb.getAtom(b)->scc < 0);
	EATEST_VERIFY(rdb.getAtom(c)->scc >= 0);
	EATEST_VERIFY(rdb.getAtom(d)->scc == rdb.getAtom(c)->scc);
	EATEST_VERIFY(rdb.getAtom(e)->scc == rdb.getAtom(c)->scc);
	return nErrorCount;
}

static constexpr int FORCE_SEED = 0;
static constexpr int NUM_TIMES = 10;
static constexpr int MAZE_NUM_ROWS = 15;
static constexpr int MAZE_NUM_COLS = 15;
static constexpr int NQUEENS_SIZE = 25;
static constexpr int SUDOKU_STARTING_HINTS = 0;
static constexpr int TOWERS_NUM_DISCS = 3;
static constexpr int KNIGHT_BOARD_DIM = 6;
static constexpr bool PRINT_VERBOSE = false;

int main(int argc, char* argv[])
{
	using namespace EA::UnitTest;
	using namespace VertexyTests;

	TestApplication Suite("Solver Tests", argc, argv);
	Suite.AddTest("ValueBitset", test_ValueBitset);
	Suite.AddTest("Digraph", test_Digraph);
	Suite.AddTest("RuleSCCs", test_ruleSCCs);
	Suite.AddTest("Clause-Basic", []() { return TestSolvers::solveClauseBasic(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Inequality-Basic", []() { return TestSolvers::solveInequalityBasic(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Cardinality-Basic", []() { return TestSolvers::solveCardinalityBasic(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Cardinality-Shift", []() { return TestSolvers::solveCardinalityShiftProblem(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("AllDifferent-Small", []() { return TestSolvers::solveAllDifferentSmall(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("AllDifferent-Large", []() { return TestSolvers::solveAllDifferentLarge(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Rules-BasicChoice", []() { return TestSolvers::solveRules_basicChoice(FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Rules-BasicDisjunction", []() { return TestSolvers::solveRules_basicDisjunction(FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Rules-BasicCycle", []() { return TestSolvers::solveRules_basicCycle(FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Rules-BasicIncomplete", []() { return TestSolvers::solveRules_incompleteCycle(FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Rules-BasicIncomplete", []() { return TestSolvers::solveProgram_hamiltonian(FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Sum-Basic", []() { return TestSolvers::solveSumBasic(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Sudoku", []() { return SudokuSolver::solve(NUM_TIMES, SUDOKU_STARTING_HINTS, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("TowersOfHanoi", []() { return TowersOfHanoiSolver::solveTowersGrid(NUM_TIMES, TOWERS_NUM_DISCS, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("TowersOfHanoi", []() { return TowersOfHanoiSolver::solveTowersDiskBased(NUM_TIMES, TOWERS_NUM_DISCS, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("TowersOfHanoi", []() { return TowersOfHanoiSolver::solverTowersDiskBasedGraph(NUM_TIMES, TOWERS_NUM_DISCS, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("KnightTourPacked", []() { return KnightTourSolver::solvePacked(NUM_TIMES, KNIGHT_BOARD_DIM, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("KnightTour", []() { return KnightTourSolver::solveAtomic(NUM_TIMES, KNIGHT_BOARD_DIM, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("NQueens-AllDifferent", []() { return NQueensSolvers::solveUsingAllDifferent(NUM_TIMES, NQUEENS_SIZE, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("NQueens-Table", []() { return NQueensSolvers::solveUsingTable(NUM_TIMES, NQUEENS_SIZE, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("NQueens-Graph", []() { return NQueensSolvers::solveUsingGraph(NUM_TIMES, NQUEENS_SIZE, FORCE_SEED, PRINT_VERBOSE); });
	//Suite.AddTest("PrefabTest-Basic", []() { return PrefabTestSolver::solveBasic(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("MazeProgram", []() { return MazeSolver::solveProgram(NUM_TIMES, MAZE_NUM_ROWS, MAZE_NUM_COLS, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Maze", []() { return MazeSolver::solveKeyDoor(NUM_TIMES, MAZE_NUM_ROWS, MAZE_NUM_COLS, FORCE_SEED, PRINT_VERBOSE); });
	return Suite.Run();
}
