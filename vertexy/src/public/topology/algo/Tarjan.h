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
public:
	using AdjacentCallback = const function<void(int /*NodeIndex*/, const function<void(int)>&) /*Recurse*/>&;

	TarjanAlgorithm()
	{
	}

	// Input is a set of nodes, where each node has a list of indices of nodes it connects to.
	// The output is a list where each element corresponds to the input node at the same index,
	// and the value identifies which strongly-connected component (SCC) the node belongs to.
	template <typename T>
	inline void findStronglyConnectedComponents(int numNodes, T&& adjCallback, vector<int>& output) const
	{
		output.clear();
		output.resize(numNodes);

		auto writeSCCs = [&](int level, auto& it)
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
		auto noop = [&](int level, int node) {};
		findStronglyConnectedComponents(numNodes, adjCallback, noop, callback);
	}

	// Input is a set of nodes, where each node has a list of indices of nodes it connects to.
	// Takes two functions: one is called each time a node is visited, and the other takes an iterator for each SCC found.
	template <typename T, typename R, typename S>
	void findStronglyConnectedComponents(int numNodes, T&& adjCallback, R&& visitFunction, S&& callback) const
	{
		m_nodeInfos.clear();
		m_nodeInfos.resize(numNodes);

		m_visitCount = 0;
		m_trail.clear();
		m_trail.reserve(numNodes);

		for (int i = 0; i < numNodes; ++i)
		{
			if (m_nodeInfos[i].visitOrder < 0)
			{
				tarjan(i, adjCallback, visitFunction, callback);
			}
		}
	}

	// Version that takes a set of changed nodes
	template <typename T, typename R, typename S>
	void findStronglyConnectedComponents(int numNodes, const vector<int>& changedIndices, T&& adjCallback, R&& visitFunction, S&& onSCC) const
	{
		m_nodeInfos.clear();
		m_nodeInfos.resize(numNodes);

		m_visitCount = 0;
		m_trail.clear();
		m_trail.reserve(numNodes);

		for (int i : changedIndices)
		{
			if (m_nodeInfos[i].visitOrder < 0)
			{
				tarjan(i, adjCallback, visitFunction, onSCC);
			}
		}
	}

private:
	struct TarjanNodeInfo
	{
		TarjanNodeInfo()
			: visitOrder(-1)
			, lowLink(-1)
			, inTrail(false)
		{
		}

		int visitOrder;
		int lowLink;
		bool inTrail;
	};

	struct SCCIterator
	{
		SCCIterator(const TarjanAlgorithm& algo, int nodeIndex)
			: m_algo(algo)
			, m_nodeIndex(nodeIndex)
			, m_hitEnd(false)
		{
			m_last = algo.m_trail.back();
			algo.m_nodeInfos[m_last].inTrail = false;
			algo.m_trail.pop_back();
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
				m_last = m_algo.m_trail.back();
				m_algo.m_nodeInfos[m_last].inTrail = false;
				m_algo.m_trail.pop_back();
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

	mutable vector<TarjanNodeInfo> m_nodeInfos;
	mutable vector<int> m_trail;
	mutable vector<int> m_fifo;
	mutable vector<int> m_hist;
	mutable vector<int> m_cursor;
	mutable vector<int> m_heads;
	mutable int m_visitCount;

	inline void popStack() const
	{
		vxy_assert(!m_fifo.empty());
		m_fifo.pop_back();
		m_cursor.pop_back();

		// trim stack
		m_hist.resize(m_heads.back());
		m_heads.pop_back();
	}

	template<typename T>
	inline void pushStack(int node, T&& adjCallback) const
	{
		m_trail.push_back(node);
		m_nodeInfos[node].inTrail = true;

		m_fifo.push_back(node);
		// backup start offset for this level
		m_heads.push_back(m_hist.size());
		// cursor for this level
		m_cursor.push_back(m_hist.size());
		// gather all direct children
		adjCallback(node, [&](int destinationNode)
		{
			TarjanNodeInfo& destNodeInfo = m_nodeInfos[destinationNode];
			m_hist.push_back(destinationNode);
		});

		auto& nodeInfo = m_nodeInfos[node];
		nodeInfo.visitOrder = m_visitCount;
		nodeInfo.lowLink = m_visitCount;
		++m_visitCount;
	}

	template <typename T, typename R, typename S>
	void tarjan(int startNode, T&& adjCallback, R&& visitFunction, S&& onSCC) const
	{
		m_fifo.clear();
		m_cursor.clear();
		m_heads.clear();
		m_hist.clear();

		visitFunction(0, startNode);
		pushStack(startNode, adjCallback);

		while (!m_fifo.empty())
		{
			// DFS through siblings
			while (m_cursor.back() < m_hist.size())
			{
				int node = m_hist[m_cursor.back()++];
				auto& parent = m_nodeInfos[m_fifo.back()];

				TarjanNodeInfo& nodeInfo = m_nodeInfos[node];
				if (nodeInfo.visitOrder >= 0)
				{
					if (nodeInfo.inTrail)
					{
						parent.lowLink = min(nodeInfo.lowLink, parent.lowLink);
					}
					continue;
				}

				visitFunction(m_fifo.size(), node);
				pushStack(node, adjCallback);
			}

			// finished this level
			auto& nodeInfo = m_nodeInfos[m_fifo.back()];
			if (nodeInfo.visitOrder == nodeInfo.lowLink)
			{
				// Strongly-connected component found.
				// SCCIterator will unwind the trail.
				SCCIterator it(*this, m_fifo.back());
				onSCC(m_fifo.size()-1, it);
			}

			popStack();

			if (!m_fifo.empty())
			{
				auto& parentNodeInfo = m_nodeInfos[m_fifo.back()];
				parentNodeInfo.lowLink = min(parentNodeInfo.lowLink, nodeInfo.lowLink);
			}
		}
	}
};

} // namespace Vertexy