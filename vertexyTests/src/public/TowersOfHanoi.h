// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"

namespace VertexyTests
{

using namespace Vertexy;

class TowersOfHanoiSolver
{
	TowersOfHanoiSolver() {}

public:
	static int solve(int Times, int Seed, bool bPrint = true);

	static int check(ConstraintSolver* Solver, const vector<vector<VarID>>* Vars);
	static void print(const ConstraintSolver* solver, const vector<VarID>& move, const vector<VarID>& moveDest, const vector<vector<VarID>>& diskOn);

};

}