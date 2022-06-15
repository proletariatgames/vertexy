// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

namespace Vertexy
{

enum class EConstraintSolverResult : uint8_t
{
    // We have not yet started solving anything.
    Uninitialized,
    // We have not yet finished solving for all variables in the system.
    Unsolved,
    // We have arrived at a full solution for the system, with all variables having a value.
    Solved,
    // We have fully searched the search tree without finding a solution, i.e. no solution exists.
    Unsatisfiable
};

}