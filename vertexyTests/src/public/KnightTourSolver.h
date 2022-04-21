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
    static int solveAtomic(int times, int boardSize, int seed, bool printVerbose=true);
    static int solvePacked(int times, int boardSize, int seed, bool printVerbose=true);
};

}