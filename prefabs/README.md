## Prefab Examples

This folder contains sample json files that can be used to create prefabs that the solver can use when creating tile-based maps. `tiles` is the only mandatory argument; the rest are optional.

## Fields
* name: String argument. A unique name for this prefab. Can be used to obtain variations of this prefab (i.e. rotation/reflection). Optional argument; defaults to "".
* allowRotation: Boolean argument. If true, this prefab can be rotated. Optional argument; defaults to false.
* allowReflection: Boolean argument. If true, this prefab can be reflected. Optional argument; defaults to false.
* tiles: A 2D array of integers representing tiles. A -1 value represents an empty tile that isn't a part of this prefab. This argument is mandatory.
* neighbors: An array of direction:string pairs representing prefab neighbors. For any of these directions, it will mandate that at least one tile in this prefab has the neighbor prefab in the appropriate direction. Optional argument, and all the neighbors are optional as well.
    * right: String argument. Optional argument; defaults to "".
    * left: String argument. Optional argument; defaults to "".
    * above: String argument. Optional argument; defaults to "".
    * below: String argument. Optional argument; defaults to "".