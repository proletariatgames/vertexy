// Copyright Proletariat, Inc. All Rights Reserved.
#include "KnightTourSolver.h"

#include "Maze.h"
#include "ConstraintSolver.h"
#include "EATest/EATest.h"
#include "constraints/ReachabilityConstraint.h"
#include "constraints/TableConstraint.h"
#include "constraints/IffConstraint.h"
#include "decision/LogOrderHeuristic.h"
#include "topology/GridTopology.h"
#include "topology/GraphRelations.h"
#include "topology/IPlanarTopology.h"
#include "util/SolverDecisionLog.h"
#include "variable/SolverVariableDomain.h"

using namespace VertexyTests;

// Move a Knight on a chessboard so that it visits each cell exactly once, and ends up on the tile it started from.
// See https://en.wikipedia.org/wiki/Knight%27s_tour
int KnightTourSolver::solveAtomic(int times, int boardSize, int seed, bool printVerbose)
{
    int nErrorCount = 0;

    ConstraintSolver solver(TEXT("KnightTour"), seed);
    VERTEXY_LOG("KnightTour(%d)", solver.getSeed());

    auto& rdb = solver.getRuleDB();

    vector<vector<vector<vector<AtomID>>>> moves;
    moves.resize(boardSize);
    vector<vector<vector<vector<AtomID>>>> valid;
    valid.resize(boardSize);

    vector<vector<AtomID>> oneMoveEntering, oneMoveLeaving;
    vector<vector<AtomID>> multipleMovesEntering, multipleMovesLeaving;
    vector<vector<AtomID>> reached;

    for (int x1 = 0; x1 < boardSize; ++x1)
    {
        moves[x1].resize(boardSize);
        valid[x1].resize(boardSize);

        oneMoveEntering.resize(boardSize);
        oneMoveLeaving.resize(boardSize);
        multipleMovesEntering.resize(boardSize);
        multipleMovesLeaving.resize(boardSize);
        reached.resize(boardSize);

        for (int y1 = 0; y1 < boardSize; ++y1)
        {
            moves[x1][y1].resize(boardSize);
            valid[x1][y1].resize(boardSize);

            wstring oneEnterName {wstring::CtorSprintf(), TEXT("oneMoveEntering(%d,%d)"), x1, y1};
            oneMoveEntering[x1].push_back(rdb.createAtom(oneEnterName.c_str()));

            wstring oneLeaveName {wstring::CtorSprintf(), TEXT("oneMoveLeaving(%d,%d)"), x1, y1};
            oneMoveLeaving[x1].push_back(rdb.createAtom(oneLeaveName.c_str()));

            wstring multEnterName {wstring::CtorSprintf(), TEXT("multMoveEntering(%d,%d)"), x1, y1};
            multipleMovesEntering[x1].push_back(rdb.createAtom(multEnterName.c_str()));

            wstring multLeaveName {wstring::CtorSprintf(), TEXT("multMoveLeaving(%d,%d)"), x1, y1};
            multipleMovesLeaving[x1].push_back(rdb.createAtom(multLeaveName.c_str()));

            wstring reachedName {wstring::CtorSprintf(), TEXT("reached(%d,%d)"), x1, y1};
            reached[x1].push_back(rdb.createAtom(reachedName.c_str()));

            for (int x2 = 0; x2 < boardSize; ++x2)
            {
                for (int y2 = 0; y2 < boardSize; ++y2)
                {
                    wstring moveName {wstring::CtorSprintf(), TEXT("move(%d,%d,%d,%d)"), x1, y1, x2, y2};
                    moves[x1][y1][x2].push_back(rdb.createAtom(moveName.c_str()));

                    wstring validName {wstring::CtorSprintf(), TEXT("valid(%d,%d,%d,%d)"), x1, y1, x2, y2};
                    valid[x1][y1][x2].push_back(rdb.createAtom(validName.c_str()));

                    // { move(x1,y1,x2,y2) } <- valid(x1,y1,x2,y2).
                    rdb.addRule(TRuleHead(moves[x1][y1][x2][y2], ERuleHeadType::Choice), AtomLiteral(valid[x1][y1][x2][y2], true));
                }
            }
        }
    }

    auto markValidMove = [&](int x1, int y1, int x2, int y2)
    {
        if (x2 >= 0 && x2 < boardSize && y2 >= 0 && y2 < boardSize)
        {
            rdb.addFact(valid[x1][y1][x2][y2]);
        }
    };

    for (int x = 0; x < boardSize; ++x)
    {
        for (int y = 0; y < boardSize; ++y)
        {
            markValidMove(x, y, x-1, y-2);
            markValidMove(x, y, x-1, y+2);
            markValidMove(x, y, x+1, y-2);
            markValidMove(x, y, x+1, y+2);

            markValidMove(x, y, x+2, y-1);
            markValidMove(x, y, x+2, y+1);
            markValidMove(x, y, x-2, y-1);
            markValidMove(x, y, x-2, y+1);
        }
    }

    for (int x1 = 0; x1 < boardSize; ++x1)
    {
        for (int y1 = 0; y1 < boardSize; ++y1)
        {
            // :- not oneMoveEntering(x1,y1).
            // rdb.disallow(-oneMoveEntering[x1][y1])
            rdb.disallow(oneMoveEntering[x1][y1].neg());
            // :- not oneMoveLeaving(x1,y1).
            // rdb.disallow(-oneMoveLeaving[x1][y1])
            rdb.disallow(oneMoveLeaving[x1][y1].neg());

            for (int x2 = 0; x2 < boardSize; ++x2)
            {
                for (int y2 = 0; y2 < boardSize; ++y2)
                {
                    // oneMoveEntering[x1][y1] << moves[x2][y2][x1][y1].and(-multipleMovesEntering[x1][y1])
                    // oneMoveEntering(x1,y1) <- move(x2,y2,x1,y1), not multipleMovesEntering(x1,y1).
                    rdb.addRule(oneMoveEntering[x1][y1], vector{
                        moves[x2][y2][x1][y1].pos(),
                        multipleMovesEntering[x1][y1].neg()
                    });

                    // oneMoveLeaving[x1][y1] << moves[x1][y1][x2][y2].and(-multipleMovesLeaving[x1][y1])
                    // oneMoveLeaving(x1,y1) <- move(x1,y1,x2,y2), not multipleMovesLeaving(x1, y1)
                    rdb.addRule(oneMoveLeaving[x1][y1], vector{
                        moves[x1][y1][x2][y2].pos(),
                        multipleMovesLeaving[x1][y1].neg()
                    });

                    for (int x3 = 0; x3 < boardSize; ++x3)
                    {
                        for (int y3 = 0; y3 < boardSize; ++y3)
                        {
                            if (x2 != x3 || y2 != y3)
                            {
                                // multipleMovesEntering(x1,y1) :- move(x2,y2,x1,y1), move(x3,y3,x1,y1), (x2 != x3 or y2 != y3).
                                rdb.addRule(multipleMovesEntering[x1][y1], vector{
                                    moves[x2][y2][x1][y1].pos(),
                                    moves[x3][y3][x1][y1].pos()
                                });
                                // multipleMovesLeaving(x1,y1) :- move(x1,y1,x2,y2), move(x1,y1,x3,y3), (x2 != x3 or y2 != y3).
                                rdb.addRule(multipleMovesLeaving[x1][y1], vector{
                                    moves[x1][y1][x2][y2].pos(),
                                    moves[x1][y1][x3][y3].pos()
                                });
                            }
                        }
                    }
                }
            }
        }
    }

    for (int x1 = 0; x1 < boardSize; ++x1)
    {
        for (int y1 = 0; y1 < boardSize; ++y1)
        {
            // reached(x1,y1) <- move(0,0,x1,y1).
            rdb.addRule(reached[x1][y1], moves[0][0][x1][y1].pos());

            for (int x2 = 0; x2 < boardSize; ++x2)
            {
                for (int y2 = 0; y2 < boardSize; ++y2)
                {
                    // reached(x1,y1) <- reached(x2,y2), move(x2,y2,x1,y1).
                    rdb.addRule(reached[x1][y1], vector{
                        reached[x2][y2].pos(),
                        moves[x2][y2][x1][y1].pos()
                    });
                }
            }

            // :- reached(x1,y1).
            rdb.disallow(reached[x1][y1].neg());
        }
    }

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
                int dx = -1, dy = -1;
                for (int x1 = 0; x1 < boardSize; ++x1)
                {
                    for (int y1 = 0; y1 < boardSize; ++y1)
                    {
                        if (solver.isAtomTrue(moves[cx][cy][x1][y1]))
                        {
                            dx = x1;
                            dy = y1;
                            EATEST_VERIFY(solver.isAtomTrue(valid[cx][cy][dx][dy]));
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

int KnightTourSolver::solvePacked(int times, int boardSize, int seed, bool printVerbose)
{
    int nErrorCount = 0;

    ConstraintSolver solver(TEXT("KnightTour-Packed"), seed);
    VERTEXY_LOG("KnightTour-Packed(%d)", solver.getSeed());

    auto& rdb = solver.getRuleDB();

    vector<vector<VarID>> moves;
    moves.resize(boardSize);
    vector<vector<vector<AtomID>>> valid;
    valid.resize(boardSize);
    vector<vector<AtomID>> reached;
    reached.resize(boardSize);

    vector<VarID> allMoves;

    SolverVariableDomain moveDomain(0, boardSize*boardSize);
    for (int x1 = 0; x1 < boardSize; ++x1)
    {
        valid[x1].resize(boardSize);
        for (int y1 = 0; y1 < boardSize; ++y1)
        {
            wstring reachedName {wstring::CtorSprintf(), TEXT("reached(%d,%d)"), x1, y1};
            reached[x1].push_back(rdb.createAtom(reachedName.c_str()));

            wstring moveName {wstring::CtorSprintf(), TEXT("move(%d,%d)"), x1, y1};
            moves[x1].push_back(solver.makeVariable(moveName, moveDomain));
            allMoves.push_back(moves[x1].back());

            valid[x1][y1].resize(boardSize*boardSize);

            for (int x2 = 0; x2 < boardSize; ++ x2) for (int y2 = 0; y2 < boardSize; ++y2)
            {
                int pos2 = x2 + y2*boardSize;

                wstring validName {wstring::CtorSprintf(), TEXT("valid(%d,%d,%d,%d)"), x1, y1, x2, y2};

                valid[x1][y1][pos2] = rdb.createAtom(validName.c_str());

                //
                // atom m = rdb.getAtom(SignedClause(moves[x1][y1], {pos2}));
                // m.choice() << valid[x1][y1];


                // { move(x1,y1,x2,y2) } <- valid(x1,y1,x2,y2).
                rdb.addRule(TRuleHead(SignedClause(moves[x1][y1], {pos2}), ERuleHeadType::Choice), valid[x1][y1][pos2].pos());
            }
        }
    }

    auto markValidMove = [&](int x1, int y1, int x2, int y2)
    {
        if (x2 >= 0 && x2 < boardSize && y2 >= 0 && y2 < boardSize)
        {
            int offs = x2 + y2*boardSize;
            rdb.addFact(valid[x1][y1][offs]);
        }
    };

    for (int x = 0; x < boardSize; ++x)
    {
        for (int y = 0; y < boardSize; ++y)
        {
            markValidMove(x, y, x-1, y-2);
            markValidMove(x, y, x-1, y+2);
            markValidMove(x, y, x+1, y-2);
            markValidMove(x, y, x+1, y+2);

            markValidMove(x, y, x+2, y-1);
            markValidMove(x, y, x+2, y+1);
            markValidMove(x, y, x-2, y-1);
            markValidMove(x, y, x-2, y+1);
        }
    }

    solver.allDifferent(allMoves);

    for (int x1 = 0; x1 < boardSize; ++x1)
    {
        for (int y1 = 0; y1 < boardSize; ++y1)
        {
            // reached(x1,y1) <- move(0,0,x1,y1).
            int pos = x1 + y1*boardSize;
            // Atom m = rdb.getAtom(SignedClause(moves[0][0], {pos});
            // reached[x][y] << m
            rdb.addRule(reached[x1][y1], SignedClause(moves[0][0], {pos}));

            for (int x2 = 0; x2 < boardSize; ++x2)
            {
                for (int y2 = 0; y2 < boardSize; ++y2)
                {
                    // reached(x1,y1) <- reached(x2,y2), move(x2,y2,x1,y1).
                    // Atom m2 = rdb.getAtom(SignedClause(moves[x2][y2], {pos});
                    // reached[x1][y1] << reached[x2][y2].and(m2);
                    rdb.addRule(reached[x1][y1], vector<AnyLiteralType>{
                        reached[x2][y2].pos(),
                        SignedClause(moves[x2][y2], {pos})
                    });
                }
            }

            // :- reached(x1,y1).
            // rdb.disallow(-reached[x1][y1]);
            rdb.disallow(reached[x1][y1].neg());
        }
    }

    vector<bool> hit;
    hit.resize(boardSize*boardSize, false);
    for (int time = 0; time < times; ++time)
    {
        solver.solve();
        EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
        solver.dumpStats(printVerbose);

        if (solver.getCurrentStatus() == EConstraintSolverResult::Solved)
        {
            int cx = 0, cy = 0;
            do
            {
                int solved = solver.getSolvedValue(moves[cx][cy]);
                int dx = solved%boardSize;
                int dy = solved/boardSize;
                EATEST_VERIFY(solver.isAtomTrue(valid[cx][cy][solved]));

                next:
                EATEST_VERIFY(dx >= 0 && dy >= 0);
                if (printVerbose)
                {
                    VERTEXY_LOG("(%d, %d) -> (%d, %d)", cx, cy, dx, dy);
                }
                if (dx >= 0 && dy >= 0)
                {
                    hit[dx + dy*boardSize] = true;
                }

                cx = dx;
                cy = dy;
            } while (cx > 0 || cy > 0);
        }

        EATEST_VERIFY(!contains(hit.begin(), hit.end(), false));
    }

    return nErrorCount;
}
