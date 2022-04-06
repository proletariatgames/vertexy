// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "topology/TopologyVertexData.h"

namespace csolverTests
{

using namespace csolver;
class MazeSolver
{
	MazeSolver()
	{
	}

public:
	static int solve(int times, int numRows, int numCols, int seed, bool printVerbose=true);
	static int check(const shared_ptr<TTopologyVertexData<VarID>>& Cells, const ConstraintSolver& solver);
	static void print(const shared_ptr<TTopologyVertexData<VarID>>& cells, const shared_ptr<TTopologyVertexData<VarID>>& edges, const ConstraintSolver& solver);
};

}