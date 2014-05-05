#include <fstream>
#include <iostream>
#include <vector>
#include "templateio.hpp"
#include "parser_combinators.hpp"
#include "profile.hpp"

using namespace std;

//----------------------------------------------------------------------------
// Example Expression Evaluating File Parser.

enum op {add = 0, sub = 1, mul = 2, div = 3};

struct return_int {
    return_int() {}
    void operator() (int *res, string const& num) const {
        *res = stoi(num);
    }
} const return_int;

struct return_op {
    return_op() {}
    void operator() (
        enum op *res, int choice, string const& add,
        string const& sub, string const& mul, string const& div
    ) const {
        *res = static_cast<enum op>(choice);
    }
} const return_op;

struct return_left {
    return_left() {}
    void operator() (int *res, int left) const {
        *res = left;
    }
} const return_left;

struct return_right {
    return_right() {}
    void operator() (int *res, enum op opr, int right) const {
        switch (opr) {
            case op::add:
                *res += right;
                break;
            case op::sub:
                *res -= right;
                break;
            case op::mul:
                *res *= right;
                break;
            case op::div:
                *res /= right;
                break;
        }
    }
} const return_right;

auto const recognise_number = some(accept(is_digit));
auto const recognise_space = many(accept(is_space));
auto const parse_operand = discard(recognise_space) && all(return_int, recognise_number);
auto const parse_operator = discard(recognise_space) && any(return_op, accept(is_char('+')), accept(is_char('-')),
    accept(is_char('*')), accept(is_char('/')));
auto const parse = all(return_left, parse_operand) && many(all(return_right, parse_operator, parse_operand));

class csv_parser {
    pstream in;

public:
    csv_parser(fstream &fs) : in(fs) {}

    int operator() () {
        decltype(parse)::result_type a; 

        bool b;
        {
            profile<csv_parser> p;
            b = parse(in, &a);
        }

        if (b) {
            cout << "OK\n";
        } else {
            cout << "FAIL\n";
        }

        cout << a << "\n";
        
        return in.get_count();
    }
};

//----------------------------------------------------------------------------

int main(int const argc, char const *argv[]) {
    if (argc < 1) {
        cerr << "no input files\n";
    } else {
        for (int i = 1; i < argc; ++i) {
            try {
                fstream in(argv[i], ios_base::in);
                cout << argv[i] << "\n";

                if (in.is_open()) {
                    csv_parser csv(in);
                    profile<csv_parser>::reset();
                    int const chars_read = csv();
                    double const mb_per_s = static_cast<double>(chars_read) / static_cast<double>(profile<csv_parser>::report());
                    cout << "parsed: " << mb_per_s << "MB/s\n";
                }
            } catch (parse_error& e) {
                cerr << argv[i] << ": " << e.what()
                    << " " << e.exp
                    << " found ";
                if (is_print(e.sym)) {
                    cerr << "'" << static_cast<char>(e.sym) << "'";
                } else {
                    cerr << "0x" << hex << e.sym;
                }
                cerr << " at line " << e.row
                    << ", column " << e.col << "\n";
                return 2;
            }
        }
    }
}
