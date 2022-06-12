// SolverTest.cpp : Defines the entry point for the application.
//

#include "SolverTest.h"

#include <BasicTests.h>
#include <NQueens.h>
#include <EATest/EATest.h>
#include <Sudoku.h>
#include <TowersOfHanoi.h>
#include <PrefabTest.h>

#include "KnightTourSolver.h"
#include "ds/ValueBitset.h"
#include "Maze.h"

using namespace Vertexy;

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
	Suite.AddTest("ValueBitset", TestSolvers::bitsetTests);
	Suite.AddTest("Digraph", TestSolvers::digraphTests);
	Suite.AddTest("RuleSCCs", TestSolvers::ruleSCCTests);
	Suite.AddTest("Clause-Basic", []() { return TestSolvers::solveClauseBasic(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Inequality-Basic", []() { return TestSolvers::solveInequalityBasic(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Cardinality-Basic", []() { return TestSolvers::solveCardinalityBasic(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Cardinality-Shift", []() { return TestSolvers::solveCardinalityShiftProblem(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("AllDifferent-Small", []() { return TestSolvers::solveAllDifferentSmall(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("AllDifferent-Large", []() { return TestSolvers::solveAllDifferentLarge(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Rules-BasicChoice", []() { return TestSolvers::solveRules_basicChoice(FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Rules-BasicDisjunction", []() { return TestSolvers::solveRules_basicDisjunction(FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Rules-BasicCycle", []() { return TestSolvers::solveRules_basicCycle(FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Rules-BasicGraph", []() { return TestSolvers::solveProgram_graphTests(FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Rules-Hamiltonian", []() { return TestSolvers::solveProgram_hamiltonian(FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Rules-HamiltonianGraph", []() { return TestSolvers::solveProgram_hamiltonianGraph(FORCE_SEED, PRINT_VERBOSE); });
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
	Suite.AddTest("PrefabTest-Basic", []() { return PrefabTestSolver::solveBasic(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("PrefabTest-Json", []() { return PrefabTestSolver::solveJson(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("PrefabTest-Rot/Refl", []() { return PrefabTestSolver::solveRotationReflection(NUM_TIMES, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("MazeProgram", []() { return MazeSolver::solveUsingGraphProgram(NUM_TIMES, MAZE_NUM_ROWS, MAZE_NUM_COLS, FORCE_SEED, PRINT_VERBOSE); });
	Suite.AddTest("Maze", []() { return MazeSolver::solveUsingRawConstraints(NUM_TIMES, MAZE_NUM_ROWS, MAZE_NUM_COLS, FORCE_SEED, PRINT_VERBOSE); });
	return Suite.Run();
}
