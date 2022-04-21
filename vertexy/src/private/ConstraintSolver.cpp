// Copyright Proletariat, Inc. All Rights Reserved.

#include "ConstraintSolver.h"

#include <fstream>
#include <string>

#include "variable/HistoricalVariableDatabase.h"
#include "constraints/IConstraint.h"
#include "constraints/AllDifferentConstraint.h"
#include "constraints/InequalityConstraint.h"
#include "constraints/OffsetConstraint.h"
#include "constraints/ClauseConstraint.h"
#include "constraints/TableConstraint.h"
#include "constraints/CardinalityConstraint.h"
#include "constraints/DisjunctionConstraint.h"
#include "constraints/IffConstraint.h"
#include "constraints/SumConstraint.h"
#include "SignedClause.h"
#include "variable/BooleanVariablePropagator.h"
#include "variable/GenericVariablePropagator.h"
#include "variable/StubVariablePropagator.h"
#include "variable/WordVariablePropagator.h"
#include "topology/GraphRelations.h"
#include "util/SolverDecisionLog.h"
#include "ds/FastLookupSet.h"
#include "rules/UnfoundedSetAnalyzer.h"
#include "util/TimeUtils.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/variant.h>
#include <EASTL/stack.h>

using namespace Vertexy;

// Whether we sanity check explanations returned from constraints. Slow!
static constexpr bool EXPLANATION_SANITY_CHECK = VERTEXY_SANITY_CHECKS;
// Whether graph-learning is enabled. When enabled, some learned constraints can be promoted to all vertices on a graph.
static constexpr bool GRAPH_LEARNING_ENABLED = true;
// Whether we should log every graph promotion that happens.
static constexpr bool LOG_GRAPH_PROMOTIONS = false;
// Whether we should test that graph promotions are valid. Happens after solve is complete (SAT or UNSAT).
// Can be used even if GRAPH_LEARNING_ENABLED = false, to verify that graph constraints *would've* been (in)correct
static constexpr bool TEST_GRAPH_PROMOTIONS = true;

// Whether we attempt to simplify clause constraints prior to solving.
static constexpr bool SIMPLIFY_CONSTRAINTS = true;

// How often we log solver steps, for progress reporting.
static constexpr int DECISION_LOG_FREQUENCY = -1;
// Whether to log EVERY variable propagation. Very noisy!
static constexpr bool LOG_VARIABLE_PROPAGATIONS = false;
// Whether to log every time the solver backtracks. Very noisy!
static constexpr bool LOG_BACKTRACKS = false;

// The literal block distance (LBD) for learned constraints where we put them in the permanent constraint pool.
// Permanent constraints will remain forever.
static constexpr int MAX_PERMANENT_CONSTRAINT_LBD = 5;
// The size of the temporary constraint pool, as a function of the number of initial constraints.
static constexpr float MAX_LEARNED_CONSTRAINTS_SCALAR = 2.f;
// The percent (0.0-1.0) of temporary learned constraints we should purge whenever the pool becomes too large.
static constexpr float CONSTRAINT_PURGE_PERCENT = 0.5f;
// How much to decay activity of constraints each time we backtrack.
static constexpr float CONSTRAINT_ACTIVITY_DECAY = 1.0f / 0.95f;
// Maximum value for constraint activities. If this value is reached, all constraint activities are rescaled by MAX_CONFLICT_ACTIVITY_RESCALE.
static constexpr float MAX_CONFLICT_ACTIVITY = 1e10f;
// How much to scale all constraint activities once any reach MAX_CONFLICT_ACTIVITY
static constexpr float MAX_CONFLICT_ACTIVITY_RESCALE = 1e-10f;

// Whether IffConstraints are replaced with equivalent ClauseConstraints.
static constexpr bool REPLACE_IFF_WITH_CLAUSES = true;
// Whether we use specialized variable propagators for different variable widths, or use the generic propagator
// for everything (slower).
static constexpr bool USE_SPECIAL_VARIABLE_PROPAGATORS = true;
// Whether to reset the all variables' last solved values when finding a new solution.
// If this is set, then each returned solution will tend to be more different than the last found one, but
// it will potentially take more time to find due to exploring very different search spaces.
static constexpr bool RESET_VARIABLE_MEMOS_ON_SOLUTION = true;

// The base heuristic used for deciding which variable/value to pick next.
using DEFAULT_BASE_HEURISTIC = CoarseLRBHeuristic;

const VarID VarID::INVALID = VarID();
const GraphConstraintID GraphConstraintID::INVALID = GraphConstraintID();

ConstraintSolver* ConstraintSolver::s_currentSolver = nullptr;

ConstraintSolver::ConstraintSolver(const wstring& name, int seed, const shared_ptr<ISolverDecisionHeuristic>& baseHeuristic)
	: m_variableDB(this)
	, m_restartPolicy(*this)
	, m_decisionLogFrequency(DECISION_LOG_FREQUENCY)
	, m_initialSeed(seed == 0 ? TimeUtils::getCycles() : seed)
	, m_random(m_initialSeed)
	, m_ruleDB(*this)
	, m_analyzer(*this)
	, m_stats(*this)
	, m_name(name)
{
	if (baseHeuristic.get() != nullptr)
	{
		m_heuristicStack.push_back(baseHeuristic);
	}
	else
	{
		m_heuristicStack.push_back(make_shared<DEFAULT_BASE_HEURISTIC>(*this));
	}

	// Dummy variable at index 0
	m_variableDomains.push_back(SolverVariableDomain{0, 1});
	m_variableToDecisionLevel.push_back(0);
	m_variablePropagators.push_back(nullptr);
	m_variableToGraphs.push_back({});
}

ConstraintSolver::~ConstraintSolver()
{
}

VarID ConstraintSolver::makeVariable(const wstring& varName, const SolverVariableDomain& domain, const vector<int>& potentialValues)
{
	vxy_assert(m_currentStatus == EConstraintSolverResult::Uninitialized);

	vector<int> xfmPotentials;
	for_each(potentialValues.begin(), potentialValues.end(), [&](int transformed)
	{
		xfmPotentials.push_back(domain.getIndexForValue(transformed));
	});

	VarID varId = m_variableDB.addVariable(varName, domain.getDomainSize(), xfmPotentials);

	vxy_assert(m_variableDomains.size() == varId.raw());
	m_variableDomains.push_back(domain);

	vxy_assert(m_variableToDecisionLevel.size() == varId.raw());
	m_variableToDecisionLevel.push_back(0);

	// Allocate the best type of propagator based on domain size of the variable
	vxy_assert(m_variablePropagators.size() == varId.raw());
	if constexpr (!USE_SPECIAL_VARIABLE_PROPAGATORS)
	{
		m_variablePropagators.emplace_back(make_unique<GenericVariablePropagator>(domain.getDomainSize()));
	}
	else
	{
		if (domain.getDomainSize() == 1)
		{
			m_variablePropagators.push_back(make_unique<StubVariablePropagator>());
		}
		else if (domain.getDomainSize() == 2)
		{
			m_variablePropagators.emplace_back(make_unique<BooleanVariablePropagator>());
		}
		else if (domain.getDomainSize() <= 32)
		{
			m_variablePropagators.emplace_back(make_unique<WordVariablePropagator>(domain.getDomainSize()));
		}
		else if (domain.getDomainSize() <= 64)
		{
			m_variablePropagators.emplace_back(make_unique<DwordVariablePropagator>(domain.getDomainSize()));
		}
		else
		{
			m_variablePropagators.emplace_back(make_unique<GenericVariablePropagator>(domain.getDomainSize()));
		}
	}

	vxy_assert(m_variableToGraphs.size() == varId.raw());
	m_variableToGraphs.push_back({});

	return varId;
}

VarID ConstraintSolver::makeVariable(const wstring& varName, const vector<int>& potentialValues)
{
	vxy_assert(!potentialValues.empty()); // can't start with an empty domain

	int minValue = INT_MAX;
	int maxValue = -INT_MAX;
	for (int value : potentialValues)
	{
		minValue = min(minValue, value);
		maxValue = max(maxValue, value);
	}
	return makeVariable(varName, SolverVariableDomain(minValue, maxValue), potentialValues);
}

void ConstraintSolver::setInitialValues(VarID varID, const vector<int>& potentialValues)
{
	auto& domain = m_variableDomains[varID.raw()];

	ValueSet values(domain.getDomainSize(), potentialValues.empty() ? true : false);
	for (int value : potentialValues)
	{
		values[domain.getIndexForValue(value)] = true;
	}

	m_variableDB.setInitialValue(varID, values);
}

IConstraint* ConstraintSolver::registerConstraint(IConstraint* constraint)
{
	m_constraints.push_back(unique_ptr<IConstraint>(move(constraint)));
	m_constraintIsChild.push_back(false);

	if (constraint->needsBacktracking())
	{
		m_backtrackingConstraints.push_back(static_cast<IBacktrackingSolverConstraint*>(constraint));
	}

	vector<VarID> constraintVars;
	for (VarID varID : constraint->getConstrainingVariables())
	{
		constraintVars.push_back(varID);
	}
	m_constraintArcs.push_back(move(constraintVars));

	return m_constraints.back().get();
}

AllDifferentConstraint& ConstraintSolver::allDifferent(const vector<VarID>& variables, bool useWeakPropagation)
{
	return makeConstraint<AllDifferentConstraint>(variables, useWeakPropagation);
}

CardinalityConstraint& ConstraintSolver::cardinality(const vector<VarID>& variables, const hash_map<int, tuple<int, int>>& cardinalitiesForValues)
{
	return makeConstraint<CardinalityConstraint>(variables, cardinalitiesForValues);
}

TableConstraint& ConstraintSolver::table(const TableConstraintDataPtr& data, const vector<VarID>& variables)
{
	return makeConstraint<TableConstraint>(data, variables);
}

ClauseConstraint& ConstraintSolver::clause(const vector<SignedClause>& clauses)
{
	return makeConstraint<ClauseConstraint>(clauses);
}

ClauseConstraint& ConstraintSolver::nogood(const vector<SignedClause>& clauses)
{
	return makeConstraint<ClauseConstraint>(ENoGood::NoGood, clauses);
}

SumConstraint& ConstraintSolver::sum(const VarID sum, const vector<VarID>& vars)
{
	stack<VarID> varStack;
	for (auto var : vars)
	{
		varStack.push(var);
	}

	VarID var1 = varStack.top();
	varStack.pop();
	VarID var2 = varStack.top();
	varStack.pop();
	int counter = 1;

	while (!varStack.empty())
	{
		int minVal = getDomain(var1).getMin() + getDomain(var2).getMin();
		int maxVal = getDomain(var1).getMax() + getDomain(var2).getMax();
		VarID intermediarySum = makeVariable({ wstring::CtorSprintf(), TEXT("IntSum%d"), counter++ }, SolverVariableDomain(minVal, maxVal));
		makeConstraint<SumConstraint>(intermediarySum, var1, var2);

		var1 = intermediarySum;
		var2 = varStack.top();
		varStack.pop();
	}

	return makeConstraint<SumConstraint>(sum, var1, var2);
}

IffConstraint& ConstraintSolver::iff(const SignedClause& head, const vector<SignedClause>& body)
{
	if (REPLACE_IFF_WITH_CLAUSES)
	{
		vector<SignedClause> negClauses;
		vector<SignedClause> posClauses;

		posClauses.reserve(2);
		negClauses.reserve(body.size() + 1);

		posClauses.push_back(head.inverted());
		negClauses.push_back(head);

		for (int i = 0; i < body.size(); ++i)
		{
			posClauses.push_back(body[i]);
			nogood(posClauses);
			posClauses.pop_back();

			negClauses.push_back(body[i].inverted());
		}
		nogood(negClauses);
		return *((IffConstraint*)(nullptr));
	}
	else
	{
		return makeConstraint<IffConstraint>(head, body);
	}
}

OffsetConstraint& ConstraintSolver::offset(VarID sum, VarID term, int delta)
{
	return makeConstraint<OffsetConstraint>(sum, term, delta);
}

InequalityConstraint& ConstraintSolver::inequality(VarID leftHandSide, EConstraintOperator op, VarID rightHandSide)
{
	return makeConstraint<InequalityConstraint>(leftHandSide, op, rightHandSide);
}

DisjunctionConstraint& ConstraintSolver::disjunction(IConstraint* consA, IConstraint* consB)
{
	return makeConstraint<DisjunctionConstraint>(consA, consB);
}

vector<VarID> ConstraintSolver::unifyVariableDomains(const vector<VarID>& variables, int* outNewMinDomain)
{
	// Unify all input variables so that their first index in ValueSet all align.
	// To do this, we create new variables and apply an OffsetConstraint between the new and original variable.

	int minDomain = INT_MAX;
	for (int i = 0; i < variables.size(); ++i)
	{
		minDomain = min(minDomain, m_variableDomains[variables[i].raw()].getMin());
	}

	vector<VarID> adjustedIDs;
	for_each(variables.begin(), variables.end(), [&](VarID varID)
	{
		adjustedIDs.push_back(getOrCreateOffsetVariable(varID, minDomain, m_variableDomains[varID.raw()].getMax()));
	});

	if (outNewMinDomain)
	{
		*outNewMinDomain = minDomain;
	}
	return adjustedIDs;
}

VarID ConstraintSolver::getOrCreateOffsetVariable(VarID varID, int minDomain, int maxDomain)
{
	if (m_variableDomains[varID.raw()].getMin() == minDomain && m_variableDomains[varID.raw()].getMax() == maxDomain)
	{
		return varID;
	}

	// If the input is already an offset variable, find the source variable.
	// This avoids us from creating chains of offsets.
	if (auto found = m_offsetVariableToSource.find(varID); found != m_offsetVariableToSource.end())
	{
		varID = found->second;
	}

	const SolverVariableDomain& curDomain = m_variableDomains[varID.raw()];

	// See if we already have a variable for this offset
	tuple<VarID, int, int> key(varID, minDomain, maxDomain);
	auto found = m_offsetVariableMap.find(key);
	if (found == m_offsetVariableMap.end())
	{
		int domainOffset = minDomain - curDomain.getMin();

		// create a new offset variable
		const wstring& sourceVarName = m_variableDB.getVariableName(varID);
		wstring newVarName = domainOffset > 0
			                     ? wstring(wstring::CtorSprintf(), TEXT("%s>>>%d"), sourceVarName.c_str(), domainOffset)
			                     : wstring(wstring::CtorSprintf(), TEXT("%s<<<%d"), sourceVarName.c_str(), -domainOffset);

		SolverVariableDomain newDomain(minDomain, maxDomain - domainOffset);

		// initialize to same potential values
		const ValueSet& valueSet = m_variableDB.getPotentialValues(varID);
		vector<int> potentialValues;
		for (auto it = valueSet.beginSetBits(), itEnd = valueSet.endSetBits(); it != itEnd; ++it)
		{
			potentialValues.push_back(curDomain.getValueForIndex(*it));
		}

		VarID newVar = makeVariable(newVarName, newDomain, potentialValues);
		makeConstraint<OffsetConstraint>(newVar, varID, -domainOffset, true);

		m_offsetVariableToSource[newVar] = varID;

		found = m_offsetVariableMap.insert({key, newVar}).first;
	}
	return found->second;
}

bool ConstraintSolver::simplify()
{
	vector<vector<int>> occurList;
	occurList.resize(m_variableDB.getNumVariables()+1, {});

	vector<ClauseConstraint*> clauses;
	clauses.reserve(m_constraints.size());

	vector<uint64_t> clauseHashes;
	clauseHashes.reserve(m_constraints.size());

	using LookupSet = TFastLookupSet<int, true>;

	LookupSet addedConstraints;
	addedConstraints.setIndexSize(m_constraints.size());

	LookupSet strengthenedConstraints;
	strengthenedConstraints.setIndexSize(m_constraints.size());

	int numConstraintsRemoved = 0;
	int numLiteralsRemoved = 0;
	int numTotalLiterals = 0;

	// stuff the clause's variables into a 64-bit bitfield. Used to quickly/conservatively discard potential subsumptions.
	auto hashClause = [](const ClauseConstraint* cons) -> uint64_t
	{
		uint64_t hash = 0;
		for (int i = 0; i < cons->getNumLiterals(); ++i)
		{
			hash |= 1<<(size_t(cons->getLiteral(i).variable.raw()) % 64ULL);
		}
		return hash;
	};

	// Propagates all clause constraints, potentially removing literals or making clauses unit.
	// Note that we may discover the problem is UNSAT here.
	auto propagateTopLevel = [&]()
	{
		vector<VarID> varsRemoved;
		bool fixPoint = false;
		while (!fixPoint)
		{
			fixPoint = true;
			for (int i = 0; i < clauses.size(); ++i)
			{
				if (!clauses[i])
				{
					continue;
				}

				varsRemoved.clear();
				if (!clauses[i]->propagateAndStrengthen(&m_variableDB, varsRemoved))
				{
					return false;
				}

				if (!varsRemoved.empty())
				{
					fixPoint = false;

					strengthenedConstraints.add(i);
					for (auto it = varsRemoved.begin(), itEnd = varsRemoved.end(); it != itEnd; ++it)
					{
						occurList[it->raw()].erase_first_unsorted(i);
						++numLiteralsRemoved;
					}
				}

				if (clauses[i]->getNumLiterals() < 2)
				{
					// VERTEXY_LOG("Remove constraint %d", i);
					numConstraintsRemoved++;

					clauses[i]->reset(&m_variableDB);
					strengthenedConstraints.remove(i);
					addedConstraints.remove(i);

					for (auto itLit = clauses[i]->beginLiterals(), itLitEnd = clauses[i]->endLiterals(); itLit != itLitEnd; ++itLit)
					{
						occurList[itLit->variable.raw()].erase_first_unsorted(i);
						++numLiteralsRemoved;
					}

					m_constraints[clauses[i]->getID()].reset();
					clauses[i] = nullptr;
				}
				else if (!varsRemoved.empty())
				{
					clauseHashes[i] = hashClause(clauses[i]);
				}
			}

			if (!propagate())
			{
				return false;
			}
		}
		return true;
	};

	if (!propagateTopLevel())
	{
		return false;
	}

	for (auto& consPtr : m_constraints)
	{
		if (consPtr.get() && !m_constraintIsChild[consPtr->getID()])
		{
			if (auto clauseCon = consPtr->asClauseConstraint())
			{
				for (int i = 0; i < clauseCon->getNumLiterals(); ++i)
				{
					auto& lit = clauseCon->getLiteral(i);
					occurList[lit.variable.raw()].push_back(clauses.size());
				}
				numTotalLiterals += clauseCon->getNumLiterals();

				addedConstraints.add(clauses.size());
				clauses.push_back(clauseCon);
				clauseHashes.push_back(hashClause(clauseCon));
			}
		}
	}

	// Check if the literals in clauseA are a subset of the literals in clauseB
	auto isSubsetOf = [&](int clauseAIdx, int clauseBIdx, VarID negateVar=VarID::INVALID)
	{
		if (clauseAIdx == clauseBIdx)
		{
			return false;
		}

		auto clauseA = clauses[clauseAIdx];
		auto clauseB = clauses[clauseBIdx];
		if (clauseA->getNumLiterals() > clauseB->getNumLiterals())
		{
			return false;
		}

		if ((clauseHashes[clauseAIdx] & ~clauseHashes[clauseBIdx]) != 0)
		{
			return false;
		}

		for (auto it = clauseA->beginLiterals(), itEnd = clauseA->endLiterals(); it != itEnd; ++it)
		{
			auto found = find_if(clauseB->beginLiterals(), clauseB->endLiterals(), [&](auto& lit) { return lit.variable == it->variable; });
			if (found == clauseB->endLiterals())
			{
				return false;
			}

			if (negateVar == it->variable)
			{
				if (found->values != it->values.inverted())
				{
					return false;
				}
			}
			else
			{
				if (!it->values.isSubsetOf(found->values))
				{
					return false;
				}
			}
		}

		return true;
	};

	// Find all clauses that this clause should subsume (i.e. clauses where this clause is a subset)
	auto findSubsumed = [&](int clauseIdx, vector<int>& outConsumed, int negateLitIdx=-1)
	{
		auto cons = clauses[clauseIdx];
		vxy_sanity(cons->getNumLiterals() > 0);

		outConsumed.clear();

		VarID bestVar = cons->getLiteral(0).variable;
		for (int i = 1; i < cons->getNumLiterals(); ++i)
		{
			auto& lit = cons->getLiteral(i);
			if (occurList[lit.variable.raw()].size() < occurList[bestVar.raw()].size())
			{
				bestVar = lit.variable;
			}
		}

		VarID negateVar = VarID::INVALID;
		if (negateLitIdx >= 0)
		{
			vxy_sanity(negateLitIdx < cons->getNumLiterals());
			negateVar = cons->getLiteral(negateLitIdx).variable;
		}

		for (auto it = occurList[bestVar.raw()].begin(), itEnd = occurList[bestVar.raw()].end(); it != itEnd; ++it)
		{
			if (isSubsetOf(clauseIdx, *it, negateVar))
			{
				outConsumed.push_back(*it);
			}
		}
	};

	// Find all literals we can remove other clauses based on the logic of this clause.
	// e.g. for a clause (a, b, c), it will find all clauses subsumed by (-a, b, c), (a, -b, c), and (a, b, -c).
	// For a clause subsumed by (-a, b, c), it can remove -a from that clause.
	auto selfSubsume = [&](int clauseIdx)
	{
		vector<int> consumed;

		auto cons = clauses[clauseIdx];
		for (int i = 0; i < cons->getNumLiterals(); ++i)
		{
			auto& lit = cons->getLiteral(i);
			findSubsumed(clauseIdx, consumed, i);
			for (auto it = consumed.begin(), itEnd = consumed.end(); it != itEnd; ++it)
			{
				auto strCons = clauses[*it];
				bool found = false;
				for (int j = 0; j < strCons->getNumLiterals(); ++j)
				{
					if (strCons->getLiteral(j).variable == lit.variable)
					{
						vxy_sanity(strCons->getLiteral(j).values == lit.values.inverted());
						strCons->removeLiteralAt(&m_variableDB, j);
						clauseHashes[*it] = hashClause(strCons);
						occurList[lit.variable.raw()].erase_first_unsorted(*it);
						strengthenedConstraints.add(*it);

						++numLiteralsRemoved;

						found = true;
						break;
					}
				}
				vxy_sanity(found);
			}
		}
	};

	// Return all clauses that contain the specified literal (exact match)
	auto getClausesWithLiteral = [&](const Literal& lit, LookupSet& outClauses)
	{
		const auto& list = occurList[lit.variable.raw()];
		for (auto it = list.begin(), itEnd = list.end(); it != itEnd; ++it)
		{
			auto cons = clauses[*it];
			auto found = find_if(cons->beginLiterals(), cons->endLiterals(), [&](auto& l) { return l.variable == lit.variable; });
			if (found != cons->endLiterals() && found->values == lit.values)
			{
				outClauses.add(*it);
			}
		}
	};

	LookupSet potentialSet;
	potentialSet.setIndexSize(clauses.size());

	LookupSet subsumeSet;
	subsumeSet.setIndexSize(clauses.size());

	vector<int> foundSubsumed;

	while (!addedConstraints.empty())
	{
		potentialSet.clear();
		for (auto it = addedConstraints.begin(), itEnd = addedConstraints.end(); it != itEnd; ++it)
		{
			auto clause = clauses[*it];
			for (auto itLit = clause->beginLiterals(), itLitEnd = clause->endLiterals(); itLit != itLitEnd; ++itLit)
			{
				getClausesWithLiteral(*itLit, potentialSet);
			}
		}

		do
		{
			subsumeSet.clear();
			for (auto it = addedConstraints.begin(), itEnd = addedConstraints.end(); it != itEnd; ++it)
			{
				subsumeSet.add(*it);

				auto clause = clauses[*it];
				for (auto itLit = clause->beginLiterals(), itLitEnd = clause->endLiterals(); itLit != itLitEnd; ++itLit)
				{
					getClausesWithLiteral(Literal(itLit->variable, itLit->values.inverted()), subsumeSet);
				}
			}
			for (auto it = strengthenedConstraints.begin(), itEnd = strengthenedConstraints.end(); it != itEnd; ++it)
			{
				subsumeSet.add(*it);
			}

			addedConstraints.clear();
			strengthenedConstraints.clear();

			for (auto it = subsumeSet.begin(), itEnd = subsumeSet.end(); it != itEnd; ++it)
			{
				selfSubsume(*it);
			}

			if (!propagateTopLevel())
			{
				return false;
			}
		}
		while (!strengthenedConstraints.empty());

		for (auto it = potentialSet.begin(), itEnd = potentialSet.end(); it != itEnd; ++it)
		{
			if (clauses[*it] == nullptr)
			{
				continue;
			}

			foundSubsumed.clear();
			findSubsumed(*it, foundSubsumed);

			for (int subsumedIdx : foundSubsumed)
			{
				// VERTEXY_LOG("Remove constraint %d", subsumedIdx);
				++numConstraintsRemoved;

				auto subsumed = clauses[subsumedIdx];
				for (auto itLit = subsumed->beginLiterals(), itLitEnd = subsumed->endLiterals(); itLit != itLitEnd; ++itLit)
				{
					occurList[itLit->variable.raw()].erase_first_unsorted(subsumedIdx);
					++numLiteralsRemoved;
				}

				subsumed->reset(&m_variableDB);

				m_constraints[subsumed->getID()].reset();
				clauses[subsumedIdx] = nullptr;
			}
		}
	}

	VERTEXY_LOG("Simplification: removed %d/%d clause constraints, %d/%d clause literals", numConstraintsRemoved, clauses.size(), numLiteralsRemoved, numTotalLiterals);
	return true;
}


hash_map<VarID, SolvedVariableRecord> ConstraintSolver::getSolution() const
{
	vxy_assert(getCurrentStatus() == EConstraintSolverResult::Solved);

	hash_map<VarID, SolvedVariableRecord> solution;
	for (int i = 1; i < m_variableDB.getNumVariables() + 1; ++i)
	{
		VarID varID(i);
		solution[VarID(i)] = {m_variableDB.getVariableName(varID), m_variableDomains[varID.raw()].getValueForIndex(m_variableDB.getSolvedValue(varID))};
	}
	return solution;
}

bool ConstraintSolver::isSolved(VarID varID) const
{
	return m_variableDB.isSolved(varID);
}

int ConstraintSolver::getSolvedValue(VarID varID) const
{
	return m_variableDomains[varID.raw()].getValueForIndex(m_variableDB.getSolvedValue(varID));
}

bool ConstraintSolver::isAtomTrue(AtomID atomID) const
{
	auto& lit = m_ruleDB.getAtom(atomID)->equivalence;
	auto& cur = m_variableDB.getPotentialValues(lit.variable);
	return cur.isSubsetOf(lit.values);
}

vector<int> ConstraintSolver::getPotentialValues(VarID varID) const
{
	vector<int> out;
	const ValueSet& values = m_variableDB.getPotentialValues(varID);
	for (auto it = values.beginSetBits(), itEnd = values.endSetBits(); it != itEnd; ++it)
	{
		out.push_back(m_variableDomains[varID.raw()].getValueForIndex(*it));
	}
	return out;
}

const wstring& ConstraintSolver::getVariableName(VarID varID) const
{
	return m_variableDB.getVariableName(varID);
}

void ConstraintSolver::addDecisionHeuristic(const shared_ptr<ISolverDecisionHeuristic>& heuristic)
{
	vxy_assert(m_currentStatus == EConstraintSolverResult::Uninitialized);
	m_heuristicStack.push_back(heuristic);
}

EConstraintSolverResult ConstraintSolver::solve()
{
	EConstraintSolverResult result = startSolving();
	while (result == EConstraintSolverResult::Unsolved)
	{
		result = step();
	}
	return result;
}

EConstraintSolverResult ConstraintSolver::startSolving()
{
	ConstraintSolver::s_currentSolver = this;

	m_stats.reset();
	m_stats.startTime = TimeUtils::getSeconds();

	if (!m_initialArcConsistencyEstablished)
	{
		vxy_assert(m_currentStatus == EConstraintSolverResult::Uninitialized);

		// create constraints for rules
		m_ruleDB.finalize();

		m_stats.numInitialConstraints = m_constraints.size();
		m_numUserConstraints = m_stats.numInitialConstraints;
		m_initialArcConsistencyEstablished = false;

		for (int i = m_heuristicStack.size() - 1; i >= 0; --i)
		{
			m_heuristicStack[i]->initialize();
		}

		for (int i = 0; i < m_constraints.size(); ++i)
		{
			if (m_constraintIsChild[i])
			{
				continue;
			}

			auto& constraint = m_constraints[i];
			if (constraint.get() != nullptr && !constraint->initialize(&m_variableDB, nullptr))
			{
				m_stats.endTime = TimeUtils::getSeconds();
				m_currentStatus = EConstraintSolverResult::Unsatisfiable;
				return m_currentStatus;
			}
		}

		if (SIMPLIFY_CONSTRAINTS && !simplify())
		{
			m_stats.endTime = TimeUtils::getSeconds();
			m_currentStatus = EConstraintSolverResult::Unsatisfiable;
			return m_currentStatus;
		}

		// if the rules aren't tight, we need to watch for and analyze unfounded sets (cyclical supports)
		if (!m_ruleDB.isTight())
		{
			m_unfoundedSetAnalyzer = make_unique<UnfoundedSetAnalyzer>(*this);
			if (!m_unfoundedSetAnalyzer->initialize())
			{
				m_stats.endTime = TimeUtils::getSeconds();
				m_currentStatus = EConstraintSolverResult::Unsatisfiable;
				return m_currentStatus;
			}
		}

		if (!propagate())
		{
			m_stats.endTime = TimeUtils::getSeconds();
			m_currentStatus = EConstraintSolverResult::Unsatisfiable;
			return m_currentStatus;
		}

		for (auto& constraint : m_constraints)
		{
			if (constraint.get() != nullptr)
			{
				constraint->onInitialArcConsistency(&m_variableDB);
			}
		}

		m_variableDB.onInitialArcConsistency();

		m_initialArcConsistencyEstablished = true;
		m_currentStatus = EConstraintSolverResult::Unsolved;
	}
	else if (m_currentStatus == EConstraintSolverResult::Solved)
	{
		if (getCurrentDecisionLevel() == 0)
		{
			m_currentStatus = EConstraintSolverResult::Unsatisfiable;
			return m_currentStatus;
		}

		// Find the next solution
		m_currentStatus = EConstraintSolverResult::Unsolved;
		m_stats.numInitialConstraints = count_if(m_constraints.begin(), m_constraints.end(), [](auto& c) { return c != nullptr; });

		// Mark the current solution as a nogood, and start the next solution
		vector<Literal> currentSolutionLits;
		currentSolutionLits.reserve(m_decisionLevels.size());
		for (int i = 1; i < m_variableDB.getNumVariables() + 1; ++i)
		{
			VarID varID(i);
			vxy_sanity(m_variableDB.getPotentialValues(varID).isSingleton());
			currentSolutionLits.push_back(Literal(varID, m_variableDB.getPotentialValues(varID)));
			currentSolutionLits.back().values.invert();
		}
		backtrackUntilDecision(0, true);

		// Maybe make this optional? If it is not done, each following solution will be very similar to the prior.
		// On the other hand, when we clear, it may take much longer to find the next solution.
		if constexpr (RESET_VARIABLE_MEMOS_ON_SOLUTION)
		{
			m_variableDB.clearLastSolvedValues();
		}

		auto& solutionCons = makeConstraint<ClauseConstraint>(currentSolutionLits);
		if (!solutionCons.initialize(&m_variableDB, nullptr))
		{
			m_currentStatus = EConstraintSolverResult::Unsatisfiable;
			return m_currentStatus;
		}
	}
	else
	{
		vxy_assert_msg(false, "startSolving called in bad state!");
		m_currentStatus = EConstraintSolverResult::Unsatisfiable;
	}

	return m_currentStatus;
}

///////////////////////////////////////////////////////////////////////////////
//
// Main loop for solver
//
EConstraintSolverResult ConstraintSolver::step()
{
	if (m_currentStatus != EConstraintSolverResult::Unsolved)
	{
		return m_currentStatus;
	}

	++m_stats.stepCount;

	// Propagate any assignments made. If this returns false, then a constraint has reported failure,
	// or a variable has no potential values left.
	if (!propagate())
	{
		m_newDescentAfterRestart = false;

		ClauseConstraint* learnedConstraint = nullptr;

		auto lastNarrowedConstraint = m_lastTriggeredSink->asConstraint();
		vxy_assert_msg(lastNarrowedConstraint, "Detected sink instead of constraint as conflict source");

		SolverDecisionLevel backtrackLevel = m_analyzer.analyzeConflict(m_lastTriggeredTs + 1, lastNarrowedConstraint, m_variableDB.getLastContradictingVariable(), learnedConstraint);
		vxy_assert(backtrackLevel < getCurrentDecisionLevel());

		// If we're going past the first decision level, there is no possible solution.
		if (backtrackLevel < 0)
		{
			m_stats.endTime = TimeUtils::getSeconds();
			m_currentStatus = EConstraintSolverResult::Unsatisfiable;
			return EConstraintSolverResult::Unsatisfiable;
		}
		else if (backtrackLevel == 0)
		{
			m_restartPolicy.onRestarted();
			for (auto heuristic : m_heuristicStack)
			{
				heuristic->onRestarted();
			}
			m_newDescentAfterRestart = true;
		}

		// Jump back to the relevant decision level.
		backtrackUntilDecision(backtrackLevel);

		//VERTEXY_LOG("Learned constraint %d: %s", learnedConstraint->getID(), clauseConstraintToString(*learnedConstraint).c_str());
		vxy_assert(learnedConstraint->getNumLiterals() > 0);
		if (learnedConstraint->getNumLiterals() == 1)
		{
			// No need to keep this around, just propagate it and forget it
			vxy_assert(backtrackLevel == 0);
			bool success = m_variableDB.constrainToValues(learnedConstraint->getLiteral(0), nullptr, nullptr);
			vxy_assert(success);

			vxy_assert(!learnedConstraint->isLocked());

			vxy_assert(m_constraints[learnedConstraint->getID()].get() == learnedConstraint);
			m_constraints[learnedConstraint->getID()].reset();
		}
		else
		{
			bool success = learnedConstraint->initialize(&m_variableDB, nullptr);
			vxy_assert(success);

			success = learnedConstraint->makeUnit(&m_variableDB, 0);
			vxy_assert(success);
		}
	}
	else
	{
		// Check if we should restart now
		if (getCurrentDecisionLevel() > 0 && m_restartPolicy.shouldRestart())
		{
			backtrackUntilDecision(0, true);

			m_restartPolicy.onRestarted();
			for (auto heuristic : m_heuristicStack)
			{
				heuristic->onRestarted();
			}
			m_newDescentAfterRestart = true;
			++m_stats.numRestarts;
		}
		else
		{
			// Get rid of old learned constraints if database has grown too large
			if (getCurrentDecisionLevel() > 0 && m_temporaryLearnedConstraints.size() >= m_numUserConstraints*MAX_LEARNED_CONSTRAINTS_SCALAR)
			{
				purgeConstraints();
				++m_stats.numConstraintPurges;
			}

			//
			// Pick a new variable/value decision, and add it to propagation queue.
			//

			startNextDecision();

			VarID pickedVar;
			ValueSet pickedValue;
			if (!getNextDecisionLiteral(pickedVar, pickedValue))
			{
				m_decisionLevels.pop_back();
				m_currentStatus = EConstraintSolverResult::Solved;
				sanityCheckValid();
				findDuplicateClauses();
				sanityCheckGraphClauses();

				m_stats.endTime = TimeUtils::getSeconds();
				return m_currentStatus;
			}

			if (VERTEXY_LOG_ACTIVE())
			{
				if (!m_decisionLevels.empty() && m_decisionLogFrequency > 0 && ((m_stats.stepCount % m_decisionLogFrequency) == 0))
				{
					VERTEXY_LOG("Level %d Step %d Var:%s[%d] Value:%s", getCurrentDecisionLevel(), m_stats.stepCount, m_variableDB.getVariableName(pickedVar).c_str(), pickedVar.raw(), valueSetToString(pickedVar, pickedValue).c_str());
				}
			}

			if (m_outputLog != nullptr)
			{
				int valueIndex = pickedValue.indexOf(true);
				vxy_sanity(valueIndex >= 0);
				vxy_sanity(pickedValue.lastIndexOf(true) == valueIndex);
				m_outputLog->addDecision(getCurrentDecisionLevel(), pickedVar, valueIndex);
			}

			vxy_assert(m_variableToDecisionLevel[pickedVar.raw()] == 0);
			m_variableToDecisionLevel[pickedVar.raw()] = getCurrentDecisionLevel();
			m_decisionLevels.back().variable = pickedVar;

			// check that the strategy is actually narrowing the solution
			vxy_sanity(!m_variableDB.getPotentialValues(pickedVar).isSubsetOf(pickedValue));

			bool success = m_variableDB.constrainToValues(pickedVar, pickedValue, nullptr);
			vxy_assert(success); // If this goes off, the strategy did not return a potential value
		}
	}

	return EConstraintSolverResult::Unsolved;
}

bool ConstraintSolver::propagate()
{
	// If we have any constraints queued up to be propagated across graphs, do so now.
	if (GRAPH_LEARNING_ENABLED)
	{
		for (auto it = m_constraintsToPromoteToGraph.begin(), itEnd = m_constraintsToPromoteToGraph.end(); it != itEnd;)
		{
			if (!it->first->isPromotableToGraph() || promoteConstraintToGraph(*it->first, it->second))
			{
				it = m_constraintsToPromoteToGraph.erase(it);
			}
			else
			{
				return false;
			}
		}
	}

	if (!propagateVariables())
	{
		return false;
	}

	// Check for unfounded sets in rules: heads that do not have any non-cyclical supports.
	if (m_unfoundedSetAnalyzer != nullptr)
	{
		// Note that this will call propagateVariables (multiple times) if it finds any unfounded sets
		if (!m_unfoundedSetAnalyzer->analyze())
		{
			return false;
		}
	}

	return true;
}

bool ConstraintSolver::propagateVariables()
{
	while (!m_variablePropagationQueue.empty() || !m_constraintPropagationQueue.empty())
	{
		if (!emptyVariableQueue())
		{
			return false;
		}

		if (!emptyConstraintQueue())
		{
			return false;
		}
	}

	return true;
}

bool ConstraintSolver::emptyVariableQueue()
{
	static ValueSet prevValue;

	auto& stack = m_variableDB.getAssignmentStack().getStack();
	while (!m_variablePropagationQueue.empty())
	{
		QueuedVariablePropagation item = m_variablePropagationQueue.back();
		m_variablePropagationQueue.pop_back();

		vxy_assert(m_variableQueuedSet[item.variable.raw()]);
		m_variableQueuedSet[item.variable.raw()] = false;

		vxy_assert(stack[item.timestamp].variable == item.variable);

		// Need a copy here, because the array could be resized due to assignments from triggers
		prevValue = stack[item.timestamp].previousValue;

		const ValueSet& currentValue = m_variableDB.getPotentialValues(item.variable);
		if (!m_variablePropagators[item.variable.raw()]->trigger(item.variable, prevValue, currentValue, &m_variableDB, &m_lastTriggeredSink, m_lastTriggeredTs)) //, Item.Constraint))
		{
			return false;
		}
	}

	return true;
}

bool ConstraintSolver::emptyConstraintQueue()
{
	while (!m_constraintPropagationQueue.empty())
	{
		int constraintID = m_constraintPropagationQueue.front();
		m_constraintPropagationQueue.pop_front();

		vxy_assert(m_constraintQueuedSet[constraintID]);
		m_constraintQueuedSet[constraintID] = false;

		IConstraint* constraint = m_constraints[constraintID].get();

		m_lastTriggeredSink = constraint;
		m_lastTriggeredTs = m_variableDB.getTimestamp();
		if (!constraint->propagate(&m_variableDB))
		{
			return false;
		}
	}

	return true;
}

void ConstraintSolver::startNextDecision()
{
	m_decisionLevels.push_back({m_variableDB.getAssignmentStack().getMostRecentTimestamp(), VarID::INVALID});
}

bool ConstraintSolver::getNextDecisionLiteral(VarID& variable, ValueSet& value)
{
	// Check if any strategies want to make a decision
	for (int i = m_heuristicStack.size() - 1; i >= 0; --i)
	{
		if (m_heuristicStack[i]->getNextDecision(getCurrentDecisionLevel(), variable, value))
		{
			return true;
		}
	}

	// No more variables to pick - we're solved!
	return false;
}

void ConstraintSolver::backtrackUntilDecision(SolverDecisionLevel decisionLevel, bool isRestart/*=false*/)
{
	vxy_assert(decisionLevel < getCurrentDecisionLevel());
	if constexpr (LOG_BACKTRACKS)
	{
		VERTEXY_LOG("Level %d Step %d BACKTRACK to %d", getCurrentDecisionLevel(), m_stats.stepCount, decisionLevel);
	}

	// Slightly grow activity incremental value, in order to prioritize more recent constraints
	m_constraintConflictIncr *= CONSTRAINT_ACTIVITY_DECAY;

	if (!isRestart)
	{
		++m_stats.numBacktracks;
		m_stats.maxBackjump = max(m_stats.maxBackjump, uint32_t((getCurrentDecisionLevel() - decisionLevel) + 1));
	}

	const SolverTimestamp newTimestamp = getTimestampForDecisionLevel(decisionLevel + 1);
	m_variableDB.backtrack(newTimestamp);

	while (getCurrentDecisionLevel() > decisionLevel)
	{
		const VarID decisionVar = m_decisionLevels.back().variable;
		if (decisionVar.isValid())
		{
			vxy_assert(m_variableToDecisionLevel[decisionVar.raw()] != 0);
			m_variableToDecisionLevel[decisionVar.raw()] = 0;
		}
		m_decisionLevels.pop_back();
	}

	while (!m_disabledWatchMarkers.empty() && m_disabledWatchMarkers.back().level > decisionLevel)
	{
		const DisabledWatchMarker& marker = m_disabledWatchMarkers.back();
		m_variablePropagators[marker.var.raw()]->setWatcherEnabled(marker.handle, marker.sink, true);
		m_disabledWatchMarkers.pop_back();
	}

	for (IBacktrackingSolverConstraint* constraint : m_backtrackingConstraints)
	{
		constraint->backtrack(&m_variableDB, decisionLevel);
	}

	if (m_unfoundedSetAnalyzer != nullptr)
	{
		m_unfoundedSetAnalyzer->onBacktrack();
	}

	// Remove any propagations that were queued (since we just undid them)
	m_variablePropagationQueue.clear();
	m_constraintPropagationQueue.clear();
	m_constraintQueuedSet.setZeroed();
	m_variableQueuedSet.setZeroed();
	m_lastTriggeredSink = nullptr;
	m_lastTriggeredTs = -1;
}

void ConstraintSolver::notifyVariableModification(VarID variable, IConstraint* constraint)
{
	if (variable.raw() >= m_variableQueuedSet.size() || !m_variableQueuedSet[variable.raw()])
	{
		m_variableQueuedSet.pad(variable.raw()+1, false);
		m_variableQueuedSet[variable.raw()] = true;
		m_variablePropagationQueue.emplace_back(constraint, variable, m_variableDB.getLastModificationTimestamp(variable));
	}

	if (LOG_VARIABLE_PROPAGATIONS)
	{
		VERTEXY_LOG("    %s -> %s", m_variableDB.getVariableName(variable).c_str(), m_variableDB.getPotentialValues(variable).toString().c_str());
	}
}

void ConstraintSolver::queueConstraintPropagation(const IConstraint* constraint)
{
	const int constraintID = constraint->getID();
	if (constraintID >= m_constraintQueuedSet.size() || !m_constraintQueuedSet[constraintID])
	{
		m_constraintQueuedSet.pad(constraintID + 1, false);
		m_constraintQueuedSet[constraintID] = true;
		m_constraintPropagationQueue.push_front(constraint->getID());
	}
}

SolverDecisionLevel ConstraintSolver::getDecisionLevelForTimestamp(SolverTimestamp time) const
{
	int found = 0;

	// Binary search to find quickly
	int last = m_decisionLevels.size() - 1;
	int left = 0;
	int right = last;
	while (right >= left)
	{
		int mid = left + ((right - left) >> 1);
		bool under = m_decisionLevels[mid].modificationIndex < time;
		bool over = (mid == last) || (m_decisionLevels[mid + 1].modificationIndex >= time);
		if (!under)
		{
			right = mid - 1;
		}
		else if (!over)
		{
			left = mid + 1;
		}
		else
		{
			found = mid + 1;
			break;
		}
	}

	return found;
}

vector<Literal> ConstraintSolver::getExplanationForModification(SolverTimestamp modificationTime) const
{
	vxy_assert(modificationTime >= 0);

	vector<Literal> explanation;
	auto& mod = m_variableDB.getAssignmentStack().getStack()[modificationTime];
	vxy_assert(mod.constraint != nullptr);

	HistoricalVariableDatabase priorDB(&m_variableDB, modificationTime);
	const ValueSet& valueAfterPropagation = m_variableDB.getValueAfter(mod.variable, modificationTime);
	NarrowingExplanationParams params(this, &priorDB, mod.constraint, mod.variable, valueAfterPropagation, modificationTime);
	if (mod.explanation != nullptr)
	{
		explanation = mod.explanation(params);
	}
	else
	{
		explanation = mod.constraint->explain(params);
	}

	sanityCheckExplanation(modificationTime, explanation);

	return explanation;
}

void ConstraintSolver::sanityCheckExplanation(SolverTimestamp modificationTime, const vector<Literal>& explanation) const
{
	if (EXPLANATION_SANITY_CHECK)
	{
		vxy_assert(!explanation.empty());

		auto& mod = m_variableDB.getAssignmentStack().getStack()[modificationTime];
		int pivotIndex = indexOfPredicate(explanation.begin(), explanation.end(), [&](auto& lit) { return lit.variable == mod.variable; });
		vxy_assert(pivotIndex >= 0);
		{
			auto& valueAfterPropagation = m_variableDB.getValueAfter(mod.variable, modificationTime);
			ValueSet removedBits = mod.previousValue.excluding(valueAfterPropagation);
			vxy_assert(!explanation[pivotIndex].values.anyPossible(removedBits));
		}

		for (int i = 0; i < explanation.size(); ++i)
		{
			vxy_assert(explanation[i].values.contains(false));
			if (i != pivotIndex)
			{
				auto& varsForConstraint = getVariablesForConstraint(mod.constraint);
				vxy_assert(contains(varsForConstraint.begin(), varsForConstraint.end(), explanation[i].variable));
				const ValueSet& argValueBefore = m_variableDB.getValueBefore(explanation[i].variable, modificationTime);
				vxy_assert(!argValueBefore.anyPossible(explanation[i].values));
			}
		}
	}
}

ClauseConstraint* ConstraintSolver::learn(const vector<Literal>& explanation, const ConstraintGraphRelationInfo* relationInfo)
{
	ClauseConstraint& learnedCons = relationInfo != nullptr
		                                ? makeConstraintForGraph<ClauseConstraint>(*relationInfo, explanation, /*bLearned=*/true)
		                                : makeConstraint<ClauseConstraint>(explanation, /*bLearned=*/true);

	learnedCons.setStepLearned(m_stats.stepCount);

	//
	// Place the newly learned constraint in the appropriate pool. We place constraints with a low LBD
	// score into the permanent pool immediately. Otherwise it is placed into the temporary pool, where it
	// may get upgraded to the permanent pool later (or discarded).
	//
	// Note that learned constraints with one variable are not stored - these are simply propagated.
	//

	if (learnedCons.getNumLiterals() > 1)
	{
		learnedCons.computeLbd(m_variableDB);
		learnedCons.incrementActivity(m_constraintConflictIncr);

		static ConstraintHashFuncs hasher;
		const uint32_t hash = hasher(&learnedCons);

		auto foundExisting = m_learnedConstraintSet.find_by_hash(&learnedCons, hash);
		if (foundExisting != m_learnedConstraintSet.end())
		{
			VERTEXY_WARN("Duplicate clause %d created for %d", learnedCons.getID(), (*foundExisting)->getID());
		}

		m_learnedConstraintSet.insert(hash, nullptr, &learnedCons);

		bool canPromoteToGraph = (GRAPH_LEARNING_ENABLED && learnedCons.isPromotableToGraph());
		if (learnedCons.getLBD() <= MAX_PERMANENT_CONSTRAINT_LBD || canPromoteToGraph)
		{
			learnedCons.setPermanent();
			m_permanentLearnedConstraints.push_back(&learnedCons);

			// Once a constraint learned from a graph is promoted to permanent pool, we
			// instantiate it over the whole graph.
			if (canPromoteToGraph)
			{
				vxy_sanity(m_constraintsToPromoteToGraph.find(&learnedCons) == m_constraintsToPromoteToGraph.end());
				m_constraintsToPromoteToGraph[&learnedCons] = 0;
			}
		}
		else
		{
			vxy_assert(!learnedCons.isPermanent());
			m_temporaryLearnedConstraints.push_back(&learnedCons);
		}
	}

	//
	// Let various heuristics know that we encountered a conflict/learned a new constraint.
	//

	m_restartPolicy.onClauseLearned(learnedCons);
	for (auto heuristic : m_heuristicStack)
	{
		heuristic->onClauseLearned();
	}

	++m_stats.numConstraintsLearned;
	return &learnedCons;
}

WatcherHandle ConstraintSolver::addVariableWatch(VarID varID, EVariableWatchType watchType, IVariableWatchSink* sink)
{
	vxy_assert(varID.isValid());
	return m_variablePropagators[varID.raw()]->addWatcher(sink, watchType);
}

void ConstraintSolver::disableWatcherUntilBacktrack(WatcherHandle handle, VarID variable, IVariableWatchSink* sink)
{
	SolverDecisionLevel curLevel = getCurrentDecisionLevel();
	if (m_variablePropagators[variable.raw()]->setWatcherEnabled(handle, sink, false))
	{
		if (curLevel > 0)
		{
			vxy_assert(m_disabledWatchMarkers.empty() || m_disabledWatchMarkers.back().level <= curLevel);
			m_disabledWatchMarkers.push_back({curLevel, variable, handle, sink});
		}
	}
}

WatcherHandle ConstraintSolver::addVariableValueWatch(VarID varID, const ValueSet& watchValues, IVariableWatchSink* sink)
{
	vxy_assert(varID.isValid());
	vxy_assert(watchValues.size() == m_variableDB.getDomainSize(varID));
	return m_variablePropagators[varID.raw()]->addValueWatcher(sink, watchValues);
}

void ConstraintSolver::removeVariableWatch(VarID varID, WatcherHandle handle, IVariableWatchSink* sink)
{
	vxy_assert(varID.isValid());
	m_variablePropagators[varID.raw()]->removeWatcher(handle, sink);
}

void ConstraintSolver::markConstraintActivity(ClauseConstraint& constraint, bool recomputeLBD)
{
	vxy_assert(constraint.isLearned());

	if (constraint.isPermanent())
	{
		return;
	}

	constraint.incrementActivity(m_constraintConflictIncr);
	if (constraint.getActivity() > MAX_CONFLICT_ACTIVITY)
	{
		// Rescale everything to stay within floating point range
		for (auto c : m_temporaryLearnedConstraints)
		{
			c->scaleActivity(MAX_CONFLICT_ACTIVITY_RESCALE);
		}
		m_constraintConflictIncr *= MAX_CONFLICT_ACTIVITY_RESCALE;
	}

	// Update LBD for clause involved in a conflict
	if (recomputeLBD && constraint.getLBD() > 2)
	{
		constraint.computeLbd(m_variableDB);
		if (constraint.getLBD() <= MAX_PERMANENT_CONSTRAINT_LBD)
		{
			constraint.setPermanent();

			m_temporaryLearnedConstraints.erase_first(&constraint);
			m_permanentLearnedConstraints.push_back(&constraint);

			// Once a constraint learned from a graph is promoted to permanent pool, we
			// instantiate it over the whole graph.
			if (GRAPH_LEARNING_ENABLED && constraint.isPromotableToGraph() &&
				m_constraintsToPromoteToGraph.find(&constraint) == m_constraintsToPromoteToGraph.end())
			{
				m_constraintsToPromoteToGraph[&constraint] = 0;
			}
		}
	}
}

bool ConstraintSolver::promoteConstraintToGraph(ClauseConstraint& constraint, int& startVertex)
{
	vxy_assert(constraint.isLearned());
	vxy_assert(constraint.isPermanent());
	vxy_assert(constraint.getGraph() != nullptr);
	vxy_assert(constraint.isPromotableToGraph());

	++m_stats.numConstraintPromotions;

	auto& graph = constraint.getGraph();

	int promotingNode = constraint.getGraphRelationInfo()->sourceGraphVertex;

	int numCreated = 0;
	int numDuplicates = 0;

	//
	// Instantiate the constraint for each applicable node in the graph
	//

	bool success = true;

	static vector<Literal> nodeClauses;
	vxy_sanity(startVertex < graph->getNumVertices());
	for (int nodeIndex = startVertex; nodeIndex < graph->getNumVertices(); ++nodeIndex)
	{
		// No need to create the same exact clause we're promoting
		if (nodeIndex == promotingNode)
		{
			continue;
		}

		ConstraintGraphRelationInfo newRelationInfo;
		if (!createLiteralsForGraphPromotion(constraint, nodeIndex, newRelationInfo, nodeClauses))
		{
			continue;
		}

		ClauseConstraint* newCons = ClauseConstraint::Factory::construct(ConstraintFactoryParams(*this, newRelationInfo), nodeClauses, true);
		static ConstraintHashFuncs hasher;
		const uint32_t hash = hasher(newCons);

		auto existingIt = m_learnedConstraintSet.find_by_hash(newCons, hash);
		if (existingIt != m_learnedConstraintSet.end())
		{
			(*existingIt)->setPromotedToGraph();

			delete newCons;
			newCons = nullptr;

			++numDuplicates;
		}
		else
		{
			++numCreated;
			++m_stats.numGraphClonedConstraints;
			++m_stats.numConstraintsLearned;

			registerConstraint(newCons);
			m_learnedConstraintSet.insert(hash, nullptr, newCons);

			newCons->setStepLearned(m_stats.stepCount);
			newCons->setPromotionSource(&constraint);

			m_temporaryLearnedConstraints.push_back(newCons);

			m_lastTriggeredSink = newCons;
			m_lastTriggeredTs = m_variableDB.getTimestamp();
			if (!newCons->initialize(&m_variableDB, nullptr))
			{
				success = false;
				startVertex = nodeIndex;
				break;
			}
			m_lastTriggeredSink = nullptr;
			m_lastTriggeredTs = -1;
		}
	}

	if (VERTEXY_LOG_ACTIVE() && LOG_GRAPH_PROMOTIONS)
	{
		ConstraintGraphRelationInfo tempInfo;
		vector<Literal> tempLits;
		vxy_verify(createLiteralsForGraphPromotion(constraint, constraint.getGraphRelationInfo()->sourceGraphVertex, tempInfo, tempLits));

		wstring relationStr;
		for (auto& entry : constraint.getGraphRelationInfo()->relations)
		{
			visit([&](auto&& typedRel)
			{
				using T = decay_t<decltype(typedRel)>;
				if constexpr (is_same_v<T, shared_ptr<const IGraphRelation<Literal>>>)
				{
					relationStr.append_sprintf(TEXT("CLAUSE(%s)\n"), typedRel->toString().c_str());
				}
				else if constexpr (is_same_v<T, shared_ptr<const IGraphRelation<VarID>>>)
				{
					VarID varID;
					vxy_verify(typedRel->getRelation(constraint.getGraphRelationInfo()->sourceGraphVertex, varID));
					const ValueSet& litVals = constraint.getLiteralForVariable(varID)->values;
					if (litVals.getNumSetBits() > (litVals.size() >> 1))
					{
						relationStr.append_sprintf(TEXT("%s <not> %s\n"), typedRel->toString().c_str(), valueSetToString(varID, litVals.inverted()).c_str());
					}
					else
					{
						relationStr.append_sprintf(TEXT("%s <is> %s\n"), typedRel->toString().c_str(), valueSetToString(varID, litVals).c_str());
					}
				}
				else
				{
					// Do not expect a IGraphRelation<FSignedClause> at this point.
					vxy_fail();
				}
			}, entry.relation);
		}

		VERTEXY_LOG("Promoted constraint %d:\n%s%d Created, %d dupes\n", constraint.getID(), relationStr.c_str(), numCreated, numDuplicates);
		if (numCreated == 0)
		{
			VERTEXY_LOG("Could not promote %s", clauseConstraintToString(constraint).c_str());
		}
	}

	if (numCreated == 0)
	{
		++m_stats.numFailedConstraintPromotions;
	}

	if (success)
	{
		constraint.setPromotedToGraph();
		startVertex = graph->getNumVertices();
	}
	return success;
}

bool ConstraintSolver::createLiteralsForGraphPromotion(const ClauseConstraint& promotingCons, int destVertex, ConstraintGraphRelationInfo& outRelInfo, vector<Literal>& outLits) const
{
	vxy_assert(promotingCons.getGraph() != nullptr);
	outLits.clear();
	outRelInfo.reset(promotingCons.getGraph(), destVertex);
	outRelInfo.reserve(promotingCons.getGraphRelationInfo()->relations.size());

	bool success = true;
	for (auto& entry : promotingCons.getGraphRelationInfo()->relations)
	{
		visit([&](auto&& typedRel)
		{
			using T = decay_t<decltype(typedRel)>;
			if constexpr (is_same_v<T, shared_ptr<const IGraphRelation<VarID>>>)
			{
				VarID correspondingVar;
				vxy_verify(typedRel->getRelation(promotingCons.getGraphRelationInfo()->sourceGraphVertex, correspondingVar));
				const ValueSet& correspondingVarInitialVals = m_variableDB.getInitialValues(correspondingVar);
				const Literal* correspondingLit = promotingCons.getLiteralForVariable(correspondingVar);
				vxy_sanity(correspondingLit != nullptr);

				VarID var;
				if (!typedRel->getRelation(destVertex, var))
				{
					success = false;
					return;
				}
				else if (m_variableDB.getInitialValues(var) != correspondingVarInitialVals)
				{
					success = false;
					return;
				}
				else
				{
					outRelInfo.addRelation(var, typedRel);
					outLits.push_back(Literal{var, correspondingLit->values});
				}
			}
			else if constexpr (is_same_v<T, shared_ptr<const IGraphRelation<Literal>>>)
			{
				Literal correspondingClause;
				vxy_verify(typedRel->getRelation(promotingCons.getGraphRelationInfo()->sourceGraphVertex, correspondingClause));
				const ValueSet& correspondingVarInitialVals = m_variableDB.getInitialValues(correspondingClause.variable);

				Literal clause;
				if (!typedRel->getRelation(destVertex, clause))
				{
					success = false;
					return;
				}
				else if (m_variableDB.getInitialValues(clause.variable) != correspondingVarInitialVals)
				{
					success = false;
					return;
				}
				else
				{
					vxy_assert(clause.values.contains(true));
					outRelInfo.addRelation(clause.variable, typedRel);
					outLits.push_back(clause);
				}
			}
			else
			{
				// Do not expect a IGraphRelation<FSignedClause> at this point.
				vxy_fail();
			}
		}, entry.relation);

		if (!success)
		{
			break;
		}
	}

	return success;
}

void ConstraintSolver::purgeConstraints()
{
	// Binary constraints always go to front, otherwise sort by activity
	quick_sort(m_temporaryLearnedConstraints.begin(), m_temporaryLearnedConstraints.end(), [&](const ClauseConstraint* lhs, const ClauseConstraint* rhs)
	{
		vxy_assert(lhs->getNumLiterals() >= 2);
		vxy_assert(rhs->getNumLiterals() >= 2);

		if (lhs->getNumLiterals() > 2 && rhs->getNumLiterals() == 2)
		{
			return false;
		}
		else if (lhs->getNumLiterals() == 2 && rhs->getNumLiterals() > 2)
		{
			return true;
		}

		return lhs->getActivity() > rhs->getActivity();
	});

	const int prevTotal = m_temporaryLearnedConstraints.size();
	const int numRemaining = prevTotal * (1.0f - CONSTRAINT_PURGE_PERCENT);
	const int numPurged = prevTotal - numRemaining;

	int bestRemovedLBD = INT_MAX;
	float bestRemovedActivity = -1.f;

	// Get rid of any watch restoration markers for constraints we're deleting
	for (int i = m_disabledWatchMarkers.size() - 1; i >= 0; --i)
	{
		for (int j = m_temporaryLearnedConstraints.size() - 1; j >= 0 && m_temporaryLearnedConstraints.size() > numRemaining; --j)
		{
			if (m_disabledWatchMarkers[i].sink == m_temporaryLearnedConstraints[j])
			{
				m_disabledWatchMarkers.erase(&m_disabledWatchMarkers[i]);
				break;
			}
		}
	}

	for (int i = m_temporaryLearnedConstraints.size() - 1; i >= 0 && m_temporaryLearnedConstraints.size() > numRemaining; --i)
	{
		auto& cons = m_temporaryLearnedConstraints[i];

		if (!cons->isLocked())
		{
			bestRemovedLBD = min(bestRemovedLBD, cons->getLBD());
			bestRemovedActivity = max(bestRemovedActivity, cons->getActivity());

			cons->reset(&m_variableDB);

			m_learnedConstraintSet.erase(cons);

			vxy_assert(m_constraints[cons->getID()].get() == cons);
			m_constraints[cons->getID()].reset();

			m_temporaryLearnedConstraints.erase_unsorted(&m_temporaryLearnedConstraints[i]);
		}
		else
		{
			++m_stats.numLockedConstraintsToPurge;
		}
	}

	m_stats.numPurgedConstraints += numPurged;
}

void ConstraintSolver::findDuplicateClauses()
{
	vector<ClauseConstraint*> allLearnedConstraints;
	allLearnedConstraints.insert(allLearnedConstraints.end(), m_temporaryLearnedConstraints.begin(), m_temporaryLearnedConstraints.end());
	allLearnedConstraints.insert(allLearnedConstraints.end(), m_permanentLearnedConstraints.begin(), m_permanentLearnedConstraints.end());

	m_stats.numDuplicateLearnedConstraints = 0;
	hash_set<ClauseConstraint*, ConstraintHashFuncs> constraintSet;
	for (ClauseConstraint* constraint : allLearnedConstraints)
	{
		if (constraintSet.find(constraint) != constraintSet.end())
		{
			++m_stats.numDuplicateLearnedConstraints;
		}
		else
		{
			constraintSet.insert(constraint);
		}
	}
}

void ConstraintSolver::sanityCheckValid()
{
	for (int i = 0; i < m_constraints.size(); ++i)
	{
		if (m_constraintIsChild[i])
		{
			continue;
		}

		auto& constraint = m_constraints[i];
		if (constraint != nullptr && constraint->checkConflicting(&m_variableDB))
		{
			vxy_assert_msg(false, "Constraint %d conflicting after solution found!", constraint->getID());
		}
	}
}

void ConstraintSolver::sanityCheckGraphClauses()
{
	if (TEST_GRAPH_PROMOTIONS)
	{
		vector<ClauseConstraint*> allLearnedConstraints;
		allLearnedConstraints.insert(allLearnedConstraints.end(), m_temporaryLearnedConstraints.begin(), m_temporaryLearnedConstraints.end());
		allLearnedConstraints.insert(allLearnedConstraints.end(), m_permanentLearnedConstraints.begin(), m_permanentLearnedConstraints.end());

		for (ClauseConstraint* constraint : allLearnedConstraints)
		{
			if (constraint->isPromotableToGraph() && !constraint->isPromotedFromGraph())
			{
				int startNode = 0;
				constraint->setPermanent();
				bool promoted = promoteConstraintToGraph(*constraint, startNode);
				vxy_assert_msg(promoted, "Invalid graph constraint %d: %s", constraint->getID(), clauseConstraintToString(*constraint).c_str());
			}
		}
	}
}

void ConstraintSolver::dumpStats(bool verbose)
{
	VERTEXY_LOG("%s", m_stats.toString(verbose).c_str());
}

void ConstraintSolver::debugSaveSolution(const wchar_t* filename) const
{
	std::basic_ofstream<wchar_t> file(filename);
	vxy_verify(file.is_open());

	for (int i = 1; i < m_variableDB.getNumVariables() + 1; ++i)
	{
		wstring str(wstring::CtorSprintf(), TEXT("%d %d"), i, m_variableDB.getSolvedValue(VarID(i)));
		file << str.c_str() << TEXT("\n");
	}
	VERTEXY_LOG("Wrote solution to %s", filename);
	file.close();
}

void ConstraintSolver::debugAttemptSolution(const wchar_t* filename)
{
	VERTEXY_WARN("Attempting predefined solution %s...", filename);

	std::basic_ifstream<wchar_t> file(filename);
	vxy_verify(file.is_open());

	vector<tuple<VarID, int>> solution;
	std::wstring line;
	while (std::getline(file, line))
	{
		int var, value;
		vxy_verify(2 == swscanf_s(line.c_str(), TEXT("%d %d"), &var, &value));
		solution.push_back(make_tuple(VarID(var), value));
	}

	if (getCurrentDecisionLevel() > 0)
	{
		backtrackUntilDecision(0, true);
	}

	m_restartPolicy.onRestarted();
	for (auto heuristic : m_heuristicStack)
	{
		heuristic->onRestarted();
	}
	m_newDescentAfterRestart = true;
	++m_stats.numRestarts;

	for (auto& entry : solution)
	{
		vxy_verify(propagate());

		startNextDecision();

		VarID pickedVar = get<0>(entry);
		int pickedValueIndex = get<1>(entry);
		ValueSet pickedValue;
		pickedValue.pad(getDomain(pickedVar).getDomainSize(), false);
		pickedValue[pickedValueIndex] = true;

		// check that the strategy is actually narrowing the solution
		if (m_variableDB.getPotentialValues(pickedVar).isSubsetOf(pickedValue))
		{
			continue;
		}

		vxy_assert(m_variableToDecisionLevel[pickedVar.raw()] == 0);
		m_variableToDecisionLevel[pickedVar.raw()] = getCurrentDecisionLevel();
		m_decisionLevels.back().variable = pickedVar;

		bool success = m_variableDB.constrainToValues(pickedVar, pickedValue, nullptr);
		vxy_assert(success); // If this goes off, the strategy did not return a potential value
	}

	VERTEXY_WARN("Finished predefined solution!", filename);
}

wstring ConstraintSolver::clauseConstraintToString(const ClauseConstraint& constraint) const
{
	vector<Literal> clauses;
	constraint.getLiterals(clauses);
	return literalArrayToString(clauses);
}

wstring ConstraintSolver::literalArrayToString(const vector<Literal>& clauses) const
{
	wstring out;
	for (const Literal& clause : clauses)
	{
		out.append_sprintf(TEXT("\n  %s"), literalToString(clause).c_str());
	}
	return out;
}

wstring ConstraintSolver::literalToString(const Literal& lit) const
{
	wstring out = m_variableDB.getVariableName(lit.variable);
	ValueSet values = lit.values;
	if (values.getNumSetBits() > (values.size() >> 1))
	{
		out += TEXT(" <not> ");
		values.invert();
	}
	else
	{
		out += TEXT(" <is> ");
	}

	out += valueSetToString(lit.variable, values);
	return out;
}

wstring ConstraintSolver::valueSetToString(VarID varID, const ValueSet& vals) const
{
	wstring out = TEXT("[");
	bool first = true;
	int start = vals.indexOf(true);
	while (start >= 0 && start < vals.size())
	{
		int end = start + 1;
		while (end < vals.size() && vals[end])
		{
			++end;
		}

		if (first)
		{
			first = false;
		}
		else
		{
			out += TEXT(", ");
		}
		if (start + 1 == end)
		{
			out.append_sprintf(TEXT("%d"), m_variableDomains[varID.raw()].getValueForIndex(start));
		}
		else
		{
			out.append_sprintf(TEXT("%d - %d"), m_variableDomains[varID.raw()].getValueForIndex(start), m_variableDomains[varID.raw()].getValueForIndex(end - 1));
		}

		start = end;
		while (start < vals.size() && !vals[start])
		{
			++start;
		}
	}
	out += TEXT("]");
	return out;
}
