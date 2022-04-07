## Introduction

Vertexy is a [conflict-driven learning constraint solver](https://en.wikipedia.org/wiki/Conflict-driven_clause_learning) that is focused on solving problems within graphs. Its primary development goal is to aid in **procedural content generation** (PCG) for games, but can be used for a variety of other purposes.

Constraint solvers allow the user to specify a set of variables and a set of constraints between variables that must be met. The solver generates one or more assignments to the variables that satisfy all constraints, assuming a solution exists.

Constraint solvers are interesting in the context of PCG because that they allow designers to specify high-level rules for content that must be satisfied, which can be difficult or impossible in traditional algorithmic or noise-based PCG methods.

The primary distinguishing feature of Vertexy apart from other constraint solvers is the ability for designers to define arbitrary graphs representing topological or causal relationships between variables. Whereas other solvers learn individual implied constraints as it searches for a solution, Vertexy can recognize implied constraints that apply to the entire graph. This can vastly reduce the search space and make otherwise intractable formulas solveable.

Vertexy is still a work-in-progress and is missing many planned features and optimizations.

## Current Features

* Integer variables with finite domain
* Graph-based constraints and learning
* Industry standard tuneable search heuristics
* A selection of industry standard restart strategies
* Plugin interface for user-defined problem-specific solving strategies
* Fully deterministic based on input seed
* A variety of dynamic graph algorithms for arbitary graph topologies
* Uses optimized [EASTL](https://github.com/electronicarts/EASTL) library instead of standard C++ library
* Modern C++ 17 codebase
* A variety of constraints with efficient implementations:
	* Clauses: `A=x or B=y or ...`
	* Implication: `C=z iff A=x or B=y`
	* Inequalities: `A < B` or `A >= C`
	* Sums: `A + B = C`
	* Table-based constraints: `table.hasRow(A=x, B=y, C=z)`
	* AllDifferent constraint: `A != B != C != ...`
	* Cardinality constraint: `x < count([A,B,C], x) < y`
	* Reachability constraint: `if (A=x and B=y) graph.reachable(A, B)`
	* Disjunctions of constraints: `ConstraintA is true or ConstraintB is true`

## Building

**NOTE Currently only supported on Windows with Visual Studio 2019+.**

_The Windows dependencies are minor and should be easy to fix though (submit a PR!)._

```
cd build
cmake .
```

This will generate a ConstraintSystem.sln file, which you can open in the IDE. 

* The **VertexyLib** project builds the main static library that includes the solver and constraint types.
* The **VertexyTestsLib** project builds a static library of various example problems.
* The **VertexyTestHarness** project builds an executable that runs the VertexyTestsLib problems and reports results.
* Other projects are EASTL and dependencies.

Three build configurations are provided:
* **Debug**: No inlining, many "sanity" checks. Intended for Vertexy internal development.
* **Development**: Inlining of many common functions, fewer asserts. Intended for every day developer usage.
* **Release**: Fully optimized build.

## Included Example Problems

* **Maze**: Generates 2D mazes with *N* locked keys and *N* locked doors. The layout of the maze is constrained by various rules (e.g. no occurrence of 2x2 all solid or all empty tiles), and the solution is constrained to require the "player" to acquire each key and unlock each door in sequence.
* **N-Queens**: A selection of different ways of solving [N-Queens](https://en.wikipedia.org/wiki/Eight_queens_puzzle) puzzles.
* **Sudoku**: Solves [Sudoku](https://en.wikipedia.org/wiki/Sudoku) puzzles.
* **TowerOfHanoi**: A selection of different ways of solving [Tower of Hanoi](https://en.wikipedia.org/wiki/Tower_of_Hanoi) puzzles.
* **Basic** A collection of simple problems to demonstrate/test various constraints.