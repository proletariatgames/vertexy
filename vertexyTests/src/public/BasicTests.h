// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"

namespace VertexyTests
{

using namespace Vertexy;
class TestSolvers
{
	TestSolvers()
	{
	}

public:
	static int solveAllDifferentSmall(int times, int seed, bool printVerbose = true);
	static int solveAllDifferentLarge(int times, int seed, bool printVerbose = true);
	static int solveCardinalityBasic(int times, int seed, bool printVerbose = true);
	static int solveCardinalityShiftProblem(int times, int seed, bool printVerbose = true);
	static int solveClauseBasic(int times, int seed, bool printVerbose = true);
	static int solveInequalityBasic(int times, int seed, bool printVerbose = true);
	static int solveSumBasic(int times, int seed, bool printVerbose = true);
	static int solveRules_basicChoice(int seed, bool printVerbose = true);
	static int solveRules_basicDisjunction(int seed, bool printVerbose = true);
	static int solveRules_basicCycle(int seed, bool printVerbose = true);
	static int solveRules_incompleteCycle(int seed, bool printVerbose = true);
	static int solveProgram_graphTests(int seed, bool printVerbose = true);
	static int solveProgram_hamiltonian(int seed, bool printVerbose = true);
	static int solveProgram_hamiltonianGraph(int seed, bool printVerbose = true);
};

}