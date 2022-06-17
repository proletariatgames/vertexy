// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include <EASTL/string.h>
#include <EASTL/vector.h>
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

		// getters
		const auto& grid() const { return m_grid; };
		const auto& tileData() const { return m_tileData; };
		const auto kernelSize() const { return m_kernelSize; };
		const auto& prefabs() const { return m_prefabs; };

	private:
		int m_kernelSize;
		bool m_allowRotation;
		bool m_allowrefletion;
		ConstraintSolver* m_solver;
		shared_ptr<PlanarGridTopology> m_grid;
		shared_ptr<TTopologyVertexData<VarID>> m_tileData;
		//input tiles
		vector<shared_ptr<Tile>> m_tiles;
		// Sore unique prefabs
		vector<shared_ptr<Prefab>> m_prefabs;
		// map to store frequency of each unique prefab
		hash_map<int, int> m_prefabFreq;
		void createConstrains(const vector<vector<Tile>>& inputGrid);
		void addPrefabVariation(shared_ptr<Prefab> prefab, int rotations, bool reflection);
		void addUnique(shared_ptr<Prefab> p);
	};
}
