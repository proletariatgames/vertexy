// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

namespace Vertexy
{

enum class ETopologySearchResponse : uint8_t
{
	Continue,
	// Continue searching
	Skip,
	// Skip any edges out of this vertex, but continue iteration
	Abort,
	// Abort the search entirely
};

} // namespace Vertexy