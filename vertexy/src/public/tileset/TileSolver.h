// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "topology/GraphRelations.h"
#include "topology/GridTopology.h"
#include "topology/IPlanarTopology.h"

namespace Vertexy
{
	class TileSolver
	{
		// the symmetry of a square can pre presented as:
		//   D4 = < a,b : a^4 = b^2 = e, ab = ba^-1 >
		// D4 is the group of symmetries (Dihedral group) of the square.
		// <a> represents a 90 degree rotations, <b> represents reflection and <e> its identity
		// A square has a set of 8 symmetries and can be composed as:
		//   [e, ea, ea^2, ea^3, eb, eba, eba^2, eba^3]
		// We can enumerate them as configuration 0 to 7.
		// When using square tiles, some of those compositions are not symmetric,
		// we can use the characters X, I, /, T, L, f to represent those asymmetries. 
		// For example an X is isomorphic for all configurations,
		// a F is anisomorphic for all configurations.
		// the cardinality of a tile represents the the set size for all configurations.
		// So X has a cardinality of 1, since doesn't matter what operations we perform, we get the same tile.
		// A F has cardinality of 8, a I has a cardinality of 2.	
		struct D4Symmetry
		{
			function<int(int)> a;
			function<int(int)> b;
			int cardinality;
			D4Symmetry(char symmetry);
		};

		struct Tile: D4Symmetry
		{
			int id;
			string name;
			char symmetry;
			float weightMin;
			float weightMax;
			Tile(int id, string name, char symmetry, float weightMin, float weightMax) :
				D4Symmetry(symmetry),
				id(id),
				name(name),
				symmetry(symmetry),
				weightMin(weightMin),
				weightMax(weightMax)
			{
			}
		};

		// a relationship of 2 adjacent tiles also follows the same configurations as D4.
		// To perform any rotation/reflection on the relationship we perform the
		// same operation to both tiles and the direction. Direction has a T symmetry.
		struct Relationship
		{
			shared_ptr<Tile> t0;
			int c0;
			shared_ptr<Tile> t1;
			int c1;
			shared_ptr<D4Symmetry> dir;
			int cd;
			Relationship* a();		
			Relationship* b();		
		};


	public:
		TileSolver(ConstraintSolver* solver, int numCols, int numRows);
		void parseJsonString(string str);
		void parseJsonFile(string filepath);
		void exportResults();
		shared_ptr<PlanarGridTopology> grid();
		shared_ptr<TTopologyVertexData<VarID>> tileData();
		shared_ptr<TTopologyVertexData<VarID>> configData();

	protected:
		ConstraintSolver* m_solver; //PLACEHOLER
		shared_ptr<PlanarGridTopology> m_grid;
		shared_ptr<TTopologyVertexData<VarID>> m_tileData;
		shared_ptr<TTopologyVertexData<VarID>> m_configData;
		vector<shared_ptr<Tile>> m_tiles;
		vector<shared_ptr<Relationship>> m_allRel;

	protected:
		void createAllPossibleRelations(const vector<shared_ptr<Relationship>>& original_rel);
		vector<int> getAllowedTiles(int t0, int dir);
		vector<int> getAllowedconfigurations(int t0, int c0, int t1, int dir);
		vector<int> getAllowedCardinalities(int t0);
		void createConstraints();
		
	};
}