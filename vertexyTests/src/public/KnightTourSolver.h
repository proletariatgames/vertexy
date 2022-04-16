// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "topology/TopologyVertexData.h"

namespace VertexyTests
{

using namespace Vertexy;

class KnightTourSolver
{
    KnightTourSolver()
    {
    }

public:
    static int solve(int times, int boardSize, int seed, bool printVerbose=true);
};

}