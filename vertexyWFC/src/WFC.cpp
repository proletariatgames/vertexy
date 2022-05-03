// SolverTest.cpp : Defines the entry point for the application.
//

#include "WFC.h"

#include <EATest/EATest.h>
#include <EASTL/set.h>

#include "ConstraintSolver.h"
#include "util/Asserts.h"
#include "ds/ValueBitset.h"
#include "ds/ESTree.h"
#include "topology/DigraphTopology.h"
#include "topology/GridTopology.h"
#include "topology/GraphRelations.h"
#include "topology/IPlanarTopology.h"
#include "cbmp.h"

using namespace Vertexy;

static constexpr int FORCE_SEED = 0;
static constexpr char* INPUT_FILE = "../../vertexyWFC/src/SimpleInput.bmp";
static constexpr char* OUTPUT_FILE = "../../vertexyWFC/src/output.bmp";
static const int KERNEL_SIZE = 2;
static const int OUTPUT_WIDTH = 16;
static const int OUTPUT_HEIGHT = 16;

unsigned makeColor(char r, char g, char b)
{
  return r & (g << 8) & (b << 16);
}

char getR(unsigned i)
{
  return i & 0x0000FF;
}

char getG(unsigned i)
{
  return i & 0x00FF00;
}

char getB(unsigned i)
{
  return i & 0xFF0000;
}

int main(int argc, char* argv[])
{
  // Read image into BMP struct
  BMP* bmp = bopen(INPUT_FILE);

  unsigned char r, g, b;

  // Gets image width in pixels
  unsigned width = get_width(bmp);

  // Gets image height in pixels
  unsigned height = get_height(bmp);

  hash_map<unsigned, int> colorToTile;
  hash_map<int, unsigned> tileToColor;

  for (int x = 0; x < width; x++)
  {
    for (int y = 0; y < height; y++)
    {
      // Gets pixel rgb values at point (x, y)
      get_pixel_rgb(bmp, x, y, &r, &g, &b);
      unsigned color = makeColor(r, g, b);
      if (colorToTile.find(color) != colorToTile.end())
      {
        int tile = colorToTile.size();
        colorToTile[color] = tile;
        tileToColor[tile] = color;
      }
    }
  }

  ConstraintSolver solver(TEXT("WFC"), FORCE_SEED);
  // The domain determines the range of values that each tile takes on
  SolverVariableDomain tileDomain(0, colorToTile.size());

  // Create the topology for the maze.
  shared_ptr<PlanarGridTopology> grid = make_shared<PlanarGridTopology>(OUTPUT_WIDTH, OUTPUT_HEIGHT);

  // Create a variable for each tile in the maze.
  auto tileData = solver.makeVariableGraph(TEXT("TileVars"), ITopology::adapt(grid), tileDomain, TEXT("Cell"));

  for (int x = 0; x < width - KERNEL_SIZE; x++)
  {
    for (int y = 0; y < height - KERNEL_SIZE; y++)
    {
      vector<GraphRelationClause> clauses;
      for (int dx = 0; dx < KERNEL_SIZE; ++dx)
      {
        for (int dy = 0; dy < KERNEL_SIZE; ++dy)
        {
          auto relation = make_shared<TTopologyLinkGraphRelation<VarID>>(tileData, 
              PlanarGridTopology::moveRight(dx).combine(PlanarGridTopology::moveDown(dy)));
          get_pixel_rgb(bmp, x+dx, y+dy, &r, &g, &b);
          clauses.push_back(GraphRelationClause(relation, EClauseSign::Inside, vector{ colorToTile[makeColor(r,g,b)] }));
        }
      }

      solver.makeGraphConstraint<ClauseConstraint>(grid, clauses);

    }
  }

  EConstraintSolverResult result = solver.startSolving();
  while (result == EConstraintSolverResult::Unsolved)
  {
    result = solver.step();
  }

  if (result == EConstraintSolverResult::Solved)
  {
    BMP* outputBMP = bopen(OUTPUT_FILE);

    for (int x = 0; x < OUTPUT_WIDTH; x++)
    {
      for (int y = 0; y < OUTPUT_HEIGHT; y++)
      {
        vector<int> cellVals = solver.getPotentialValues(tileData->get(grid->coordinateToIndex(x, y)));
        unsigned val = tileToColor[cellVals[0]];
        unsigned char r = getR(val);
        unsigned char g = getG(val);
        unsigned char b = getB(val);
        set_pixel_rgb(outputBMP, x, y, getR(val), getG(val), getB(val));
      }
    }
    // Write bmp contents to file
    bwrite(outputBMP, OUTPUT_FILE);
    // Free memory
    bclose(outputBMP);
  }

  // Free memory
  bclose(bmp);

  return 0;
	
}
