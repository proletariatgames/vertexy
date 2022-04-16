// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/ClauseConstraint.h"

#include "variable/IVariableDatabase.h"
#include "variable/SolverVariableDatabase.h"

using namespace Vertexy;

#define SANITY_CHECK VERTEXY_SANITY_CHECKS

static constexpr bool USE_WATCHER_DISABLE = true;

static constexpr int DISABLE_WATCHER_MIN_DOMAIN_LENGTH = 64;
static_assert(DISABLE_WATCHER_MIN_DOMAIN_LENGTH >= 2, "DISABLE_WATCHER_MIN_DOMAIN_LENGTH < 2 makes no sense");

ClauseConstraint* ClauseConstraint::ClauseConstraintFactory::construct(const ConstraintFactoryParams& params, const vector<SignedClause>& clauses)
{
	vector<Literal> transformedClauses;
	for_each(clauses.begin(), clauses.end(), [&](auto& clause)
	{
		transformedClauses.push_back(clause.translateToLiteral(params));
	});

	return construct(params, transformedClauses);
}

ClauseConstraint* ClauseConstraint::ClauseConstraintFactory::construct(const ConstraintFactoryParams& params, ENoGood noGood, const vector<SignedClause>& clauses)
{
	vxy_assert(noGood == ENoGood::NoGood);
	vector<Literal> transformedClauses;
	for_each(clauses.begin(), clauses.end(), [&](auto& clause)
	{
		transformedClauses.push_back(clause.inverted().translateToLiteral(params));
	});

	return construct(params, transformedClauses);
}

ClauseConstraint* ClauseConstraint::ClauseConstraintFactory::construct(const ConstraintFactoryParams& params, const vector<Literal>& lits, bool isLearned)
{
	vxy_assert(lits.size() < 0xFFFF);
	int baseSize = sizeof(ClauseConstraint);
	int clauseSize = baseSize + sizeof(Literal) * lits.size();
	auto buffer = reinterpret_cast<ClauseConstraint*>(new uint8_t[clauseSize]);
	return new(buffer) ClauseConstraint(params, lits, isLearned);
}

ClauseConstraint::ClauseConstraint(const ConstraintFactoryParams& params, const vector<Literal>& literals, bool isLearned)
	: IConstraint(params)
	, m_watches{INVALID_WATCHER_HANDLE, INVALID_WATCHER_HANDLE}
	, m_numLiterals(literals.size())
{
	// construct the in-place literals array
	for (int i = 0; i < m_numLiterals; ++i)
	{
		new(&m_literals[i]) Literal(literals[i]);
	}

	#if VERTEXY_SANITY_CHECKS
	{
		for (int i = 0; i < m_numLiterals; ++i)
		{
			for (int j = i+1; j < m_numLiterals; ++j)
			{
				vxy_sanity_msg(m_literals[i].variable != m_literals[j].variable,
					"Clause %d contains variable %d twice!", getID(), m_literals[i].variable
				);
			}
		}
	}
	#endif

	if (isLearned)
	{
		m_extendedInfo = make_unique<ExtendedInfo>();
		m_extendedInfo->isLearned = true;
		m_extendedInfo->isPermanent = false;
		m_extendedInfo->isPromoted = false;
		m_extendedInfo->promotionSource = nullptr;
	}
}

vector<VarID> ClauseConstraint::getConstrainingVariables() const
{
	vector<VarID> variables;
	variables.reserve(m_numLiterals);
	for (int i = 0; i < m_numLiterals; ++i)
	{
		variables.push_back(m_literals[i].variable);
	}
	return variables;
}

bool ClauseConstraint::initialize(IVariableDatabase* db, IConstraint* outerConstraint)
{
	int numSupports = m_numLiterals;
	if (!isLearned() || isPromotedFromGraph())
	{
		// User-specified constraints and those created from graph promotion don't necessarily have true literals
		// at the front. Make it so.

		// TODO: We could move the false literals to the back of the list and keep a separate NumValidLiterals count,
		// but that would increase storage size of the literal. Worth investigating.

		numSupports = 0;
		bool fullySatisfied = false;
		for (int destIndex = 0; destIndex < 2; ++destIndex)
		{
			vector<Literal> newLiterals;
			for (int searchIndex = destIndex; searchIndex < m_numLiterals; ++searchIndex)
			{
				auto& vals = db->getPotentialValues(m_literals[searchIndex].variable);
				if (vals.anyPossible(m_literals[searchIndex].values))
				{
					if (vals.isSubsetOf(m_literals[searchIndex].values))
					{
						fullySatisfied = true;
					}

					swap(m_literals[destIndex], m_literals[searchIndex]);
					++numSupports;
					break;
				}
			}
		}

		if (fullySatisfied)
		{
			db->markConstraintFullySatisfied(this);
		}
	}

	// Register watchers. We only need to do this if we have more than one support (otherwise we just narrow or fail
	// immediately below), or if we have an outer constraint (in which case we can't rely on narrowing permanently)
	if (numSupports > 1 || outerConstraint != nullptr)
	{
		if (m_numLiterals >= 1)
		{
			m_watches[0] = db->addVariableValueWatch(m_literals[0].variable, m_literals[0].values, this);
		}
		if (m_numLiterals >= 2)
		{
			m_watches[1] = db->addVariableValueWatch(m_literals[1].variable, m_literals[1].values, this);
		}
	}

	if (numSupports == 0)
	{
		return false;
	}
	else if (numSupports == 1)
	{
		// Propagate unit clause
		if (!db->constrainToValues(m_literals[0].variable, m_literals[0].values, this))
		{
			return false;
		}
	}

	return true;
}

bool ClauseConstraint::propagateAndStrengthen(IVariableDatabase* db, vector<VarID>& outVarsRemoved)
{
	outVarsRemoved.clear();

	// Remove any literals that are impossible
	for (int i = 0; i < m_numLiterals;)
	{
		if (db->anyPossible(m_literals[i]))
		{
			++i;
		}
		else
		{
			outVarsRemoved.push_back(m_literals[i].variable);
			removeLiteralAt(db, i);
		}
	}

	if (m_numLiterals == 0)
	{
		return false;
	}
	else if (m_numLiterals == 1)
	{
		return db->constrainToValues(m_literals[0].variable, m_literals[0].values, this);
	}

	return true;
}

bool ClauseConstraint::makeUnit(IVariableDatabase* db, int literalIndex)
{
	vxy_assert(isLearned());

	#if SANITY_CHECK
	for (int i = 0; i < m_numLiterals; ++i)
	{
		if (i == literalIndex)
		{
			continue;
		}
		vxy_assert(!db->anyPossible(m_literals[i]));
	}
	#endif

	return db->constrainToValues(m_literals[literalIndex].variable, m_literals[literalIndex].values, this);
}

void ClauseConstraint::reset(IVariableDatabase* db)
{
	if (m_watches[0] != INVALID_WATCHER_HANDLE)
	{
		db->removeVariableWatch(m_literals[0].variable, m_watches[0], this);
		m_watches[0] = INVALID_WATCHER_HANDLE;
	}

	if (m_watches[1] != INVALID_WATCHER_HANDLE)
	{
		db->removeVariableWatch(m_literals[1].variable, m_watches[1], this);
		m_watches[1] = INVALID_WATCHER_HANDLE;
	}
}

bool ClauseConstraint::onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet&, bool& removeWatch)
{
	vxy_assert(variable == m_literals[0].variable || variable == m_literals[1].variable);
	int index = (variable == m_literals[0].variable) ? 0 : 1;
	int otherIndex = index == 0 ? 1 : 0;
	vxy_assert(m_watches[index] != INVALID_WATCHER_HANDLE);

	auto& narrowedLit = m_literals[index];
	if (!USE_WATCHER_DISABLE || db->getDomainSize(variable) <= DISABLE_WATCHER_MIN_DOMAIN_LENGTH)
	{
		auto& vals = db->getPotentialValues(variable);
		if (vals.anyPossible(narrowedLit.values))
		{
			if (vals.isSubsetOf(narrowedLit.values))
			{
				db->markConstraintFullySatisfied(this);
			}
			return true;
		}
	}
	else
	{
		vxy_sanity(!db->anyPossible(variable, narrowedLit.values));
	}

	// Search for a new support, and swap it with our position
	for (int nextSupportIndex = 2; nextSupportIndex < m_numLiterals; ++nextSupportIndex)
	{
		auto& nextSupportLit = m_literals[nextSupportIndex];
		auto& vals = db->getPotentialValues(nextSupportLit.variable);
		if (vals.anyPossible(nextSupportLit.values))
		{
			if (vals.isSubsetOf(nextSupportLit.values))
			{
				db->markConstraintFullySatisfied(this);
			}
			// remove old watch
			// NOTE we only do this if we find a support. We still need two watches if we backtrack.
			removeWatch = true;

			swap(nextSupportLit, narrowedLit);

			// add new watch
			m_watches[index] = db->addVariableValueWatch(m_literals[index].variable, m_literals[index].values, this);
			return true;
		}
	}

	#if SANITY_CHECK
	for (int i = 0; i < m_numLiterals; ++i)
	{
		vxy_assert(i == otherIndex || !db->anyPossible(m_literals[i]));
	}
	#endif

	if (USE_WATCHER_DISABLE && db->getDomainSize(variable) > DISABLE_WATCHER_MIN_DOMAIN_LENGTH)
	{
		db->disableWatcherUntilBacktrack(m_watches[index], narrowedLit.variable, this);
	}

	if (otherIndex >= m_numLiterals)
	{
		// should only be possible when we are a child constraint
		return false;
	}
	return db->constrainToValues(m_literals[otherIndex].variable, m_literals[otherIndex].values, this);
}

void ClauseConstraint::removeLiteralAt(IVariableDatabase* db, int litIndex)
{
	vxy_assert(litIndex >= 0 && litIndex < m_numLiterals);
	if (litIndex < 2 && m_watches[litIndex] != INVALID_WATCHER_HANDLE)
	{
		vxy_sanity(m_watches[litIndex] != INVALID_WATCHER_HANDLE);
		db->removeVariableWatch(m_literals[litIndex].variable, m_watches[litIndex], this);
		m_watches[litIndex] = INVALID_WATCHER_HANDLE;
	}

	if (litIndex != m_numLiterals-1)
	{
		swap(m_literals[litIndex], m_literals[m_numLiterals-1]);
		if (m_numLiterals <= 2)
		{
			swap(m_watches[litIndex], m_watches[m_numLiterals-1]);
		}
	}
	--m_numLiterals;

	if (litIndex < 2 && litIndex < m_numLiterals)
	{
		if (!db->anyPossible(m_literals[litIndex]))
		{
			// attempt to keep both two watched literals positive
			for (int j = 2; j < m_numLiterals; ++j)
			{
				if (db->anyPossible(m_literals[j]))
				{
					swap(m_literals[litIndex], m_literals[j]);
					if (m_watches[litIndex] != INVALID_WATCHER_HANDLE)
					{
						db->removeVariableWatch(m_literals[litIndex].variable, m_watches[litIndex], this);
						m_watches[litIndex] = INVALID_WATCHER_HANDLE;
					}
					break;
				}
			}
		}

		if (m_watches[litIndex] == INVALID_WATCHER_HANDLE)
		{
			m_watches[litIndex] = db->addVariableValueWatch(m_literals[litIndex].variable, m_literals[litIndex].values, this);
		}
	}

	// !!FIXME!! We can't currently graph-promote constraints that have literals removed, because
	// the conflict analyzer relies on the full constraint. In the future we could rebuild the
	// removed literals from the relations to do this.
	m_graphRelationInfo.reset();
}

void ClauseConstraint::getLiterals(vector<Literal>& outLiterals) const
{
	outLiterals.clear();
	outLiterals.reserve(m_numLiterals);
	for (int i = 0; i < m_numLiterals; ++i)
	{
		outLiterals.push_back(m_literals[i]);
	}
}

vector<Literal> ClauseConstraint::getLiteralsCopy() const
{
	vector<Literal> outLiterals;
	outLiterals.reserve(m_numLiterals);
	for (int i = 0; i < m_numLiterals; ++i)
	{
		outLiterals.push_back(m_literals[i]);
	}
	return outLiterals;
}

bool ClauseConstraint::checkConflicting(IVariableDatabase* db) const
{
	for (int i = 0; i < m_numLiterals; ++i)
	{
		if (db->anyPossible(m_literals[i]))
		{
			return false;
		}
	}
	return true;
}

void ClauseConstraint::computeLbd(const SolverVariableDatabase& db)
{
	auto& stack = db.getAssignmentStack().getStack();

	static TValueBitset<> decisionLevels;
	decisionLevels.pad(db.getDecisionLevel() + 1, false);
	decisionLevels.setZeroed();

	int numUniqueDecisionLevels = 0;
	for (int i = 0; i < m_numLiterals; ++i)
	{
		SolverTimestamp latestTime = db.getLastModificationTimestamp(m_literals[i].variable);
		while (latestTime >= 0)
		{
			vxy_assert(stack[latestTime].variable == m_literals[i].variable);
			if (stack[latestTime].previousValue.anyPossible(m_literals[i].values))
			{
				break;
			}

			latestTime = stack[latestTime].previousVariableAssignment;
		}

		SolverDecisionLevel decisionLevel = db.getDecisionLevelForTimestamp(latestTime);
		if (decisionLevel > 0 && !decisionLevels[decisionLevel])
		{
			decisionLevels[decisionLevel] = true;
			++numUniqueDecisionLevels;
		}
	}

	if (numUniqueDecisionLevels + 1 < getLBD())
	{
		// LBD stored as a byte, so clamp the value
		if (numUniqueDecisionLevels > 0xFF)
		{
			m_extendedInfo->LBD = 0xFF;
		}
		else
		{
			m_extendedInfo->LBD = uint8_t(numUniqueDecisionLevels);
		}
	}

	// learned constraints from UnfoundedSetAnalyzer can have a zero LBD
	// vxy_assert(m_extendedInfo->LBD > 0);
}

#undef SANITY_CHECK
