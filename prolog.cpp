#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <sstream>

#include "stream_iterator.hpp"
#include "templateio.hpp"
#include "profile.hpp"

using namespace std;

//============================================================================
// Logic Language Parser.
//
// A simple parser for a Prolog like language.

//----------------------------------------------------------------------------
// Syntactic Structure.
//
// A global set of names keeps string comparisons for atoms and varibles to
// a single iterator (pointer) comparison. A single type is used for atoms
// and structs, which is combined with a variable type in an expression 
// supertype. Clauses combine heads and goals, and keep track of repeated 
// variables in the head for efficient post-unification cycle checking.

using name_t = set<string>::const_iterator;

struct type_expression {
    virtual ostream& operator<< (ostream& out) const = 0;
};

struct type_variable : public type_expression {
    name_t const name;

    type_variable(name_t n) : name(n) {}

    virtual ostream& operator<< (ostream& out) const override {
        out << *name;
        return out;
    }
};

struct type_struct : public type_expression {
    name_t const name;
    vector<type_expression*> const args;

    template <typename As>
    type_struct(name_t n, As&& as) : name(n), args(forward<As>(as)) {}

    ostream& operator<< (ostream& out) const override {
        out << *name << "(";
        for (auto i = args.cbegin(); i != args.cend(); ++i) {
            (*i)->operator<<(out);
            auto j = i;
            if (++j != args.end()) {
                out << ", ";
            }
        }
        out << ")";
        return out;
    }
};

ostream& operator<< (ostream& out, type_expression* exp) {
    exp->operator<<(out);
    return out;
}

struct clause {
    type_struct* const head;
    vector<type_struct*> const impl;
    set<type_variable*> const reps;

    template <typename Is, typename Rs>
    clause(type_struct* h, Is&& is, Rs&& rs) : head(h), impl(forward<Is>(is)),
        reps(forward<Rs>(rs)) {}
};

ostream& operator<< (ostream& out, clause* cls) {
    cls->head->operator<<(out);
    auto f = cls->impl.cbegin();
    auto const l = cls->impl.cend();
    if (f != l) {
        out << " :-" << endl;
        for (auto i = f; i != l; ++i) {
            out << "\t";
            (*i)->operator<<(out);
            auto j = i;
            if (++j != l) {
                out << "," << endl;
            }
        }
    }
    out << ".";

    auto g = cls->reps.cbegin();
    auto const m = cls->reps.cend();
    if (g != m) {
        out << " [";
        for (auto i = g; i != m; ++i) {
            (*i)->operator<<(out);
            auto j = i;
            if (++j != m) {
                out << ", ";
            }
        }
        out << "]";
    }
    out << endl;

    return out;
}

//----------------------------------------------------------------------------
// Parser State
//
// Making this uncopyable prevents backtracking being used, as the 'attempt'
// combinator will try and make a copy of the inherited attribute for 
// backtracking. This can be seen as a constraint on the parser (to ensure
// performance) or a safety measure to make sure we deal with copying if we 
// want backtracking.

using var_t = map<string const*, type_variable*>::const_iterator;

struct parser_state {
    set<string> names; // global name table
    map<string const*, type_variable*> variables; 
    set<type_variable*> repeated;
    set<type_variable*> repeated_in_goal;

    parser_state() {};
    parser_state(parser_state const&) = delete;
    parser_state& operator= (parser_state const&) = delete;

    name_t get_name(string const& name) {
        name_t const n = names.find(name);
        if (n == names.end()) {
            return names.insert(name).first;
        } else {
            return n;
        }
    }
};

//----------------------------------------------------------------------------
// Grammar
//
// Function objects get passed the results of the sub-parsers, along with the 
// inherited attribute. As the parser-combinators are templated, the inherited
// attribute is whatever type is passed as the final argument to the parser.

struct return_variable {
    return_variable() {}
    void operator() (type_variable** res, string const& name, parser_state* st) const {
        name_t const n = st->get_name(name);
        var_t i = st->variables.find(&(*n));
        if (i == st->variables.end()) {
            type_variable* const var = new type_variable(n);
            st->variables.insert(make_pair(&(*n), var));
            *res = var;
        } else {
            st->repeated.insert(i->second);
            *res = i->second;
        }
    }
} const return_variable;

struct return_args {
    return_args() {}
    void operator() (vector<type_expression*>* res, int n, type_variable* var, type_struct* str, parser_state*) const {
        switch(n) {
            case 0:
                res->push_back(var);
                break;
            case 1:
                res->push_back(str);
                break;
        }
    }
} const return_args;

struct return_struct {
    return_struct() {}
    void operator() (type_struct** res, string const& name, vector<type_expression*>& args, parser_state* st) const {
        name_t n = st->get_name(name);
        *res = new type_struct(n, args);
    }
} const return_struct;

struct return_head {
    return_head() {}
    void operator() (type_struct** res, type_struct* str, parser_state* st) const {
        *res = str;
        st->repeated_in_goal = st->repeated;
    }
} const return_head;

struct return_goal {
    return_goal() {}
    void operator() (vector<type_struct*>* res, type_struct* impl, parser_state*) const {
        res->push_back(impl);
    }
} const return_goal;

struct return_clause {
    return_clause() {}
    void operator() (vector<clause*>* res, type_struct* head, vector<type_struct*>& impl, parser_state* st) const {
        res->push_back(new clause(head, impl, st->repeated_in_goal));
        st->variables.clear();
        st->repeated.clear();
        st->repeated_in_goal.clear();
    }
} const return_clause;

struct return_goals {
    return_goals() {}
    void operator() (vector<clause*>* res, vector<type_struct*>& impl, parser_state* st) const {
        vector<type_expression*> vars;
        for (auto v : st->variables) {
            vars.push_back(v.second);
        }
        res->push_back(new clause(
            new type_struct(st->get_name("goal"), vars), impl, set<type_variable*> {}
        ));

        st->variables.clear();
        st->repeated.clear();
        st->repeated_in_goal.clear();
    }
} const return_goals;

//----------------------------------------------------------------------------
// Parser
//
// The parsers and user grammar are all stateless, so can be const objects and 
// still thread safe. The final composed parser is a regular procedure, all state 
// is either in the state object passed in, or in the returned values.

auto const atom_tok = tokenise(accept(is_lower) && many(accept(is_alnum || is_char('_'))));
auto const var_tok = tokenise(accept(is_upper || is_char('_')) && many(accept(is_alnum || is_char('_'))));
auto const open_tok = tokenise(accept(is_char('(')));
auto const close_tok = tokenise(accept(is_char(')')));
auto const sep_tok = tokenise(accept(is_char(',')));
auto const impl_tok = tokenise(accept_str(":-"));
auto const end_tok = tokenise(accept(is_char('.')));
auto const comment_tok = tokenise(accept(is_char('#')) && many(accept(is_print)) && accept(is_eol));

// the "definitions" help clean up the EBNF output in error reports
auto const variable = define("variable", all(return_variable, var_tok));
auto const atom = define("atom", atom_tok);

// a function from a parser to a parser.
pstream_handle<type_struct*, parser_state> recursive_structure(pstream_handle<type_struct*, parser_state> const s) {
    return all(return_struct, atom, option(discard(open_tok)
        && sep_by(any(return_args, variable, s), discard(sep_tok))
        && discard(close_tok)));
}

// tie the recursive knot.
auto const structure = fix("struct", recursive_structure);

auto const comment = define("comment", discard(comment_tok));
auto const goals = define("goals", discard(impl_tok)
    && sep_by(all(return_goal, structure), discard(sep_tok)));
auto const query = define("query", all(return_goals, goals) && discard(end_tok));
auto const clause = define("clause", all(return_clause, all(return_head, structure), option(goals) && discard(end_tok)));

auto const program = first_token && strict("unexpected character", some(clause || query || comment));

struct expression_parser;

template <typename Range>
int parse(Range const &r) {
    decltype(program)::result_type a {}; 
    typename Range::iterator i = r.first;
    parser_state st;

    profile<expression_parser> p;
    if (program(i, r, &a, &st)) {
        cout << "OK" << endl;
    } else {
        cout << "FAIL" << endl;
    }

    cout << a << endl;
    
    return i - r.first;
}

//----------------------------------------------------------------------------
// The stream_range allows file iterators to be used like random_iterators
// and abstracts the difference between C++ stdlib streams and file_vectors.


int main(int const argc, char const *argv[]) {
    if (argc < 1) {
        cerr << "no input files" << endl;
    } else {
        for (int i = 1; i < argc; ++i) {
            profile<expression_parser>::reset();
            stream_range in(argv[i]);
            cout << argv[i] << endl;
            int const chars_read = parse(in);
            double const mb_per_s = static_cast<double>(chars_read) / static_cast<double>(profile<expression_parser>::report());
            cout << "parsed: " << mb_per_s << "MB/s" << endl;
        }
    }
}
