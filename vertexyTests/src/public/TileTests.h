// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "ConstraintSolver.h"
#include "prefab/TileSolver.h"

namespace VertexyTests
{
	using namespace Vertexy;

	//class ConstraintSolver;
	//class TilingImplicit;

	class TileTests
	{
		TileTests()
		{
		}

	public:
		static int solveImplicit(int times, int seed, bool printVerbose = true);
		static int check();
		static void print(ConstraintSolver& solver, TileSolver& tileSolver);
	};

}