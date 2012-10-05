// Copyright 2012 by Laszlo Nagy [see file MIT-LICENSE]

#include "ModuleAnalysis.hpp"
#include "ScopeAnalysis.hpp"

#include <iterator>
#include <map>

#include <clang/AST/AST.h>
#include <clang/AST/RecursiveASTVisitor.h>

#include <boost/noncopyable.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/range.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm/transform.hpp>
#include <boost/range/algorithm/count_if.hpp>


namespace {

// Report function for pseudo constness analysis.
void ReportVariablePseudoConstness(clang::DiagnosticsEngine & DE, clang::DeclaratorDecl const * const V) {
    static char const * const Message = "variable '%0' could be declared as const";
    unsigned const Id = DE.getCustomDiagID(clang::DiagnosticsEngine::Warning, Message);
    clang::DiagnosticBuilder DB = DE.Report(V->getLocStart(), Id);
    DB << V->getNameAsString();
    DB.setForceEmit();
}

void ReportFunctionPseudoConstness(clang::DiagnosticsEngine & DE, clang::DeclaratorDecl const * const V) {
    static char const * const Message = "function '%0' could be declared as const";
    unsigned const Id = DE.getCustomDiagID(clang::DiagnosticsEngine::Warning, Message);
    clang::DiagnosticBuilder DB = DE.Report(V->getLocStart(), Id);
    DB << V->getNameAsString();
    DB.setForceEmit();
}

void ReportFunctionPseudoStaticness(clang::DiagnosticsEngine & DE, clang::DeclaratorDecl const * const V) {
    static char const * const Message = "function '%0' could be declared as static";
    unsigned const Id = DE.getCustomDiagID(clang::DiagnosticsEngine::Warning, Message);
    clang::DiagnosticBuilder DB = DE.Report(V->getLocStart(), Id);
    DB << V->getNameAsString();
    DB.setForceEmit();
}

// Report function for debug functionality.
void ReportVariableDeclaration(clang::DiagnosticsEngine & DE, clang::DeclaratorDecl const * const V) {
    static char const * const Message = "variable '%0' declared here";
    unsigned const Id = DE.getCustomDiagID(clang::DiagnosticsEngine::Note, Message);
    clang::DiagnosticBuilder DB = DE.Report(V->getLocStart(), Id);
    DB << V->getNameAsString();
    DB.setForceEmit();
}

void ReportFunctionDeclaration(clang::DiagnosticsEngine & DE, clang::FunctionDecl const * const F) {
    static char const * const Message = "function '%0' declared here";
    unsigned const Id = DE.getCustomDiagID(clang::DiagnosticsEngine::Note, Message);
    clang::DiagnosticBuilder DB = DE.Report(F->getSourceRange().getBegin(), Id);
    DB << F->getNameAsString();
    DB.setForceEmit();
}


typedef std::set<clang::DeclaratorDecl const *> Variables;
typedef std::set<clang::CXXMethodDecl const *> Methods;


// Pseudo constness analysis detects what variable can be declare as const.
// This analysis runs through multiple scopes. We need to store the state of
// the ongoing analysis. Once the variable was changed can't be const.
class PseudoConstnessAnalysisState : public boost::noncopyable {
public:
    PseudoConstnessAnalysisState()
        : boost::noncopyable()
        , Candidates()
        , Changed()
    { }

    void Eval(ScopeAnalysis const & Analysis, clang::DeclaratorDecl const * const V) {
        if (Analysis.WasChanged(V)) {
            Candidates.erase(V);
            Changed.insert(V);
        } else if (Analysis.WasReferenced(V)) {
            if (Changed.end() == Changed.find(V)) {
                if (! IsConst(*V)) {
                    Candidates.insert(V);
                }
            }
        }
    }

    void GenerateReports(clang::DiagnosticsEngine & DE) const {
        boost::for_each(Candidates,
            boost::bind(ReportVariablePseudoConstness, boost::ref(DE), _1));
    }

private:
    static bool IsConst(clang::DeclaratorDecl const & D) {
        return (D.getType().getNonReferenceType().isConstQualified());
    }

private:
    Variables Candidates;
    Variables Changed;
};


// method to copy variables out from declaration context
Variables GetVariablesFromContext(clang::DeclContext const * const F, bool const WithoutArgs = false) {
    Variables Result;
    for (clang::DeclContext::decl_iterator It(F->decls_begin()), End(F->decls_end()); It != End; ++It ) {
        if (clang::VarDecl const * const D = clang::dyn_cast<clang::VarDecl const>(*It)) {
            if (! (WithoutArgs && (clang::dyn_cast<clang::ParmVarDecl const>(D)))) {
                Result.insert(D);
            }
        }
    }
    return Result;
}

// method to copy variables out from class declaration 
Variables GetVariablesFromRecord(clang::RecordDecl const * const F) {
    Variables Result;
    for (clang::RecordDecl::field_iterator It(F->field_begin()), End(F->field_end()); It != End; ++It ) {
        if (clang::FieldDecl const * const D = clang::dyn_cast<clang::FieldDecl const>(*It)) {
            Result.insert(D);
        }
    }
    return Result;
}

// method to copy methods out from class declaration 
Methods GetMethodsFromRecord(clang::CXXRecordDecl const * const F) {
    Methods Result;
    for (clang::CXXRecordDecl::method_iterator It(F->method_begin()), End(F->method_end()); It != End; ++It) {
        if (clang::CXXMethodDecl const * const D = clang::dyn_cast<clang::CXXMethodDecl const>(*It)) {
            Result.insert(D);
        }
    }
    return Result;
}


// Base class for analysis. Implement function declaration visitor, which visit
// functions only once. The traversal algorithm is calling all methods, which is
// not desired. In case of a CXXMethodDecl, it was calling the VisitFunctionDecl
// and the VisitCXXMethodDecl as well. This dispatching is reworked in this class.
class ModuleVisitor
    : public boost::noncopyable
    , public clang::RecursiveASTVisitor<ModuleVisitor> {
public:
    typedef boost::shared_ptr<ModuleVisitor> Ptr;
    static ModuleVisitor::Ptr CreateVisitor(Target);

    virtual ~ModuleVisitor()
    { }

public:
    // public visitor method.
    bool VisitFunctionDecl(clang::FunctionDecl const * F) {
        if (! (F->isThisDeclarationADefinition()))
            return true;

        if (clang::CXXMethodDecl const * const D = clang::dyn_cast<clang::CXXMethodDecl const>(F)) {
            OnCXXMethodDecl(D);
        } else {
            OnFunctionDecl(F);
        }
        return true;
    }

public:
    // interface methods with different visibilities.
    virtual void Dump(clang::DiagnosticsEngine &) const = 0;

protected:
    virtual void OnFunctionDecl(clang::FunctionDecl const *) = 0;
    virtual void OnCXXMethodDecl(clang::CXXMethodDecl const *) = 0;
};


class DebugFunctionDeclarations
    : public ModuleVisitor {
protected:
    void OnFunctionDecl(clang::FunctionDecl const * const F) {
        Functions.insert(F);
    }

    void OnCXXMethodDecl(clang::CXXMethodDecl const * const F) {
        Functions.insert(F);
    }

    void Dump(clang::DiagnosticsEngine & DE) const {
        boost::for_each(Functions,
            boost::bind(ReportFunctionDeclaration, boost::ref(DE), _1));
    }

protected:
    std::set<clang::FunctionDecl const *> Functions;
};


class DebugVariableDeclarations
    : public ModuleVisitor {
private:
    void OnFunctionDecl(clang::FunctionDecl const * const F) {
        boost::copy(GetVariablesFromContext(F),
            std::insert_iterator<Variables>(Result, Result.begin()));
    }

    void OnCXXMethodDecl(clang::CXXMethodDecl const * const F) {
        boost::copy(GetVariablesFromContext(F, F->isVirtual()),
            std::insert_iterator<Variables>(Result, Result.begin()));
        boost::copy(GetVariablesFromRecord(F->getParent()->getCanonicalDecl()),
            std::insert_iterator<Variables>(Result, Result.begin()));
    }

    void Dump(clang::DiagnosticsEngine & DE) const {
        boost::for_each(Result,
            boost::bind(ReportVariableDeclaration, boost::ref(DE), _1));
    }

private:
    Variables Result;
};


class DebugVariableUsages
    : public DebugFunctionDeclarations {
private:
    static void ReportVariableUsage(clang::DiagnosticsEngine & DE, clang::FunctionDecl const * F) {
        ScopeAnalysis const & Analysis = ScopeAnalysis::AnalyseThis(*(F->getBody()));
        Analysis.DebugReferenced(DE);
    }

    void Dump(clang::DiagnosticsEngine & DE) const {
        boost::for_each(Functions,
            boost::bind(ReportVariableUsage, boost::ref(DE), _1));
    }
};


class DebugVariableChanges
    : public DebugFunctionDeclarations {
private:
    static void ReportVariableUsage(clang::DiagnosticsEngine & DE, clang::FunctionDecl const * F) {
        ScopeAnalysis const & Analysis = ScopeAnalysis::AnalyseThis(*(F->getBody()));
        Analysis.DebugChanged(DE);
    }

    void Dump(clang::DiagnosticsEngine & DE) const {
        boost::for_each(Functions,
            boost::bind(ReportVariableUsage, boost::ref(DE), _1));
    }
};


class AnalyseVariableUsage
    : public ModuleVisitor {
private:
    void OnFunctionDecl(clang::FunctionDecl const * const F) {
        ScopeAnalysis const & Analysis = ScopeAnalysis::AnalyseThis(*(F->getBody()));
        boost::for_each(GetVariablesFromContext(F),
            boost::bind(&PseudoConstnessAnalysisState::Eval, &State, boost::cref(Analysis), _1));
    }

    void OnCXXMethodDecl(clang::CXXMethodDecl const * const F) {
        clang::CXXRecordDecl const * const RecordDecl =
            F->getParent()->getCanonicalDecl();
        Variables const MemberVariables = GetVariablesFromRecord(RecordDecl);
        // check variables first,
        ScopeAnalysis const & Analysis = ScopeAnalysis::AnalyseThis(*(F->getBody()));
        boost::for_each(GetVariablesFromContext(F, F->isVirtual()),
            boost::bind(&PseudoConstnessAnalysisState::Eval, &State, boost::cref(Analysis), _1));
        boost::for_each(MemberVariables,
            boost::bind(&PseudoConstnessAnalysisState::Eval, &State, boost::cref(Analysis), _1));
        // then check the method itself.
        if ((! F->isVirtual()) &&
            (! F->isStatic()) &&
            (! F->isConst()) &&
            F->isUserProvided() &&
            IsCXXMethodDeclOnly(F)
        ) {
            Methods const MemberFunctions = GetMethodsFromRecord(RecordDecl);
            // check the constness first..
            unsigned int const MemberChanges =
                boost::count_if(MemberVariables,
                    boost::bind(&ScopeAnalysis::WasChanged, &Analysis, _1));
            unsigned int const FunctionChanges =
                boost::count_if(
                    MemberFunctions |
                        boost::adaptors::filtered(IsMutatingMethod()),
                    boost::bind(&ScopeAnalysis::WasReferenced, &Analysis, _1));
            // if it looks const, it might be even static..
            if ((0 == MemberChanges) && (0 == FunctionChanges)) {
                unsigned int const MemberAccess =
                    boost::count_if(MemberVariables,
                        boost::bind(&ScopeAnalysis::WasReferenced, &Analysis, _1));
                unsigned int const FunctionAccess =
                    boost::count_if(
                        MemberFunctions |
                            boost::adaptors::filtered(IsMemberMethod()),
                        boost::bind(&ScopeAnalysis::WasReferenced, &Analysis, _1));
                if ((0 == MemberAccess) && (0 == FunctionAccess)) {
                    StaticCandidates.insert(F);
                } else {
                    ConstCandidates.insert(F);
                }
            }
        }
    }

    void Dump(clang::DiagnosticsEngine & DE) const {
        State.GenerateReports(DE);
        boost::for_each(ConstCandidates,
            boost::bind(ReportFunctionPseudoConstness, boost::ref(DE), _1));
        boost::for_each(StaticCandidates,
            boost::bind(ReportFunctionPseudoStaticness, boost::ref(DE), _1));
    }

private:
    struct IsMutatingMethod {
        bool operator()(clang::CXXMethodDecl const * const F) const {
            return (! F->isStatic()) && (! F->isConst());
        }
    };

    struct IsMemberMethod {
        bool operator()(clang::CXXMethodDecl const * const F) const {
            return (! F->isStatic());
        }
    };

    static bool IsCXXMethodDeclOnly(clang::CXXMethodDecl const * const F) {
        return
            (0 == clang::dyn_cast<clang::CXXConstructorDecl const>(F))
        &&  (0 == clang::dyn_cast<clang::CXXConversionDecl const>(F))
        &&  (0 == clang::dyn_cast<clang::CXXDestructorDecl const>(F));
    }

private:
    PseudoConstnessAnalysisState State;
    Methods ConstCandidates;
    Methods StaticCandidates;
};


ModuleVisitor::Ptr ModuleVisitor::CreateVisitor(Target const State) {
    switch (State) {
    case FuncionDeclaration :
        return ModuleVisitor::Ptr( new DebugFunctionDeclarations() );
    case VariableDeclaration :
        return ModuleVisitor::Ptr( new DebugVariableDeclarations() );
    case VariableChanges:
        return ModuleVisitor::Ptr( new DebugVariableChanges() );
    case VariableUsages :
        return ModuleVisitor::Ptr( new DebugVariableUsages() );
    case PseudoConstness :
        return ModuleVisitor::Ptr( new AnalyseVariableUsage() );
    }
}

} // namespace anonymous


ModuleAnalysis::ModuleAnalysis(clang::CompilerInstance const & Compiler, Target T)
    : boost::noncopyable()
    , clang::ASTConsumer()
    , Reporter(Compiler.getDiagnostics())
    , State(T)
{ }

void ModuleAnalysis::HandleTranslationUnit(clang::ASTContext & Ctx) {
    ModuleVisitor::Ptr V = ModuleVisitor::CreateVisitor(State);
    V->TraverseDecl(Ctx.getTranslationUnitDecl());
    V->Dump(Reporter);
}
