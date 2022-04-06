// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "BreadthFirstSearch.h"
#include "ConstraintTypes.h"

namespace csolver
{
class ShortestPathAlgorithm
{
public:
	ShortestPathAlgorithm()
	{
	}

	template <typename T>
	inline bool find(const TTopology<T>& topology, int startNode, int endNode, vector<int>& outPath)
	{
		vector<int> parentLinks;
		parentLinks.resize(topology->getNumNodes(), -1);

		m_bfs.search(topology, startNode, [&](int node, int parent)
		{
			parentLinks[node] = parent;
			return node == endNode ? ETopologySearchResponse::Abort : ETopologySearchResponse::Continue;
		});

		if (parentLinks[endNode] < 0)
		{
			return false;
		}

		outPath.clear();
		for (int node = endNode; node != startNode; node = parentLinks[node])
		{
			outPath.push_back(node);
		}
		outPath.push_back(startNode);

		reverse(outPath.begin(), outPath.end());
		return true;
	}

	// Version that returns a path as pair of (node, outedge)
	template <typename T>
	inline bool find(const TTopology<T>& topology, int startNode, int endNode, vector<tuple<int, int>>& outPath)
	{
		vector<tuple<int, int>> parentLinks;
		parentLinks.resize(topology.getNumNodes(), make_tuple(-1, -1));

		m_bfs.search(topology, startNode, [&](int level, int node, int parent, int edgeIndex)
		{
			parentLinks[node] = make_tuple(parent, edgeIndex);
			return node == endNode ? ETopologySearchResponse::Abort : ETopologySearchResponse::Continue;
		});

		if (get<0>(parentLinks[endNode]) < 0)
		{
			return false;
		}

		outPath.clear();
		int node = endNode;
		int edge = -1;
		while (node != startNode)
		{
			outPath.push_back(make_tuple(node, edge));
			edge = get<1>(parentLinks[node]);
			node = get<0>(parentLinks[node]);
		}
		outPath.push_back(make_tuple(startNode, edge));

		reverse(outPath.begin(), outPath.end());
		return true;
	}

protected:
	BreadthFirstSearchAlgorithm m_bfs;
};

} // namespace csolver