// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

namespace csolver
{

enum class ETopologySearchResponse : uint8_t
{
	Continue,
	// Continue searching
	Skip,
	// Skip any edges out of this node, but continue iteration
	Abort,
	// Abort the search entirely
};

} // namespace csolver