// Copyright Proletariat, Inc. All Rights Reserved.
#include "prefab/Tile.h"
#include <EASTL/vector.h>
#include "util/Asserts.h"

using namespace Vertexy;

Tile::Tile(int id, string name, char symmetry, int configuration):
	m_id(id),
	m_name(name),
	m_symmetry(symmetry),
	m_configuration(configuration)
{
	// This set of characters represents different types of tile symmetry,
	// e.g. the letter "X" is symmetrical for all rotations and reflections.
	// See the header file for a more detailed explanation.
	vector<char> v = { 'X', 'I', '/', 'T', 'L', 'F' };
	vxy_assert((find(v.begin(), v.end(), symmetry) != v.end()));
	switch (symmetry)
	{
	case 'X':
		m_cardinality = 1;
		m_a = [](int i) { vxy_assert(i < 1); return 0; };
		m_b = [](int i) { vxy_assert(i < 1); return 0; };
		break;
	case 'I':
		m_cardinality = 2;
		m_a = [](int i) { vxy_assert(i < 2); return 1 - i; };
		m_b = [](int i) { vxy_assert(i < 2); return i; };
		break;
	case '/':
		m_cardinality = 2;
		m_a = [](int i) { vxy_assert(i < 2); return 1 - i; };
		m_b = [](int i) { vxy_assert(i < 2); return 1 - i; };
		break;
	case 'T':
		m_cardinality = 4;
		m_a = [](int i) { vxy_assert(i < 4); return (1 + i) % 4; };
		m_b = [](int i) { vxy_assert(i < 4); return i % 2 == 0 ? i : 4 - i; };
		break;
	case 'L':
		m_cardinality = 4;
		m_a = [](int i) { vxy_assert(i < 4); return (1 + i) % 4; };
		m_b = [](int i) { vxy_assert(i < 4); return 3 - i; };
		break;
	case 'F':
		m_cardinality = 8;
		m_a = [](int i) { vxy_assert(i < 8); return i < 4 ? (i + 1) % 4 : 4 + (i - 1) % 4; };
		m_b = [](int i) { vxy_assert(i < 8); return i < 4 ? i + 4 : i - 4; };
		break;
	default:
		m_cardinality = 1;
		m_a = [](int i) { vxy_assert(i < 1); return 0; };
		m_b = [](int i) { vxy_assert(i < 1); return 0; };
	}
}

void Tile::rotate()
{
	m_configuration = m_a(m_configuration);
}

void Tile::reflect()
{
	m_configuration = m_b(m_configuration);
}
