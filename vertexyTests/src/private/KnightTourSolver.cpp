// Copyright Proletariat, Inc. All Rights Reserved.
#include "KnightTourSolver.h"

#include "Maze.h"
#include "ConstraintSolver.h"
#include "EATest/EATest.h"
#include "constraints/ReachabilityConstraint.h"
#include "constraints/TableConstraint.h"
#include "constraints/IffConstraint.h"
#include "decision/LogOrderHeuristic.h"
#include "program/ProgramDSL.h"
#include "topology/GridTopology.h"
#include "topology/GraphRelations.h"
#include "topology/IPlanarTopology.h"
#include "util/SolverDecisionLog.h"
#include "variable/SolverVariableDomain.h"

using namespace VertexyTests;

// Move a Knight on a chessboard so that it visits each cell exactly once, and ends up on the tile it started from.
// See https://en.wikipedia.org/wiki/Knight%27s_tour
int KnightTourSolver::solve(int times, int boardSize, int seed, bool printVerbose)
{
    int nErrorCount = 0;
    constexpr int BOARD_SIZE = 6;

    VXY_DOMAIN_BEGIN(BoardDomain)
        VXY_DOMAIN_VALUE_ARRAY(dest, BOARD_SIZE*BOARD_SIZE);
    VXY_DOMAIN_END()

    auto prg = Program::define([&](ProgramVertex vertex)
    {
        VXY_WILDCARD(X); VXY_WILDCARD(Y);

        // All valid Knight moves on a chessboard:
        vector validMoves = {
            Program::graphLink(PlanarGridTopology::moveLeft(1).combine(PlanarGridTopology::moveUp(2))),
            Program::graphLink(PlanarGridTopology::moveRight(1).combine(PlanarGridTopology::moveDown(2))),
            Program::graphLink(PlanarGridTopology::moveRight(1).combine(PlanarGridTopology::moveUp(2))),            
            Program::graphLink(PlanarGridTopology::moveLeft(1).combine(PlanarGridTopology::moveDown(2))),
            Program::graphLink(PlanarGridTopology::moveRight(2).combine(PlanarGridTopology::moveDown(1))),
            Program::graphLink(PlanarGridTopology::moveLeft(2).combine(PlanarGridTopology::moveUp(1))),
            Program::graphLink(PlanarGridTopology::moveRight(2).combine(PlanarGridTopology::moveUp(1))),
            Program::graphLink(PlanarGridTopology::moveLeft(2).combine(PlanarGridTopology::moveDown(1))),
        };
        vector reverseMoves = {1, 0, 3, 2, 5, 4, 7, 6};
        VXY_FORMULA(validMove, 2);
        for (int i = 0; i < validMoves.size(); ++i)
        {
            validMove(vertex,X) <<= validMoves[i](vertex, X);
            validMove(X,vertex) <<= validMoves[reverseMoves[i]](vertex, X);
        }

        VXY_DOMAIN_FORMULA(knightMove, BoardDomain, 1);
        knightMove(vertex).is(knightMove.dest[Y]).choice() <<= validMove(vertex,Y);
        
        // ensure every cell is reached
        VXY_FORMULA(reached, 1);
        reached(vertex) <<= knightMove(0).is(knightMove.dest[vertex]);
        reached(vertex) <<= reached(X) && validMove(X, vertex) && knightMove(X).is(knightMove.dest[vertex]);
        Program::disallow(~reached(vertex));

        return knightMove;
    });

    ConstraintSolver solver(TEXT("KnightsTour"), seed);
    
    auto grid = make_shared<PlanarGridTopology>(BOARD_SIZE, BOARD_SIZE);
    auto tileMoves = solver.makeVariableGraph(TEXT("moves"), ITopology::adapt(grid), SolverVariableDomain(0, BOARD_SIZE*BOARD_SIZE-1), TEXT("move"));
    
    auto inst = prg(ITopology::adapt(grid));
    inst->getResult().bind(solver, [&](const ProgramSymbol& _src)
    {
        return tileMoves->get(_src.getInt());
    });

    solver.allDifferent(tileMoves->getData());
    
    solver.addProgram(inst);
    
    auto isValid = [](int x1, int y1, int x2, int y2)
    {
        if (abs(x1 - x2) == 2) return abs(y1-y2) == 1;
        else if (abs(y1 - y2) == 2) return abs(x1-x2) == 1;
        return false;
    };
    
    for (int time = 0; time < times; ++time)
    {
        solver.solve();
        EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
        solver.dumpStats(printVerbose);
    
        if (solver.getCurrentStatus() == EConstraintSolverResult::Solved)
        {
            vector<bool> hit;
            hit.resize(BOARD_SIZE*BOARD_SIZE, false);

            int cx = 0, cy = 0;
            do
            {
                int vertex = grid->coordinateToIndex(cx, cy);
                int solved = solver.getSolvedValue(tileMoves->get(vertex));
                int dx = solved%BOARD_SIZE;
                int dy = solved/BOARD_SIZE;
                EATEST_VERIFY(isValid(cx, cy, dx, dy));
            
                next:
                EATEST_VERIFY(dx >= 0 && dy >= 0);
                if (printVerbose)
                {
                    VERTEXY_LOG("(%d, %d) -> (%d, %d)", cx, cy, dx, dy);
                }
                if (dx >= 0 && dy >= 0)
                {
                    hit[dx + dy*BOARD_SIZE] = true;
                }
            
                cx = dx;
                cy = dy;
            } while (cx > 0 || cy > 0);
    
            EATEST_VERIFY(!contains(hit.begin(), hit.end(), false));
        }
    }
    
    return nErrorCount;
}
