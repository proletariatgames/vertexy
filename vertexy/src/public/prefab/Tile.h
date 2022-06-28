// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include <EASTL/string.h>

namespace Vertexy
{
	using namespace eastl;

	// The symmetry of a square can be presented as:
	//   D4 = < a,b : a^4 = b^2 = e, ab = ba^-1 >
	// in which D4 is the group of symmetries (Dihedral group) of the square, <a> represents a 90
	// degree rotation, <b> represents horizontal reflection and <e> its identity.
	// A square has in total a set of 8 symmetries and can be composed as:
	//   [e, ea, ea^2, ea^3, eb, eba, eba^2, eba^3]
	// We can enumerate them as configuration 0 to 7.
	// When using square tiles, some of those compositions are not symmetric, and we can represent
	// those asymmetries with the characters X, I, /, T, L and F.
	// For example an X is isomorphic for all configurations, and F is anisomorphic for all configurations.
	// The cardinality of a tile represents the the set size for all configurations.
	// So X has a cardinality of 1, since doesn't matter what operations we perform, we get the same tile.
	class Tile
	{
	public:
		Tile(int id, string name = "", char symmetry = 'X', int configuration = 0);
		Tile(const Tile& tile, int configuration = -1);

		//rotate tile 90 degrees clockwise
		void rotate();

		//mirror tile horizontally
		void reflect();

		// getters
		const int id() const { return m_id; };
		const int configuration() const { return m_configuration; };
		const string& name() const { return m_name; };
		const char symmetry() const { return m_symmetry; };

	private:
		int m_id;
		string m_name;
		char m_symmetry;
		int m_configuration;
		int m_cardinality;

		//rotation
		function<int(int)> m_a;

		//reflection
		function<int(int)> m_b;
	};
}