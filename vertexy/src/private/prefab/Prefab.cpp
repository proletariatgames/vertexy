// Copyright Proletariat, Inc. All Rights Reserved.
#include "prefab/Prefab.h"

#include "ConstraintTypes.h"
#include "prefab/Tile.h"

using namespace Vertexy;

Prefab::Prefab(int inID, const vector<vector<Tile>>& inTiles)
	: m_tiles(inTiles)
	, m_id(inID)
{
	updatePositions();
}

const Position& Prefab::getPositionForIndex(int index)
{
	vxy_assert(index >= 0 && index < m_positions.size());
	return m_positions[index];
}

int Prefab::getNumTiles()
{
	return m_positions.size();
}

int Prefab::getTileValAtPos(int x, int y)
{
	vxy_assert(x >= 0 && x < m_tiles.size());
	vxy_assert(y >= 0 && y < m_tiles[x].size());
	return m_tiles[x][y].id();
}

void Prefab::rotate(int times)
{
	for (int t = 0; t < times; t++)
	{
		transpose();
		reverse();
		updatePositions();
		for (int i = 0; i < m_tiles.size(); i++)
		{
			for (int j = 0; j < m_tiles[0].size(); j++)
			{
				m_tiles[i][j].rotate();
			}
		}
	}
}

void Prefab::reflect()
{
	reverse();
	updatePositions();
	for (int i = 0; i < m_tiles.size(); i++)
	{
		for (int j = 0; j < m_tiles[0].size(); j++)
		{
			m_tiles[i][j].reflect();
		}
	}
}

void Prefab::transpose()
{
	vector<vector<Tile>> tmp(m_tiles[0].size(), vector<Tile>());
	for (int i = 0; i < m_tiles.size(); i++)
	{
		for (int j = 0; j < m_tiles[0].size(); j++)
		{
			tmp[j].push_back(m_tiles[i][j]);
		}
	}
	m_tiles = tmp;
}

void Prefab::reverse()
{
	for (int i = 0; i < m_tiles.size(); i++)
	{
		for (int j = 0, k = m_tiles[0].size() - 1; j < k; j++, k--)
		{
			swap(m_tiles[i][j], m_tiles[i][k]);
		}
	}
}

void Prefab::updatePositions()
{
	m_positions.clear();
	for (int x = 0; x < m_tiles.size(); x++)
	{
		for (int y = 0; y < m_tiles[x].size(); y++)
		{
			// Skip elements that aren't in the prefab
			if (m_tiles[x][y].id() == INVALID_TILE)
			{
				continue;
			}
			m_positions.push_back(Position{ x,y });
		}
	}
}
