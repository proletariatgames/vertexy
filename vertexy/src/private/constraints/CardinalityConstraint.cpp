// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/CardinalityConstraint.h"
#include "topology/BipartiteGraph.h"
#include "ds/DisjointSet.h"
#include "variable/IVariableDatabase.h"

#include <EASTL/sort.h>

using namespace Vertexy;

#define MATCHING_SANITY_CHECK VERTEXY_SANITY_CHECKS

CardinalityConstraint* CardinalityConstraint::CardinalityConstraintFactory::construct(const ConstraintFactoryParams& params, const vector<VarID>& variables, const hash_map<int, tuple<int, int>>& cardinalitiesPerValue)
{
	int minDomain;
	vector<VarID> unifiedVars = params.unifyVariableDomains(variables, &minDomain);

	vector<int> mins;
	vector<int> maxs;
	for (auto it = cardinalitiesPerValue.begin(), itEnd = cardinalitiesPerValue.end(); it != itEnd; ++it)
	{
		int value = it->first - minDomain;
		while (mins.size() <= value)
		{
			mins.push_back(0);
		}
		while (maxs.size() <= value)
		{
			maxs.push_back(variables.size() + 1);
		}
		mins[value] = get<0>(it->second);
		maxs[value] = get<1>(it->second);
	}
	return new CardinalityConstraint(params, unifiedVars, mins, maxs);
}

CardinalityConstraint::CardinalityConstraint(const ConstraintFactoryParams& params, const vector<VarID>& inVariables, const vector<int>& inMins, const vector<int>& inMaxs)
	: IBacktrackingSolverConstraint(params)
	, m_allVariables(inVariables)
	, m_mins(inMins)
	, m_maxs(inMaxs)
	, m_hasUpperBoundConstraint(false)
	, m_hasLowerBoundConstraint(false)
	, m_maxDomainSize(-1)
{
}

bool CardinalityConstraint::initialize(IVariableDatabase* db)
{
	m_maxDomainSize = 0;
	m_minDomainValue = INT_MAX;
	m_maxDomainValue = -INT_MAX;
	for (int i = 0; i < m_allVariables.size(); ++i)
	{
		VarID var = m_allVariables[i];

		if (db->getDomainSize(var) > m_maxDomainSize)
		{
			m_maxDomainSize = db->getDomainSize(var);

			while (m_mins.size() < m_maxDomainSize)
			{
				m_mins.push_back(0);
			}
			while (m_maxs.size() < m_maxDomainSize)
			{
				m_maxs.push_back(m_allVariables.size() + 1);
			}
		}

		const ValueSet& values = db->getPotentialValues(var);
		m_minDomainValue = min(m_minDomainValue, values.indexOf(true));
		m_maxDomainValue = max(m_maxDomainValue, values.lastIndexOf(true));

		// If this variable could potentially be impacted by the upper or lower bound constraints, add them
		// to the relevant sets.
		bool addLowerBound = false;
		bool addUpperBound = false;
		for (auto it = values.beginSetBits(), itEnd = values.endSetBits(); it != itEnd; ++it)
		{
			if (m_mins[*it] > 0)
			{
				addLowerBound = true;
			}
			if (m_maxs[*it] < m_allVariables.size())
			{
				addUpperBound = true;
			}
		}

		if (addLowerBound)
		{
			vxy_sanity(!contains(m_lowerBoundVariables.begin(), m_lowerBoundVariables.end(), var));
			m_lowerBoundVariables.push_back(var);
		}

		if (addUpperBound)
		{
			vxy_sanity(!contains(m_upperBoundVariables.begin(), m_upperBoundVariables.end(), var));
			m_upperBoundVariables.push_back(var);
		}

		if (addLowerBound || addUpperBound)
		{
			m_watcherHandles[var] = db->addVariableWatch(var, EVariableWatchType::WatchModification, this);
		}
	}

	//
	// Set Mins/Maxs arrays to the total domain size, for convenience.
	//

	while (m_mins.size() > m_maxDomainSize)
	{
		m_mins.pop_back();
	}

	while (m_maxs.size() > m_maxDomainSize)
	{
		m_maxs.pop_back();
	}

	// Fail immediately if there is a minimum for some value that is not in any variables.
	for (int i = m_maxDomainValue + 1; i < m_maxDomainSize; ++i)
	{
		if (m_mins[i] > 0)
		{
			return false;
		}
	}

	// TrimmedMins/TrimmedMaxs is for the values that are actually in the input set.
	m_trimmedMins.reserve((m_maxDomainValue - m_minDomainValue) + 1);
	m_trimmedMaxs.reserve((m_maxDomainValue - m_minDomainValue) + 1);

	m_lowerBoundConstrainedValues.pad(m_maxDomainSize, false);
	m_upperBoundConstrainedValues.pad(m_maxDomainSize, false);

	m_numValueNodes = 0;
	m_lbcTotalOccurrenceSum = 0;
	for (int i = m_minDomainValue; i <= m_maxDomainValue; ++i)
	{
		if (m_mins[i] > 0)
		{
			m_hasLowerBoundConstraint = true;
			m_lowerBoundConstrainedValues[i] = true;
			m_lbcTotalOccurrenceSum += m_mins[i];
		}

		if (m_maxs[i] < m_upperBoundVariables.size())
		{
			m_hasUpperBoundConstraint = true;
			m_upperBoundConstrainedValues[i] = true;

			m_numValueNodes += m_maxs[i];
		}
		else
		{
			m_numValueNodes += 1;
		}

		m_trimmedMins.push_back(m_mins[i]);
		m_trimmedMaxs.push_back(m_maxs[i]);
	}

	if (m_upperBoundVariables.size() == 0)
	{
		m_hasUpperBoundConstraint = false;
	}

	if (m_lowerBoundVariables.size() == 0)
	{
		m_hasLowerBoundConstraint = false;
	}

	m_backtrackStack.push_back({0, 0, {}, 0});

	if (m_hasUpperBoundConstraint)
	{
		m_matchingGraph.initialize(m_upperBoundVariables.size(), (m_maxDomainValue - m_minDomainValue) + 1, &m_trimmedMaxs);
		m_sccToNode.resize(m_upperBoundVariables.size());
		m_nodeToScc.resize(m_upperBoundVariables.size());
		m_variableNodeToMatchedNode.resize(m_upperBoundVariables.size());
		m_valueNodeToMatchedNode.resize(m_numValueNodes);

		// In the implicit flow graph, each value node with a max constraint is repeated MaxOccurrence times.
		// Values without a max constraint only occur once.
		// Create a mapping from value->first node index, and a mapping from node index->value.
		m_nodeIndexToActualValue.reserve(m_numValueNodes);
		m_trimmedValueToNodeIndex.reserve((m_maxDomainValue - m_minDomainValue) + 1);
		for (int i = m_minDomainValue; i <= m_maxDomainValue; ++i)
		{
			m_trimmedValueToNodeIndex.push_back(m_upperBoundVariables.size() + m_nodeIndexToActualValue.size());
			if (m_maxs[i] < m_upperBoundVariables.size())
			{
				for (int j = 0; j < m_maxs[i]; ++j)
				{
					m_nodeIndexToActualValue.push_back(i);
				}
			}
			else
			{
				m_nodeIndexToActualValue.push_back(i);
			}
		}

		// Initialize SCC structures, initially putting everything in the same SCC.
		// Also add all variables to the process list for initial propagation.
		for (int i = 0; i < m_sccToNode.size(); ++i)
		{
			m_sccToNode[i] = i;
			m_nodeToScc[i] = i;
			m_upperBoundProcessList.push_back(i);
		}

		m_varIndicesInOldScc.reserve(m_upperBoundVariables.size());

		m_upperBoundExplainer.initialize(*db, m_upperBoundVariables, m_minDomainValue, m_maxDomainValue, m_trimmedMaxs);
	}

	if (isUpperBoundFullySatisfied(db) && isLowerBoundFullySatisfied(db))
	{
		db->markConstraintFullySatisfied(this);
		m_fullySatisfiedLevel = db->getDecisionLevel();
	}

	return propagate(db);
}

void CardinalityConstraint::reset(IVariableDatabase* db)
{
	for (auto it = m_watcherHandles.begin(), itEnd = m_watcherHandles.end(); it != itEnd; ++it)
	{
		db->removeVariableWatch(it->first, it->second, this);
	}
	m_watcherHandles.clear();
}

void CardinalityConstraint::backtrack(const IVariableDatabase* db, SolverDecisionLevel level)
{
	m_failedUpperBoundMatching = false;
	m_failedLowerBoundMatching = false;

	if (m_fullySatisfiedLevel > level)
	{
		m_fullySatisfiedLevel = -1;
	}

	while (m_backtrackStack.back().level > level)
	{
		m_sccSplits.backtrack(m_backtrackStack.back().sccSplitcount);
		for (int index : m_backtrackStack.back().upperBoundProcessList)
		{
			if (!contains(m_upperBoundProcessList.begin(), m_upperBoundProcessList.end(), index))
			{
				m_upperBoundProcessList.push_back(index);
			}
		}
		m_numUpperBoundVarsOutsideUBC = m_backtrackStack.back().numUBCVariablesRemoved;
		m_numUnitSCCs = m_backtrackStack.back().numUnitSCCs;
		m_backtrackStack.pop_back();
	}

	int c = 0;
	for (auto v : m_upperBoundVariables)
	{
		if (!db->anyPossible(v, m_upperBoundConstrainedValues))
		{
			++c;
		}
	}
	vxy_assert(c == m_numUpperBoundVarsOutsideUBC);
}

CardinalityConstraint::BacktrackInfo& CardinalityConstraint::backtrackRecord(SolverDecisionLevel level)
{
	if (m_backtrackStack.back().level != level)
	{
		vxy_assert(m_backtrackStack.back().level < level);
		m_backtrackStack.push_back({level, m_sccSplits.size(), m_upperBoundProcessList, m_numUpperBoundVarsOutsideUBC, m_numUnitSCCs});
	}
	return m_backtrackStack.back();
}

bool CardinalityConstraint::onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& prevValues, bool&)
{
	if (m_fullySatisfiedLevel >= 0)
	{
		vxy_sanity(db->getDecisionLevel() >= m_fullySatisfiedLevel);
		return true;
	}
	else if (isUpperBoundFullySatisfied(db) && isLowerBoundFullySatisfied(db))
	{
		db->markConstraintFullySatisfied(this);
		m_fullySatisfiedLevel = db->getDecisionLevel();
		return true;
	}

	bool needUpperPropagation = false;
	bool needLowerPropagation = false;

	// We only care if this has any potential values that are constrained
	if (m_hasUpperBoundConstraint)
	{
		if (db->anyPossible(variable, m_upperBoundConstrainedValues))
		{
			needUpperPropagation = true;
		}
		else if (prevValues.anyPossible(m_upperBoundConstrainedValues))
		{
			backtrackRecord(db->getDecisionLevel());
			vxy_sanity(contains(m_upperBoundVariables.begin(), m_upperBoundVariables.end(), variable));
			m_numUpperBoundVarsOutsideUBC++;
			vxy_assert(m_numUpperBoundVarsOutsideUBC <= m_upperBoundVariables.size());
		}
	}

	// We only care if any of the removed bits are constrained
	if (m_hasLowerBoundConstraint)
	{
		const ValueSet& curValues = db->getPotentialValues(variable);
		ValueSet removedBits = prevValues.xoring(curValues);

		if (removedBits.anyPossible(m_lowerBoundConstrainedValues))
		{
			needLowerPropagation = true;
		}
	}

	if (needUpperPropagation)
	{
		const int varIdx = indexOf(m_upperBoundVariables.begin(), m_upperBoundVariables.end(), variable);
		vxy_assert(varIdx >= 0);
		if (!contains(m_upperBoundProcessList.begin(), m_upperBoundProcessList.end(), varIdx))
		{
			m_upperBoundProcessList.push_back(varIdx);
		}
	}

	if (needUpperPropagation || needLowerPropagation)
	{
		db->queueConstraintPropagation(this);
	}

	return true;
}

bool CardinalityConstraint::propagate(IVariableDatabase* db)
{
	bool success = true;
	if (m_hasUpperBoundConstraint && !m_upperBoundProcessList.empty())
	{
		success = processUpperboundConstraint(db);
	}

	if (success && m_hasLowerBoundConstraint)
	{
		success = processLowerboundConstraint(db);
	}

	m_upperBoundProcessList.clear();
	return success;
}

bool CardinalityConstraint::isUpperBoundFullySatisfied(IVariableDatabase* db) const
{
	vxy_assert(m_numUpperBoundVarsOutsideUBC+m_numUnitSCCs <= m_upperBoundVariables.size());
	return !m_hasUpperBoundConstraint || (m_numUpperBoundVarsOutsideUBC+m_numUnitSCCs) >= m_upperBoundVariables.size();
}

bool CardinalityConstraint::isLowerBoundFullySatisfied(IVariableDatabase* db) const
{
	if (m_hasLowerBoundConstraint)
	{
		int totalSolved = 0;
		vector<int> sumSolved;
		sumSolved.resize(m_maxDomainSize, 0);

		for (auto& varID : m_lowerBoundVariables)
		{
			auto& vals = db->getPotentialValues(varID);

			int solvedValue;
			if (vals.isSingleton(solvedValue))
			{
				sumSolved[solvedValue]++;
				++totalSolved;
			}
		}

		if (totalSolved < m_lbcTotalOccurrenceSum)
		{
			return false;
		}

		for (int i = 0; i < sumSolved.size(); ++i)
		{
			if (sumSolved[i] < m_mins[i])
			{
				return false;
			}
		}
	}
	return true;
}

bool CardinalityConstraint::getMaximalMatching(IVariableDatabase* db)
{
	for (int varIndex : m_upperBoundProcessList)
	{
		m_matchingGraph.removeEdges(varIndex);

		vxy_assert(!db->isInContradiction(m_upperBoundVariables[varIndex]));
		const ValueSet& values = db->getPotentialValues(m_upperBoundVariables[varIndex]);
		m_matchingGraph.reserveEdges(varIndex, (m_maxDomainValue - m_minDomainValue) + 1);
		for (auto it = values.beginSetBits(), itEnd = values.endSetBits(); it != itEnd; ++it)
		{
			m_matchingGraph.addEdge(varIndex, (*it) - m_minDomainValue);
		}
	}

	#if MATCHING_SANITY_CHECK
	for (int i = 0; i < m_upperBoundVariables.size(); ++i)
	{
		auto& potentialVals = db->getPotentialValues(m_upperBoundVariables[i]);
		for (auto it = potentialVals.beginSetBits(), itEnd = potentialVals.endSetBits(); it != itEnd; ++it)
		{
			vxy_assert(m_matchingGraph.hasBipartiteEdge(i, (*it)-m_minDomainValue));
		}
	}
	#endif

	return m_matchingGraph.incrementalMaximalMatching();
}

//
// For an explanation of how upper bound consistency is propagated see:
// See "Generalised Arc Consistency for the AllDifferent Constraint: An Empirical Study", Gent et. al.
// https://www-users.cs.york.ac.uk/pwn503/gac-alldifferent.pdf
// Note that this has been adapted for use in the cardinality constraint.
//
bool CardinalityConstraint::processUpperboundConstraint(IVariableDatabase* db)
{
	vxy_assert(m_upperBoundProcessList.size() > 0);

	#if MATCHING_SANITY_CHECK
	for (int i = 0; i < m_sccToNode.size(); ++i)
	{
		vxy_assert(indexOf(m_sccToNode.rbegin(), m_sccToNode.rend(), m_sccToNode[i]) == m_sccToNode.size()-i-1);
	}
	#endif

	//
	// Grab the maximal matching between variables and values
	//

	if (m_backtrackStack.back().level != db->getDecisionLevel())
	{
		backtrackRecord(db->getDecisionLevel());
	}
	else
	{
		for (int varIndex : m_upperBoundProcessList)
		{
			auto& list = m_backtrackStack.back().upperBoundProcessList;
			if (!contains(list.begin(), list.end(), varIndex))
			{
				m_backtrackStack.back().upperBoundProcessList.push_back(varIndex);
			}
		}
	}

	if (!getMaximalMatching(db))
	{
		// We could not match all variables with a value, so we can't satisfy.
		m_failedUpperBoundMatching = true;
		return false;
	}

	//
	// Get number of times each value is used in the matching, and set the index of the corresponding node
	// in the flow graph for the matched value of a variable.
	//

	m_valueToSumInMatching.clear();
	m_valueToSumInMatching.resize(m_trimmedMaxs.size(), 0);

	m_valueNodeToMatchedNode.clear();
	m_valueNodeToMatchedNode.resize(m_numValueNodes, -1);

	for (int varIndex = 0; varIndex < m_upperBoundVariables.size(); ++varIndex)
	{
		int matchingValue = m_matchingGraph.getMatchedRightSide(varIndex);
		vxy_assert(matchingValue >= 0);

		int variableNode = variableIndexToNodeIndex(varIndex);
		int valueNode = valueToFirstValueNode(matchingValue + m_minDomainValue);
		if (m_trimmedMaxs[matchingValue] < m_upperBoundVariables.size())
		{
			valueNode += m_valueToSumInMatching[matchingValue];
		}
		m_variableNodeToMatchedNode[variableNode] = valueNode;
		m_valueNodeToMatchedNode[valueNode - m_upperBoundVariables.size()] = variableNode;

		m_valueToSumInMatching[matchingValue]++;
		vxy_assert(m_valueToSumInMatching[matchingValue] <= m_trimmedMaxs[matchingValue]);
	}

	//
	// For each variable, grab the SCC that it belongs to. These SCCs will have to be revisited/potentially rebuilt.
	//

	fixed_vector<int, 8> changedSCCs;
	if (m_sccSplits.size() != 0)
	{
		for (int varIndex : m_upperBoundProcessList)
		{
			int scc = m_nodeToScc[varIndex];

			// search backward to find the first element in this SCC: either beginning of the list, or a split.
			int sccStart = scc;
			while (sccStart > 0 && !m_sccSplits.contains(sccStart - 1))
			{
				--sccStart;
			}

			// Ignore one-element SCCs
			if (!m_sccSplits.contains(sccStart))
			{
				if (!contains(changedSCCs.begin(), changedSCCs.end(), sccStart))
				{
					changedSCCs.push_back(sccStart);
				}
			}
		}
	}
	else
	{
		changedSCCs.push_back(0);
	}

	//
	// Process each dirty SCC. This might cause an SCC to split into multiple.
	//

	for (int scc : changedSCCs)
	{
		if (!processChangedSCC(db, scc))
		{
			return false;
		}
	}

	#if MATCHING_SANITY_CHECK
	for (int i = 0; i < m_sccToNode.size(); ++i)
	{
		vxy_assert(indexOf(m_sccToNode.rbegin(), m_sccToNode.rend(), m_sccToNode[i]) == m_sccToNode.size() - i - 1);
	}
	#endif

	return true;
}

bool CardinalityConstraint::processChangedSCC(IVariableDatabase* db, int scc)
{
	m_varIndicesInOldScc.clear();
	m_tarjanNextScc = scc;
	m_tarjanFoundSccSplit = false;
	m_tarjanRecurseFailure = false;

	//
	// Get all variables in the (potentially stale) SCC.
	//

	const int numVariables = m_upperBoundVariables.size();
	for (int i = scc; i < numVariables; ++i)
	{
		int nodeIndex = m_sccToNode[i];
		if (isVariableNode(nodeIndex))
		{
			m_varIndicesInOldScc.push_back(nodeIndex);
		}

		// If set, this marks the end of the SCC.
		if (m_sccSplits.contains(i))
		{
			break;
		}
	}

	if (m_varIndicesInOldScc.empty())
	{
		// SCC only contains values/sink, so no impact.
		return true;
	}

	//
	// Note whenever the Tarjan algorithm reaches a Variable or Value node. This is used to determine whether SCCs
	// need to be rebuilt.
	//
	auto reachedNode = [&](int level, int node)
	{
		if (level == 0)
		{
			m_tarjanVarsReached = 0;
			m_tarjanValsReached = 0;
		}

		if (isVariableNode(node))
		{
			++m_tarjanVarsReached;
		}
		else if (isValueNode(node))
		{
			++m_tarjanValsReached;
		}
	};

	// When an SCC is found, check if the graph is partitioned. If so, we need to rebuild all later SCCs.
	auto foundScc = [&](int level, auto it)
	{
		if (level > 0 || m_tarjanVarsReached < m_upperBoundVariables.size() || m_tarjanValsReached < m_numValueNodes)
		{
			m_tarjanFoundSccSplit = true;
		}

		if (m_tarjanFoundSccSplit)
		{
			// Strongly-connected component found; mark it and find all variables in it.
			TValueBitset<EASTLAllocatorType, 8> sccVarIndices;
			sccVarIndices.pad(m_upperBoundVariables.size(), false);

			TValueBitset<EASTLAllocatorType, 8> foundValues;
			foundValues.pad(m_maxDomainSize, false);

			int numVariablesInSCC = 0;

			//
			// First loop: identify all members of the SCC, and write the new SCC set.
			//
			for (; it; ++it)
			{
				int sccMember = *it;
				if (isVariableNode(sccMember))
				{
					// Point to new SCC
					m_sccToNode[m_tarjanNextScc] = sccMember;
					m_nodeToScc[sccMember] = m_tarjanNextScc;
					m_tarjanNextScc++;

					sccVarIndices[variableNodeToVariableIndex(sccMember)] = true;
					numVariablesInSCC++;
				}
				else if (isValueNode(sccMember))
				{
					foundValues[valueNodeToValue(sccMember)] = true;
				}
			}

			// Found the total SCC. Mark the split.
			if (numVariablesInSCC > 0)
			{
				m_sccSplits.add(m_tarjanNextScc - 1);
				if (numVariablesInSCC == 1 && foundValues.isSubsetOf(m_upperBoundConstrainedValues))
				{
					m_numUnitSCCs++;
				}
			}

			auto explainer = [&](auto params)
			{
				ValueSet removedValues = params.database->getPotentialValues(params.propagatedVariable).excluding(params.propagatedValues);
				return m_upperBoundExplainer.getExplanation(*params.database, params.propagatedVariable, removedValues);
			};

			//
			// Second loop: For any values discovered in the SCC, remove them from all variables no longer in the SCC.
			//
			if (!m_tarjanRecurseFailure)
			{
				for (int varIndex : m_varIndicesInOldScc)
				{
					// skip if still in this SCC
					if (sccVarIndices[varIndex])
					{
						continue;
					}

					if (db->anyPossible(m_upperBoundVariables[varIndex], foundValues))
					{
						// Don't exclude if this is part of the matching.
						// This can happen if we split an SCC at a later recursion depth, and we hit a value node first.
						int matchedValue = valueNodeToValue(m_variableNodeToMatchedNode[varIndex]);
						bool prevBit = foundValues[matchedValue];
						foundValues[matchedValue] = false;

						// Can return false if this variable was narrowed but we haven't been notified yet.
						if (!db->excludeValues(m_upperBoundVariables[varIndex], foundValues, this, explainer))
						{
							// Just note the failure. We still need to process the remaining SCCs; otherwise the
							// SCCToNode/NodeToSCC tables will be corrupted.
							m_tarjanRecurseFailure = true;
							break;
						}

						foundValues[matchedValue] = prevBit;
					}
				}
			}
		}
	};

	const int numNodes = m_upperBoundVariables.size() + m_numValueNodes + 1;
	m_tarjan.findStronglyConnectedComponents(numNodes, m_varIndicesInOldScc,
		[&](int node, auto visitor) { tarjanVisit(db, node, visitor); },
		reachedNode,
		foundScc
	);

	return !m_tarjanRecurseFailure;
}

// Visit nodes Using an implicit graph based on the matching.
// See Algorithm 2 in https://www-users.cs.york.ac.uk/pwn503/gac-alldifferent.pdf for more details.
template <typename T>
void CardinalityConstraint::tarjanVisit(IVariableDatabase* db, int node, T&& visitor)
{
	if (isSinkNode(node))
	{
		//
		// Sink: edge to each value that are below maximum usage in the matching
		//
		for (int value = m_minDomainValue; value <= m_maxDomainValue; ++value)
		{
			int relValue = value - m_minDomainValue;
			int maxCapacity = m_trimmedMaxs[relValue];
			if (maxCapacity < m_upperBoundVariables.size())
			{
				if (m_valueToSumInMatching[relValue] < maxCapacity)
				{
					// The first copies of the value node are reserved for those in the matching.
					// The remainder are unmatched.
					const int firstValueNode = valueToFirstValueNode(value);
					for (int dest = firstValueNode + m_valueToSumInMatching[relValue]; dest < firstValueNode + maxCapacity; ++dest)
					{
						vxy_assert(valueNodeToValue(dest) == value);
						visitor(dest);
					}
				}
			}
			else
			{
				// This is an unconstrained value, so is always considered unmatched.
				// Essentially, treat it as if there are infinite nodes for this value.
				vxy_assert(m_valueToSumInMatching[relValue] < maxCapacity);
				visitor(valueToFirstValueNode(value));
			}
		}
	}
	else if (isVariableNode(node))
	{
		//
		// Variable node: Go through the value in the matching
		//

		int destinationNode = m_variableNodeToMatchedNode[variableNodeToVariableIndex(node)];
		visitor(destinationNode);
	}
	else
	{
		//
		// Value node: Go through Value->Variable edges NOT in matching.
		// Also, if the value node is in the matching, it flows into the sink.
		//

		const int matchedVariableNode = m_valueNodeToMatchedNode[node - m_upperBoundVariables.size()];

		int value = valueNodeToValue(node);
		for (int varIndex : m_varIndicesInOldScc)
		{
			int varNode = variableIndexToNodeIndex(varIndex);

			// If this is a constrained value, we visit all variable nodes that don't match to exactly this node.
			// For unconstrained values, we treat it as if there infinite nodes for that value, so it would
			// never be in the matching.
			if (matchedVariableNode != varNode || m_trimmedMaxs[value - m_minDomainValue] >= m_upperBoundVariables.size())
			{
				if (db->isPossible(m_upperBoundVariables[varIndex], value))
				{
					visitor(variableIndexToNodeIndex(varIndex));
				}
			}
		}

		// Edges go from values to the sink if the value appears in the matching.
		if (matchedVariableNode >= 0)
		{
			visitor(m_upperBoundVariables.size() + m_numValueNodes);
		}
	}
}

//
// NOTE: lower bound constraint is (currently) only bounds-consistent.
// See "An Efficient Bounds Consistency Algorithm for the Global Cardinality Constraint", Quimper et. al.
// https://cs.uwaterloo.ca/~vanbeek/Publications/cp03.pdf
//
// In particular, see Algorithm 1 in the paper.
//
bool CardinalityConstraint::processLowerboundConstraint(IVariableDatabase* db)
{
	// Grab the min/max value for each variable.
	m_bounds.clear();
	m_bounds.reserve(m_lowerBoundVariables.size());

	m_lbcVars.clear();

	for (int i = 0; i < m_lowerBoundVariables.size(); ++i)
	{
		if (db->anyPossible(m_lowerBoundVariables[i], m_lowerBoundConstrainedValues))
		{
			int minValue = db->getMinimumPossibleValue(m_lowerBoundVariables[i]);
			int maxValue = db->getMaximumPossibleValue(m_lowerBoundVariables[i]);
			m_bounds.emplace_back(minValue, maxValue, m_lbcVars.size());
			m_lbcVars.push_back(m_lowerBoundVariables[i]);
		}
	}

	if (!lbcLow(db, m_bounds))
	{
		return false;
	}
	if (!lbcHi(db, m_bounds))
	{
		return false;
	}
	return true;
}

bool CardinalityConstraint::lbcLow(IVariableDatabase* db, vector<Interval>& intervals)
{
	m_bucketCapacities.clear();
	m_bucketCapacities.reserve(m_maxDomainSize + 2);

	m_bucketCapacities.push_back(intervals.size() + 1);
	m_bucketCapacities.insert(m_bucketCapacities.end(), m_mins.begin(), m_mins.end());
	while (m_bucketCapacities.size() < m_maxDomainSize + 1)
	{
		m_bucketCapacities.push_back(0);
	}
	m_bucketCapacities.push_back(intervals.size() + 1);

	m_lbcPotentials.reset(m_maxDomainSize);
	m_lbcStable.reset(m_maxDomainSize);

	m_lbcFailures.clear();
	m_lbcFailures.pad(m_maxDomainSize, true);

	for (int i = 0; i < m_mins.size(); ++i)
	{
		if (m_mins[i] <= 0)
		{
			m_lbcFailures[i] = false;
		}
	}

	quick_sort(intervals.begin(), intervals.end(), [](auto& a, auto& b)
	{
		if (a.maxValue < b.maxValue)
		{
			return true;
		}
		else if (a.maxValue > b.maxValue)
		{
			return false;
		}
		return a.key < b.key;
	});

	m_lbcBoundaries.resize(intervals.size());

	for (int i = 0; i < intervals.size(); ++i)
	{
		int a = intervals[i].minValue;
		int b = intervals[i].maxValue;
		int z = a;
		for (; m_bucketCapacities[z + 1] == 0; ++z)
		{
		}

		if (z > a)
		{
			int aset = m_lbcPotentials.find(a);
			for (int u = a + 1; u <= min(b, z); ++u)
			{
				m_lbcPotentials.makeUnion(aset, u);
			}
		}

		if (z > b)
		{
			int sset = m_lbcPotentials.find(b);
			for (int k = sset + 1; k < m_lbcPotentials.size() && m_lbcPotentials.find(k) == sset; ++k)
			{
				m_lbcStable.makeUnion(sset, k);
			}
		}
		else
		{
			m_bucketCapacities[z + 1]--;
			z = a;
			for (; m_bucketCapacities[z + 1] == 0; ++z)
			{
			}

			int min = a;
			for (; !m_lbcFailures[min]; ++min)
			{
			}

			m_lbcBoundaries[i] = min;

			if (z > b)
			{
				int j = b;
				for (; j >= 0 && m_bucketCapacities[j + 1] == 0; --j)
				{
				}
				for (int k = j + 1; k <= b; ++k)
				{
					m_lbcFailures[k] = false;
				}
			}
		}
	}

	if (m_lbcFailures.contains(true))
	{
		m_failedLowerBoundMatching = true;
		return false;
	}

	auto explainer = [&](auto params) { return explainLowerBoundPropagation(params); };

	for (int i = 0; i < intervals.size(); ++i)
	{
		int a = intervals[i].minValue;
		int b = intervals[i].maxValue;

		if (m_lbcStable.find(a) != m_lbcStable.find(b))
		{
			if (!db->excludeValuesLessThan(m_lbcVars[intervals[i].key], m_lbcBoundaries[i], this, explainer))
			{
				return false;
			}
		}
	}
	return true;
}

bool CardinalityConstraint::lbcHi(IVariableDatabase* db, vector<Interval>& intervals)
{
	m_bucketCapacities.clear();
	m_bucketCapacities.reserve(m_maxDomainSize + 2);

	m_bucketCapacities.push_back(intervals.size() + 1);
	m_bucketCapacities.insert(m_bucketCapacities.end(), m_mins.begin(), m_mins.end());
	while (m_bucketCapacities.size() < m_maxDomainSize + 1)
	{
		m_bucketCapacities.push_back(0);
	}
	m_bucketCapacities.push_back(intervals.size() + 1);

	m_lbcPotentials.reset(m_maxDomainSize);
	m_lbcStable.reset(m_maxDomainSize);

	m_lbcFailures.clear();
	m_lbcFailures.pad(m_maxDomainSize, true);

	for (int i = 0; i < m_mins.size(); ++i)
	{
		if (m_mins[i] <= 0)
		{
			m_lbcFailures[i] = false;
		}
	}

	quick_sort(intervals.begin(), intervals.end(), [](auto& a, auto& b)
	{
		if (a.minValue > b.minValue)
		{
			return true;
		}
		else if (a.minValue < b.minValue)
		{
			return false;
		}
		return a.key < b.key;
	});

	m_lbcBoundaries.resize(intervals.size());

	for (int i = 0; i < intervals.size(); ++i)
	{
		int a = intervals[i].minValue;
		int b = intervals[i].maxValue;
		int z = b;
		for (; m_bucketCapacities[z + 1] == 0; --z)
		{
		}

		if (z < b)
		{
			int bset = m_lbcPotentials.find(b);
			for (int u = b - 1; u >= max(a, z); --u)
			{
				m_lbcPotentials.makeUnion(bset, u);
			}
		}

		if (z < a)
		{
			int sset = m_lbcPotentials.find(a);
			for (int k = sset + 1; k < m_lbcPotentials.size() && m_lbcPotentials.find(k) == sset; ++k)
			{
				m_lbcStable.makeUnion(sset, k);
			}
		}
		else
		{
			m_bucketCapacities[z + 1]--;
			z = b;
			for (; m_bucketCapacities[z + 1] == 0; --z)
			{
			}

			int max = b;
			for (; !m_lbcFailures[max]; --max)
			{
			}

			m_lbcBoundaries[i] = max;

			if (z < a)
			{
				int j = a;
				for (; m_bucketCapacities[j + 1] == 0; ++j)
				{
				}
				for (int k = a; k < j; ++k)
				{
					m_lbcFailures[k] = false;
				}
			}
		}
	}

	if (m_lbcFailures.contains(true))
	{
		m_failedLowerBoundMatching = true;
		return false;
	}

	auto explainer = [&](auto params) { return explainLowerBoundPropagation(params); };

	for (int i = 0; i < intervals.size(); ++i)
	{
		int a = intervals[i].minValue;
		int b = intervals[i].maxValue;

		if (m_lbcStable.find(a) != m_lbcStable.find(b))
		{
			if (!db->excludeValuesGreaterThan(m_lbcVars[intervals[i].key], m_lbcBoundaries[i], this, explainer))
			{
				return false;
			}
		}
	}

	return true;
}

vector<Literal> CardinalityConstraint::explainLowerBoundPropagation(const NarrowingExplanationParams& params) const
{
	// TODO: This seems like a reasonable explanation, but maybe there is a better one?

	//
	// Values were originally removed due to this variable needing to be one of the values that are lower-bound
	// constrained. The values with an LBC should be in the result of the propagation.
	//

	auto db = params.database;
	const ValueSet constrainedVals = m_lowerBoundConstrainedValues.intersecting(params.propagatedValues);
	vxy_assert(!constrainedVals.isZero());

	vector<Literal> out;
	out.push_back(Literal(params.propagatedVariable, constrainedVals));

	//
	// We would not have needed to propagate this if there were enough other variables that had the LBC values.
	// Add to the explanation all variables that *initially* supported the LBC values, but no longer do.
	//

	for (VarID var : m_lowerBoundVariables)
	{
		if (var == params.propagatedVariable)
		{
			continue;
		}
		else if (db->getInitialValues(var).anyPossible(constrainedVals) && !db->anyPossible(var, constrainedVals))
		{
			out.push_back(Literal(var, constrainedVals));
		}
	}

	return out;
}


bool CardinalityConstraint::checkConflicting(IVariableDatabase* db) const
{
	vector<int> numDefinite;
	vector<int> numPossible;
	numDefinite.resize(m_maxDomainSize, 0);
	numPossible.resize(m_maxDomainSize, 0);

	for (VarID var : m_allVariables)
	{
		const bool isSolved = db->isSolved(var);
		const ValueSet& values = db->getPotentialValues(var);
		for (auto it = values.beginSetBits(), itEnd = values.endSetBits(); it != itEnd; ++it)
		{
			if (isSolved)
			{
				numDefinite[*it]++;
			}
			numPossible[*it]++;
		}
	}

	for (int i = 0; i < m_maxDomainSize; ++i)
	{
		if (numPossible[i] < m_mins[i])
		{
			return true;
		}
		if (numDefinite[i] > m_maxs[i])
		{
			return true;
		}
	}

	return false;
}

vector<Literal> CardinalityConstraint::explain(const NarrowingExplanationParams& params) const
{
	vector<Literal> outClauses;
	if (m_failedUpperBoundMatching)
	{
		outClauses = m_upperBoundExplainer.getExplanation(*params.database, VarID::INVALID, {});
	}
	else
	{
		vxy_assert(m_failedLowerBoundMatching);

		// TODO: This seems like a reasonable explanation, but maybe there is a better one?
		//
		// Create a matching from variables to values, with each value connecting up to Min(Val) variables.
		// Then, find all values that had less than Min(Val) nodes in the matching.
		// Finally, report as an explanation all variables that could *initially* have had those values but have been
		// narrowed so they no longer do.
		//

		const int numValueNodes = (m_maxDomainValue - m_minDomainValue) + 1;
		BipartiteGraph graph(m_lowerBoundVariables.size(), numValueNodes, &m_trimmedMins);

		for (int i = 0; i < m_lowerBoundVariables.size(); ++i)
		{
			VarID var = m_lowerBoundVariables[i];
			for (int val = m_minDomainValue; val <= m_maxDomainValue; ++val)
			{
				if (m_trimmedMins[val - m_minDomainValue] > 0 && params.database->isPossible(var, val))
				{
					graph.addEdge(i, val - m_minDomainValue);
				}
			}
		}

		graph.computeMaximalMatching();

		ValueSet violatedVals(m_maxDomainSize, false);
		for (int val = m_minDomainValue; val <= m_maxDomainValue; ++val)
		{
			if (graph.getNumRightSideMatched(val - m_minDomainValue) < m_trimmedMins[val - m_minDomainValue])
			{
				violatedVals[val - m_minDomainValue] = true;
			}
		}
		vxy_assert(!violatedVals.isZero());

		for (VarID var : m_lowerBoundVariables)
		{
			auto& initialVals = params.database->getInitialValues(var);
			if (initialVals.anyPossible(violatedVals) && !params.database->anyPossible(var, violatedVals))
			{
				outClauses.push_back(Literal(var, initialVals.intersecting(violatedVals)));
			}
		}
		vxy_assert(!outClauses.empty());
	}
	return outClauses;
}
