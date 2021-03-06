// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/set.h>
#include "topology/GraphRelations.h"
#include "topology/GridTopology.h"

namespace Vertexy
{
	using namespace eastl;
	class Tile;
	class Prefab;

	class TileSolver
	{
	public:
		TileSolver(ConstraintSolver* solver, int numCols, int numRows, int kernelSize, bool rotation, bool reflection);
		void parseJsonFile(string filepath);
		void parseJsonString(string str);
		void exportJson(string path);

		// Getters
		const auto& grid() const { return m_grid; };
		const auto& tileData() const { return m_tileData; };
		const auto kernelSize() const { return m_kernelSize; };
		const auto& prefabs() const { return m_prefabs; };

	private:
		// Size of the kernel to be used to extract the patterns from the input.
		int m_kernelSize;

		// Allows rotation and reflection of the patterns
		bool m_allowRotation;
		bool m_allowReflection;

		// Solver variables
		ConstraintSolver* m_solver;
		shared_ptr<PlanarGridTopology> m_grid;
		shared_ptr<TTopologyVertexData<VarID>> m_tileData;

		// Input tiles
		vector<shared_ptr<Tile>> m_tiles;

		// Unique prefabs
		vector<shared_ptr<Prefab>> m_prefabs;

		// Frequency of each unique prefab in the input
		hash_map<int, int> m_prefabFreq;

		// Allowed overlaps (prefab id => offset <x,y> => allowed neighbours)
		hash_map<int, hash_map<tuple<int, int>, set<int>>> m_overlaps;

		void createConstraints(const vector<vector<Tile>>& inputGrid);
		void addPrefabVariation(const shared_ptr<Prefab>& prefab, int rotations, bool reflection);
		void addUnique(const shared_ptr<Prefab>& p);
	};
}
