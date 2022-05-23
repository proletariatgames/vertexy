// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include "ConstraintTypes.h"
#include "topology/GraphRelations.h"

namespace Vertexy
{

class RuleStatement;
using URuleStatement = unique_ptr<RuleStatement>;

class ProgramSymbol;

// unique ID to identify a ProgramVariable
enum VariableUID : int32_t { };
// unique ID to identify a named Formula
enum FormulaUID : int32_t { };

// Represents an ungrounded variable within a rule program
class ProgramVariable
{
    friend struct ProgramVariableHasher;
public:
    explicit ProgramVariable(const wchar_t* name=nullptr);
    VariableUID getID() const { return m_uid; }
    const wchar_t* getName() const { return m_name; }

    bool operator==(const ProgramVariable& rhs) const
    {
        return m_uid == rhs.m_uid;
    }

private:
    const wchar_t* m_name;
    VariableUID m_uid = VariableUID(0);
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
    using AbstractOverrideMap = hash_map<ProgramSymbol*, ProgramSymbol>;
    
    virtual ~Instantiator() {}
    // Find the first match/reset to first match
    virtual void first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) = 0;
    // Find the next match
    virtual void match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) = 0;
    // Whether we've run out of matches
    virtual bool hitEnd() const = 0;
};

using VariableMap = hash_map<ProgramVariable, shared_ptr<ProgramSymbol>>;

} // namespace Vertexy

namespace eastl
{

// Hashing for ProgramVariable
template<>
struct hash<Vertexy::ProgramVariable>
{
    uint32_t operator()(const Vertexy::ProgramVariable& var) const
    {
        return hash<int>()(var.getID());
    }
};

}

