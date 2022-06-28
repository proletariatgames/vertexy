// Copyright Proletariat, Inc. All Rights Reserved.
#include "TowersOfHanoi.h"
#include "EATest/EATest.h"
#include "ConstraintSolver.h"
#include "constraints/DisjunctionConstraint.h"
#include "program/ProgramDSL.h"
#include "topology/GraphRelations.h"
#include "topology/GridTopology.h"
#include "topology/IPlanarTopology.h"
#include "variable/SolverVariableDomain.h"

using namespace VertexyTests;

static constexpr int NUM_PEGS = 3;
static constexpr int NUM_DISCS = 4;
static constexpr int NUM_TURNS = (1 << NUM_DISCS); //2^n

int TowersOfHanoiSolver::solve(int times, int seed, bool printVerbose)
{
	int nErrorCount = 0;
	for (int run = 0; run < times; ++run)
	{
		ConstraintSolver solver(TEXT("Towers-Of-Hanoi"), seed);
		
		VXY_DOMAIN_BEGIN(LocationDomain)
			VXY_DOMAIN_VALUE_ARRAY(loc, NUM_PEGS+NUM_DISCS);
		VXY_DOMAIN_END()

		constexpr int FIRST_DISC_IDX = NUM_PEGS;

		struct Result
		{
			FormulaResult<1, LocationDomain> move;
			FormulaResult<1, LocationDomain> where;
			FormulaResult<2, LocationDomain> discOn;
		};
		
		auto prg = Program::define([&](ProgramVertex time)
		{
			VXY_WILDCARD(DISC);
			VXY_WILDCARD(LOCATION);

			// location(N) exists for every location (pegs + discs)
			VXY_FORMULA(location, 1);
			location = Program::range(0, NUM_PEGS+NUM_DISCS-1);
			
			// disc(N) exists for every location that is not a peg
			VXY_FORMULA(isDisc, 1);
			isDisc = Program::range(NUM_PEGS, NUM_PEGS+NUM_DISCS-1);
			
			VXY_FORMULA(start, 2);
			VXY_FORMULA(end, 2);
			// biggest disc starts on first peg
			start(FIRST_DISC_IDX, 0);
			// biggest disc ends on last peg
			end(FIRST_DISC_IDX, NUM_PEGS-1);
			// other discs start/end on top of the next-largest disc.
			start(DISC, DISC-1) <<= isDisc(DISC) && start(DISC-1, LOCATION);
			end(DISC, DISC-1) <<= isDisc(DISC) && end(DISC-1, LOCATION);
			
			// move(time) == the disc that moved at this time
			VXY_DOMAIN_FORMULA(move, LocationDomain, 1);
			// Choose a move to make each turn
			move(time).is(move.loc[DISC]).choice() <<= isDisc(DISC);
			
			// where(time) == the location the moved disc moves to this turn 
			VXY_DOMAIN_FORMULA(where, LocationDomain, 1);
			// Choose the destination to move to each turn
			where(time).is(where.loc[LOCATION]).choice() <<= location(LOCATION); 

			// discOn(time, disc) == the disc/peg that "disc" is on top of this turn
			VXY_DOMAIN_FORMULA(discOn, LocationDomain, 2);
			// starting state
			discOn(0, DISC).is(discOn.loc[LOCATION]) <<= start(DISC, LOCATION);
			// the moved disc changes what it's on top of to the selected destination the previous turn.
			discOn(time, DISC).is(discOn.loc[LOCATION]) <<=
				location(LOCATION) && isDisc(DISC) && DISC != LOCATION &&
				move(time-1).is(move.loc[DISC]) && where(time-1).is(where.loc[LOCATION]);
			// If the disc on a location wasn't moved last turn, it remains on the location this turn.
			discOn(time, DISC).is(discOn.loc[LOCATION]) <<= location(LOCATION) &&
				discOn(time-1, DISC).is(discOn.loc[LOCATION]) && ~move(time-1).is(move.loc[DISC]);

			// Cannot move on top of a smaller disc
			Program::disallow(location(LOCATION) && discOn(time, DISC).is(discOn.loc[LOCATION]) && LOCATION > DISC);
			// Cannot move a disc if the disc is under something
			Program::disallow(isDisc(DISC) && discOn(time, LOCATION).is(discOn.loc[DISC]) && move(time).is(move.loc[DISC]));
			// Don't move the same disc twice in a row
			Program::disallow(isDisc(DISC) && move(time).is(move.loc[DISC]) && move(time-1).is(move.loc[DISC]) && time < NUM_TURNS-1);

			// Ensure we reach goal state
			Program::disallow(end(DISC, LOCATION) && ~discOn(NUM_TURNS-1, DISC).is(discOn.loc[LOCATION]));

			return Result{ move, where, discOn };
		});
		
		auto timeGraph = make_shared<PlanarGridTopology>(NUM_TURNS, 1);

		//
		// Create the variables for binding each potential discOn() pair
		//
		
		auto discOnData = make_shared<TTopologyVertexData<vector<VarID>>>(ITopology::adapt(timeGraph), vector<VarID>{}, TEXT("discOn"));
		for (int turn = 0; turn < NUM_TURNS; ++turn)
		{
			auto& discOnThisTurn = discOnData->get(turn);
			discOnThisTurn.resize(NUM_DISCS);
			for (int disc = 0; disc < NUM_DISCS; ++disc)
			{
				wstring varName;
				varName.sprintf(TEXT("discOn(%d, %d)"), turn, FIRST_DISC_IDX+disc);
				discOnThisTurn[disc] = solver.makeVariable(varName, LocationDomain::get()->getSolverDomain());
			}

			// CONSTRAINT: Ensure that every disc is on top of something unique each turn
			solver.allDifferent(discOnThisTurn);
		}

		//
		// Create the variables for binding move() and where()
		//
		
		auto moveData = solver.makeVariableGraph(TEXT("move"), ITopology::adapt(timeGraph), LocationDomain::get()->getSolverDomain(), TEXT("move-"));
		auto moveDestData = solver.makeVariableGraph(TEXT("where"), ITopology::adapt(timeGraph), LocationDomain::get()->getSolverDomain(), TEXT("moveDest-"));
		
		//
		// Bind the formulas to the variables
		//
		
		auto prgInst = prg(ITopology::adapt(timeGraph));
		prgInst->getResult().move.bind([&](const ProgramSymbol& time)
		{
			return moveData->get(time.getInt());
		});
		prgInst->getResult().where.bind([&](const ProgramSymbol& time)
		{
			return moveDestData->get(time.getInt());
		});
		prgInst->getResult().discOn.bind([&](const ProgramSymbol& time, const ProgramSymbol& disc)
		{
			return discOnData->get(time.getInt())[disc.getInt()-NUM_PEGS];
		});

		//
		// Add the program and solve it!
		//

		solver.addProgram(prgInst);
		
	 	auto result = solver.solve();
	 	solver.dumpStats(printVerbose);
	 	EATEST_VERIFY(result == EConstraintSolverResult::Solved);

		if (printVerbose)
		{
			print(&solver, moveData->getData(), moveDestData->getData(), discOnData->getData());
		}
	}
	return nErrorCount;
}

void TowersOfHanoiSolver::print(const ConstraintSolver* solver, const vector<VarID>& move, const vector<VarID>& moveDest, const vector<vector<VarID>>& discOn)
{
	for (int turn = 0; turn < NUM_TURNS; ++turn)
	{
		VERTEXY_LOG("Turn %d:", turn);
		vector<int> onMe;
		onMe.resize(NUM_DISCS+NUM_PEGS, -1);
		for (int i = 0; i < NUM_DISCS; ++i)
		{
			int on = solver->getSolvedValue(discOn[turn][i]);
			onMe[on] = NUM_PEGS+i;
		}

		for (int peg = 0; peg < NUM_PEGS; ++peg)
		{
			int cur = onMe[peg];
			wstring pegS = TEXT("-");
			while (cur != -1)
			{
				pegS.append_sprintf(TEXT("%d"), cur-NUM_PEGS);
				cur = onMe[cur];
			}
			VERTEXY_LOG("%s", pegS.c_str());
		}
		VERTEXY_LOG("Move %d->%d", solver->getSolvedValue(move[turn]) - NUM_PEGS, solver->getSolvedValue(moveDest[turn]));
	}
}
