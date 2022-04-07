// Copyright Proletariat, Inc. All Rights Reserved.
#include "topology/BipartiteGraph.h"

using namespace Vertexy;

// Implementation of Hopcroft-Karp maximum matching algorithm
// see https://www.geeksforgeeks.org/hopcroft-karp-algorithm-for-maximum-matching-set-2-implementation/
//
// The algorithm has been extended to allow right-side vertices to have multiple pairs, at same time complexity. See
// https://cs.uwaterloo.ca/~vanbeek/Publications/cp04b.pdf
//
// Additionally functionality has been added to optionally deprioritize matching a given left vertex until after the
// maximal matching of all vertices excluding it has been found.
bool BipartiteGraph::incrementalMaximalMatching(int leftVertexToDeprioritize)
{
	vector<int> leftEdges;

	m_bfsEdges.resize(m_numLeft + m_numRight);

	int bfsRet;
	while ((bfsRet = hopcroftBfs(leftVertexToDeprioritize, leftEdges)) == 1)
	{
		for (int left : leftEdges)
		{
			vxy_assert(left != leftVertexToDeprioritize && getMatchedRightSide(left) < 0);
			findAugmentingPath(left);
		}
	}

	// Finally find any potential connection for the deprioritized vertex.
	if (leftVertexToDeprioritize >= 0 && getMatchedRightSide(leftVertexToDeprioritize) < 0)
	{
		for (int right : m_adjLeft[leftVertexToDeprioritize])
		{
			vxy_assert(right >= 0);
			if (isBelowCapacity(right))
			{
				match(leftVertexToDeprioritize, right);
				break;
			}
		}
	}

	return bfsRet >= 0;
}

void BipartiteGraph::computeMaximalMatching(int leftVertexToDeprioritize)
{
	m_pairLeft.clear();
	m_pairLeft.resize(m_numLeft, -1);

	incrementalMaximalMatching(leftVertexToDeprioritize);
}

int BipartiteGraph::hopcroftBfs(int deprioritize, vector<int>& freeLeft)
{
	freeLeft.clear();

	m_seenVertex.clear();
	m_seenVertex.resize(m_numLeft + m_numRight, false);

	// Grab all vertices on left side that don't yet have a matching, and put them in a FIFO queue.
	m_queue.clear();
	if (m_queue.capacity() < m_numLeft)
	{
		m_queue.set_capacity(m_numLeft);
	}

	for (int left = 0; left < m_numLeft; ++left)
	{
		if (getMatchedRightSide(left) < 0 && left != deprioritize)
		{
			vxy_sanity(!m_queue.full());
			m_queue.push_back(left);
			freeLeft.push_back(left);
			m_bfsEdges[left].clear();
		}
	}

	if (m_queue.empty())
	{
		// Found full match
		return 0;
	}

	// Breadth-first search, one layer at a time
	const int leftSideBoundary = m_numLeft;
	while (!m_queue.empty())
	{
		bool foundRightFree = false;

		// Left -> Right edges: those not in the matching
		while (!m_queue.empty() && m_queue.front() < leftSideBoundary)
		{
			const int vertex = m_queue.front();
			const int left = vertex;
			m_queue.pop_front();

			for (auto it = m_adjLeft[left].begin(), itEnd = m_adjLeft[left].end(); it != itEnd; ++it)
			{
				const int right = *it;
				const int rightVertex = right + leftSideBoundary;
				if (getMatchedRightSide(left) != right && !m_seenVertex[rightVertex])
				{
					m_seenVertex[rightVertex] = true;

					m_bfsEdges[vertex].push_back(rightVertex);
					m_bfsEdges[rightVertex].clear();
					m_queue.push_back(rightVertex);
				}

				if (isBelowCapacity(right))
				{
					foundRightFree = true;
				}
			}
		}

		if (foundRightFree)
		{
			// Reached a free right vertex
			return 1;
		}

		// Right -> Left edges: those in the matching
		while (!m_queue.empty() && m_queue.front() >= leftSideBoundary)
		{
			const int vertex = m_queue.front();
			const int right = vertex - leftSideBoundary;
			m_queue.pop_front();

			for (auto it = m_adjRight[right].begin(), itEnd = m_adjRight[right].end(); it != itEnd; ++it)
			{
				const int left = *it;
				const int leftVertex = left;
				if (getMatchedRightSide(left) == right && !m_seenVertex[leftVertex])
				{
					m_seenVertex[leftVertex] = true;

					m_bfsEdges[vertex].push_back(leftVertex);
					m_bfsEdges[leftVertex].clear();

					m_queue.push_back(leftVertex);
				}
			}
		}
	}

	// No free right vertices left
	return -1;
}

// Starting from a free vertex, recurse through edges attempting to find another free vertex.
// If we find one, we'll back up through the stack, assigning each edge visited to the Matching.
bool BipartiteGraph::findAugmentingPath(int leftVertex)
{
	const int left = leftVertex;

	vector<int>& leftEdges = m_bfsEdges[leftVertex];
	while (!leftEdges.empty())
	{
		const int rightVertex = leftEdges.back();
		leftEdges.pop_back();

		const int right = rightVertex - m_numLeft;

		if (isBelowCapacity(right))
		{
			match(left, right);
			return true;
		}

		vector<int>& rightEdges = m_bfsEdges[rightVertex];
		while (!rightEdges.empty())
		{
			const int nextLeftVertex = rightEdges.back();
			rightEdges.pop_back();

			if (findAugmentingPath(nextLeftVertex))
			{
				match(left, right);
				return true;
			}
		}
	}

	return false;
}
