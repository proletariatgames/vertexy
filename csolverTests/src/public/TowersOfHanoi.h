// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"

namespace csolverTests
{

using namespace csolver;

class TowersOfHanoiSolver
{
	TowersOfHanoiSolver() {}

public:
	static int solveTowersGrid(int Times, int N, int Seed, bool bPrint = true);
	static int solveTowersDiskBased(int Times, int N, int Seed, bool bPrint = true);
	static int solverTowersDiskBasedGraph(int Times, int N, int Seed, bool bPrint = true);

	static int check(int N, ConstraintSolver* Solver, const vector<vector<VarID>>* Vars);
	static void print(int N, ConstraintSolver* Solver, const vector<vector<VarID>>* Vars, const vector<VarID>& Moved);
	static void printDiskBased(int numDisks, ConstraintSolver* solver, const vector<VarID>& move, const vector<VarID>& moveDest, const vector<vector<VarID>>& diskOn);

};

}