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
		static int solveBasic(int times, int seed, bool printVerbose = true);
		static int solveRotationReflection(int times, int seed, bool printVerbose = true);

		static int check(ConstraintSolver& solver, TileSolver& tileSolver);
		static void print(ConstraintSolver& solver, TileSolver& tileSolver);

	private:
		static int solve(int times, int seed, string input, bool allowRotation, bool allowReflection, bool printVerbose = true);
	};

}