// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "ITopology.h"
#include <EASTL/fixed_vector.h>
#include <EASTL/tuple.h>

namespace Vertexy
{

/** Class the describes a relative movement inside of a topology, represented as a list of
 *  (MoveDirection, MoveLength) entries.
 */
struct TopologyLink
{
	struct LinkItem
	{
		bool operator==(const LinkItem& rhs) const
		{
			return direction == rhs.direction && distance == rhs.distance;
		}

		int direction;
		int distance;
	};

	using DirectionList = fixed_vector<LinkItem, 3>;

	// Used to indicate no movement.
	static const TopologyLink SELF;

	TopologyLink()
	{
	}

	TopologyLink(const TopologyLink& other)
	{
		m_directions = other.m_directions;
	}

	TopologyLink(TopologyLink&& other) noexcept
	{
		m_directions = move(other.m_directions);
	}

	explicit TopologyLink(const DirectionList& list)
		: m_directions(list)
	{
	}

	explicit TopologyLink(DirectionList&& list) noexcept
		: m_directions(move(list))
	{
	}

	/** Construction by passing in a list of tuples, with first element in pair being the movement direction,
	 *  and second direction in the pair being a movement amount.
	 *  E.g.
	 *  FTopologyLink DownLeft(make_tuple(FVarGrid::Directions::Down, 1), make_tuple(FVarGrid::Directions::Left, 1));
	 */
	template <typename... DirectionsAndLengths>
	static TopologyLink create(DirectionsAndLengths&&... dirs)
	{
		TopologyLink out;
		builder(out.m_directions, forward<DirectionsAndLengths>(dirs)...);
		return out;
	}

	/** Add a new movement to the tail of the movement list. */
	void append(int direction, int length)
	{
		m_directions.push_back({direction, length});
	}

	void assign(const vector<int>& inDirections)
	{
		m_directions.clear();
		m_directions.reserve(inDirections.size());
		for (int dir : inDirections)
		{
			m_directions.push_back({dir, 1});
		}
	}

	void assign(const vector<tuple<int, int>>& inDirectionsAndLengths)
	{
		m_directions.clear();
		for (auto& directionAndDist : inDirectionsAndLengths)
		{
			m_directions.push_back({get<0>(directionAndDist), get<1>(directionAndDist)});
		}
	}

	void assign(const vector<LinkItem>& inDirectionsAndLengths)
	{
		m_directions.clear();
		m_directions.insert(m_directions.end(), inDirectionsAndLengths.begin(), inDirectionsAndLengths.end());
	}

	/** Reset the movement list */
	void clear()
	{
		m_directions.clear();
	}

	/** Append another movement onto this one, returning the result */
	TopologyLink combine(const TopologyLink& link) const
	{
		TopologyLink out = *this;
		if (!m_directions.empty() && !link.m_directions.empty() && m_directions.back().direction == link.m_directions[0].direction)
		{
			out.m_directions.back().distance += link.m_directions[0].distance;
			out.m_directions.insert(out.m_directions.end(), link.m_directions.begin() + 1, link.m_directions.end());
		}
		else
		{
			out.m_directions.insert(out.m_directions.end(), link.m_directions.begin(), link.m_directions.end());
		}
		return out;
	}

	template <typename TopologyType>
	bool isEquivalent(const TopologyLink& rhs, const TopologyType& topo) const
	{
		return topo.areTopologyLinksEquivalent(*this, rhs);
	}

	bool operator==(const TopologyLink& rhs) const
	{
		return m_directions == rhs.m_directions;
	}

	template <typename T>
	inline bool resolve(const shared_ptr<T>& topoInst, int index, int& outIndex) const
	{
		return resolve(*topoInst.get(), index, outIndex);
	}

	/** Resolve the reference against a topology instance, given the vertex index to start movement from.
	 *  @returns true if the movement was successful, in which case OutIndex is where the movement ended.
	 *  @returns false if the movement was unsuccessful due to hitting a boundary in the topology, in which
	 *  case OutIndex will be the furthest that was travelled.
	 */
	template <typename T>
	bool resolve(const T& topoInst, int index, int& outIndex) const
	{
		vxy_assert(topoInst.isValidVertex(index));

		outIndex = index;
		for (const LinkItem& instr : m_directions)
		{
			// Note we don't check return value here, because we don't care about traversability, just
			// whether the edge exists at all.
			int nextIndex;
			topoInst.getOutgoingDestination(outIndex, instr.direction, instr.distance, nextIndex);
			if (nextIndex < 0)
			{
				return false;
			}
			outIndex = nextIndex;
		}

		return true;
	}

	wstring toString(const shared_ptr<ITopology>& topo) const
	{
		if (m_directions.empty())
		{
			return TEXT("[Self]");
		}

		wstring out = TEXT("[");
		for (int i = 0; i < m_directions.size(); ++i)
		{
			out.append_sprintf(TEXT("%s[%d]"), topo->edgeIndexToString(m_directions[i].direction).c_str(), m_directions[i].distance);
			if (i != m_directions.size() - 1)
			{
				out += TEXT(", ");
			}
		}
		out += TEXT("]");
		return out;
	}

	const DirectionList& getDirections() const { return m_directions; }

	size_t hash() const
	{
		size_t hash = 0;
		for (auto& dir : m_directions)
		{
			hash = combineHashes(hash, combineHashes(eastl::hash<int>()(dir.direction), eastl::hash<int>()(dir.distance)));
		}
		return hash;
	}

private:
	// Helper function for constructor that takes variable number of arguments
	template <typename Dir, typename... Rem>
	static void builder(DirectionList& output, tuple<Dir, int>&& direction, Rem&&... dirs)
	{
		builder(output, forward<tuple<Dir, int>>(direction));
		builder(output, forward<Rem>(dirs)...);
	}

	// Helper function for constructor that takes variable number of arguments
	template <typename Dir>
	static void builder(DirectionList& output, tuple<Dir, int>&& direction)
	{
		output.push_back({get<0>(direction), get<1>(direction)});
	}

	DirectionList m_directions;
};

} // namespace Vertexy