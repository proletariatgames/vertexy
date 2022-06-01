// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "ConstraintTypes.h"
#include "prefab/PrefabManager.h"
#include "topology/GridTopology.h"
#include "topology/GraphRelations.h"
#include "topology/IPlanarTopology.h"

namespace VertexyTests
{
	using namespace Vertexy;

	class PrefabTestSolver
	{
		PrefabTestSolver()
		{
		}

	public:
		static int solveBasic(int times, int seed, bool printVerbose = true);
		static int solveRotationReflection(int times, int seed, bool printVerbose = true);

		static int check(ConstraintSolver* solver, shared_ptr<TTopologyVertexData<VarID>> tileData, const shared_ptr<PrefabManager>& prefabManager);
		static void print(ConstraintSolver* solver, shared_ptr<PlanarGridTopology> grid, shared_ptr<TTopologyVertexData<VarID>> tileData, const shared_ptr<PrefabManager>& prefabManager);
	};

}