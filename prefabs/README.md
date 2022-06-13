## Prefab Examples

This folder contains sample json files that can be used to create prefabs that the solver can use when creating tile-based maps. `tiles` is the only mandatory argument; the rest are optional.

## Fields
* name: String argument. A unique name for this prefab. Can be used to obtain variations of this prefab (i.e. rotation/reflection). Optional argument; defaults to "".
* allowRotation: Boolean argument. If true, this prefab can be rotated. Optional argument; defaults to false.
* allowReflection: Boolean argument. If true, this prefab can be reflected. Optional argument; defaults to false.
* tiles: A 2D array of integers representing tiles. A -1 value represents an empty tile that isn't a part of this prefab. This argument is mandatory.