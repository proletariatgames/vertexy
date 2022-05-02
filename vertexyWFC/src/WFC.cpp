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
#include "cbmp.h"

using namespace Vertexy;

static constexpr int FORCE_SEED = 0;
static constexpr char* INPUT_FILE = "../../vertexyWFC/src/SimpleInput.bmp";
static const int KERNEL_SIZE = 2;
static const int OUTPUT_WIDTH = 50;
static const int OUTPUT_HEIGHT = 50;

int main(int argc, char* argv[])
{
  // Read image into BMP struct
  BMP* bmp = bopen(INPUT_FILE);

  unsigned int width, height;
  unsigned char r, g, b;

  // Gets image width in pixels
  width = get_width(bmp);

  // Gets image height in pixels
  height = get_height(bmp);

  for (int x = 0; x < width - KERNEL_SIZE; x++)
  {
    for (int y = 0; y < height - KERNEL_SIZE; y++)
    {
      for (int dx = 0; dx < KERNEL_SIZE; ++dx)
      {
        for (int dy = 0; dy < KERNEL_SIZE; ++dy)
        {

        }
      }
      // Gets pixel rgb values at point (x, y)
      get_pixel_rgb(bmp, x, y, &r, &g, &b);

      // Sets pixel rgb values at point (x, y)
      set_pixel_rgb(bmp, x, y, 255 - r, 255 - g, 255 - b);
    }
  }

  // Write bmp contents to file
  bwrite(bmp, "../../vertexyWFC/src/output.bmp");

  // Free memory
  bclose(bmp);

  return 0;
	
}
