// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include "ConstraintTypes.h"
#include "topology/GraphRelations.h"

namespace Vertexy
{

class RuleStatement;
using URuleStatement = unique_ptr<RuleStatement>;

class ProgramSymbol;

// Represents an ungrounded variable within a rule program
class ProgramWildcard
{
    friend struct ProgramVariableHasher;
public:
    explicit ProgramWildcard(const wchar_t* name=nullptr);
    WildcardUID getID() const { return m_uid; }
    const wchar_t* getName() const { return m_name.c_str(); }

    bool operator==(const ProgramWildcard& rhs) const
    {
        return m_uid == rhs.m_uid;
    }

private:
    wstring m_name;
    WildcardUID m_uid = WildcardUID(0);
};

class ProgramVertex
{
    friend class Program;
    friend class ProgramSymbol;
private:
    ProgramVertex() {}
};

// Base class for instantiating all values of a particular literal in a program rule
class Instantiator
{
public:
    using AbstractOverrideMap = hash_map<ProgramSymbol*, int>;
    
    virtual ~Instantiator() {}
    // Find the first match/reset to first match
    virtual void first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) = 0;
    // Find the next match
    virtual void match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) = 0;
    // Whether we've run out of matches
    virtual bool hitEnd() const = 0;
};

using WildcardMap = hash_map<ProgramWildcard, shared_ptr<ProgramSymbol>>;

} // namespace Vertexy

namespace eastl
{

// Hashing for ProgramVariable
template<>
struct hash<Vertexy::ProgramWildcard>
{
    uint32_t operator()(const Vertexy::ProgramWildcard& var) const
    {
        return hash<int>()(var.getID());
    }
};

}

