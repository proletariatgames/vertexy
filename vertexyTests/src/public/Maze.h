// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "topology/TopologyVertexData.h"

namespace VertexyTests
{

using namespace Vertexy;
class MazeSolver
{
	MazeSolver()
	{
	}

public:
	static int solveUsingRawConstraints(int times, int numRows, int numCols, int seed, bool printVerbose=true);
	static int check(const shared_ptr<TTopologyVertexData<VarID>>& Cells, const ConstraintSolver& solver);
	static void print(const shared_ptr<TTopologyVertexData<VarID>>& cells, const shared_ptr<TTopologyVertexData<VarID>>& edges, const ConstraintSolver& solver);

	static int solveUsingProgram(int times, int numRows, int numCols, int seed, bool printVerbose=true);
	static int solveUsingGraphProgram(int times, int numRows, int numCols, int seed, bool printVerbose=true);
};

}