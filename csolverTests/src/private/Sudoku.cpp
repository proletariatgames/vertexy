// Copyright Proletariat, Inc. All Rights Reserved.
#include "Sudoku.h"

#include "ConstraintSolver.h"
#include "EATest/EATest.h"
#include "constraints/TableConstraint.h"
#include "topology/GridTopology.h"
#include "topology/IPlanarTopology.h"
#include "variable/SolverVariableDomain.h"

using namespace csolverTests;

int SudokuSolver::solve(int times, int n, int seed, bool printVerbose)
{
	int nErrorCount = 0;

	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("Sudoku"), seed);

		// Initialize vars
		SolverVariableDomain domain(1, 9);
		vector<VarID> variables;
		vector<vector<VarID>> rows;
		vector<vector<VarID>> columns;
		vector<vector<VarID>> squares;

		for (int i = 0; i < 9; ++i)
		{
			rows.emplace_back(vector<VarID>{});
			columns.emplace_back(vector<VarID>{});
			squares.emplace_back(vector<VarID>{});
		}

		// Create a variable for each slot in the sudoku puzzle and assign them to the various rows/cols/squares
		for (int i = 0; i < 81; ++i)
		{
			int row = i / 9;
			int col = i % 9;

			VarID var = solver.makeVariable({wstring::CtorSprintf(), TEXT("SudokuVar[%d-%d]"), row, col}, domain);
			variables.push_back(var);

			rows[row].push_back(var);
			columns[col].push_back(var);
			int squareNum = 3 * (row / 3) + (col / 3);
			squares[squareNum].push_back(var);
		}

		// Ensure all values in each row/col/square are different
		for (int i = 0; i < 9; ++i)
		{
			solver.allDifferent(rows[i]);
			solver.allDifferent(columns[i]);
			solver.allDifferent(squares[i]);
		}

		// Initialize the puzzle with N values
		//initializePuzzle(n, &solver, variables, printVerbose);

		solver.solve();
		solver.dumpStats(printVerbose);

		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
		if (printVerbose)
		{
			print(&solver, variables);
		}
		for (int i = 0; i < 9; ++i)
		{
			nErrorCount += check(&solver, rows[i]);
			nErrorCount += check(&solver, columns[i]);
			nErrorCount += check(&solver, squares[i]);
		}
	}

	return nErrorCount;
}

void SudokuSolver::initializePuzzle(int n, ConstraintSolver* solver, const vector<VarID>& vars, bool printVerbose)
{
	// Make sure the given number of constants is valid
	if (n < 0)
	{
		n = 0;
	}
	else if (n > 80)
	{
		n = 80;
	}

	for (int i = 0; i < n; ++i)
	{
	}

	// Print out the initial puzzle before it's solved
	if (printVerbose)
	{
		print(solver, vars);
	}
}

void SudokuSolver::print(ConstraintSolver* solver, const vector<VarID>& vars)
{
	for (int row = 0; row < 9; ++row)
	{
		wstring outString;
		for (int col = 0; col < 9; ++col)
		{
			VarID val = vars[(int)(row * 9 + col)];
			if (solver->isSolved(val))
			{
				outString += {wstring::CtorSprintf(), TEXT("[%d]"), solver->getSolvedValue(val)};
			}
			else
			{
				outString += TEXT("[ ]");
			}
		}
		CS_LOG("%s", outString.c_str());
	}
}

// Pass in a row, column, or square to ensure every valid value is represented exactly once
int SudokuSolver::check(ConstraintSolver* solver, const vector<VarID>& vars)
{
	int nErrorCount = 0;

	for (int i = 0; i < 9; ++i)
	{
		bool foundValue = false;

		for (int j = 1; j < 10; ++j)
		{
			if (solver->getSolvedValue(vars[i]) == j)
			{
				if (foundValue)
				{
					nErrorCount++;
				}

				foundValue = true;
			}
		}

		if (!foundValue)
		{
			nErrorCount++;
		}
	}

	return nErrorCount;
}
