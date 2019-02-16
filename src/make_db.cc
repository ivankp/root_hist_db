#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <cstring>
#include <cstdio>
#include <algorithm>

// #include <nlohmann/json.hpp>
#include <boost/regex.hpp>

#include <sqlite3.h>

#include <TFile.h>
#include <TDirectory.h>
#include <TH1.h>

#include "ivanp/string.hh"
#include "ivanp/program_options.hh"
#include "ivanp/timed_counter.hh"
#include "ivanp/root/tkey.hh"
#include "ivanp/functional.hh"

#define TEST(var) \
  std::cout << "\033[36m" #var "\033[0m = " << (var) << std::endl;

using std::cout;
using std::cerr;
using std::endl;
using std::vector;
using std::string;
using std::get;
using boost::regex;
// using nlohmann::json;
using ivanp::cat;
using ivanp::error;
using ivanp::y_combinator;

regex operator""_re (const char* str, size_t n) { return regex(str); }

template <typename T>
vector<string> operator/ (const T& str, const regex& re) {
  boost::sregex_token_iterator it(begin(str), end(str), re), _end;
  return { it, _end };
}
vector<string> operator/ (const char* str, const regex& re) {
  boost::cregex_token_iterator it(str, str+strlen(str), re), _end;
  return { it, _end };
}

template <typename V, typename T>
vector<V> operator+ (vector<V> v, T&& x) {
  v.emplace_back(std::forward<T>(x));
  return v;
}
template <typename V, typename T>
vector<V> operator+ (vector<V> v, vector<T>&& xs) {
  for (auto&& x : xs) v.emplace_back(x);
  return v;
}

template <typename T>
std::ostream& operator<< (std::ostream& o, const vector<T>& v) {
  for (const auto& x : v) o << ' ' << x;
  return o;
}

class SQL_error: ivanp::error { using ivanp::error::error; };
class sqlite {
  sqlite3* db;
public:
  sqlite(const char* fname) {
    if (sqlite3_open(fname,&db)) throw SQL_error(
      "Cannot open sqlite database file \"", fname, '\"'
    );
  }
  typedef int (*callback_t)(void*,int,char**,char**);
  void operator()(const char* str, callback_t callback = nullptr, void* a = nullptr) {
    char* err_msg = nullptr;
    if (sqlite3_exec(db, str, callback, a, &err_msg) != SQLITE_OK) {
      cerr <<"\033[31m"<< err_msg <<"\033[0m"<< endl;
      sqlite3_free(err_msg);
      throw SQL_error(str);
    }
  }
  void operator()(const string& str, callback_t callback = nullptr, void* a = nullptr) {
    (*this)(str.c_str(),callback,a);
  }
  ~sqlite() { sqlite3_close(db); }
};

std::set<vector<double>> edges;
vector<const vector<double>*> edges_list;
// std::map<vector<string>,json> hs;
struct hist_t {
  vector<string> props;
  vector<double> bins;
  unsigned edges;
};
vector<hist_t> hs;

int main(int argc, char* argv[]) {
  vector<const char*> ifnames;
  vector<string> props_names;
  const char* ofname;

  try {
    using namespace ivanp::po;
    if (program_options()
      (ifnames,'i',"input ROOT files",req(),pos())
      (ofname,'o',"output sqlite database",req())
      (props_names,'p',"names of properties")
      .parse(argc,argv,true)) return 0;
  } catch (const std::exception& e) {
    cerr << e << endl;
    return 1;
  }

  const regex file_regex(".*/(.+)\\.root$");
  boost::cmatch match;

  for (const char* ifname : ifnames) {
    cout << ifname << endl;
    if (!boost::regex_match(ifname, match, file_regex)) {
      cerr << "File name \""<<ifname<<"\" did not match required format\n";
      return 1;
    }

    TFile f(ifname);
    if (f.IsZombie()) {
      cerr << "Cannot open file \"" << ifname << "\"\n";
      return 1;
    }

    y_combinator([](auto f, auto* dir,
      const vector<string>& props, int depth=0
    ) -> void {
      for (auto& key : get_keys(dir)) {
        // directory
        auto* dir = safe_key_cast<TDirectory>(key);
        if (dir) { f(dir,props+dir->GetName(),depth+1); continue; }
        // histogram
        auto* h = safe_key_cast<TH1>(key);
        if (!h || (depth==0 && !strcmp(h->GetName(),"N"))) continue;

        hs.emplace_back();
        auto& hist = hs.back();

        const int n = h->GetNbinsX() + 1;
        vector<double> h_edges(n);
        for (int i=0; i<n; ++i)
          h_edges[i] = h->GetBinLowEdge(i+1);
        const auto edges_it = edges.emplace(std::move(h_edges));
        if (get<1>(edges_it)) {
          edges_list.emplace_back(&*get<0>(edges_it));
          hist.edges = edges_list.size() - 1;
        } else {
          hist.edges =
            std::find(edges_list.begin(),edges_list.end(),&*get<0>(edges_it))
            - edges_list.begin();
        }

        hist.bins.resize(n+1);
        for (int i=0; i<=n; ++i)
          hist.bins[i] = h->GetBinContent(i);

        hist.props = props + (h->GetName()/"[^_]+_[^_]+"_re);
        // TEST(props2)
      }
    })(&f, match[1].str()/"[^_]+"_re);
  }

  TEST(edges.size())
  // for (const auto& x : edges) cout << x << endl;

  size_t nprops = 0;
  for (const auto& h : hs)
    nprops = std::max(nprops,h.props.size());
  TEST(nprops)
  for (size_t i=props_names.size(); i<nprops; ++i)
    props_names.emplace_back(cat("prop",i));

  cout << "Writing " << ofname << endl;

  remove(ofname);
  sqlite db(ofname);
  std::stringstream cmd;
  cmd << "CREATE TABLE hist(";
  for (const auto& name : props_names) cmd << '\n' << name << " TEXT,";
  cmd << "\nedges INTEGER,\nbins TEXT\n);";
  db(cmd.str());
  cmd.str({});
  db("BEGIN;");
  for (ivanp::timed_counter<> cnt(hs.size()); cnt.ok(); ++cnt) {
    const auto& h = hs[cnt];
  // for (const auto& h : hs) {
    cmd << "insert into hist values (";
    for (const auto& prop : h.props) cmd << '\'' << prop << "\',";
    for (size_t i=h.props.size(); i<nprops; ++i) cmd << "\'\',";
    cmd << h.edges << ",\'";
    char buff[16];
    for (unsigned i=0, n=h.bins.size(); i<n; ++i) {
      if (i) cmd << ',';
      sprintf(buff,"%.6g",h.bins[i]);
      cmd << buff;
    }
    cmd << "\')";
    db(cmd.str());
    cmd.str({});
  }
  db("END;");
}
