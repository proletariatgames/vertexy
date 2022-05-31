// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include <EASTL/string.h>

namespace Vertexy
{
	using namespace eastl;

	class Tile
	{
	public:
		Tile(int id, string name = "", char symmetry = 'X', int configuration = 0);
		void rotate();
		void reflect();
		const int id() const { return m_id; };
		const int configuration() const { return m_configuration; };	
		static const Tile INVALID;

	private:
		int m_id;
		string m_name;
		char m_symmetry;
		int m_configuration;
		int m_cardinality;
		function<int(int)> m_a;
		function<int(int)> m_b;
	};
}