// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include "ConstraintTypes.h"
#include "topology/GraphRelations.h"

namespace Vertexy
{

class RuleStatement;
using URuleStatement = unique_ptr<RuleStatement>;

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
private:
    ProgramVertex() {}
};

enum class ESymbolType : uint8_t
{
    Integer = 0,
    ID,
    Formula,
    External,
    Abstract,
    Invalid
};

class ConstantFormula;
class ProgramSymbol;

// Structure for providing arguments to IExternalFormulaProvider::match(), where
// some arguments may already be bound to values, and other arguments are expected to be bound.
class ExternalFormulaMatchArg
{
    explicit ExternalFormulaMatchArg(const shared_ptr<ProgramSymbol>& output)
        : m_const(false)
        , m_inner(output)
    {
    }

    explicit ExternalFormulaMatchArg(const ProgramSymbol& input)
        : m_const(true)
        , m_inner(make_shared<ProgramSymbol>(input))
    {
    }

public:
    static ExternalFormulaMatchArg makeUnbound(const shared_ptr<ProgramSymbol>& output)
    {
        return ExternalFormulaMatchArg(output);
    }
    static ExternalFormulaMatchArg makeBound(const ProgramSymbol& input)
    {
        return ExternalFormulaMatchArg(input);
    }

    bool isBound() const { return m_const; }
    const ProgramSymbol& get() const
    {
        return *m_inner;
    }

    ProgramSymbol* getUnbound() const
    {
        vxy_assert(!isBound());
        return m_inner.get();
    }

protected:
    bool m_const;
    shared_ptr<ProgramSymbol> m_inner;
};

// Base class for instantiating all values of a particular literal in a program rule
class Instantiator
{
public:
    virtual ~Instantiator() {}
    // Find the first match/reset to first match
    virtual void first() = 0;
    // Find the next match
    virtual void match() = 0;
    // Whether we've run out of matches
    virtual bool hitEnd() const = 0;
};

class IExternalFormulaProvider
{
public:
    virtual ~IExternalFormulaProvider() {}

    virtual size_t hash() const = 0;

    // evaluate whether this formula is true with these concrete arguments
    virtual bool eval(const vector<ProgramSymbol>& args) const = 0;
    // Whether this formula can instantiate variables in the specified argument slot
    virtual bool canInstantiate(int argIndex) const = 0;

    // Reset to the beginning of the list of potential matches.
    virtual void startMatching(vector<ExternalFormulaMatchArg>&& args) = 0;
    // Bind to the next set of arguments for this provider.
    virtual bool matchNext(bool& isFact) = 0;
};

using IExternalFormulaProviderPtr = shared_ptr<IExternalFormulaProvider>;


// Represents a constant value in a rule program: either an integer, a string ID, or a grounded formula call.
// Internally it is represented as a tagged pointer.
class ProgramSymbol
{
public:
    ProgramSymbol()
    {
        m_packed = encode(ESymbolType::Invalid, 0);
    }

    explicit ProgramSymbol(const GraphVertexRelationPtr& relation)
    {
        m_packed = encode(ESymbolType::Abstract, 0);
        setAbstractRelation(relation);
    }

    ProgramSymbol(int32_t constant)
    {
        m_packed = encode(ESymbolType::Integer, constant);
    }
    ProgramSymbol(const wchar_t* name)
    {
        m_packed = encode(ESymbolType::ID, reinterpret_cast<intptr_t>(name));
    }

    ProgramSymbol(const ProgramSymbol& other);

    ProgramSymbol(FormulaUID formula, const wchar_t* name, const vector<ProgramSymbol>& args, bool negated, const IExternalFormulaProviderPtr& relation=nullptr);
    ProgramSymbol(FormulaUID formula, const wchar_t* name, vector<ProgramSymbol>&& args, bool negated, const IExternalFormulaProviderPtr& relation=nullptr);

    ProgramSymbol& operator=(const ProgramSymbol& rhs);

    ProgramSymbol(const ConstantFormula* formula, bool negated, const IExternalFormulaProviderPtr& relation=nullptr);

    ~ProgramSymbol();

    ESymbolType getType() const
    {
        return ESymbolType(m_packed >> 56);
    }

    int getInt() const
    {
        vxy_sanity(getType() == ESymbolType::Integer);
        return m_packed;
    }

    const wchar_t* getID() const
    {
        vxy_sanity(getType() == ESymbolType::ID);
        return reinterpret_cast<const wchar_t*>(decode(m_packed));
    }

    const ConstantFormula* getFormula() const
    {
        vxy_sanity(isFormula());
        return reinterpret_cast<ConstantFormula*>(decode(m_packed));
    }

    bool isAbstract() const { return getType() == ESymbolType::Abstract; }
    bool isInteger() const { return getType() == ESymbolType::Integer; }
    bool isID() const { return getType() == ESymbolType::ID; }
    bool isExternalFormula() const { return getType() == ESymbolType::External; }
    bool isNormalFormula() const { return getType() == ESymbolType::Formula; }
    bool isFormula() const { return isNormalFormula() || isExternalFormula(); }

    bool isPositive() const
    {
        return !isNegated();
    }
    
    bool isNegated() const
    {
        return isFormula() ? ((m_packed & 1) == 0) : false;
    }

    bool isValid() const
    {
        return getType() != ESymbolType::Invalid;
    }

    bool isInvalid() const
    {
        return !isValid();
    }

    bool containsAbstract() const;
    ProgramSymbol makeConcrete(int vertex) const;

    ProgramSymbol negatedFormula() const;

    ProgramSymbol absolute() const
    {
        return isNegated() ? negatedFormula() : *this;
    }

    const GraphVertexRelationPtr& getAbstractRelation() const
    {
        vxy_assert(getType() == ESymbolType::Abstract);
        return *reinterpret_cast<const GraphVertexRelationPtr*>(m_smartPtrBytes);
    }

    const IExternalFormulaProviderPtr& getExternalFormulaProvider() const
    {
        if (getType() == ESymbolType::Formula)
        {
            const static IExternalFormulaProviderPtr nullPtr = nullptr;
            return nullPtr;
        }

        vxy_assert(getType() == ESymbolType::External);
        return *reinterpret_cast<const IExternalFormulaProviderPtr*>(m_smartPtrBytes);
    }

    uint32_t hash() const
    {
        switch (getType())
        {
        case ESymbolType::Abstract:
            return getAbstractRelation()->hash();
        case ESymbolType::External:
            return combineHashes(eastl::hash<uint64_t>()(m_packed), getExternalFormulaProvider()->hash());
        default:
            return eastl::hash<uint64_t>()(m_packed);
        }
    }

    bool operator==(const ProgramSymbol& rhs) const
    {
        if (this == &rhs) { return true; }
        if (rhs.getType() != getType()) { return false; }

        switch (getType())
        {
        case ESymbolType::Abstract:
            return getAbstractRelation()->equals(*rhs.getAbstractRelation());
        case ESymbolType::External:
            return m_packed == rhs.m_packed && getExternalFormulaProvider() == rhs.getExternalFormulaProvider();
        default:
            return m_packed == rhs.m_packed;
        }
    }

    bool operator!=(const ProgramSymbol& rhs) const
    {
        return !operator==(rhs);
    }

    wstring toString() const;

private:
    void destroySmartPointer();

    static uint64_t encode(ESymbolType type, intptr_t ptr)
    {
        return (static_cast<uint64_t>(type) << 56) | ptr;
    }
    static intptr_t decode(uint64_t packed)
    {
        return packed & 0x00FFFFFFFFFFFFFEULL;
    }

    void setExternalProvider(const IExternalFormulaProviderPtr& provider);
    void setAbstractRelation(const GraphVertexRelationPtr& relation);

    IExternalFormulaProviderPtr* getExternalFormulaProviderPtr();
    GraphVertexRelationPtr* getAbstractRelationPtr();

    uint64_t m_packed;
    uint64_t m_smartPtrBytes[sizeof(std::shared_ptr<int>)/sizeof(uint64_t)];
};

// Represents a unique grounded formula call.
class ConstantFormula
{
    /*private*/ ConstantFormula(FormulaUID formula, const wchar_t* formulaName, const vector<ProgramSymbol>& args)
        : uid(formula)
        , name(formulaName)
        , args(args)
    {
    }

public:
    FormulaUID uid;
    wstring name;
    vector<ProgramSymbol> args;

    static ConstantFormula* get(FormulaUID formula, const wchar_t* name, const vector<ProgramSymbol>& args)
    {
        size_t hash;
        if (ConstantFormula* existing = getExisting(formula, name, args, hash))
        {
            return existing;
        }

        s_formulas.push_back(unique_ptr<ConstantFormula>(new ConstantFormula(formula, name, args)));
        s_lookup.insert(hash, nullptr, s_formulas.back().get());
        return s_formulas.back().get();
    }

    static ConstantFormula* get(FormulaUID formula, const wchar_t* name, vector<ProgramSymbol>&& args)
    {
        size_t hash;
        if (ConstantFormula* existing = getExisting(formula, name, args, hash))
        {
            return existing;
        }

        s_formulas.push_back(unique_ptr<ConstantFormula>(new ConstantFormula(formula, name, move(args))));
        s_lookup.insert(hash, nullptr, s_formulas.back().get());
        return s_formulas.back().get();
    }

    wstring toString() const
    {
        wstring out = name;
        out.append(TEXT("("));

        bool first = true;
        for (auto& arg : args)
        {
            if (!first)
            {
                out.append(TEXT(", "));
            }
            first = false;
            out.append(arg.toString());
        }

        out.append(TEXT(")"));
        return out;
    }

private:
    static ConstantFormula* getExisting(FormulaUID formula, const wchar_t* name, const vector<ProgramSymbol>& args, size_t& outHash)
    {
        outHash = makeHash(formula, args);
        auto range = s_lookup.find_range_by_hash(outHash);
        for (auto it = range.first; it != range.second; ++it)
        {
            if ((*it)->uid == formula)
            {
                vxy_assert((*it)->args.size() == args.size());
                bool match = true;
                for (int i = 0; i < args.size(); ++i)
                {
                    if ((*it)->args[i] != args[i])
                    {
                        match = false;
                        break;
                    }
                }

                if (match)
                {
                    return *it;
                }
            }
        }
        return nullptr;
    }

    static uint32_t makeHash(FormulaUID formula, const vector<ProgramSymbol>& args)
    {
        uint32_t out = hash<int>()(formula);
        for (auto& arg : args)
        {
            out ^= arg.hash();
        }
        return out;
    }

    struct Hash
    {
        bool operator()(const ConstantFormula* consA, const ConstantFormula* consB) const
        {
            if (consA == consB)
            {
                return true;
            }
            if (consA->uid != consB->uid || consA->args.size() != consB->args.size())
            {
                return false;
            }
            for (int i = 0; i < consA->args.size(); ++i)
            {
                if (consA->args[i] != consB->args[i])
                {
                    return false;
                }
            }
            return true;
        }

        uint32_t operator()(const ConstantFormula* cons) const
        {
            uint32_t out = hash<int>()(cons->uid);
            for (auto& arg : cons->args)
            {
                out ^= arg.hash();
            }
            return out;
        }
    };

    static hash_set<ConstantFormula*, Hash, Hash> s_lookup;
    static vector<unique_ptr<ConstantFormula>> s_formulas;
};

inline ProgramSymbol::ProgramSymbol(FormulaUID formula, const wchar_t* formulaName, const vector<ProgramSymbol>& args, bool negated, const IExternalFormulaProviderPtr& provider)
{
    setExternalProvider(provider);

    ConstantFormula* f = ConstantFormula::get(formula, formulaName, args);
    m_packed = encode(
        provider != nullptr ? ESymbolType::External : ESymbolType::Formula,
        reinterpret_cast<intptr_t>(f)
    ) | (negated ? 0 : 1);
    vxy_sanity(reinterpret_cast<ConstantFormula*>(decode(m_packed)) == f);
}

inline ProgramSymbol::ProgramSymbol(FormulaUID formula, const wchar_t* name, vector<ProgramSymbol>&& args, bool negated, const IExternalFormulaProviderPtr& provider)
{
    setExternalProvider(provider);

    ConstantFormula* f = ConstantFormula::get(formula, name, move(args));
    m_packed = encode(
        provider != nullptr ? ESymbolType::External : ESymbolType::Formula,
        reinterpret_cast<intptr_t>(f)
    ) | (negated ? 0 : 1);
    vxy_sanity(reinterpret_cast<ConstantFormula*>(decode(m_packed)) == f);
}

inline ProgramSymbol::ProgramSymbol(const ConstantFormula* formula, bool negated, const IExternalFormulaProviderPtr& provider)
{
    setExternalProvider(provider);

    m_packed = encode(
        provider != nullptr ? ESymbolType::External : ESymbolType::Formula,
        reinterpret_cast<intptr_t>(formula)
    ) | (negated ? 0 : 1);
    vxy_sanity(reinterpret_cast<ConstantFormula*>(decode(m_packed)) == formula);
}

inline ProgramSymbol::ProgramSymbol(const ProgramSymbol& other)
    : m_packed(other.m_packed)
{
    if (other.isExternalFormula())
    {
        setExternalProvider(other.getExternalFormulaProvider());
    }
    else if (other.isAbstract())
    {
        setAbstractRelation(other.getAbstractRelation());
    }
}

inline ProgramSymbol::~ProgramSymbol()
{
    destroySmartPointer();
}

inline void ProgramSymbol::destroySmartPointer()
{
    switch (getType())
    {
    case ESymbolType::External:
        {
            IExternalFormulaProviderPtr* provider = getExternalFormulaProviderPtr();
            (*provider).~IExternalFormulaProviderPtr();
        }
        break;
    case ESymbolType::Abstract:
        {
            GraphVertexRelationPtr* relation = getAbstractRelationPtr();
            (*relation).~GraphVertexRelationPtr();
        }
        break;

    default:
        // No need to do anything
        break;
    }
}

inline ProgramSymbol& ProgramSymbol::operator=(const ProgramSymbol& rhs)
{
    destroySmartPointer();

    m_packed = rhs.m_packed;
    if (isExternalFormula())
    {
        setExternalProvider(rhs.getExternalFormulaProvider());
    }
    else if (isAbstract())
    {
        setAbstractRelation(rhs.getAbstractRelation());
    }
    return *this;
}

inline ProgramSymbol ProgramSymbol::negatedFormula() const
{
    return ProgramSymbol(getFormula(), !isNegated(), getExternalFormulaProvider());
}

inline bool ProgramSymbol::containsAbstract() const
{
    switch (getType())
    {
    case ESymbolType::Abstract:
        return true;

    case ESymbolType::Formula:
    case ESymbolType::External:
        for (auto& arg : getFormula()->args)
        {
            if (arg.containsAbstract())
            {
                return true;
            }
        }
        return false;

    case ESymbolType::Integer:
    case ESymbolType::ID:
    case ESymbolType::Invalid:
        return false;

    default:
        vxy_fail_msg("unexpected symbol type");
        return false;
    }
}

inline ProgramSymbol ProgramSymbol::makeConcrete(int vertex) const
{
    switch (getType())
    {
    case ESymbolType::Integer:
    case ESymbolType::ID:
        return *this;

    case ESymbolType::Abstract:
        {
            int destVertex;
            if (!getAbstractRelation()->getRelation(vertex, destVertex))
            {
                return {};
            }
            return ProgramSymbol(destVertex);
        }

    case ESymbolType::Formula:
        {
            auto sig = getFormula();

            vector<ProgramSymbol> concreteArgs;
            concreteArgs.reserve(sig->args.size());
            for (auto& arg : sig->args)
            {
                ProgramSymbol concreteArg = arg.makeConcrete(vertex);
                if (!concreteArg.isValid())
                {
                    return {};
                }
                concreteArgs.push_back(move(concreteArg));
            }

            return ProgramSymbol(sig->uid, sig->name.c_str(), move(concreteArgs), isNegated());
        }

    case ESymbolType::External:
        {
            auto sig = getFormula();
            vector<ProgramSymbol> concreteArgs;
            concreteArgs.reserve(sig->args.size());
            for (auto& arg : sig->args)
            {
                ProgramSymbol concreteArg = arg.makeConcrete(vertex);
                if (!concreteArg.isValid())
                {
                    return {};
                }
                concreteArgs.push_back(move(concreteArg));
            }

            bool valid = getExternalFormulaProvider()->eval(concreteArgs);
            if (valid == isNegated())
            {
                return {};
            }

            return ProgramSymbol(sig->uid, sig->name.c_str(), move(concreteArgs), isNegated());
        }
    case ESymbolType::Invalid:
    default:
        vxy_fail_msg("Unexpected symbol type");
        return {};
    }
}

inline wstring ProgramSymbol::toString() const
{
    switch (getType())
    {
    case ESymbolType::Formula:
    case ESymbolType::External:
        return isNegated() ? (TEXT("~") + getFormula()->toString()) : getFormula()->toString();
    case ESymbolType::Integer:
        return {wstring::CtorSprintf(), TEXT("%d"), getInt()};
    case ESymbolType::ID:
        return getID();
    case ESymbolType::Abstract:
        return {wstring::CtorSprintf(), TEXT("$(%s)"), getAbstractRelation()->toString().c_str()};
    case ESymbolType::Invalid:
        return TEXT("<Invalid>");
    default:
        vxy_fail_msg("unexpected symbol type");
        return TEXT("<unexpected>");
    }
}

inline void ProgramSymbol::setExternalProvider(const IExternalFormulaProviderPtr& provider)
{
    new(m_smartPtrBytes) IExternalFormulaProviderPtr(provider);
}

inline void ProgramSymbol::setAbstractRelation(const GraphVertexRelationPtr& relation)
{
    new(m_smartPtrBytes) GraphVertexRelationPtr(relation);
}

inline GraphVertexRelationPtr* ProgramSymbol::getAbstractRelationPtr()
{
    return reinterpret_cast<GraphVertexRelationPtr*>(m_smartPtrBytes);
}

inline IExternalFormulaProviderPtr* ProgramSymbol::getExternalFormulaProviderPtr()
{
    return reinterpret_cast<IExternalFormulaProviderPtr*>(m_smartPtrBytes);
}

struct CompilerAtom
{
    ProgramSymbol symbol;
    bool isFact;
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

// Hashing for ProgramSymbol
template<>
struct hash<Vertexy::ProgramSymbol>
{
    inline size_t operator()(const Vertexy::ProgramSymbol& lit) const
    {
        return lit.hash();
    }
};

// Hashing for CompilerAtom
template<>
struct hash<Vertexy::CompilerAtom>
{
    inline size_t operator()(const Vertexy::CompilerAtom& atom) const
    {
        return atom.symbol.hash();
    }
};

}

