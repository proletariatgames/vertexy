// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "topology/ITopology.h"

namespace Vertexy
{

// Interface for a mapping between a vertices in a graph and values.
template <typename T>
class IGraphRelation : public enable_shared_from_this<IGraphRelation<T>>
{
public:
    using RelationType = T;
    using VertexID = ITopology::VertexID;

    IGraphRelation()
    {
    }

    virtual ~IGraphRelation()
    {
    }

    template <typename U>
    shared_ptr<const IGraphRelation<typename U::RelationType>> map(const shared_ptr<U>& relation) const;
    template<typename U>
    shared_ptr<const IGraphRelation<T>> filter(U&& filter) const;

    virtual bool equals(const IGraphRelation<T>& rhs) const
    {
        return this == &rhs;
    }

    bool operator==(const IGraphRelation<T>& rhs) const
    {
        return equals(rhs);
    }

    virtual size_t hash() const = 0;

    virtual bool getRelation(VertexID sourceVertex, T& out) const = 0;
    virtual wstring toString() const { return TEXT("Custom"); }
};

template<typename T>
using IGraphRelationPtr = shared_ptr<const IGraphRelation<T>>;

using GraphVertexRelationPtr = IGraphRelationPtr<ITopology::VertexID>;

}