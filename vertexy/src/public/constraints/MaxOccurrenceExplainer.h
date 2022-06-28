// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "topology/BipartiteGraph.h"
#include "topology/algo/tarjan.h"

namespace Vertexy
{

// Utility class for explaining propagation of upper-bound limits on number of occurrences of values in a set of variables.
// Used by both AllDifferent and Cardinality constraints.
class MaxOccurrenceExplainer
{
public:
	MaxOccurrenceExplainer();

	void initialize(const IVariableDatabase& db, const vector<VarID>& variables, int minDomainValue, int maxDomainValue, const vector<int>& maxOccurrencesByValue, bool useBoundsConsistency=false);
	void getExplanation(const IVariableDatabase& db, VarID variableToExplain, const ValueSet& removedValuesToExplain, vector<Literal>& outExplanation);

protected:
	using NodeIndex = int;

	template<typename T>
	void recurseAndMarkVisited(NodeIndex nodeIndex, T&& callback);

	vector<VarID> m_variables;
	int m_numValueNodes = -1;
	int m_domainSize = -1;
	int m_minDomainValue = -1;
	int m_maxDomainValue = -1;
	bool m_useBoundsConsistency = false;
	bool m_hasUnconstrainedValues = false;

	TarjanAlgorithm m_tarjan;
	BipartiteGraph m_graph;

	vector<int> m_numMatchedPerValue;
	vector<int> m_variableToMatchedNode;
	vector<int> m_valueNodeToMatchedNode;

	vector<bool> m_inMatchingSet;
	ValueSet m_variablesMatched;
	vector<bool> m_visited;
	vector<NodeIndex> m_nodeToScc;

	vector<int> m_maxs;
	vector<int> m_trimmedValueToNodeIndex;
	vector<int> m_nodeIndexToActualValue;

	vector<VarID> m_workingVariables;
	ValueSet m_constrainedValues;

	// working data:
	vector<NodeIndex> m_nodeStack;
	vector<NodeIndex> m_explainingValueNodes;

	inline int variableIndexToNodeIndex(int n) const { return n; }
	inline int variableNodeToVariableIndex(int n) const { return n; }
	inline bool isVariableNode(int n) const { return n < m_workingVariables.size(); }
	inline bool isValueNode(int n) const { return n >= m_workingVariables.size(); }

	inline int valueToFirstValueNode(int val) const
	{
		return m_trimmedValueToNodeIndex[val - m_minDomainValue] + m_workingVariables.size();
	}

	inline int valueNodeToValue(int n) const
	{
		return m_nodeIndexToActualValue[n - m_workingVariables.size()];
	}
};

} // namespace Vertexy