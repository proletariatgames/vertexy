// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "ConstraintTypes.h"
#include "tileset/TileSolver.h"

namespace VertexyTests
{
	using namespace Vertexy;

	class TileSet
	{
		TileSet()
		{
		}

	public:
		static int solve(int times, int numRows, int numCols, int seed, bool printVerbose = true);
		static int check(ConstraintSolver* solver, const vector<VarID>& vars);
		static void print(ConstraintSolver& solver, TileSolver& tileSolver);
	};

}