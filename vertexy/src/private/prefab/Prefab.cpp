// Copyright Proletariat, Inc. All Rights Reserved.
#include "prefab/Prefab.h"

#include "ConstraintTypes.h"
#include "prefab/Tile.h"

using namespace Vertexy;

Prefab::Prefab(int inID, const vector<vector<Tile>> inTiles, const NeighborData& inNeighborData) :
	m_id(inID),
	m_tiles(inTiles),
	m_neighborData(inNeighborData)
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

	updateNeighbors();
}

void Prefab::updateNeighbors()
{
	// Clear neighbor tiles
	m_neighborData.rightTiles.clear();
	m_neighborData.leftTiles.clear();
	m_neighborData.aboveTiles.clear();
	m_neighborData.belowTiles.clear();

	int positionIndex = 0;

	for (int x = 0; x < m_tiles.size(); x++)
	{
		for (int y = 0; y < m_tiles[x].size(); y++)
		{
			// Skip gaps in the prefab
			if (m_tiles[x][y].id() == INVALID_TILE)
			{
				continue;
			}

			if (x == 0 || m_tiles[x - 1][y].id() == INVALID_TILE)
			{
				m_neighborData.aboveTiles.push_back(positionIndex);
			}
			
			if (x == m_tiles.size() - 1 || m_tiles[x + 1][y].id() == INVALID_TILE)
			{
				m_neighborData.belowTiles.push_back(positionIndex);
			}

			if (y == 0 || m_tiles[x][y - 1].id() == INVALID_TILE)
			{
				m_neighborData.leftTiles.push_back(positionIndex);
			}

			if (y == m_tiles[x].size() - 1 || m_tiles[x][y + 1].id() == INVALID_TILE)
			{
				m_neighborData.rightTiles.push_back(positionIndex);
			}

			positionIndex++;
		}
	}
}

bool Prefab::isEqual(const Prefab& other)
{
	if (m_tiles.size() != other.tiles().size() || m_tiles[0].size() != other.tiles()[0].size())
	{
		return false;
	}
	for (int i = 0; i < m_tiles.size(); i++)
	{
		for (int j = 0; j < m_tiles[0].size(); j++)
		{
			if (m_tiles[i][j].id() != other.tiles()[i][j].id() || m_tiles[i][j].configuration() != other.tiles()[i][j].configuration())
			{
				return false;
			}
		}
	}
	return true;
}

bool Prefab::canOverlap(const Prefab& other, int dx, int dy)
{
	for (int y = 0; y < m_tiles.size(); y++)
	{
		for (int x = 0; x < m_tiles[0].size(); x++)
		{
			if (y - dy >= other.tiles().size() || y - dy < 0) { continue; }
			if (x - dx >= other.tiles()[0].size() || x - dx < 0) { continue; }

			if (m_tiles[y][x].id() != other.tiles()[y - dy][x - dx].id() ||
				m_tiles[y][x].configuration() != other.tiles()[y - dy][x - dx].configuration())
			{
				return false;
			}
		}
	}
	return true;
}
