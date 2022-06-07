// Copyright Proletariat, Inc. All Rights Reserved.
#include "AllTests.h"

#include "BasicTests.h"
#include "Maze.h"
#include "NQueens.h"
#include "Sudoku.h"
#include "TowersOfHanoi.h"

using namespace VertexyTests;

void AllTests::runAllTests(int seed, int numTimes)
{
	TestSolvers::solveClauseBasic(numTimes, seed, false);
	TestSolvers::solveInequalityBasic(numTimes, seed, false);
	TestSolvers::solveCardinalityBasic(numTimes, seed, false);
	TestSolvers::solveCardinalityShiftProblem(numTimes, seed, false);
	TestSolvers::solveAllDifferentLarge(numTimes, seed, false);
	TestSolvers::solveAllDifferentSmall(numTimes, seed, false);
	TestSolvers::solveSumBasic(numTimes, seed, false);
	NQueensSolvers::solveUsingGraph(numTimes, 25, seed, false);
	NQueensSolvers::solveUsingTable(numTimes, 25, seed, false);
	NQueensSolvers::solveUsingAllDifferent(numTimes, 25, seed, false);
	MazeSolver::solveUsingRawConstraints(numTimes, 9, 9, seed, false);
	SudokuSolver::solve(numTimes, 0, seed, false);
	TowersOfHanoiSolver::solveTowersGrid(numTimes, 3, seed, false);
}
