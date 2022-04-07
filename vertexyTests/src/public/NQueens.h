// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "ConstraintTypes.h"

namespace VertexyTests
{

using namespace Vertexy;
class NQueensSolvers
{
	NQueensSolvers()
	{
	}

public:
	static int solveUsingAllDifferent(int times, int n, int seed, bool printVerbose = true);
	static int solveUsingTable(int times, int n, int seed, bool printVerbose = true);
	static int solveUsingGraph(int times, int n, int seed, bool printVerbose = true);

	static int check(int n, ConstraintSolver* solver, const vector<VarID>& vars);
	static void print(int n, ConstraintSolver* solver, const vector<VarID>& vars);
};

}