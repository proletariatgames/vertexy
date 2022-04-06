// Copyright Proletariat, Inc. All Rights Reserved.
#include "topology/BipartiteGraph.h"

using namespace csolver;

// Implementation of Hopcroft-Karp maximum matching algorithm
// see https://www.geeksforgeeks.org/hopcroft-karp-algorithm-for-maximum-matching-set-2-implementation/
//
// The algorithm has been extended to allow right-side nodes to have multiple pairs, at same time complexity. See
// https://cs.uwaterloo.ca/~vanbeek/Publications/cp04b.pdf
//
// Additionally functionality has been added to optionally deprioritize matching a given left node until after the
// maximal matching of all nodes excluding it has been found.
bool BipartiteGraph::incrementalMaximalMatching(int leftNodeToDeprioritize)
{
	vector<int> leftEdges;

	m_bfsEdges.resize(m_numLeft + m_numRight);

	int bfsRet;
	while ((bfsRet = hopcroftBfs(leftNodeToDeprioritize, leftEdges)) == 1)
	{
		for (int left : leftEdges)
		{
			cs_assert(left != leftNodeToDeprioritize && getMatchedRightSide(left) < 0);
			findAugmentingPath(left);
		}
	}

	// Finally find any potential connection for the deprioritized node.
	if (leftNodeToDeprioritize >= 0 && getMatchedRightSide(leftNodeToDeprioritize) < 0)
	{
		for (int right : m_adjLeft[leftNodeToDeprioritize])
		{
			cs_assert(right >= 0);
			if (isBelowCapacity(right))
			{
				match(leftNodeToDeprioritize, right);
				break;
			}
		}
	}

	return bfsRet >= 0;
}

void BipartiteGraph::computeMaximalMatching(int leftNodeToDeprioritize)
{
	m_pairLeft.clear();
	m_pairLeft.resize(m_numLeft, -1);

	incrementalMaximalMatching(leftNodeToDeprioritize);
}

int BipartiteGraph::hopcroftBfs(int deprioritize, vector<int>& freeLeft)
{
	freeLeft.clear();

	m_seenNode.clear();
	m_seenNode.resize(m_numLeft + m_numRight, false);

	// Grab all nodes on left side that don't yet have a matching, and put them in a FIFO queue.
	m_queue.clear();
	if (m_queue.capacity() < m_numLeft)
	{
		m_queue.set_capacity(m_numLeft);
	}

	for (int left = 0; left < m_numLeft; ++left)
	{
		if (getMatchedRightSide(left) < 0 && left != deprioritize)
		{
			cs_sanity(!m_queue.full());
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
			const int node = m_queue.front();
			const int left = node;
			m_queue.pop_front();

			for (auto it = m_adjLeft[left].begin(), itEnd = m_adjLeft[left].end(); it != itEnd; ++it)
			{
				const int right = *it;
				const int rightNode = right + leftSideBoundary;
				if (getMatchedRightSide(left) != right && !m_seenNode[rightNode])
				{
					m_seenNode[rightNode] = true;

					m_bfsEdges[node].push_back(rightNode);
					m_bfsEdges[rightNode].clear();
					m_queue.push_back(rightNode);
				}

				if (isBelowCapacity(right))
				{
					foundRightFree = true;
				}
			}
		}

		if (foundRightFree)
		{
			// Reached a free right node
			return 1;
		}

		// Right -> Left edges: those in the matching
		while (!m_queue.empty() && m_queue.front() >= leftSideBoundary)
		{
			const int node = m_queue.front();
			const int right = node - leftSideBoundary;
			m_queue.pop_front();

			for (auto it = m_adjRight[right].begin(), itEnd = m_adjRight[right].end(); it != itEnd; ++it)
			{
				const int left = *it;
				const int leftNode = left;
				if (getMatchedRightSide(left) == right && !m_seenNode[leftNode])
				{
					m_seenNode[leftNode] = true;

					m_bfsEdges[node].push_back(leftNode);
					m_bfsEdges[leftNode].clear();

					m_queue.push_back(leftNode);
				}
			}
		}
	}

	// No free right nodes left
	return -1;
}

// Starting from a free vertex, recurse through edges attempting to find another free vertex.
// If we find one, we'll back up through the stack, assigning each edge visited to the Matching.
bool BipartiteGraph::findAugmentingPath(int leftNode)
{
	const int left = leftNode;

	vector<int>& leftEdges = m_bfsEdges[leftNode];
	while (!leftEdges.empty())
	{
		const int rightNode = leftEdges.back();
		leftEdges.pop_back();

		const int right = rightNode - m_numLeft;

		if (isBelowCapacity(right))
		{
			match(left, right);
			return true;
		}

		vector<int>& rightEdges = m_bfsEdges[rightNode];
		while (!rightEdges.empty())
		{
			const int nextLeftNode = rightEdges.back();
			rightEdges.pop_back();

			if (findAugmentingPath(nextLeftNode))
			{
				match(left, right);
				return true;
			}
		}
	}

	return false;
}
