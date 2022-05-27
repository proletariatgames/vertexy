// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "SignedClause.h"
#include "topology/GraphRelations.h"
#include <EASTL/variant.h>

namespace Vertexy
{

class ITopology;


template<typename T>
struct TWeighted
{
    T value;
    int weight;
};

struct AtomLiteral;

struct AtomID
{
    AtomID() : value(0) {}
    explicit AtomID(int32_t value) : value(value)
    {
        vxy_sanity(value > 0);
    }
    bool isValid() const { return value > 0; }
    bool operator==(const AtomID& other) const { return value == other.value; }
    bool operator!=(const AtomID& other) const { return value != other.value; }
    AtomLiteral pos() const;
    AtomLiteral neg() const;

    int32_t value;
};

// Relation type for abstract atom literals.
class IAtomGraphRelation : public IGraphRelation<Literal>
{
public:
    // Whether we need to instantiate this atom. Only true if the underlying formula has a binder.
    virtual bool needsInstantiation() const = 0;    
    // Bind the variable for this vertex and assign its deduced value.
    virtual bool instantiateNecessary(int vertex, Literal& outLiteral) const = 0;
    // Notify the relation that it should not create any more variables/that the RDB has been destroyed.
    virtual void lockVariableCreation() const = 0;
};

using AtomGraphRelationPtr = shared_ptr<const IAtomGraphRelation>;

class AbstractAtomRelationInfo
{
public:
    // Maps the abstract atom literal to the variable/value it is bound to.
    AtomGraphRelationPtr literalRelation;
    // The set of relations used to map this abstract literal to its body
    vector<GraphVertexRelationPtr> argumentRelations;

    size_t hash() const { return literalRelation->hash(); }
    bool operator==(const AbstractAtomRelationInfo& rhs) const
    {
        if (this == &rhs)
        {
            return true;
        }
        if (!literalRelation->equals(*rhs.literalRelation))
        {
            return false;
        }
        if (argumentRelations.size() != rhs.argumentRelations.size())
        {
            return false;
        }
        for (int i = 0; i < argumentRelations.size(); ++i)
        {
            if (argumentRelations[i]->equals(*rhs.argumentRelations[i]))
            {
                return false;
            }
        }

        return true;
    }

    const GraphLiteralRelationPtr& getInverseRelation() const
    {
        if (m_invRelation == nullptr)
        {
            m_invRelation = make_shared<InvertLiteralGraphRelation>(literalRelation);
        }
        return m_invRelation;
    }

private:
    mutable GraphLiteralRelationPtr m_invRelation;
};
using AbstractAtomRelationInfoPtr = shared_ptr<AbstractAtomRelationInfo>;

struct AtomLiteral
{
    using IDType = AtomID;

    AtomLiteral() : m_value(0) {}
    explicit AtomLiteral(AtomID id, bool value=true, const AbstractAtomRelationInfoPtr& relationInfo=nullptr)
        : m_value(value ? id.value : -id.value)
        , m_relationInfo(relationInfo)
    {
    }

    AtomLiteral inverted() const { return AtomLiteral(id(), !sign(), m_relationInfo); }
    bool sign() const { return m_value > 0; }
    AtomID id() const { return AtomID(m_value < 0 ? -m_value : m_value); }
    bool isValid() const { return m_value != 0; }

    const AbstractAtomRelationInfoPtr& getRelationInfo() const { return m_relationInfo; }
    void setRelationInfo(const AbstractAtomRelationInfoPtr& info) { m_relationInfo = info; }

    bool operator==(const AtomLiteral& other) const { return m_value == other.m_value; }
    bool operator!=(const AtomLiteral& other) const { return m_value != other.m_value; }

protected:
    int32_t m_value;
    AbstractAtomRelationInfoPtr m_relationInfo;
};

inline AtomLiteral AtomID::pos() const
{
    return AtomLiteral(*this, true);
}

inline AtomLiteral AtomID::neg() const
{
    return AtomLiteral(*this, false);
}

enum class ERuleHeadType : uint8_t
{
    Normal,
    Disjunction,
    Choice
};

} // namespace Vertexy
