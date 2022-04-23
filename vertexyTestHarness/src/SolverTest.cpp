// SolverTest.cpp : Defines the entry point for the application.
//

#include "SolverTest.h"

#include <BasicTests.h>
#include <NQueens.h>
#include <EATest/EATest.h>
#include <EASTL/set.h>
#include <Sudoku.h>
#include <TowersOfHanoi.h>

#include "ConstraintSolver.h"
#include "KnightTourSolver.h"
#include "util/Asserts.h"
#include "ds/ValueBitset.h"
#include "Maze.h"
#include "ds/ESTree.h"
#include "program/Program.h"
#include "program/Program.h"
#include "topology/DigraphTopology.h"

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

	// Rule formulas can only be defined within a Program::define() block
	auto simpleMaze = Program::define([](int width, int height, int entranceX, int entranceY, int exitX, int exitY)
	{
		// Floating variables. These don't mean anything outside the context of a rule statement.
		// Within a rule statement, they encode equality. E.g. if "X" shows up in two places in a rule,
		// it means that those Xs are the same. See below.
		FormulaParameter X, Y, X1, Y1;

		// define col(1), col(2), ... col(width) as atoms.
		Formula<1> col = Program::range(1, width);
		// define row(1), row(2), ... row(height) as atoms.
		Formula<1> row = Program::range(1, height);

		// define a rule grid(X,Y), which is only true if X is a column and Y is a row.
		Formula<2> grid;
		grid(X,Y) <<= col(X) && row(Y);

		// define a rule formula adjacent(x1,y1,x2,y2), which is only true for two adjacent tiles.
		Formula<4> adjacent;
		adjacent(X,Y,X,Y1) <<= grid(X,Y) && Y1 == Y + 1 && row(Y1);
		adjacent(X,Y,X,Y1) <<= grid(X,Y) && Y1 == Y - 1 && row(Y1);
		adjacent(X,Y,X1,Y) <<= grid(X,Y) && X1 == X + 1 && col(X1);
		adjacent(X,Y,X1,Y) <<= grid(X,Y) && X1 == X - 1 && col(X1);

		// Define a rule formula border(x,y), which is only true at the edges of the map.
		Formula<2> border;
		border(1,Y) <<= row(Y);
		border(X,1) <<= col(X);
		border(X,Y) <<= row(Y) && X == width;
		border(X,Y) <<= col(X) && Y == height;

		// define the entrance/exit positions, based on the program inputs.
		Formula<2> entrance, exit;
		entrance(X,Y) <<= X == entranceX && Y == entranceY;
		exit(X,Y) <<= X == exitX && Y == exitY;

		Formula<2> wall, empty;
		// wall OR empty may be true if this is a grid tile that is not on the border and not the entrance or exit.
		(wall(X,Y) | empty(X,Y)) <<= grid(X,Y) && -border(X,Y) && -entrance(X,Y) && -exit(X,Y);
		// border is a wall as long as it's not the entrance or exit.
		wall(X,Y) <<= border(X,Y) && -entrance(X,Y) && -exit(X,Y);

		// entrance/exit are always empty.
		empty(X,Y) <<= entrance(X,Y);
		empty(X,Y) <<= exit(X,Y);

		// disallow a 2x2 block of walls
		Program::disallow(wall(X,Y) && wall(X1, Y) && wall(X, Y1) && wall(X1, Y1) && X1 == X+1 && Y1 == Y+1);
		// disallow a 2x2 block of empty
		Program::disallow(empty(X,Y) && empty(X1, Y) && empty(X, Y1) && empty(X1, Y1) && X1 == X+1 && Y1 == Y+1);

		// If two walls are on a diagonal of a 2 x 2 square, both common neighbors should not be empty.
		Program::disallow(wall(X,Y) && wall(X1+1,Y1+1) && empty(X1+1,Y) && empty(X, Y1+1));
		Program::disallow(wall(X+1,Y) && wall(X,Y+1) && empty(X, Y) && empty(X+1, Y1+1));

		// wallWithAdjacentWall(x,y) is only true when there is an adjacent cell that is a wall.
		Formula<2> wallWithAdjacentWall;
		wallWithAdjacentWall(X,Y) <<= wall(X,Y) && adjacent(X, Y, X1, Y1) && wall(X1, Y1);

		// disallow walls that don't have any adjacent walls
		Program::disallow(wall(X,Y) && -border(X,Y) && -wallWithAdjacentWall(X,Y));

		// encode reachability (faster to do this with a reachability constraint)
		Formula<2> reach;
		reach(X,Y) <<= entrance(X,Y);
		reach(X1,Y1) <<= adjacent(X,Y,X1,Y1) && reach(X,Y) && empty(X1,Y1);
		Program::disallow(empty(X,Y) && -reach(X,Y));
	});

	// give an instantiated version of the program to the solver.
	// this is an addition to whatever other constraints you want to add!
	solver.addProgram(simpleMaze(10, 10, /*entrance:*/1,5, /*exit:*/5,10));
	solver.solve();


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
	Suite.AddTest("Maze", []() { return MazeSolver::solve(NUM_TIMES, MAZE_NUM_ROWS, MAZE_NUM_COLS, FORCE_SEED, PRINT_VERBOSE); });
	return Suite.Run();
}
