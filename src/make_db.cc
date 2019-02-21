#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <regex>

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
using std::regex;
using ivanp::cat;
using ivanp::error;
using ivanp::y_combinator;

regex operator""_re (const char* str, size_t n) { return regex(str); }

template <typename T>
vector<string> operator/ (const T& str, const regex& re) {
  std::sregex_token_iterator it(begin(str), end(str), re), _end;
  return { it, _end };
}
vector<string> operator/ (const char* str, const regex& re) {
  std::cregex_token_iterator it(str, str+strlen(str), re), _end;
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

// class SQL_error: ivanp::error { using ivanp::error::error; };
struct SQL_error : std::runtime_error {
  using std::runtime_error::runtime_error;
  template <typename... T>
  SQL_error(T&&... x): std::runtime_error(cat(x...)) { };
  SQL_error(const char* str): std::runtime_error(str) { };
};
class sqlite {
  sqlite3* db;
public:
  sqlite(const char* fname) {
    if (sqlite3_open(fname, &db))
      throw SQL_error(sqlite3_errmsg(db), "\": ", fname);
  }
  void operator()(const char* str) {
    char* err_msg = nullptr;
    if (sqlite3_exec(db, str, nullptr, nullptr, &err_msg)) {
      cerr <<"\033[31m"<< err_msg <<"\033[0m"<< endl;
      sqlite3_free(err_msg);
      throw SQL_error(str);
    }
  }
  void operator()(const char* str, const void* blob, size_t blob_size) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,str,-1,&stmt,NULL)) {
      cerr <<"\033[31m"<< sqlite3_errmsg(db) <<"\033[0m"<< endl;
      throw SQL_error(str);
    } else {
      if (sqlite3_bind_blob(stmt, 1, blob, blob_size, SQLITE_STATIC)) {
        cerr <<"\033[31m"<< sqlite3_errmsg(db) <<"\033[0m"<< endl;
        throw SQL_error(str);
      } else {
        if (sqlite3_step(stmt) != SQLITE_DONE) {
          cerr <<"\033[31m"<< sqlite3_errmsg(db) <<"\033[0m"<< endl;
          throw SQL_error(str);
        }
      }
    }
    sqlite3_finalize(stmt);
  }
  ~sqlite() { sqlite3_close(db); }
};
std::stringstream& operator>> (std::stringstream& ss, sqlite& db) {
  db(ss.str().c_str());
  ss.str({});
  return ss;
}

std::set<vector<double>> edges;
vector<const vector<double>*> edges_list;
struct hist_t {
  vector<string> labels;
  vector<double> bins;
  unsigned edges;
};
vector<hist_t> hs;

int main(int argc, char* argv[]) {
  vector<const char*> ifnames;
  vector<string> labels_names;
  const char* ofname;

  try {
    using namespace ivanp::po;
    if (program_options()
      (ifnames,'i',"input ROOT files",req(),pos())
      (ofname,'o',"output sqlite database",req())
      (labels_names,'l',"names of histogram labels")
      .parse(argc,argv,true)) return 0;
  } catch (const std::exception& e) {
    cerr << e << endl;
    return 1;
  }

  const regex file_regex("(?:.*/)?(.+)\\.root$");
  std::cmatch match;

  for (const char* ifname : ifnames) {
    cout << ifname << endl;
    if (!std::regex_match(ifname, match, file_regex)) {
      cerr << "File name \""<<ifname<<"\" did not match required format\n";
      return 1;
    }

    TFile f(ifname);
    if (f.IsZombie()) {
      cerr << "Cannot open file \"" << ifname << "\"\n";
      return 1;
    }

    y_combinator([](auto f, auto* dir,
      const vector<string>& labels, int depth=0
    ) -> void {
      for (auto& key : get_keys(dir)) {
        // directory
        auto* dir = safe_key_cast<TDirectory>(key);
        if (dir) { f(dir,labels+dir->GetName(),depth+1); continue; }
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

        hist.bins.resize((n+1)*2);
        for (int i=0; i<=n; ++i) {
          hist.bins[i*2] = h->GetBinContent(i);
          hist.bins[i*2+1] = h->GetBinError(i);
        }

        hist.labels = labels + (h->GetName()/"[^_]+_[^_]+"_re);
        // TEST(labels2)
      }
    })(&f, match[1].str()/"[^_]+"_re);
  }

  cout << "Distinct axes: " << edges.size() << endl;

  size_t nlabels = 0;
  for (const auto& h : hs)
    nlabels = std::max(nlabels,h.labels.size());
  for (size_t i=labels_names.size(); i<nlabels; ++i)
    labels_names.emplace_back(cat("label",i));

  cout << "Hist labels: " << nlabels << endl;

  cout << "Writing " << ofname << endl;

  remove(ofname);
  sqlite db(ofname);

  std::stringstream cmd;
  cmd << "CREATE TABLE hist(";
  for (const auto& name : labels_names) cmd << '\n' << name << " TEXT,";
  cmd << "\naxis INTEGER,\nbins TEXT\n);";
  cmd >> db;

  cmd << "CREATE TABLE axes("
    "\nid INTEGER PRIMARY KEY,"
    "\nedges TEXT"
    "\n);";
  cmd >> db;

  db("BEGIN;");
  for (ivanp::timed_counter<> cnt(hs.size()); !!cnt; ++cnt) {
    const auto& h = hs[cnt];
    cmd << "insert into hist values (";
    for (const auto& label : h.labels) cmd << '\'' << label << "\',";
    for (size_t i=h.labels.size(); i<nlabels; ++i) cmd << "\'\',";
    cmd << h.edges << ",\'";
    // db(cmd.str().c_str(),h.bins.data(),h.bins.size()*sizeof(double));
    // const char* blob = "blob";
    // db(cmd.str().c_str(),blob,5);
    // cmd >> db;
    char buff[16];
    for (unsigned i=0, n=h.bins.size(); i<n; ++i) {
      if (i) cmd << ',';
      sprintf(buff,"%.6g",h.bins[i]);
      cmd << buff;
    }
    cmd << "\')";
    cmd >> db;
  }
  db("END;");

  db("BEGIN;");
  for (unsigned i=0, n=edges_list.size(); i<n; ++i) {
    cmd << "insert into axes values (" << i << ",\'";
    const auto& edges = *edges_list[i];
    for (unsigned j=0, m=edges.size(); j<m; ++j) {
      if (j) cmd << ',';
      cmd << edges[j];
    }
    cmd << "\');";
    cmd >> db;
  }
  db("END;");
}
