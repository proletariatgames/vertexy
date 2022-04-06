// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/MaxOccurrenceExplainer.h"
#include "variable/IVariableDatabase.h"

using namespace csolver;

MaxOccurrenceExplainer::MaxOccurrenceExplainer()
{
}

void MaxOccurrenceExplainer::initialize(const IVariableDatabase& db, const vector<VarID>& variables, int minDomainValue, int maxDomainValue, const vector<int>& maxOccurrencesByValue, bool useBoundsConsistency)
{
	m_minDomainValue = minDomainValue;
	m_maxDomainValue = maxDomainValue;
	m_variables = variables;
	m_workingVariables = variables;
	m_maxs = maxOccurrencesByValue;
	m_useBoundsConsistency = useBoundsConsistency;

	m_domainSize = 0;
	for (int i = 0; i < m_variables.size(); ++i)
	{
		VarID var = m_variables[i];
		m_domainSize = max(m_domainSize, db.getDomainSize(var));
	}

	int trimmedDomainSize = (m_maxDomainValue-m_minDomainValue)+1;
	cs_sanity(trimmedDomainSize > 0);
	cs_sanity(trimmedDomainSize == maxOccurrencesByValue.size());

	// In the implicit flow graph, each value node with a max constraint is repeated MaxOccurrence times.
	// Values without a max constraint only occur once.
	// Create a mapping from value->first node index, and a mapping from node index->value.

	m_numValueNodes = 0;
	for (int i = 0; i < trimmedDomainSize; ++i)
	{
		if (m_maxs[i] < m_variables.size())
		{
			m_numValueNodes += m_maxs[i];
		}
		else
		{
			m_numValueNodes += 1;
		}
	}

	m_constrainedValues.init(m_domainSize, false);
	m_nodeIndexToActualValue.reserve(m_numValueNodes);
	m_trimmedValueToNodeIndex.reserve((m_maxDomainValue - m_minDomainValue) + 1);
	for (int i = m_minDomainValue; i <= m_maxDomainValue; ++i)
	{
		int trimmedIndex = i-m_minDomainValue;
		m_trimmedValueToNodeIndex.push_back(m_nodeIndexToActualValue.size());
		if (m_maxs[trimmedIndex] < m_variables.size())
		{
			m_constrainedValues[i] = true;
			for (int j = 0; j < m_maxs[trimmedIndex]; ++j)
			{
				m_nodeIndexToActualValue.push_back(i);
			}
		}
		else
		{
			m_nodeIndexToActualValue.push_back(i);
		}
	}

	m_hasUnconstrainedValues = m_constrainedValues.indexOf(false) >= 0;
}

vector<Literal> MaxOccurrenceExplainer::getExplanation(const IVariableDatabase& db, VarID variableToExplain, const ValueSet& removedValuesToExplain)
{
	int indexOfVariableToExplain = -1;

	if (m_hasUnconstrainedValues)
	{
		// Get the set of variables that are possibly contributing. We can ignore any that don't have any constrained values
		m_workingVariables.clear();
		for (int i = 0; i < m_variables.size(); ++i)
		{
			if (db.anyPossible(m_variables[i], m_constrainedValues))
			{
				if (m_variables[i] == variableToExplain)
				{
					indexOfVariableToExplain = m_workingVariables.size();
				}
				m_workingVariables.push_back(m_variables[i]);
			}
		}
	}
	else if (variableToExplain.isValid())
	{
		indexOfVariableToExplain = indexOf(m_workingVariables.begin(), m_workingVariables.end(), variableToExplain);
	}

	cs_assert(!variableToExplain.isValid() || indexOfVariableToExplain >= 0);

	const int numVariables = m_workingVariables.size();
	const int numPotentialValues = (m_maxDomainValue - m_minDomainValue) + 1;

	//
	// Create bipartite graph with edges between each variable and its potential values.
	// Solve for maximal Matching in graph: the set of as many edges as possible where no two edges share an endpoint.
	//

	m_graph.initialize(numVariables, numPotentialValues, &m_maxs);
	for (int varIndex = 0; varIndex < numVariables; ++varIndex)
	{
		cs_assert(!db.isInContradiction(m_workingVariables[varIndex]));
		if (!m_useBoundsConsistency)
		{
			// full consistency: add edges only for potential values
			const ValueSet& values = db.getPotentialValues(m_workingVariables[varIndex]);
			for (auto it = values.beginSetBits(), itEnd = values.endSetBits(); it != itEnd; ++it)
			{
				m_graph.addEdge(varIndex, (*it) - m_minDomainValue);
			}
		}
		else
		{
			// bounds consistency only, so add all edges between min/max potential values
			int boundMin = db.getMinimumPossibleValue(m_workingVariables[varIndex]);
			int boundMax = db.getMaximumPossibleValue(m_workingVariables[varIndex]);
			for (int i = boundMin; i <= boundMax; ++i)
			{
				m_graph.addEdge(varIndex, i - m_minDomainValue);
			}
		}
	}
	m_graph.computeMaximalMatching(/*matchLast=*/indexOfVariableToExplain);

	//
	// Create a new graph, where edges in the Matching point in direction Variable->Value, and unmatched edges
	// point Value->Variable.
	//

	const int numNodes = numVariables + m_numValueNodes;

	m_numMatchedPerValue.clear();
	m_numMatchedPerValue.resize(numPotentialValues, 0);

	m_variablesMatched.init(numVariables, false);

	m_inMatchingSet.clear();
	m_inMatchingSet.resize(numNodes, false);

	m_variableToMatchedNode.resize(numVariables);

	m_valueNodeToMatchedNode.clear();
	m_valueNodeToMatchedNode.resize(m_numValueNodes, -1);

	// First set up the Variable -> Value edges for pairs in matching
	for (int varIndex = 0; varIndex < numVariables; ++varIndex)
	{
		const int matchedSide = m_graph.getMatchedRightSide(varIndex);
		if (matchedSide < 0)
		{
			m_variableToMatchedNode[varIndex] = -1;
			continue;
		}

		const int matchedValue = matchedSide + m_minDomainValue;
		const int varNode = variableIndexToNodeIndex(varIndex);

		int valueNode = valueToFirstValueNode(matchedValue);
		if (m_maxs[matchedSide] < numVariables)
		{
			valueNode += m_numMatchedPerValue[matchedSide];
		}

		m_variableToMatchedNode[varIndex] = valueNode;
		m_valueNodeToMatchedNode[valueNode - numVariables] = varNode;

		m_numMatchedPerValue[matchedSide]++;
		cs_assert(m_numMatchedPerValue[matchedSide] <= m_maxs[matchedSide]);

		m_inMatchingSet[varNode] = true;
		m_inMatchingSet[valueNode] = true;

		m_variablesMatched[varIndex] = true;
	}

	// We may not have a specific variable to explain if no matching was possible when propagation occurred.
	// In this case, there should be at least one unmatched variable here, so use that as the variable to explain.
	if (!variableToExplain.isValid())
	{
		indexOfVariableToExplain = m_variablesMatched.indexOf(false);
		cs_sanity(indexOfVariableToExplain >= 0);
		variableToExplain = m_workingVariables[indexOfVariableToExplain];
	}

	//
	// The visit function for the implicit graph.
	//
	auto visitFunction = [&](int node, const function<void(int)>& visitor)
	{
		if (isVariableNode(node))
		{
			// Edge from variable to its matched value
			int varIndex = variableNodeToVariableIndex(node);
			if (m_variableToMatchedNode[varIndex] >= 0)
			{
				visitor(m_variableToMatchedNode[varIndex]);
			}
		}
		else
		{
			// Edge from value to each unmatched variable
			cs_assert(isValueNode(node));
			const int matchedVariableNode = m_valueNodeToMatchedNode[node - numVariables];
			const int value = valueNodeToValue(node);
			const int trimmedValue = value - m_minDomainValue;
			for (int varIndex = 0; varIndex < numVariables; ++varIndex)
			{
				int varNode = variableIndexToNodeIndex(varIndex);

				// If this is a constrained value, we visit all variable nodes that don't match to exactly this node.
				// For unconstrained values, we treat it as if there infinite nodes for that value, so it would
				// never be in the matching.
				if (matchedVariableNode != varNode || m_maxs[trimmedValue] >= numVariables)
				{
					if (db.isPossible(m_workingVariables[varIndex], value))
					{
						visitor(varNode);
					}
				}
			}
		}
	};

	//
	// Grab the strongly-connected components (SCCs), then
	// find all nodes reachable from the VariableToExplain's node as well
	// as the nodes reachable from nodes representing a value removed from VariableToExplain.
	// Ignore any nodes that are reachable from nodes not in the Matching set, as well
	// as any nodes in the same SCC as the explaining variable.
	//

	m_nodeToScc.clear();
	m_tarjan.findStronglyConnectedComponents(numNodes, visitFunction, m_nodeToScc);

	//
	// Mark all nodes that are reachable from a free node (i.e. value not contained in matching set) as visited,
	// since these values do not affect propagation.
	//
	// Note that unconstrained values (Max > NumVars) are always treated as a free node.
	//

	m_visited.clear();
	m_visited.resize(numNodes, 0);

	for (NodeIndex i = numVariables; i < numNodes; ++i)
	{
		int trimmedValue = valueNodeToValue(i) - m_minDomainValue;
		if (!m_visited[i] && (!m_inMatchingSet[i] || m_maxs[trimmedValue] >= numVariables))
		{
			recurseAndMarkVisited(i, visitFunction);
		}
	}

	vector<NodeIndex> nodeStack;
	vector<NodeIndex> explainingValueNodes;

	// Start search from the nodes that represent values removed that we're trying to explain.
	for (int value = m_minDomainValue; value <= m_maxDomainValue; ++value)
	{
		if (value < removedValuesToExplain.size() && removedValuesToExplain[value])
		{
			const int baseNode = valueToFirstValueNode(value);
			const int max = m_maxs[value - m_minDomainValue];
			if (max < numVariables)
			{
				for (int i = 0; i < max; ++i)
				{
					nodeStack.push_back(baseNode + i);
				}
			}
			else
			{
				nodeStack.push_back(baseNode);
			}
			explainingValueNodes.push_back(baseNode);
		}
	}

	// removedValuesToExplain might be empty (when propagation failed to find a matching).
	// In that case, start search from the first variable that is not in the matching
	if (nodeStack.empty())
	{
		cs_sanity(indexOfVariableToExplain >= 0);
		nodeStack.push_back(indexOfVariableToExplain);
	}

	vector<NodeIndex> explainingVariableNodes;
	explainingVariableNodes.push_back(indexOfVariableToExplain);
	const int explainedVariableScc = m_nodeToScc[explainingVariableNodes.back()];

	while (!nodeStack.empty())
	{
		NodeIndex node = nodeStack.back();
		nodeStack.pop_back();

		m_visited[node] = true;

		if (isVariableNode(node))
		{
			const int varIndex = variableNodeToVariableIndex(node);
			// recurse through values NOT in the matching.
			for (int value = m_minDomainValue; value <= m_maxDomainValue; ++value)
			{
				const int trimmedValue = value - m_minDomainValue;
				if (db.isPossible(m_workingVariables[varIndex], value))
				{
					int baseValueNode = valueToFirstValueNode(value);
					int maxValue = m_maxs[trimmedValue];
					if (maxValue < numVariables)
					{
						for (int i = 0; i < maxValue; ++i)
						{
							const int valueNode = baseValueNode + i;
							if (!m_visited[valueNode] &&
								m_nodeToScc[valueNode] != explainedVariableScc &&
								m_valueNodeToMatchedNode[valueNode - numVariables] != node)
							{
								nodeStack.push_back(valueNode);
								explainingValueNodes.push_back(valueNode);
							}
						}
					}
					else
					{
						if (!m_visited[baseValueNode] && m_nodeToScc[baseValueNode] != explainedVariableScc)
						{
							nodeStack.push_back(baseValueNode);
							explainingValueNodes.push_back(baseValueNode);
						}
					}
				}
			}
		}
		else
		{
			cs_assert(isValueNode(node));

			// recurse through the variable we matched with.
			int varNode = m_valueNodeToMatchedNode[node - numVariables];
			if (varNode >= 0 && !m_visited[varNode] && m_nodeToScc[varNode] != explainedVariableScc)
			{
				cs_assert(isVariableNode(varNode));
				nodeStack.push_back(varNode);
				explainingVariableNodes.push_back(varNode);
			}
		}
	}

	cs_assert(explainingVariableNodes.size() > 1);
	cs_sanity(contains(explainingVariableNodes.begin(), explainingVariableNodes.end(), indexOfVariableToExplain));

	//
	// Create the ValueSet for each literal in the explanation. Each value node reached during the previous
	// recursion should be excluded.
	// Note that there might be duplicate values here - that's fine/expected.
	//
	ValueSet explainingValues(m_domainSize, true);
	for (NodeIndex valueNode : explainingValueNodes)
	{
		const int value = valueNodeToValue(valueNode);
		explainingValues[value] = false;
	}

	// Create the literals for the explanation: those we reached during the previous recursion
	// (including variableToExplain)
	vector<Literal> explanation;
	for (NodeIndex varNode : explainingVariableNodes)
	{
		int varIndex = variableNodeToVariableIndex(varNode);
		explanation.push_back(Literal(m_workingVariables[varIndex], explainingValues));
	}
	return explanation;
}


void MaxOccurrenceExplainer::recurseAndMarkVisited(NodeIndex nodeIndex, TarjanAlgorithm::AdjacentCallback callback)
{
	m_visited[nodeIndex] = true;
	callback(nodeIndex, [&](int dest)
	{
		if (!m_visited[dest])
		{
			recurseAndMarkVisited(dest, callback);
		}
	});
}
