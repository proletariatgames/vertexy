// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "SignedClause.h"
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

template<int SIG> struct TBoolLiteral;

template<int SIG>
struct TBoolID
{
    TBoolID() : value(0) {}
    explicit TBoolID(int32_t value) : value(value)
    {
        vxy_sanity(value > 0);
    }
    bool isValid() const { return value > 0; }
    bool operator==(const TBoolID& other) const { return value == other.value; }
    bool operator!=(const TBoolID& other) const { return value != other.value; }
    TBoolLiteral<SIG> pos() const { return TBoolLiteral<SIG>(*this, true); }
    TBoolLiteral<SIG> neg() const { return TBoolLiteral<SIG>(*this, false); }

    int32_t value;
};

template<int SIG>
struct TBoolLiteral
{
    using IDType = TBoolID<SIG>;

    TBoolLiteral() : value(0) {}
    explicit TBoolLiteral(IDType id, bool value=true)
        : value(value ? id.value : -id.value)
    {
    }

    TBoolLiteral inverted() const { return TBoolLiteral(id(), !sign()); }
    bool sign() const { return value > 0; }
    IDType id() const { return IDType(value < 0 ? -value : value); }
    bool isValid() const { return value != 0; }

    bool operator==(const TBoolLiteral& other) const { return value == other.value; }
    bool operator!=(const TBoolLiteral& other) const { return value != other.value; }

    int32_t value;
};

// template parameter here is just to ensure these are treated as separate types.
using AtomID = TBoolID<0>;
using GraphAtomID = TBoolID<1>;

using AtomLiteral = TBoolLiteral<0>;
using GraphAtomLiteral = TBoolLiteral<1>;

enum class ERuleHeadType : uint8_t
{
    Normal,
    Disjunction,
    Choice
};

template<typename T>
struct TRuleHead
{
    TRuleHead(ERuleHeadType type) : type(type) {}
    TRuleHead(const T& head, ERuleHeadType type=ERuleHeadType::Normal) : type(type)
    {
        heads.push_back(head);
    }
    TRuleHead(T&& head, ERuleHeadType type=ERuleHeadType::Normal) noexcept : type(type)
    {
        heads.push_back(move(head));
    }
    TRuleHead(const vector<T>& hds, ERuleHeadType type) : type(type)
    {
        vxy_assert(!hds.empty());
        vxy_assert_msg(type != ERuleHeadType::Normal || hds.size() == 1, "Normal rule heads can only have one element");
        vxy_assert_msg(type != ERuleHeadType::Disjunction || hds.size() > 1, "Disjunction rule heads must have at least two elements");
        heads = hds;
    }

    TRuleHead(vector<T>&& hds, ERuleHeadType type) noexcept : type(type)
    {
        vxy_assert(!hds.empty());
        vxy_assert_msg(type != ERuleHeadType::Normal || hds.size() == 1, "Normal rule heads can only have one element");
        vxy_assert_msg(type != ERuleHeadType::Disjunction || hds.size() > 1, "Disjunction rule heads must have at least two elements");
        heads = move(hds);
    }

    ERuleHeadType type;
    vector<T> heads;
};

template<typename T>
struct TRuleBodyElement
{
public:
    TRuleBodyElement()
    {
    }

    static TRuleBodyElement<T> create(const vector<T>& values)
    {
        TRuleBodyElement<T> out;
        out.values = values;
        return out;
    }

    static TRuleBodyElement<T> create(const T& value)
    {
        TRuleBodyElement<T> out;
        out.values.push_back(value);
        return out;
    }

    static TRuleBodyElement<T> create(T&& value) noexcept
    {
        TRuleBodyElement<T> out;
        out.values.push_back(move(value));
        return out;
    }

    static TRuleBodyElement<T> createSum(const TWeighted<T>& weightedValues, int lowerBound)
    {
        TRuleBodyElement<T> out;
        out.values.reserve(weightedValues.size());
        out.weights.reserve(weightedValues.size());
        for (auto it = weightedValues.begin(), itEnd = weightedValues.end(); it != itEnd; ++it)
        {
            out.values.push_back(it->value);
            out.weights.push_back(it->weight);
        }
        out.lowerBound = lowerBound;
        out.isSum = true;
        return out;
    }

    static TRuleBodyElement<T> createCount(const vector<T>& values, int lowerBound)
    {
        TRuleBodyElement<T> out;
        out.values.reserve(values.size());
        out.weights.reserve(values.size());
        for (auto it = values.begin(), itEnd = values.end(); it != itEnd; ++it)
        {
            out.values.push_back(*it);
            out.weights.push_back(1);
        }
        out.lowerBound = lowerBound;
        out.isSum = true;
        return out;
    }

    vector<T> values;
    vector<int> weights;
    int lowerBound = -1;
    bool isSum = false;
};

using GraphAtomRelationPtr = shared_ptr<const IGraphRelation<vector<AtomID>>>;

using RuleGraphRelationPtr = variant<
    GraphLiteralRelationPtr,
    GraphClauseRelationPtr,
    GraphAtomRelationPtr
>;

using AnyLiteralType = variant<
    AtomLiteral, SignedClause, Literal
>;

using AnyBodyElement = variant<
    TRuleBodyElement<AtomLiteral>,
    TRuleBodyElement<SignedClause>,
    TRuleBodyElement<Literal>
>;

class BindCaller;

using BoundGraphAtomID = pair<GraphAtomID, BindCaller*>;
using BoundGraphAtomLiteral = pair<GraphAtomLiteral, BindCaller*>;

using AnyGraphLiteralType = variant<
    BoundGraphAtomLiteral,
    AtomLiteral
>;

using AnyGraphBodyElement = variant<
    TRuleBodyElement<BoundGraphAtomLiteral>,
    TRuleBodyElement<AtomLiteral>
>;

using AnyGraphHeadType = variant<
    TRuleHead<AtomID>,
    TRuleHead<BoundGraphAtomID>
>;

using RuleBodyList = vector<AnyBodyElement>;
using GraphRuleBodyList = vector<AnyGraphBodyElement>;

template<typename HeadType, typename BodyType>
class TRule
{
public:
    TRule() {}
    TRule(const TRuleHead<HeadType>& head) : m_head(head) {}
    TRule(TRuleHead<HeadType>&& head) noexcept : m_head(move(head)) {}

    TRule(const TRuleHead<HeadType>& head, const vector<BodyType>& body) : m_head(head), m_body(body) {}
    TRule(TRuleHead<HeadType>&& head, vector<BodyType>&& body) noexcept : m_head(move(head)), m_body(move(body)) {}
    // TODO: Add other move constructor variants?

    // for passing in e.g. a vector<Literal> directly as the body
    template<typename T>
    TRule(const TRuleHead<HeadType>& head, const vector<T>& body)
        : m_head(head)
    {
        m_body.reserve(body.size());
        for (auto it = body.begin(), itEnd = body.end(); it != itEnd; ++it)
        {
            m_body.push_back(BodyType(*it));
        }
    }

    void addBodyElement(const BodyType& element)
    {
        m_body.push_back(element);
    }

    template<typename T>
    void addBodyElement(const T& element)
    {
        m_body.push_back(BodyType(element));
    }

    const TRuleHead<HeadType>& getHead() const { return m_head; }
    const vector<BodyType>& getBody() const { return m_body; }
    vector<BodyType>& getBody() { return m_body; }

    int getNumBodyElements() const { return m_body.size(); }

    const BodyType& getBodyElement(int idx) const
    {
        return m_body[idx];
    }
    BodyType& getBodyElement(int idx)
    {
        return m_body[idx];
    }

    // Note: undefined behavior of type is wrong!
    template<typename T>
    inline const T& getBodyElement(int idx) const
    {
        return get<T>(m_body[idx]);
    }

    template<typename T>
    inline bool isBodyElementType(int idx) const
    {
        return visit([](auto&& typedBody)
        {
            using ElementType = decay_t<decltype(typedBody)>;
            return is_same_v<ElementType, T>;
        }, m_body[idx]);
    }

protected:
    TRuleHead<HeadType> m_head;
    vector<BodyType> m_body;
};

template<typename T>
using TRuleDefinition = TRule<T, AnyBodyElement>;

template<typename T>
using TGraphRuleDefinition = TRule<T, AnyGraphBodyElement>;

} // namespace Vertexy
