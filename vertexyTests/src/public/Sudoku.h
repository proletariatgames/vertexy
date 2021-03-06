// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "ConstraintTypes.h"

namespace VertexyTests
{
using namespace Vertexy;

class SudokuSolver
{
	SudokuSolver()
	{
	}

public:
	static int solve(int times, int n, int seed, bool printVerbose = true);

	static void initializePuzzle(ConstraintSolver* solver, const vector<VarID>& vars, bool printVerbose);
	static int check(ConstraintSolver* solver, const vector<VarID>& vars);
	static void print(ConstraintSolver* solver, const vector<VarID>& vars);
};

}