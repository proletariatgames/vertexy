// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ProgramTypes.h"
#include "ProgramSymbol.h"
#include "FormulaDomain.h"
#include "rules/RuleTypes.h"
#include "topology/algo/TopologySearchResponse.h"

namespace Vertexy
{

class RuleDatabase;
class ProgramCompiler;

class Instantiator;
using UInstantiator = unique_ptr<Instantiator>;

class WildcardTerm;
class VertexTerm;

class Term
{
public:
    using EVisitResponse = ETopologySearchResponse;

    virtual ~Term() {}

    void forChildren(const function<void(const Term*)>& visitor) const;
    void visit(const function<void(const Term*)>& visitor) const;

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const = 0;
    virtual void collectWildcards(vector<tuple<WildcardTerm*, bool>>& outWildcards, bool canEstablish = true) const;
    virtual wstring toString() const = 0;

    template<typename T>
    bool contains()
    {
        bool contained = false;
        visit([&](const Term* t)
        {
            if (dynamic_cast<const T*>(t) != nullptr)
            {
                contained = true;
                return EVisitResponse::Abort;
            }
            return EVisitResponse::Continue;
        });
        return contained;
    }

    template<typename T, typename Pred>
    bool contains(Pred&& pred)
    {
        bool contained = false;
        visit([&](const Term* t)
        {
            if (auto typed = dynamic_cast<const T*>(t))
            {
                if (pred(typed))
                {
                    contained = true;
                }
                return EVisitResponse::Abort;
            }           
            return EVisitResponse::Continue;
        });
        return contained;
    }

    virtual unique_ptr<Term> clone() const = 0;
    virtual void replace(const function<unique_ptr<Term>(Term*)>& visitor) = 0;

protected:
    template<typename T>
    bool maybeReplaceChild(unique_ptr<T>& child, const function<unique_ptr<Term>(Term*)>& visitor)
    {
        auto t = visitor(child.get());
        if (t != nullptr)
        {
            T* downcast = static_cast<T*>(t.detach());
            child.reset(downcast);

            return true;
        }
        return false;
    }
};

using UTerm = unique_ptr<Term>;

class LiteralTerm : public Term
{
public:
    using AbstractOverrideMap = hash_map<ProgramSymbol*, int>;

    virtual ProgramSymbol eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const = 0;
    virtual UInstantiator instantiate(ProgramCompiler& compiler, bool canBeAbstract, const ITopologyPtr& topology);
    virtual bool match(const ProgramSymbol& sym, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex);
    virtual bool containsAbstracts() const { return false; }
    virtual size_t hash() const = 0;
    virtual bool operator==(const LiteralTerm& rhs) const = 0;
    bool operator !=(const LiteralTerm& rhs) const { return !operator==(rhs); }

    bool createWildcardReps(WildcardMap& bound);

    virtual wstring toString() const override;
};

using ULiteralTerm = unique_ptr<LiteralTerm>;

class WildcardTerm : public LiteralTerm
{
public:
    WildcardTerm(ProgramWildcard param);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(Term*)>& visitor) override {}    
    virtual UTerm clone() const override;
    virtual void collectWildcards(vector<tuple<WildcardTerm*, bool>>& outWildcards, bool canEstablish = true) const override;
    virtual bool match(const ProgramSymbol& sym, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) override;
    virtual bool containsAbstracts() const override;
    virtual size_t hash() const override
    {
        return eastl::hash<ProgramWildcard>()(wildcard);
    }
    virtual ProgramSymbol eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const override;
    virtual wstring toString() const override;
    virtual bool operator==(const LiteralTerm& rhs) const override;

    ProgramWildcard wildcard;
    bool isBinder = false;
    shared_ptr<ProgramSymbol> sharedBoundRef;
};

using UWildcardTerm = unique_ptr<WildcardTerm>;

class SymbolTerm : public LiteralTerm
{
public:
    SymbolTerm(const ProgramSymbol& sym);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(Term*)>& visitor) override {}
    virtual UTerm clone() const override;
    virtual ProgramSymbol eval(const AbstractOverrideMap&, const ProgramSymbol& boundVertex) const override  { return sym; }
    virtual UInstantiator instantiate(ProgramCompiler& compiler, bool canBeAbstract, const ITopologyPtr& topology) override;
    virtual bool operator==(const LiteralTerm& rhs) const override;
    virtual size_t hash() const override
    {
        return eastl::hash<ProgramSymbol>()(sym);
    }

    ProgramSymbol sym;
};

class VertexTerm : public LiteralTerm
{
public:
    VertexTerm();

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(Term*)>& visitor) override {}
    virtual UTerm clone() const override;
    virtual bool match(const ProgramSymbol& sym, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) override;
    virtual bool containsAbstracts() const override { return true; }
    virtual ProgramSymbol eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const override;
    virtual bool operator==(const LiteralTerm& rhs) const override;
    virtual size_t hash() const override { return 0; }
};

class UnaryOpTerm : public LiteralTerm
{
public:
    UnaryOpTerm(EUnaryOperatorType op, ULiteralTerm&& child);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(Term*)>& visitor) override;
    virtual UTerm clone() const override;
    virtual ProgramSymbol eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const override;
    virtual bool containsAbstracts() const override;
    virtual wstring toString() const override;
    virtual size_t hash() const override;
    virtual bool operator==(const LiteralTerm& rhs) const override;

    EUnaryOperatorType op;
    ULiteralTerm child;
};

class BinaryOpTerm : public LiteralTerm
{
public:
    BinaryOpTerm(EBinaryOperatorType op, ULiteralTerm&& lhs, ULiteralTerm&& rhs);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<UTerm(Term*)>& visitor) override;
    virtual void collectWildcards(vector<tuple<WildcardTerm*, bool>>& outWildcards, bool canEstablish) const override;
    virtual ProgramSymbol eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const override;
    virtual bool containsAbstracts() const override;
    virtual UTerm clone() const override;
    virtual wstring toString() const override;
    virtual UInstantiator instantiate(ProgramCompiler& compiler, bool canBeAbstract, const ITopologyPtr& topology) override;
    virtual size_t hash() const override;
    virtual bool operator==(const LiteralTerm& rhs) const override;

    EBinaryOperatorType op;
    ULiteralTerm lhs;
    ULiteralTerm rhs;
};

class LinearTerm : public LiteralTerm
{
public:
    LinearTerm(ULiteralTerm&& wildcardTerm, int offset, int multiplier);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<UTerm(Term*)>& visitor) override;
    virtual void collectWildcards(vector<tuple<WildcardTerm*, bool>>& outWildcards, bool canEstablish) const override;
    virtual bool match(const ProgramSymbol& sym, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) override;
    virtual ProgramSymbol eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const override;
    virtual bool containsAbstracts() const override;
    virtual UTerm clone() const override;
    virtual wstring toString() const override;
    virtual size_t hash() const override;
    virtual bool operator==(const LiteralTerm& rhs) const override;
    
    ULiteralTerm childTerm;
    int offset;
    int multiplier;
};

using UBinaryOpTerm = unique_ptr<BinaryOpTerm>;

class DomainTerm : public Term
{
public:
    using AbstractOverrideMap = hash_map<ProgramSymbol*, int>;

    virtual bool eval(ValueSet& inOutMask, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const = 0;
    virtual bool match(const ValueSet& mask, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) const = 0;
    virtual bool containsAbstracts() const = 0;
    virtual size_t hash() const = 0;
};

using UDomainTerm = unique_ptr<DomainTerm>;

class ExplicitDomainTerm : public DomainTerm
{
public:
    explicit ExplicitDomainTerm(ValueSet&& mask);
    
    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual wstring toString() const override;
    virtual unique_ptr<Term> clone() const override;
    virtual void replace(const function<unique_ptr<Term>(Term*)>& visitor) override {}
    virtual bool containsAbstracts() const override { return false; }
    
    virtual bool eval(ValueSet& inOutMask, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const override;
    virtual bool match(const ValueSet& mask, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) const override;
    virtual size_t hash() const override;
    
    ValueSet mask;
};

class SubscriptDomainTerm : public DomainTerm
{
public:
    SubscriptDomainTerm(const FormulaDomainValueArray& array, ULiteralTerm&& subscriptTerm);
    
    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual wstring toString() const override;
    virtual unique_ptr<Term> clone() const override;
    virtual void replace(const function<unique_ptr<Term>(Term*)>& visitor) override;
    virtual bool containsAbstracts() const override;

    virtual bool eval(ValueSet& inOutMask, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const override;
    virtual bool match(const ValueSet& mask, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) const override;
    virtual size_t hash() const override;

    FormulaDomainValueArray array;
    ULiteralTerm subscriptTerm;
};

class UnionDomainTerm : public DomainTerm
{
public:
    UnionDomainTerm(UDomainTerm&& left, UDomainTerm&& right);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual wstring toString() const override;
    virtual unique_ptr<Term> clone() const override;
    virtual void replace(const function<unique_ptr<Term>(Term*)>& visitor) override;
    virtual bool containsAbstracts() const override;

    virtual bool eval(ValueSet& inOutMask, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const override;
    virtual bool match(const ValueSet& mask, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) const override;
    virtual size_t hash() const override;

protected:
    UDomainTerm left;
    UDomainTerm right;
};

class FunctionTerm : public LiteralTerm
{
public:
    FunctionTerm(FormulaUID functionUID, const wchar_t* functionName, int domainSize, vector<ULiteralTerm>&& arguments, vector<UDomainTerm>&& domainTerms, bool negated, const IExternalFormulaProviderPtr& provider);

    virtual void collectWildcards(vector<tuple<WildcardTerm*, bool>>& outWildcards, bool canEstablish = true) const override;
    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(Term*)>& visitor) override;
    virtual ProgramSymbol eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const override;
    virtual UTerm clone() const override;
    virtual UInstantiator instantiate(ProgramCompiler& compiler, bool canBeAbstract, const ITopologyPtr& topology) override;
    virtual bool match(const ProgramSymbol& sym, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) override;
    virtual bool containsAbstracts() const override;
    virtual wstring toString() const override;
    virtual size_t hash() const override;
    virtual bool operator==(const LiteralTerm& rhs) const override;

    bool domainContainsAbstracts() const;

    ValueSet getDomain(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const;
    
    FormulaUID functionUID;
    const wchar_t* functionName;
    int domainSize;
    vector<ULiteralTerm> arguments;
    vector<UDomainTerm> domainTerms;
    IExternalFormulaProviderPtr provider;
    bool negated;
    bool assignedToFact = false;
    bool recursive = false;
    ValueSet boundMask;
};

using UFunctionTerm = unique_ptr<FunctionTerm>;

class HeadTerm : public Term
{
public:
    using AbstractOverrideMap = LiteralTerm::AbstractOverrideMap;

    virtual bool mustBeConcrete(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const = 0;
    virtual void bindAsFacts(ProgramCompiler& compiler, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, const ITopologyPtr& topology) = 0;
    virtual vector<ProgramSymbol> eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, bool& isNormalRule) = 0;
    virtual ERuleHeadType getHeadType() const = 0;
};

using UHeadTerm = unique_ptr<HeadTerm>;

class FunctionHeadTerm : public HeadTerm
{
public:
    FunctionHeadTerm(FormulaUID inUID, const wchar_t* inName, int domainSize, vector<ULiteralTerm>&& arguments, vector<UDomainTerm>&& domainTerms);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(Term*)>& visitor) override;
    virtual UTerm clone() const override;

    virtual bool mustBeConcrete(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const override;
    virtual void bindAsFacts(ProgramCompiler& compiler, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, const ITopologyPtr& topology) override;
    virtual vector<ProgramSymbol> eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, bool& isNormalRule) override;
    virtual wstring toString() const override;
    virtual ERuleHeadType getHeadType() const override { return ERuleHeadType::Normal; }

    ProgramSymbol evalSingle(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const;
    ValueSet getDomain(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const;

    FormulaUID functionUID;
    const wchar_t* functionName;
    int domainSize;
    vector<ULiteralTerm> arguments;
    vector<UDomainTerm> domainTerms;
};

using UFunctionHeadTerm = unique_ptr<FunctionHeadTerm>;

class DisjunctionTerm : public HeadTerm
{
public:
    DisjunctionTerm(vector<UFunctionHeadTerm>&& children);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(Term*)>& visitor) override;
    virtual UTerm clone() const override;

    virtual bool mustBeConcrete(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const override;
    virtual void bindAsFacts(ProgramCompiler& compiler, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, const ITopologyPtr& topology) override;
    virtual vector<ProgramSymbol> eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, bool& isNormalRule) override;
    virtual wstring toString() const override;
    virtual ERuleHeadType getHeadType() const override { return ERuleHeadType::Disjunction; }

    vector<UFunctionHeadTerm> children;
};

class ChoiceTerm : public HeadTerm
{
public:
    ChoiceTerm(UFunctionHeadTerm&& term);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(Term*)>& visitor) override;
    virtual UTerm clone() const override;

    virtual bool mustBeConcrete(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const override;
    virtual void bindAsFacts(ProgramCompiler& compiler, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, const ITopologyPtr& topology) override;
    virtual vector<ProgramSymbol> eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, bool& isNormalRule) override;
    virtual wstring toString() const override;
    virtual ERuleHeadType getHeadType() const override { return ERuleHeadType::Choice; }

    UFunctionHeadTerm subTerm;
};

class RuleStatement
{
public:
    RuleStatement(UHeadTerm&& head, vector<ULiteralTerm>&& body);
    RuleStatement(UHeadTerm&& head) : head(move(head)) {}

    URuleStatement clone() const;

    template<typename T=Term>
    void replace(const function<UTerm(T* visitor)>& visitor)
    {
        replaceInHead(visitor);
        replaceInBody(visitor);
    }

    wstring toString() const;

    template<typename T=Term>
    void replaceInHead(const function<UTerm(T* visitor)>& visitor)
    {
        if (auto ht = dynamic_cast<T*>(head.get()))
        {
            auto newHead = visitor(ht);
            if (newHead != nullptr)
            {
                head = UHeadTerm(move(static_cast<HeadTerm*>(newHead.detach())));
                return;
            }
        }

        if (head != nullptr) head->replace([&](Term* term)
        {
           if (auto t = dynamic_cast<T*>(term))
           {
               return visitor(t);
           }
            return UTerm(nullptr);
        });
    }

    template<typename T=Term>
    void replaceInBody(const function<UTerm(T* visitor)>& visitor)
    {
        for (auto&& bodyTerm : body)
        {
            if (auto bt = dynamic_cast<T*>(bodyTerm.get()))
            {
                auto newBodyTerm = visitor(bt);
                if (newBodyTerm != nullptr)
                {
                   bodyTerm = ULiteralTerm(move(static_cast<LiteralTerm*>(newBodyTerm.detach())));
                   continue;
                }
            }

            bodyTerm->replace([&](Term* term)
            {
                if (auto t = dynamic_cast<T*>(term))
                {
                    return visitor(t);
                }
                return UTerm(nullptr);
            });
        }
    }

    template<typename T=Term>
    bool visit(const function<Term::EVisitResponse(const T*)>& visitor) const
    {
        if (!visitHead<T>(visitor))
        {
            return false;
        }
        return visitBody<T>(visitor);
    }

    template<typename T=Term>
    void visit(const function<void(const T*)>& visitor) const
    {
        visitHead<T>(visitor);
        visitBody<T>(visitor);
    }

    template<typename T=Term>
    bool visitHead(const function<Term::EVisitResponse(const T*)>& visitor) const
    {
        if (head == nullptr)
        {
            return true;
        }

        return head->visit([&](const Term* term)
        {
           if (auto f = dynamic_cast<const T*>(term))
           {
               return visitor(f);
            }
            return Term::EVisitResponse::Continue;
        });
    }

    template<typename T=Term>
    void visitHead(const function<void(const T*)>& visitor) const
    {
        if (head != nullptr) head->visit([&](const Term* term)
        {
            if (auto f = dynamic_cast<const T*>(term))
            {
                visitor(f);
            }
        });
    }

    template<typename T>
    bool visitBody(const function<Term::EVisitResponse(const T*)>& visitor) const
    {
        for (auto& bodyTerm : body)
        {
            bool ret = bodyTerm->visit([&](const Term* term)
            {
                if (auto f = dynamic_cast<const T*>(term))
                {
                    return visitor(f);
                }
                return Term::EVisitResponse::Continue;
            });

            if (!ret)
            {
                return false;
            }
        }
        return true;
    }

    template<typename T>
    void visitBody(const function<void(const T*)>& visitor) const
    {
        for (auto& bodyTerm : body)
        {
            bodyTerm->visit([&](const Term* term)
            {
                if (auto f = dynamic_cast<const T*>(term))
                {
                    visitor(f);
                }
            });
        }
    }

    template<typename T>
    bool headContains() const
    {
        return head->contains<T>();
    }

    template<typename T>
    bool bodyContains() const
    {
        for (auto& bodyTerm : body)
        {
            if (bodyTerm->contains<T>())
            {
                return true;
            }
        }
        return false;
    }

    template<typename T>
    bool contains() const
    {
        return headContains<T>() || !bodyContains<T>();
    }

    UHeadTerm head;
    vector<ULiteralTerm> body;
};

};