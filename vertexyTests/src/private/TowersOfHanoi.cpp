// Copyright Proletariat, Inc. All Rights Reserved.
#include "TowersOfHanoi.h"
#include "EATest/EATest.h"
#include "ConstraintSolver.h"
#include "constraints/DisjunctionConstraint.h"
#include "topology/GraphRelations.h"
#include "topology/GridTopology.h"
#include "topology/IPlanarTopology.h"
#include "variable/SolverVariableDomain.h"

using namespace VertexyTests;

int TowersOfHanoiSolver::solveTowersGrid(int times, int numDisks, int seed, bool bPrint)
{
	int nErrorCount = 0;
	for (int time = 0; time < times; ++time)
	{
		ConstraintSolver solver(TEXT("Towers-Of-Hanoi"), seed);

		const int turns = (1 << numDisks); //2^n
		const int numPegs = 3;

		vector<vector<VarID>> pegs[numPegs];

		vector<VarID> moved;
		moved.reserve(turns);
		for (int turn = 1; turn < turns; turn++)
		{
			moved.push_back(solver.makeVariable({ wstring::CtorSprintf(), TEXT("M-%d"), turn }, { 0, 1, 2 }));
		}

		//Pegs[NumPegs][Turns][numDisks]
		for (int turn = 0; turn < turns; ++turn)
		{
			if (turn == 0 || turn == turns-1)
			{
				int fullPegInd = turn == 0 ? 0 : 1;
				int emptyPegInds[2] = { turn == 0 ? 1 : 0, 2 };

				vector<VarID> fullPeg;
				for (int i = 0; i < numDisks; ++i)
				{
					fullPeg.push_back(solver.makeVariable({ wstring::CtorSprintf(), TEXT("%d-%d-%d"), fullPegInd, turn, i }, { numDisks - i }));
				}

				pegs[fullPegInd].push_back(fullPeg);

				for (int pegIndInd = 0; pegIndInd < 2; ++pegIndInd)
				{
					int pegInd = emptyPegInds[pegIndInd];
					vector<VarID> emptyPeg;
					for (int i = 0; i < numDisks; ++i)
					{
						emptyPeg.push_back(solver.makeVariable({ wstring::CtorSprintf(), TEXT("%d-%d-%d"), pegInd, turn, i }, { 0 }));
					}

					pegs[pegInd].push_back(emptyPeg);
				}
			}
			else
			{
				for (int pegInd = 0; pegInd < numPegs; ++pegInd)
				{
					vector<VarID> unknownPeg;
					for (int i = 0; i < numDisks; ++i)
					{
						unknownPeg.push_back(solver.makeVariable(
							{ wstring::CtorSprintf(), TEXT("%d-%d-%d"), pegInd, turn, i },
							SolverVariableDomain(0, numDisks)));
					}

					pegs[pegInd].push_back(unknownPeg);
				}
			}
		}

		for (int peg = 0; peg < numPegs; ++peg)
		{
			for (auto& pegV : pegs[peg])
			{
				for (int i = 1; i < numDisks; ++i)
				{
					solver.inequality(pegV[i], EConstraintOperator::LessThanEq, pegV[i - 1]);
				}
			}
		}

		hash_map<int, tuple<int, int>> cardinalities;
		cardinalities[0] = make_tuple(2 * numDisks, 2 * numDisks);
		for (int i = 1; i <= numDisks; ++i)
		{
			cardinalities[i] = make_tuple(1, 1);
		}

		for (int turn = 1; turn < turns - 1; ++turn)
		{
			vector<VarID> snapshot;
			for (int peg = 0; peg < numPegs; ++peg)
			{
				auto& toInsert = pegs[peg][turn];
				snapshot.insert(snapshot.end(), toInsert.begin(), toInsert.end());
			}
			solver.cardinality(snapshot, cardinalities);
		}
		vector<int> zero{ 0 };

		for (int turn = 1; turn < turns; ++turn)
		{
			for (int peg = 0; peg < numPegs; ++peg)
			{
				for (int i = 1; i < numDisks; ++i)
				{
					// For testing Disjunctions. Equivalent clauses are commented out.
					auto notYourTurnClause = solver.clause({
						// either not your turn...
						SignedClause(moved[turn - 1], EClauseSign::Outside, vector<int>{peg}),
						// or you were not a disk last turn...
						SignedClause(pegs[peg][turn - 1][i], zero)
					});

					// vector<SignedClause> clauses;
					// clauses.reserve(4);
					// //if it's your turn (if it's not your turn see below)
					// clauses.push_back(SignedClause(moved[turn - 1], EClauseSign::Inside, vector<int>{peg}));
					// //if you were a disc last turn.
					// clauses.push_back(SignedClause(pegs[peg][turn - 1][i], EClauseSign::Outside, zero));

					//last turn cannot be different from this turn for the one below you.
					for (int ii = 0; ii <= numDisks; ++ii)
					{
						for (int jj = 0; jj <= numDisks; ++jj)
						{
							if (ii != jj)
							{
								auto ng = solver.nogood({
									SignedClause(pegs[peg][turn - 1][i - 1], { ii }),
									SignedClause(pegs[peg][turn][i - 1], { jj })
								});
								solver.disjunction(notYourTurnClause, ng);

								// clauses.push_back(SignedClause(pegs[peg][turn - 1][i - 1], { ii }));
								// clauses.push_back(SignedClause(pegs[peg][turn][i - 1], { jj }));
								//
								// solver.nogood(clauses);
								//
								// clauses.pop_back();
								// clauses.pop_back();
							}
						}
					}
				}

				for (int i = 0; i < numDisks; ++i)
				{
					vector<SignedClause> clauses;
					clauses.reserve(3);
					//if you are NOT the active peg
					clauses.push_back(SignedClause(moved[turn - 1], EClauseSign::Outside, vector<int>{peg}));

					//you shouldn't have changed value
					for (int ii = 1; ii <= numDisks; ++ii) //doesn't apply to 0
					{
						for (int jj = 0; jj <= numDisks; ++jj)
						{
							if (ii != jj)
							{
								clauses.push_back(SignedClause(pegs[peg][turn - 1][i], { ii }));
								clauses.push_back(SignedClause(pegs[peg][turn][i], { jj }));

								solver.nogood(clauses);

								clauses.pop_back();
								clauses.pop_back();
							}
						}
					}
				}
			}
		}

		solver.solve();
		auto solved = solver.getCurrentStatus();

		solver.dumpStats(bPrint);


		EATEST_VERIFY(solver.getCurrentStatus() == EConstraintSolverResult::Solved);
		if (bPrint) print(numDisks, &solver, pegs, moved);
		nErrorCount += check(numDisks, &solver, pegs);

	}
	return nErrorCount;
}

int TowersOfHanoiSolver::solveTowersDiskBased(int times, int numDisks, int seed, bool printVerbose)
{
	int nErrorCount = 0;
	for (int run = 0; run < times; ++run)
	{
		ConstraintSolver solver(TEXT("Towers-Of-Hanoi"), seed);

		const int numTurns = (1 << numDisks); //2^n
		const int numPegs = 3;

		// move[turn] = disk moving this turn
		vector<VarID> move;
		// moveDest[turn] = destination disk/peg
		vector<VarID> moveDest;
		// diskOn[turn][disk] = disk/peg the disk is on this turn
		vector<vector<VarID>> diskOn;

		auto diskDomain = SolverVariableDomain(0, numDisks-1);
		auto pegsPlusDisksDomain = SolverVariableDomain(0, numDisks+numPegs-1);
		for (int turn = 0; turn < numTurns; ++turn)
		{
			move.push_back(solver.makeVariable({wstring::CtorSprintf(), TEXT("move-%d"), turn}, diskDomain));
			moveDest.push_back(solver.makeVariable({wstring::CtorSprintf(), TEXT("dest-%d"), turn}, pegsPlusDisksDomain));

			diskOn.push_back({});
			auto& diskOnThisTurn = diskOn.back();

			diskOnThisTurn.reserve(numDisks);
			for (int disk = 0; disk < numDisks; ++disk)
			{
				diskOnThisTurn.push_back(solver.makeVariable({wstring::CtorSprintf(), TEXT("turn-%d-disk-%d-on"), turn, disk}, pegsPlusDisksDomain));
			}

			if (turn > 0)
			{
				// Encode movement: if we moved previous turn, next move we should be on the destination.
				// Also, if we didn't move this turn, we should remain on the same thing.
				for (int disk = 0; disk < numDisks; ++disk)
				{
					for (int diskOrPeg = 0; diskOrPeg < numPegs+numDisks; ++diskOrPeg)
					{
						solver.nogood({
							SignedClause(move[turn-1], {disk}),
							SignedClause(moveDest[turn-1], {diskOrPeg}),
							SignedClause(diskOnThisTurn[disk], EClauseSign::Outside, {diskOrPeg})
						});
						solver.nogood({
							SignedClause(move[turn-1], EClauseSign::Outside, {disk}),
							SignedClause(diskOn[turn-1][disk], {diskOrPeg}),
							SignedClause(diskOnThisTurn[disk], EClauseSign::Outside, {diskOrPeg})
						});
					}
				}
			}

			// encode placement: only one thing can be on a thing at a time
			solver.allDifferent(diskOnThisTurn);

			// Constrain movement (note: disk indices are ordered from largest to smallest)
			for (int disk = 0; disk < numDisks; ++disk)
			{
				if (disk < numDisks-1)
				{
					vector<int> smallerDisks = {};
					for (int smallerDisk=disk+1; smallerDisk < numDisks; ++smallerDisk)
					{
						smallerDisks.push_back(numPegs+smallerDisk);

						// can't move if a disk is on us
						solver.nogood({
							SignedClause(move[turn], {disk}),
							SignedClause(SignedClause(diskOnThisTurn[smallerDisk], {numPegs+disk}))
						});
					}

					// bigger disks can't ever be on smaller disks
					solver.nogood({
						SignedClause(diskOnThisTurn[disk], smallerDisks)
					});
				}

				// can't move onto a disk or peg that has something already on it
				for (int destDiskOrPeg = 0; destDiskOrPeg < numPegs+numDisks; ++destDiskOrPeg)
				{
					solver.nogood({
						SignedClause(moveDest[turn], {destDiskOrPeg}),
						SignedClause(diskOnThisTurn[disk], {destDiskOrPeg})
					});
				}

				// can't move onto yourself
				solver.nogood({
					SignedClause(move[turn], {disk}),
					SignedClause(moveDest[turn], {numPegs+disk})
				});

				// Don't move the same disk twice in a row
				if (turn > 0 && turn < numTurns-1)
				{
					solver.nogood({
						SignedClause(move[turn], {disk}),
						SignedClause(move[turn-1], {disk})
					});
				}
			}
		}

		solver.setInitialValues(diskOn[0][0], {0}); // largest disk starts on first peg
		solver.setInitialValues(diskOn[numTurns-1][0], {1}); // largest disk ends on second peg
		// remaining disks should start/end on top of the next largest disk.
		for (int i = 1; i < numDisks; ++i)
		{
			solver.setInitialValues(diskOn[0][i], {numPegs+(i-1)});
			solver.setInitialValues(diskOn[numTurns-1][i], {numPegs+(i-1)});
		}

		auto result = solver.solve();
		solver.dumpStats(printVerbose);
		EATEST_VERIFY(result == EConstraintSolverResult::Solved);

		if (printVerbose)
		{
			printDiskBased(numDisks, &solver, move, moveDest, diskOn);
		}
	}
	return nErrorCount;
}

int TowersOfHanoiSolver::solverTowersDiskBasedGraph(int times, int numDisks, int seed, bool printVerbose)
{
	int nErrorCount = 0;
	for (int run = 0; run < times; ++run)
	{
		ConstraintSolver solver(TEXT("Towers-Of-Hanoi"), seed);

		const int numTurns = (1 << numDisks); //2^n
		const int numPegs = 3;

		auto diskDomain = SolverVariableDomain(0, numDisks-1);
		auto pegsPlusDisksDomain = SolverVariableDomain(0, numDisks+numPegs-1);

		auto timeGraph = make_shared<PlanarGridTopology>(numTurns, 1);

		// diskOn[turn][disk] = disk/peg the disk is on this turn
		auto diskOnData = make_shared<TTopologyVertexData<vector<VarID>>>(ITopology::adapt(timeGraph), vector<VarID>{}, TEXT("diskOn"));
		// move[turn] = disk moving this turn
		auto moveData = solver.makeVariableGraph(TEXT("moves"), ITopology::adapt(timeGraph), diskDomain, TEXT("move-"));
		// moveDest[turn] = destination disk/peg
		auto moveDestData = solver.makeVariableGraph(TEXT("moveDests"), ITopology::adapt(timeGraph), pegsPlusDisksDomain, TEXT("moveDest-"));

		//
		// graph relations
		//

		auto prevTurnRel = make_shared<TopologyLinkIndexGraphRelation>(ITopology::adapt(timeGraph), PlanarGridTopology::moveLeft());

		auto curMoveRel = make_shared<TVertexToDataGraphRelation<VarID>>(ITopology::adapt(timeGraph), moveData);
		auto prevMoveRel = prevTurnRel->map(curMoveRel);

		auto curMoveDestRel = make_shared<TVertexToDataGraphRelation<VarID>>(ITopology::adapt(timeGraph), moveDestData);
		auto prevMoveDestRel = prevTurnRel->map(curMoveDestRel);

		auto disksOnRel = make_shared<TVertexToDataGraphRelation<vector<VarID>>>(ITopology::adapt(timeGraph), diskOnData);
		auto prevDisksOnRel = prevTurnRel->map(disksOnRel);

		//
		// encode placement: only one thing can be on a thing at a time
		//

		for (int turn = 0; turn < numTurns; ++turn)
		{
			auto& diskOnThisTurn = diskOnData->get(turn);

			diskOnThisTurn.reserve(numDisks);
			for (int disk = 0; disk < numDisks; ++disk)
			{
				diskOnThisTurn.push_back(solver.makeVariable({wstring::CtorSprintf(), TEXT("turn-%d-disk-%d-on"), turn, disk}, pegsPlusDisksDomain));
			}

			solver.allDifferent(diskOnThisTurn);
		}

	 	for (int disk = 0; disk < numDisks; ++disk)
	 	{
	 		GraphVariableRelationPtr diskOnThisTurnRel = make_shared<TArrayAccessGraphRelation<VarID>>(disksOnRel, disk);
	 		GraphVariableRelationPtr diskOnLastTurnRel = make_shared<TArrayAccessGraphRelation<VarID>>(prevDisksOnRel, disk);

	 		// Encode movement: if we moved previous turn, next move we should be on the destination.
	 		// Also, if we didn't move this turn, we should remain on the same thing.
	 		for (int diskOrPeg = 0; diskOrPeg < numPegs+numDisks; ++diskOrPeg)
	 		{
	 			solver.makeGraphConstraint<ClauseConstraint>(timeGraph, ENoGood::NoGood,
	 				GraphRelationClause(prevMoveRel, {disk}),
	 				GraphRelationClause(prevMoveDestRel, {diskOrPeg}),
	 				GraphRelationClause(diskOnThisTurnRel, EClauseSign::Outside, {diskOrPeg})
	 			);
	 			solver.makeGraphConstraint<ClauseConstraint>(timeGraph, ENoGood::NoGood,
	 				GraphRelationClause(prevMoveRel, EClauseSign::Outside, {disk}),
	 				GraphRelationClause(diskOnLastTurnRel, {diskOrPeg}),
	 				GraphRelationClause(diskOnThisTurnRel, EClauseSign::Outside, {diskOrPeg})
	 			);
	 		}

		 	// Constrain movement (note: disk indices are ordered from largest to smallest)
	 		if (disk < numDisks-1)
	 		{
	 			vector<int> smallerDisks = {};
	 			for (int smallerDisk=disk+1; smallerDisk < numDisks; ++smallerDisk)
	 			{
	 				auto smallerDiskOnThisTurnRel = make_shared<TArrayAccessGraphRelation<VarID>>(disksOnRel, smallerDisk);
	 				smallerDisks.push_back(numPegs+smallerDisk);

	 				// can't move if a disk is on us
	 				solver.makeGraphConstraint<ClauseConstraint>(timeGraph, ENoGood::NoGood,
	 					GraphRelationClause(curMoveRel, {disk}),
	 					GraphRelationClause(smallerDiskOnThisTurnRel, {numPegs+disk})
	 				);
	 			}

	 			// bigger disks can't ever be on smaller disks
	 			solver.makeGraphConstraint<ClauseConstraint>(timeGraph, ENoGood::NoGood,
	 				GraphRelationClause(diskOnThisTurnRel, smallerDisks)
	 			);
	 		}

	 		// can't move onto a disk or peg that has something already on it
	 		for (int destDiskOrPeg = 0; destDiskOrPeg < numPegs+numDisks; ++destDiskOrPeg)
	 		{
	 			solver.makeGraphConstraint<ClauseConstraint>(timeGraph, ENoGood::NoGood,
	 				GraphRelationClause(curMoveDestRel, {destDiskOrPeg}),
	 				GraphRelationClause(diskOnThisTurnRel, {destDiskOrPeg})
	 			);
	 		}

	 		// can't move onto yourself
	 		solver.makeGraphConstraint<ClauseConstraint>(timeGraph, ENoGood::NoGood,
	 			GraphRelationClause(curMoveRel, {disk}),
	 			GraphRelationClause(curMoveDestRel, {numPegs+disk})
	 		);
		}

		// Don't move the same disk twice in a row
		// (not graph constraints, because last turn is excluded since it doesn't move anything)
		for (int turn = 1; turn < numTurns-1; ++turn)
		{
			for (int disk = 0; disk < numDisks; ++disk)
			{
				solver.nogood({
					SignedClause(moveData->get(turn), {disk}),
					SignedClause(moveData->get(turn-1), {disk})
				});
			}
		}

	 	solver.setInitialValues(diskOnData->get(0)[0], {0}); // largest disk starts on first peg
	 	solver.setInitialValues(diskOnData->get(numTurns-1)[0], {1}); // largest disk ends on second peg
	 	// remaining disks should start/end on top of the next largest disk.
	 	for (int i = 1; i < numDisks; ++i)
	 	{
	 		solver.setInitialValues(diskOnData->get(0)[i], {numPegs+(i-1)});
	 		solver.setInitialValues(diskOnData->get(numTurns-1)[i], {numPegs+(i-1)});
	 	}

	 	auto result = solver.solve();
	 	solver.dumpStats(printVerbose);
	 	EATEST_VERIFY(result == EConstraintSolverResult::Solved);

		if (printVerbose)
		{
			printDiskBased(numDisks, &solver, moveData->getData(), moveDestData->getData(), diskOnData->getData());
		}
	}
	return nErrorCount;
}

void TowersOfHanoiSolver::print(int numDisks, ConstraintSolver* solver, const vector<vector<VarID>>* vars, const vector<VarID>& moved)
{
	const int turns = (1 << numDisks); //2^n
	const int numPegs = 3;

	for (int turn = 0; turn < turns; ++turn)
	{
		for (int i = numDisks - 1; i >= 0; --i)
		{
			wstring rowS = { wstring::CtorSprintf(), TEXT("%d %d %d"),
				solver->getSolvedValue(vars[0][turn][i]),
				solver->getSolvedValue(vars[1][turn][i]),
				solver->getSolvedValue(vars[2][turn][i])};
			VERTEXY_LOG("%s", rowS.c_str());
		}
		if (turn == 0)
		{
			VERTEXY_LOG("-----");
		}
		else
		{
			VERTEXY_LOG("-----%d", solver->getSolvedValue(moved[turn - 1]));
		}

	}
}

void TowersOfHanoiSolver::printDiskBased(int numDisks, ConstraintSolver* solver, const vector<VarID>& move, const vector<VarID>& moveDest, const vector<vector<VarID>>& diskOn)
{
	const int numTurns = 1<<numDisks;
	const int numPegs = 3;
	for (int turn = 0; turn < numTurns; ++turn)
	{
		VERTEXY_LOG("Turn %d:", turn);
		vector<int> onMe;
		onMe.resize(numDisks+numPegs, -1);
		for (int i = 0; i < numDisks; ++i)
		{
			int on = solver->getSolvedValue(diskOn[turn][i]);
			onMe[on] = numPegs+i;
		}

		for (int peg = 0; peg < numPegs; ++peg)
		{
			int cur = onMe[peg];
			wstring pegS = TEXT("-");
			while (cur != -1)
			{
				pegS.append_sprintf(TEXT("%d"), cur-numPegs);
				cur = onMe[cur];
			}
			VERTEXY_LOG("%s", pegS.c_str());
		}
		VERTEXY_LOG("Move %d->%d", solver->getSolvedValue(move[turn]), solver->getSolvedValue(moveDest[turn]));
	}
}

void moveBetween(int numDisks, vector<vector<int>>* pegs, int fromTurn, int xPeg, int yPeg)
{
	int topX = numDisks;
	int topY = numDisks;
	for (int i = numDisks - 1; i >= 0; --i)
	{
		if (pegs[xPeg][fromTurn][i] == 0)
		{
			topX = i;
		}
		if (pegs[yPeg][fromTurn][i] == 0)
		{
			topY = i;
		}
	}

	if (topX == 0)
	{//move from Y to X
		pegs[xPeg][fromTurn + 1][topX] = pegs[yPeg][fromTurn][topY - 1];
		pegs[yPeg][fromTurn + 1][topY - 1] = 0;
	}
	else if (topY == 0)
	{//move from X to Y
		int foo = pegs[yPeg][fromTurn][topX - 1];
		pegs[yPeg][fromTurn + 1][topY] = pegs[xPeg][fromTurn][topX - 1];
		pegs[xPeg][fromTurn + 1][topX - 1] = 0;
	}
	else if (pegs[xPeg][fromTurn][topX - 1] > pegs[yPeg][fromTurn][topY - 1])
	{//move from Y to X
		pegs[xPeg][fromTurn + 1][topX] = pegs[yPeg][fromTurn][topY - 1];
		pegs[yPeg][fromTurn + 1][topY - 1] = 0;
	}
	else if (pegs[yPeg][fromTurn][topY - 1] > pegs[xPeg][fromTurn][topX - 1])
	{//move from X to Y
		pegs[yPeg][fromTurn + 1][topY] = pegs[xPeg][fromTurn][topX - 1];
		pegs[xPeg][fromTurn + 1][topX - 1] = 0;
	}
}


int TowersOfHanoiSolver::check(int numDisks, ConstraintSolver* solver, const vector<vector<VarID>>* vars)
{
	int nErrorCount = 0;

	const int turns = 1 << numDisks;
	const int numPegs = 3;
	vector<vector<int>> correctSequence[numPegs];
	const int pegA = 0;
	const int pegB = numDisks % 2 ? 1 : 2;
	const int pegC = numDisks % 2 ? 2 : 1;

	vector<int> fullPeg;
	fullPeg.reserve(numDisks);
	for (int i = 0; i < numDisks; ++i)
	{
		fullPeg.push_back(numDisks - i);
	}

	vector<int> emptyPeg;
	emptyPeg.reserve(numDisks);
	for (int i = 0; i < numDisks; ++i)
	{
		emptyPeg.push_back(0);
	}

	correctSequence[0].push_back(fullPeg);
	for (int peg = 1; peg < numPegs; ++peg)
	{
		correctSequence[peg].push_back(emptyPeg);
	}


	for (int turn = 1; turn < turns; ++turn)
	{
		for (int peg = 0; peg < numPegs; ++peg)
		{
			vector<int> toAdd;
			vector<int> toInsert = correctSequence[peg][turn - 1];
			toAdd.insert(toAdd.end(), toInsert.begin(), toInsert.end());
			correctSequence[peg].push_back(toAdd);
		}

		int turnType = (turn-1) % 3;

		if (turnType == 0)
		{
			moveBetween(numDisks, correctSequence, turn - 1, pegA, pegB);
		}
		else if (turnType == 1)
		{
			moveBetween(numDisks, correctSequence, turn - 1, pegA, pegC);
		}
		else
		{
			moveBetween(numDisks, correctSequence, turn - 1, pegB, pegC);
		}

		for (int peg = 0; peg < numPegs; ++peg)
		{
			for (int i = 0; i < numDisks; ++i)
			{
				if (solver->getSolvedValue(vars[peg][turn][i]) != correctSequence[peg][turn][i])
				{
					++nErrorCount;
				}
			}
		}
	}
	return nErrorCount;
}
