// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include "ConstraintTypes.h"
#include "Topology.h"

namespace Vertexy
{

/** Represents information stored with each vertex of a topology, with efficient retrieval */
template <typename DataType>
class TTopologyVertexData
{
public:
	TTopologyVertexData()
	{
	}

	template <typename Impl>
	explicit TTopologyVertexData(const shared_ptr<TTopology<Impl>>& topology, const DataType& defaultValue = {}, const wstring& inName = TEXT("Data"))
	{
		initialize(topology, defaultValue, inName);
	}

	explicit TTopologyVertexData(const shared_ptr<ITopology>& topology, const DataType& defaultValue = {}, const wstring& inName = TEXT("Data"))
	{
		initialize(topology, defaultValue, inName);
	}

	template <typename Impl>
	void initialize(const shared_ptr<TTopology<Impl>>& topology, const DataType& defaultValue = {}, const wstring& inName = TEXT("Data"))
	{
		m_sourceTopology = ITopology::adapt(topology);
		m_data.resize(topology->getNumVertices(), defaultValue);
		m_name = inName;
	}

	void initialize(const shared_ptr<ITopology>& topology, const DataType& defaultValue = {}, const wstring& inName = TEXT("Data"))
	{
		m_sourceTopology = topology;
		m_data.resize(topology->getNumVertices(), defaultValue);
		m_name = inName;
	}

	inline const DataType& get(int vertexIndex) const { return m_data[vertexIndex]; }
	DataType& get(int vertexIndex) { return m_data[vertexIndex]; }

	inline void set(int vertexIndex, const DataType& value) { m_data[vertexIndex] = value; }

	inline int32_t indexOf(const DataType& vertexValue) const
	{
		return Vertexy::indexOf(m_data.begin(), m_data.end(), vertexValue);
	}

	inline const shared_ptr<ITopology>& getSource() const { return m_sourceTopology; }
	inline const vector<DataType>& getData() const { return m_data; }

	const wstring& getName() const { return m_name; }

protected:
	shared_ptr<ITopology> m_sourceTopology;
	vector<DataType> m_data;
	wstring m_name;
};

} // namespace Vertexy