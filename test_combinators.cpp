#include <fstream>
#include <iostream>
#include <vector>
#include <sstream>

#include "templateio.hpp"
#include "parser_combinators.hpp"
#include "profile.hpp"
#include "stream_iterator.hpp"

using namespace std;

//----------------------------------------------------------------------------
// Example CSV file parser.

struct parse_int {
    parse_int() {}
    void operator() (vector<int> *ts, string const& num) const {
        ts->push_back(stoi(num));
    }
} const parse_int;

struct parse_line {
    parse_line() {}
    void operator() (vector<vector<int>> *ts, vector<int> &line) const {
        ts->push_back(move(line)); // move modifies 'line' so don't make it const
    }
} const parse_line;

auto const number_tok = tokenise(some(accept(is_digit)));
auto const separator_tok = tokenise(accept(is_char(',')));

auto const parse_csv = strict("error parsing csv",
    first_token && some(all(parse_line, sep_by(all(parse_int, number_tok), separator_tok)))
);

struct csv_parser;

template <typename InputIterator>
int parse(InputIterator i, InputIterator const last) {
    decltype(parse_csv)::result_type a; 
    InputIterator const first = i;

    profile<csv_parser> p;
    if (parse_csv(i, last, &a)) {
        cout << "OK\n";
    } else {
        cout << "FAIL\n";
    }

    int sum = 0;
    for (int i = 0; i < a.size(); ++i) {
        for (int j = 0; j < a[i].size(); j++) {
           sum += a[i][j];
        }
    }
    sum /= a.size();
    cerr << sum << endl;
    
    return i - first;
}

//----------------------------------------------------------------------------

int main(int const argc, char const *argv[]) {
    if (argc < 1) {
        cerr << "no input files\n";
    } else {
        for (int i = 1; i < argc; ++i) {
            fstream in(argv[i], ios_base::in);
            cout << argv[i] << "\n";

            if (in.is_open()) {
                profile<csv_parser>::reset();
                stream_range  r(in.rdbuf());
                int const chars_read = parse(r.cbegin(), r.cend());
                double const mb_per_s = static_cast<double>(chars_read) / static_cast<double>(profile<csv_parser>::report());
                cout << "parsed: " << mb_per_s << "MB/s\n";
            }
        }
    }
}
