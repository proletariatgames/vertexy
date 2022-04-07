// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include <EASTL/functional.h>

namespace Vertexy
{
//
// Implementation of Tarjan's algorithm for Strongly-Connected Components (SCCs).
// See https://en.wikipedia.org/wiki/Tarjan%27s_strongly_connected_components_algorithm
//
class TarjanAlgorithm
{
	struct TarjanNodeInfo
	{
		TarjanNodeInfo()
			: visitOrder(-1)
			, lowLink(-1)
			, onStack(false)
		{
		}

		int visitOrder;
		int lowLink;
		bool onStack;
	};

	mutable vector<TarjanNodeInfo> m_nodeInfos;
	mutable vector<int> m_stack;

	struct SCCIterator
	{
		SCCIterator(const TarjanAlgorithm& algo, int nodeIndex)
			: m_algo(algo)
			, m_nodeIndex(nodeIndex)
			, m_hitEnd(false)
		{
			m_last = algo.m_stack.back();
			algo.m_stack.pop_back();
			algo.m_nodeInfos[m_last].onStack = false;
		}

		inline int operator*() const
		{
			return m_last;
		}

		inline int representative() const { return m_nodeIndex; }

		SCCIterator& operator++()
		{
			vxy_assert(!m_hitEnd);
			if (m_last == m_nodeIndex)
			{
				m_hitEnd = true;
			}
			else
			{
				m_last = m_algo.m_stack.back();
				m_algo.m_stack.pop_back();

				m_algo.m_nodeInfos[m_last].onStack = false;
			}
			return *this;
		}

		inline operator bool() const { return !m_hitEnd; }

	protected:
		const TarjanAlgorithm& m_algo;
		int m_nodeIndex;
		bool m_hitEnd;
		int m_last;
	};

public:
	TarjanAlgorithm()
	{
	}

	using AdjacentCallback = const function<void(int /*NodeIndex*/, const function<void(int)>&) /*Recurse*/>&;
	using SCCFoundCallback = const function<void(int /*Level*/, SCCIterator&)>;
	using ReachedCallback = const function<void(int /*Level*/, int /*Node*/)>;

	// Input is a set of nodes, where each node has a list of indices of nodes it connects to.
	// The output is a list where each element corresponds to the input node at the same index,
	// and the value identifies which strongly-connected component (SCC) the node belongs to.
	template <typename T>
	inline void findStronglyConnectedComponents(int numNodes, T&& adjCallback, vector<int>& output) const
	{
		output.clear();
		output.resize(numNodes);

		auto writeSCCs = [&](int level, SCCIterator& it)
		{
			for (; it; ++it)
			{
				int sccMember = *it;
				output[sccMember] = it.representative();
			}
		};
		findStronglyConnectedComponents(numNodes, adjCallback, writeSCCs);
	}

	// Input is a set of nodes, where each node has a list of indices of nodes it connects to.
	// Takes a function that takes an iterator for each SCC found.
	template <typename T, typename S>
	inline void findStronglyConnectedComponents(int numNodes, T&& adjCallback, S&& callback) const
	{
		auto noop = [&](int level, int node)
		{
		};
		findStronglyConnectedComponents(numNodes, adjCallback, noop, callback);
	}

	// Input is a set of nodes, where each node has a list of indices of nodes it connects to.
	// Takes two functions: one is called each time a node is visited, and the other takes an iterator for each SCC found.
	template <typename T, typename R, typename S>
	void findStronglyConnectedComponents(int numNodes, T&& adjCallback, R&& visitFunction, S&& callback) const
	{
		m_nodeInfos.clear();
		m_nodeInfos.resize(numNodes);

		int visitCount = 0;
		m_stack.clear();
		m_stack.reserve(numNodes);

		for (int i = 0; i < numNodes; ++i)
		{
			if (m_nodeInfos[i].visitOrder < 0)
			{
				tarjan(0, adjCallback, i, visitCount, visitFunction, callback);
			}
		}
	}

	// Version that takes a set of changed nodes
	template <typename T, typename R, typename S>
	void findStronglyConnectedComponents(int numNodes, const vector<int>& changedIndices, T&& adjCallback, R&& visitFunction, S&& onSCC) const
	{
		m_nodeInfos.clear();
		m_nodeInfos.resize(numNodes);

		int visitCount = 0;
		m_stack.clear();
		m_stack.reserve(numNodes);

		for (int i : changedIndices)
		{
			if (m_nodeInfos[i].visitOrder < 0)
			{
				tarjan(0, adjCallback, i, visitCount, visitFunction, onSCC);
			}
		}
	}

private:
	template <typename T, typename R, typename S>
	void tarjan(int level, T&& adjCallback, int nodeIndex, int& visitCount, R&& visitFunction, S&& onSCC) const
	{
		TarjanNodeInfo& nodeInfo = m_nodeInfos[nodeIndex];
		vxy_assert(nodeInfo.visitOrder < 0);

		nodeInfo.visitOrder = visitCount;
		nodeInfo.lowLink = visitCount;
		++visitCount;

		nodeInfo.onStack = true;
		m_stack.push_back(nodeIndex);

		visitFunction(level, nodeIndex);

		adjCallback(nodeIndex, [&](int destinationNode)
		{
			TarjanNodeInfo& destNodeInfo = m_nodeInfos[destinationNode];
			if (destNodeInfo.visitOrder < 0)
			{
				tarjan(level + 1, adjCallback, destinationNode, visitCount, visitFunction, onSCC);
				nodeInfo.lowLink = min(nodeInfo.lowLink, destNodeInfo.lowLink);
			}
			else if (destNodeInfo.onStack)
			{
				vxy_assert(destNodeInfo.visitOrder >= 0);
				nodeInfo.lowLink = min(nodeInfo.lowLink, destNodeInfo.visitOrder);
			}
		});

		if (nodeInfo.visitOrder == nodeInfo.lowLink)
		{
			// Strongly-connected component found
			SCCIterator it(*this, nodeIndex);
			onSCC(level, it);
		}
	}
};

} // namespace Vertexy