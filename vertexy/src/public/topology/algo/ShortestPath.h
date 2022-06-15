// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "BreadthFirstSearch.h"
#include "ConstraintTypes.h"

namespace Vertexy
{
class ShortestPathAlgorithm
{
public:
	ShortestPathAlgorithm()
	{
	}

	template <typename T>
	inline bool find(const TTopology<T>& topology, int startVertex, int endVertex, vector<int>& outPath)
	{
		vector<int> parentLinks;
		parentLinks.resize(topology.getNumVertices(), -1);

		m_bfs.search(topology, startVertex, [&](int vertex, int parent)
		{
			parentLinks[vertex] = parent;
			return vertex == endVertex ? ETopologySearchResponse::Abort : ETopologySearchResponse::Continue;
		});

		if (parentLinks[endVertex] < 0)
		{
			return false;
		}

		outPath.clear();
		for (int vertex = endVertex; vertex != startVertex; vertex = parentLinks[vertex])
		{
			outPath.push_back(vertex);
		}
		outPath.push_back(startVertex);

		reverse(outPath.begin(), outPath.end());
		return true;
	}

	// Version that returns a path as pair of (vertex, outedge)
	template <typename T>
	inline bool find(const TTopology<T>& topology, int startVertex, int endVertex, vector<tuple<int, int>>& outPath)
	{
		vector<tuple<int, int>> parentLinks;
		parentLinks.resize(topology.getNumVertices(), make_tuple(-1, -1));

		m_bfs.search(topology, startVertex, [&](int level, int vertex, int parent, int edgeIndex)
		{
			parentLinks[vertex] = make_tuple(parent, edgeIndex);
			return vertex == endVertex ? ETopologySearchResponse::Abort : ETopologySearchResponse::Continue;
		});

		if (get<0>(parentLinks[endVertex]) < 0)
		{
			return false;
		}

		outPath.clear();
		int vertex = endVertex;
		int edge = -1;
		while (vertex != startVertex)
		{
			outPath.push_back(make_tuple(vertex, edge));
			edge = get<1>(parentLinks[vertex]);
			vertex = get<0>(parentLinks[vertex]);
		}
		outPath.push_back(make_tuple(startVertex, edge));

		reverse(outPath.begin(), outPath.end());
		return true;
	}

protected:
	BreadthFirstSearchAlgorithm m_bfs;
};

} // namespace Vertexy