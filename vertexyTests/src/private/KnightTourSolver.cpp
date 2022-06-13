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

    auto prg = Program::define([&](ProgramVertex vertex)
    {
        VXY_VARIABLE(X); VXY_VARIABLE(Y);

        // All valid Knight moves on a chessboard:
        vector validMoves = {
            Program::graphLink(PlanarGridTopology::moveLeft(1).combine(PlanarGridTopology::moveUp(2))),
            Program::graphLink(PlanarGridTopology::moveLeft(1).combine(PlanarGridTopology::moveDown(2))),
            Program::graphLink(PlanarGridTopology::moveRight(1).combine(PlanarGridTopology::moveUp(2))),
            Program::graphLink(PlanarGridTopology::moveRight(1).combine(PlanarGridTopology::moveDown(2))),
            Program::graphLink(PlanarGridTopology::moveRight(2).combine(PlanarGridTopology::moveUp(1))),
            Program::graphLink(PlanarGridTopology::moveRight(2).combine(PlanarGridTopology::moveDown(1))),
            Program::graphLink(PlanarGridTopology::moveLeft(2).combine(PlanarGridTopology::moveUp(1))),
            Program::graphLink(PlanarGridTopology::moveLeft(2).combine(PlanarGridTopology::moveDown(1))),
        };
        vector reverseMoves = {
            Program::graphLink(PlanarGridTopology::moveRight(1).combine(PlanarGridTopology::moveDown(2))),
            Program::graphLink(PlanarGridTopology::moveRight(1).combine(PlanarGridTopology::moveUp(2))),
            Program::graphLink(PlanarGridTopology::moveLeft(1).combine(PlanarGridTopology::moveDown(2))),
            Program::graphLink(PlanarGridTopology::moveLeft(1).combine(PlanarGridTopology::moveUp(2))),
            Program::graphLink(PlanarGridTopology::moveLeft(2).combine(PlanarGridTopology::moveDown(1))),
            Program::graphLink(PlanarGridTopology::moveLeft(2).combine(PlanarGridTopology::moveUp(1))),
            Program::graphLink(PlanarGridTopology::moveRight(2).combine(PlanarGridTopology::moveDown(1))),
            Program::graphLink(PlanarGridTopology::moveRight(2).combine(PlanarGridTopology::moveUp(1))),
        };
        VXY_FORMULA(validMove, 2);
        for (int i = 0; i < validMoves.size(); ++i)
        {
            validMove(vertex,X) <<= validMoves[i](vertex, X);
            validMove(X,vertex) <<= reverseMoves[i](vertex, X);
        }

        VXY_FORMULA(knightMove, 2);
        knightMove(vertex, X).choice() <<= validMove(vertex,X);
        knightMove(X, vertex).choice() <<= validMove(X,vertex);

        // ensure each tile is entered/exited once.
        VXY_FORMULA(twoMovesEntering, 1);
        twoMovesEntering(vertex) <<= knightMove(X,vertex) && knightMove(Y,vertex) && X != Y;
        VXY_FORMULA(twoMovesLeaving, 1);
        twoMovesLeaving(vertex) <<= knightMove(vertex,X) && knightMove(vertex,Y) && X != Y;
        VXY_FORMULA(singleMoveEntering, 1);
        singleMoveEntering(vertex) <<= knightMove(X,vertex) && ~twoMovesEntering(vertex);
        VXY_FORMULA(singleMoveLeaving, 1);
        singleMoveLeaving(vertex) <<= knightMove(vertex,X) && ~twoMovesLeaving(vertex);

        Program::disallow(~singleMoveEntering(vertex));
        Program::disallow(~singleMoveLeaving(vertex));
        
        // ensure every cell is reached
        VXY_FORMULA(reached, 1);
        reached(0) <<= knightMove(0, X);
        reached(vertex) <<= reached(X) && knightMove(X, vertex);
        Program::disallow(~reached(vertex));

        return knightMove;
    });

    ConstraintSolver solver(TEXT("KnightsTour"), seed);
    
    auto grid = make_shared<PlanarGridTopology>(boardSize, boardSize);
    vector<vector<VarID>> possibleMoves;
    possibleMoves.resize(boardSize*boardSize);
    
    for (int y1 = 0; y1 < boardSize; ++y1) for (int x1 = 0; x1 < boardSize; ++x1) 
    {
        possibleMoves[x1+y1*boardSize].resize(boardSize*boardSize);
    }
    
    auto inst = prg(ITopology::adapt(grid));
    inst->getResult().bind(solver, [&](const ProgramSymbol& _src, const ProgramSymbol& _dest)
    {
        int src = _src.getInt(), dest = _dest.getInt();
        auto& var = possibleMoves[src][dest];
        vxy_assert(!var.isValid());

        int x1, y1, x2, y2;
        grid->indexToCoordinate(src, x1, y1);
        grid->indexToCoordinate(dest, x2, y2);

        wstring name;
        name.sprintf(TEXT("knightMove(%dx%d, %dx%d)"), x1, y1, x2, y2);

        var = solver.makeBoolean(name);
        return var;
    });
    
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
            hit.resize(boardSize*boardSize, false);
            
            int cx = 0, cy = 0;
            do
            {
                auto& movesForTile = possibleMoves[cx+cy*boardSize];
                int dx = -1, dy = -1;
                for (int x1 = 0; x1 < boardSize; ++x1)
                {
                    for (int y1 = 0; y1 < boardSize; ++y1)
                    {
                        if (movesForTile[x1+y1*boardSize].isValid() && solver.getSolvedValue(movesForTile[x1+y1*boardSize]) != 0)
                        {
                            dx = x1;
                            dy = y1;
                            EATEST_VERIFY(isValid(cx, cy, dx, dy));
                            goto next;
                        }
                    }
                }
                
                next:
                EATEST_VERIFY(dx >= 0 && dy >= 0);
                if (printVerbose)
                {
                    VERTEXY_LOG("(%d, %d) -> (%d, %d)", cx, cy, dx, dy);
                }
                if (dx >= 0 && dy >= 0)
                {
                    hit[dx + boardSize*dy] = true;
                }
                
                cx = dx;
                cy = dy;                
            } while (cx > 0 || cy > 0);        
    
            EATEST_VERIFY(!contains(hit.begin(), hit.end(), false));
        }
    }
    
    return nErrorCount;
}
