#include <bits/stdc++.h>
using namespace std;

struct User {
  string password;
  string username;
  int privilege = 0;
  int login_count = 0;
};

struct Book {
  string isbn;
  string name;
  string author;
  string keyword_raw;
  vector<string> keywords;
  long long price_cents = 0;
  long long stock = 0;
};

struct Session {
  string user_id;
  string selected_isbn;
};

enum class ShowFilterType { ALL, ISBN, NAME, AUTHOR, KEYWORD };

struct ShowFilter {
  ShowFilterType type = ShowFilterType::ALL;
  string value;
};

static constexpr uint32_t kDbMagic = 0x424B5354;   // BKST
static constexpr uint32_t kDbVersion = 1;
static const char *kDbFileName = "bookstore.db";

template <typename T>
static bool WriteRaw(ostream &out, const T &value) {
  out.write(reinterpret_cast<const char *>(&value), sizeof(T));
  return static_cast<bool>(out);
}

template <typename T>
static bool ReadRaw(istream &in, T &value) {
  in.read(reinterpret_cast<char *>(&value), sizeof(T));
  return static_cast<bool>(in);
}

static bool WriteString(ostream &out, const string &s) {
  uint32_t len = static_cast<uint32_t>(s.size());
  if (!WriteRaw(out, len)) return false;
  out.write(s.data(), static_cast<streamsize>(len));
  return static_cast<bool>(out);
}

static bool ReadString(istream &in, string &s) {
  uint32_t len = 0;
  if (!ReadRaw(in, len)) return false;
  if (len > 1000000) return false;
  s.resize(len);
  in.read(s.data(), static_cast<streamsize>(len));
  return static_cast<bool>(in);
}

static bool WriteInt128(ostream &out, __int128 value) {
  unsigned __int128 u = static_cast<unsigned __int128>(value);
  uint64_t low = static_cast<uint64_t>(u);
  uint64_t high = static_cast<uint64_t>(u >> 64);
  return WriteRaw(out, low) && WriteRaw(out, high);
}

static bool ReadInt128(istream &in, __int128 &value) {
  uint64_t low = 0;
  uint64_t high = 0;
  if (!ReadRaw(in, low) || !ReadRaw(in, high)) return false;
  unsigned __int128 u = (static_cast<unsigned __int128>(high) << 64) | static_cast<unsigned __int128>(low);
  value = static_cast<__int128>(u);
  return true;
}

static bool IsAsciiPrintableOrSpace(char c) {
  unsigned char uc = static_cast<unsigned char>(c);
  return uc <= 127 && uc >= 32;
}

static bool IsVisibleAscii(char c) {
  unsigned char uc = static_cast<unsigned char>(c);
  return uc >= 33 && uc <= 126;
}

static bool ValidateIDOrPassword(const string &s) {
  if (s.empty() || s.size() > 30) return false;
  for (char c : s) {
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
  }
  return true;
}

static bool ValidateUsername(const string &s) {
  if (s.empty() || s.size() > 30) return false;
  for (char c : s) {
    if (!IsVisibleAscii(c)) return false;
  }
  return true;
}

static bool ValidateISBN(const string &s) {
  if (s.empty() || s.size() > 20) return false;
  for (char c : s) {
    if (!IsVisibleAscii(c)) return false;
  }
  return true;
}

static bool ValidateQuotedText(const string &s, size_t max_len) {
  if (s.empty() || s.size() > max_len) return false;
  for (char c : s) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 32 || uc > 126) return false;
    if (c == '"') return false;
  }
  return true;
}

static bool ParseIntLimited(const string &s, int max_len, int max_value, bool positive_required, int &out) {
  if (s.empty() || static_cast<int>(s.size()) > max_len) return false;
  long long value = 0;
  for (char c : s) {
    if (!isdigit(static_cast<unsigned char>(c))) return false;
    value = value * 10 + (c - '0');
    if (value > max_value) return false;
  }
  if (positive_required && value <= 0) return false;
  out = static_cast<int>(value);
  return true;
}

static bool ParseMoneyToCents(const string &s, long long &out_cents) {
  if (s.empty() || s.size() > 13) return false;
  int dot_count = 0;
  for (char c : s) {
    if (!(isdigit(static_cast<unsigned char>(c)) || c == '.')) return false;
    if (c == '.') dot_count++;
  }
  if (dot_count > 1) return false;

  string int_part;
  string frac_part;
  size_t dot_pos = s.find('.');
  if (dot_pos == string::npos) {
    int_part = s;
  } else {
    if (dot_pos == 0) return false;
    int_part = s.substr(0, dot_pos);
    frac_part = s.substr(dot_pos + 1);
    if (frac_part.empty() || frac_part.size() > 2) return false;
  }

  long long int_value = 0;
  for (char c : int_part) {
    int_value = int_value * 10 + (c - '0');
  }

  long long frac_value = 0;
  if (!frac_part.empty()) {
    frac_value = frac_part[0] - '0';
    if (frac_part.size() == 2) {
      frac_value = frac_value * 10 + (frac_part[1] - '0');
    } else {
      frac_value *= 10;
    }
  }
  out_cents = int_value * 100 + frac_value;
  return true;
}

static string Int128ToString(__int128 v) {
  if (v == 0) return "0";
  bool neg = v < 0;
  if (neg) v = -v;
  string s;
  while (v > 0) {
    int digit = static_cast<int>(v % 10);
    s.push_back(static_cast<char>('0' + digit));
    v /= 10;
  }
  if (neg) s.push_back('-');
  reverse(s.begin(), s.end());
  return s;
}

static string FormatMoney(__int128 cents) {
  __int128 whole = cents / 100;
  __int128 frac = cents % 100;
  if (frac < 0) frac = -frac;
  string out = Int128ToString(whole);
  out.push_back('.');
  int frac_int = static_cast<int>(frac);
  out.push_back(static_cast<char>('0' + frac_int / 10));
  out.push_back(static_cast<char>('0' + frac_int % 10));
  return out;
}

static bool Tokenize(const string &line, vector<string> &tokens) {
  tokens.clear();
  for (char c : line) {
    if (!IsAsciiPrintableOrSpace(c)) return false;
  }
  string current;
  bool in_quote = false;
  for (char c : line) {
    if (c == '"') {
      in_quote = !in_quote;
      current.push_back(c);
    } else if (c == ' ' && !in_quote) {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (in_quote) return false;
  if (!current.empty()) tokens.push_back(current);
  return true;
}

static bool ParseQuotedOption(const string &token, const string &prefix, string &value) {
  if (token.rfind(prefix, 0) != 0) return false;
  string rest = token.substr(prefix.size());
  if (rest.size() < 2) return false;
  if (rest.front() != '"' || rest.back() != '"') return false;
  value = rest.substr(1, rest.size() - 2);
  return true;
}

static bool ParseKeywords(const string &raw, vector<string> &keywords) {
  keywords.clear();
  if (raw.empty()) return false;
  unordered_set<string> seen;
  size_t start = 0;
  while (true) {
    size_t pos = raw.find('|', start);
    string part = (pos == string::npos) ? raw.substr(start) : raw.substr(start, pos - start);
    if (part.empty()) return false;
    if (seen.find(part) != seen.end()) return false;
    seen.insert(part);
    keywords.push_back(part);
    if (pos == string::npos) break;
    start = pos + 1;
  }
  return true;
}

static bool SaveState(
    const unordered_map<string, User> &users,
    const map<string, Book> &books,
    const vector<pair<__int128, __int128>> &finance_records) {
  ofstream out(kDbFileName, ios::binary | ios::trunc);
  if (!out.is_open()) return false;

  if (!WriteRaw(out, kDbMagic) || !WriteRaw(out, kDbVersion)) return false;

  uint32_t user_count = static_cast<uint32_t>(users.size());
  if (!WriteRaw(out, user_count)) return false;
  for (const auto &kv : users) {
    const string &uid = kv.first;
    const User &u = kv.second;
    int32_t priv = u.privilege;
    if (!WriteString(out, uid) || !WriteString(out, u.password) || !WriteString(out, u.username) ||
        !WriteRaw(out, priv)) {
      return false;
    }
  }

  uint32_t book_count = static_cast<uint32_t>(books.size());
  if (!WriteRaw(out, book_count)) return false;
  for (const auto &kv : books) {
    const Book &b = kv.second;
    if (!WriteString(out, b.isbn) || !WriteString(out, b.name) || !WriteString(out, b.author) ||
        !WriteString(out, b.keyword_raw) || !WriteRaw(out, b.price_cents) || !WriteRaw(out, b.stock)) {
      return false;
    }
  }

  uint32_t finance_count = static_cast<uint32_t>(finance_records.size());
  if (!WriteRaw(out, finance_count)) return false;
  for (const auto &txn : finance_records) {
    if (!WriteInt128(out, txn.first) || !WriteInt128(out, txn.second)) return false;
  }
  return static_cast<bool>(out);
}

static bool LoadState(
    unordered_map<string, User> &users,
    map<string, Book> &books,
    vector<pair<__int128, __int128>> &finance_records) {
  ifstream in(kDbFileName, ios::binary);
  if (!in.is_open()) return false;

  uint32_t magic = 0;
  uint32_t version = 0;
  if (!ReadRaw(in, magic) || !ReadRaw(in, version)) return false;
  if (magic != kDbMagic || version != kDbVersion) return false;

  unordered_map<string, User> new_users;
  map<string, Book> new_books;
  vector<pair<__int128, __int128>> new_finance;

  uint32_t user_count = 0;
  if (!ReadRaw(in, user_count)) return false;
  for (uint32_t i = 0; i < user_count; ++i) {
    string uid, pwd, uname;
    int32_t priv = 0;
    if (!ReadString(in, uid) || !ReadString(in, pwd) || !ReadString(in, uname) || !ReadRaw(in, priv)) {
      return false;
    }
    User u;
    u.password = std::move(pwd);
    u.username = std::move(uname);
    u.privilege = priv;
    u.login_count = 0;
    new_users[std::move(uid)] = std::move(u);
  }

  uint32_t book_count = 0;
  if (!ReadRaw(in, book_count)) return false;
  for (uint32_t i = 0; i < book_count; ++i) {
    Book b;
    if (!ReadString(in, b.isbn) || !ReadString(in, b.name) || !ReadString(in, b.author) ||
        !ReadString(in, b.keyword_raw) || !ReadRaw(in, b.price_cents) || !ReadRaw(in, b.stock)) {
      return false;
    }
    if (!b.keyword_raw.empty()) {
      if (!ParseKeywords(b.keyword_raw, b.keywords)) return false;
    }
    new_books[b.isbn] = std::move(b);
  }

  uint32_t finance_count = 0;
  if (!ReadRaw(in, finance_count)) return false;
  new_finance.reserve(finance_count);
  for (uint32_t i = 0; i < finance_count; ++i) {
    __int128 in_money = 0, out_money = 0;
    if (!ReadInt128(in, in_money) || !ReadInt128(in, out_money)) return false;
    new_finance.push_back({in_money, out_money});
  }

  users = std::move(new_users);
  books = std::move(new_books);
  finance_records = std::move(new_finance);
  return true;
}

static string FormatBookLine(const Book &b) {
  string line;
  line.reserve(b.isbn.size() + b.name.size() + b.author.size() + b.keyword_raw.size() + 64);
  line += b.isbn;
  line.push_back('\t');
  line += b.name;
  line.push_back('\t');
  line += b.author;
  line.push_back('\t');
  line += b.keyword_raw;
  line.push_back('\t');
  line += FormatMoney(b.price_cents);
  line.push_back('\t');
  line += to_string(b.stock);
  line.push_back('\n');
  return line;
}

static void RemoveFromIndex(unordered_map<string, set<string>> &index, const string &key, const string &isbn) {
  if (key.empty()) return;
  auto it = index.find(key);
  if (it == index.end()) return;
  it->second.erase(isbn);
  if (it->second.empty()) index.erase(it);
}

static void AddToIndex(unordered_map<string, set<string>> &index, const string &key, const string &isbn) {
  if (key.empty()) return;
  index[key].insert(isbn);
}

int main() {
  ios::sync_with_stdio(false);
  cin.tie(nullptr);

  unordered_map<string, User> users;
  users.reserve(65536);
  map<string, Book> books;
  vector<pair<__int128, __int128>> finance_records;
  finance_records.reserve(262144);

  bool loaded_ok = LoadState(users, books, finance_records);
  if (!loaded_ok) {
    users.clear();
    books.clear();
    finance_records.clear();
  }
  if (users.find("root") == users.end()) {
    users["root"] = User{"sjtu", "root", 7, 0};
  }
  for (auto &kv : users) kv.second.login_count = 0;

  vector<Session> sessions;
  sessions.reserve(256);

  unordered_map<string, set<string>> name_index;
  unordered_map<string, set<string>> author_index;
  unordered_map<string, set<string>> keyword_index;
  name_index.reserve(65536);
  author_index.reserve(65536);
  keyword_index.reserve(65536);

  vector<__int128> finance_in_prefix(1, 0);
  vector<__int128> finance_out_prefix(1, 0);
  finance_in_prefix.reserve(262144);
  finance_out_prefix.reserve(262144);
  for (const auto &kv : books) {
    const Book &b = kv.second;
    AddToIndex(name_index, b.name, b.isbn);
    AddToIndex(author_index, b.author, b.isbn);
    for (const string &kw : b.keywords) AddToIndex(keyword_index, kw, b.isbn);
  }
  for (const auto &txn : finance_records) {
    finance_in_prefix.push_back(finance_in_prefix.back() + txn.first);
    finance_out_prefix.push_back(finance_out_prefix.back() + txn.second);
  }

  auto current_priv = [&]() -> int {
    if (sessions.empty()) return 0;
    return users[sessions.back().user_id].privilege;
  };

  auto output_invalid = [&]() { cout << "Invalid\n"; };

  auto add_finance = [&](const __int128 income, const __int128 expense) {
    finance_records.push_back({income, expense});
    finance_in_prefix.push_back(finance_in_prefix.back() + income);
    finance_out_prefix.push_back(finance_out_prefix.back() + expense);
  };

  string line;
  while (getline(cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();

    vector<string> tokens;
    if (!Tokenize(line, tokens)) {
      output_invalid();
      continue;
    }
    if (tokens.empty()) continue;

    const string &cmd = tokens[0];

    if (cmd == "quit" || cmd == "exit") {
      if (tokens.size() != 1) {
        output_invalid();
        continue;
      }
      break;
    }

    if (cmd == "su") {
      if (!(tokens.size() == 2 || tokens.size() == 3)) {
        output_invalid();
        continue;
      }
      const string &uid = tokens[1];
      if (!ValidateIDOrPassword(uid)) {
        output_invalid();
        continue;
      }
      auto it = users.find(uid);
      if (it == users.end()) {
        output_invalid();
        continue;
      }
      if (tokens.size() == 2) {
        if (current_priv() <= it->second.privilege) {
          output_invalid();
          continue;
        }
      } else {
        const string &pwd = tokens[2];
        if (!ValidateIDOrPassword(pwd) || it->second.password != pwd) {
          output_invalid();
          continue;
        }
      }
      it->second.login_count++;
      sessions.push_back(Session{uid, ""});
      continue;
    }

    if (cmd == "logout") {
      if (tokens.size() != 1 || current_priv() < 1 || sessions.empty()) {
        output_invalid();
        continue;
      }
      auto it = users.find(sessions.back().user_id);
      if (it != users.end()) {
        it->second.login_count--;
      }
      sessions.pop_back();
      continue;
    }

    if (cmd == "register") {
      if (tokens.size() != 4) {
        output_invalid();
        continue;
      }
      const string &uid = tokens[1];
      const string &pwd = tokens[2];
      const string &uname = tokens[3];
      if (!ValidateIDOrPassword(uid) || !ValidateIDOrPassword(pwd) || !ValidateUsername(uname)) {
        output_invalid();
        continue;
      }
      if (users.find(uid) != users.end()) {
        output_invalid();
        continue;
      }
      users[uid] = User{pwd, uname, 1, 0};
      continue;
    }

    if (cmd == "passwd") {
      if (!(tokens.size() == 3 || tokens.size() == 4) || current_priv() < 1) {
        output_invalid();
        continue;
      }
      const string &uid = tokens[1];
      if (!ValidateIDOrPassword(uid)) {
        output_invalid();
        continue;
      }
      auto it = users.find(uid);
      if (it == users.end()) {
        output_invalid();
        continue;
      }

      if (tokens.size() == 3) {
        const string &new_pwd = tokens[2];
        if (current_priv() != 7 || !ValidateIDOrPassword(new_pwd)) {
          output_invalid();
          continue;
        }
        it->second.password = new_pwd;
      } else {
        const string &cur_pwd = tokens[2];
        const string &new_pwd = tokens[3];
        if (!ValidateIDOrPassword(cur_pwd) || !ValidateIDOrPassword(new_pwd) || it->second.password != cur_pwd) {
          output_invalid();
          continue;
        }
        it->second.password = new_pwd;
      }
      continue;
    }

    if (cmd == "useradd") {
      if (tokens.size() != 5 || current_priv() < 3) {
        output_invalid();
        continue;
      }
      const string &uid = tokens[1];
      const string &pwd = tokens[2];
      const string &priv_s = tokens[3];
      const string &uname = tokens[4];
      if (!ValidateIDOrPassword(uid) || !ValidateIDOrPassword(pwd) || !ValidateUsername(uname)) {
        output_invalid();
        continue;
      }
      if (priv_s.size() != 1 || !isdigit(static_cast<unsigned char>(priv_s[0]))) {
        output_invalid();
        continue;
      }
      int priv = priv_s[0] - '0';
      if (!(priv == 1 || priv == 3 || priv == 7) || priv >= current_priv()) {
        output_invalid();
        continue;
      }
      if (users.find(uid) != users.end()) {
        output_invalid();
        continue;
      }
      users[uid] = User{pwd, uname, priv, 0};
      continue;
    }

    if (cmd == "delete") {
      if (tokens.size() != 2 || current_priv() < 7) {
        output_invalid();
        continue;
      }
      const string &uid = tokens[1];
      if (!ValidateIDOrPassword(uid)) {
        output_invalid();
        continue;
      }
      auto it = users.find(uid);
      if (it == users.end() || it->second.login_count > 0) {
        output_invalid();
        continue;
      }
      users.erase(it);
      continue;
    }

    if (cmd == "show") {
      if (tokens.size() >= 2 && tokens[1] == "finance") {
        if (current_priv() < 7 || !(tokens.size() == 2 || tokens.size() == 3)) {
          output_invalid();
          continue;
        }
        int count = -1;
        if (tokens.size() == 3) {
          if (!ParseIntLimited(tokens[2], 10, 2147483647, false, count)) {
            output_invalid();
            continue;
          }
        }
        int total_trans = static_cast<int>(finance_records.size());
        if (count == -1) {
          __int128 income = finance_in_prefix.back();
          __int128 expense = finance_out_prefix.back();
          cout << "+ " << FormatMoney(income) << " - " << FormatMoney(expense) << '\n';
        } else {
          if (count == 0) {
            cout << '\n';
            continue;
          }
          if (count > total_trans) {
            output_invalid();
            continue;
          }
          int n = total_trans;
          __int128 income = finance_in_prefix[n] - finance_in_prefix[n - count];
          __int128 expense = finance_out_prefix[n] - finance_out_prefix[n - count];
          cout << "+ " << FormatMoney(income) << " - " << FormatMoney(expense) << '\n';
        }
        continue;
      }

      if (current_priv() < 1 || !(tokens.size() == 1 || tokens.size() == 2)) {
        output_invalid();
        continue;
      }

      ShowFilter filter;
      if (tokens.size() == 2) {
        const string &arg = tokens[1];
        if (arg.rfind("-ISBN=", 0) == 0) {
          string v = arg.substr(6);
          if (!ValidateISBN(v)) {
            output_invalid();
            continue;
          }
          filter.type = ShowFilterType::ISBN;
          filter.value = v;
        } else if (arg.rfind("-name=", 0) == 0) {
          string v;
          if (!ParseQuotedOption(arg, "-name=", v) || !ValidateQuotedText(v, 60)) {
            output_invalid();
            continue;
          }
          filter.type = ShowFilterType::NAME;
          filter.value = v;
        } else if (arg.rfind("-author=", 0) == 0) {
          string v;
          if (!ParseQuotedOption(arg, "-author=", v) || !ValidateQuotedText(v, 60)) {
            output_invalid();
            continue;
          }
          filter.type = ShowFilterType::AUTHOR;
          filter.value = v;
        } else if (arg.rfind("-keyword=", 0) == 0) {
          string v;
          if (!ParseQuotedOption(arg, "-keyword=", v) || !ValidateQuotedText(v, 60)) {
            output_invalid();
            continue;
          }
          if (v.find('|') != string::npos) {
            output_invalid();
            continue;
          }
          filter.type = ShowFilterType::KEYWORD;
          filter.value = v;
        } else {
          output_invalid();
          continue;
        }
      }

      bool printed_any = false;
      if (filter.type == ShowFilterType::ALL) {
        for (const auto &kv : books) {
          cout << FormatBookLine(kv.second);
          printed_any = true;
        }
      } else if (filter.type == ShowFilterType::ISBN) {
        auto it = books.find(filter.value);
        if (it != books.end()) {
          cout << FormatBookLine(it->second);
          printed_any = true;
        }
      } else if (filter.type == ShowFilterType::NAME) {
        auto it = name_index.find(filter.value);
        if (it != name_index.end()) {
          for (const string &isbn : it->second) {
            auto bit = books.find(isbn);
            if (bit != books.end()) {
              cout << FormatBookLine(bit->second);
              printed_any = true;
            }
          }
        }
      } else if (filter.type == ShowFilterType::AUTHOR) {
        auto it = author_index.find(filter.value);
        if (it != author_index.end()) {
          for (const string &isbn : it->second) {
            auto bit = books.find(isbn);
            if (bit != books.end()) {
              cout << FormatBookLine(bit->second);
              printed_any = true;
            }
          }
        }
      } else {
        auto it = keyword_index.find(filter.value);
        if (it != keyword_index.end()) {
          for (const string &isbn : it->second) {
            auto bit = books.find(isbn);
            if (bit != books.end()) {
              cout << FormatBookLine(bit->second);
              printed_any = true;
            }
          }
        }
      }

      if (!printed_any) cout << '\n';
      continue;
    }

    if (cmd == "buy") {
      if (tokens.size() != 3 || current_priv() < 1) {
        output_invalid();
        continue;
      }
      const string &isbn = tokens[1];
      if (!ValidateISBN(isbn)) {
        output_invalid();
        continue;
      }
      int qty = 0;
      if (!ParseIntLimited(tokens[2], 10, 2147483647, true, qty)) {
        output_invalid();
        continue;
      }
      auto it = books.find(isbn);
      if (it == books.end() || it->second.stock < qty) {
        output_invalid();
        continue;
      }
      it->second.stock -= qty;
      __int128 income = static_cast<__int128>(it->second.price_cents) * qty;
      add_finance(income, 0);
      cout << FormatMoney(income) << '\n';
      continue;
    }

    if (cmd == "select") {
      if (tokens.size() != 2 || current_priv() < 3 || sessions.empty()) {
        output_invalid();
        continue;
      }
      const string &isbn = tokens[1];
      if (!ValidateISBN(isbn)) {
        output_invalid();
        continue;
      }
      if (books.find(isbn) == books.end()) {
        Book b;
        b.isbn = isbn;
        books.emplace(isbn, std::move(b));
      }
      sessions.back().selected_isbn = isbn;
      continue;
    }

    if (cmd == "modify") {
      if (tokens.size() < 2 || current_priv() < 3 || sessions.empty() || sessions.back().selected_isbn.empty()) {
        output_invalid();
        continue;
      }
      const string old_isbn = sessions.back().selected_isbn;
      auto it_old = books.find(old_isbn);
      if (it_old == books.end()) {
        output_invalid();
        continue;
      }

      bool has_isbn = false, has_name = false, has_author = false, has_keyword = false, has_price = false;
      string new_isbn = old_isbn;
      string new_name = it_old->second.name;
      string new_author = it_old->second.author;
      string new_keyword_raw = it_old->second.keyword_raw;
      vector<string> new_keywords = it_old->second.keywords;
      long long new_price_cents = it_old->second.price_cents;

      bool parse_ok = true;
      for (size_t i = 1; i < tokens.size() && parse_ok; ++i) {
        const string &arg = tokens[i];
        if (arg.rfind("-ISBN=", 0) == 0) {
          if (has_isbn) {
            parse_ok = false;
            break;
          }
          has_isbn = true;
          string v = arg.substr(6);
          if (!ValidateISBN(v)) {
            parse_ok = false;
            break;
          }
          new_isbn = v;
        } else if (arg.rfind("-name=", 0) == 0) {
          if (has_name) {
            parse_ok = false;
            break;
          }
          has_name = true;
          string v;
          if (!ParseQuotedOption(arg, "-name=", v) || !ValidateQuotedText(v, 60)) {
            parse_ok = false;
            break;
          }
          new_name = v;
        } else if (arg.rfind("-author=", 0) == 0) {
          if (has_author) {
            parse_ok = false;
            break;
          }
          has_author = true;
          string v;
          if (!ParseQuotedOption(arg, "-author=", v) || !ValidateQuotedText(v, 60)) {
            parse_ok = false;
            break;
          }
          new_author = v;
        } else if (arg.rfind("-keyword=", 0) == 0) {
          if (has_keyword) {
            parse_ok = false;
            break;
          }
          has_keyword = true;
          string v;
          vector<string> k;
          if (!ParseQuotedOption(arg, "-keyword=", v) || !ValidateQuotedText(v, 60) || !ParseKeywords(v, k)) {
            parse_ok = false;
            break;
          }
          new_keyword_raw = v;
          new_keywords = std::move(k);
        } else if (arg.rfind("-price=", 0) == 0) {
          if (has_price) {
            parse_ok = false;
            break;
          }
          has_price = true;
          string v = arg.substr(7);
          long long cents = 0;
          if (!ParseMoneyToCents(v, cents)) {
            parse_ok = false;
            break;
          }
          new_price_cents = cents;
        } else {
          parse_ok = false;
          break;
        }
      }

      if (!parse_ok) {
        output_invalid();
        continue;
      }
      if (has_isbn) {
        if (new_isbn == old_isbn) {
          output_invalid();
          continue;
        }
        if (books.find(new_isbn) != books.end()) {
          output_invalid();
          continue;
        }
      }

      Book old_book = it_old->second;
      RemoveFromIndex(name_index, old_book.name, old_book.isbn);
      RemoveFromIndex(author_index, old_book.author, old_book.isbn);
      for (const string &kw : old_book.keywords) {
        RemoveFromIndex(keyword_index, kw, old_book.isbn);
      }

      if (has_isbn) {
        auto node = books.extract(old_isbn);
        if (node.empty()) {
          output_invalid();
          continue;
        }
        node.key() = new_isbn;
        node.mapped().isbn = new_isbn;
        books.insert(std::move(node));
      }

      Book &book = books[has_isbn ? new_isbn : old_isbn];
      book.isbn = has_isbn ? new_isbn : old_isbn;
      book.name = new_name;
      book.author = new_author;
      book.keyword_raw = new_keyword_raw;
      book.keywords = new_keywords;
      book.price_cents = new_price_cents;

      AddToIndex(name_index, book.name, book.isbn);
      AddToIndex(author_index, book.author, book.isbn);
      for (const string &kw : book.keywords) {
        AddToIndex(keyword_index, kw, book.isbn);
      }

      if (has_isbn) {
        for (Session &s : sessions) {
          if (s.selected_isbn == old_isbn) s.selected_isbn = new_isbn;
        }
      }
      continue;
    }

    if (cmd == "import") {
      if (tokens.size() != 3 || current_priv() < 3 || sessions.empty() || sessions.back().selected_isbn.empty()) {
        output_invalid();
        continue;
      }
      auto it = books.find(sessions.back().selected_isbn);
      if (it == books.end()) {
        output_invalid();
        continue;
      }
      int qty = 0;
      if (!ParseIntLimited(tokens[1], 10, 2147483647, true, qty)) {
        output_invalid();
        continue;
      }
      long long cost_cents = 0;
      if (!ParseMoneyToCents(tokens[2], cost_cents) || cost_cents <= 0) {
        output_invalid();
        continue;
      }
      it->second.stock += qty;
      add_finance(0, cost_cents);
      continue;
    }

    if (cmd == "log") {
      if (tokens.size() != 1 || current_priv() < 7) {
        output_invalid();
        continue;
      }
      continue;
    }

    if (cmd == "report") {
      if (tokens.size() != 2 || current_priv() < 7) {
        output_invalid();
        continue;
      }
      if (tokens[1] != "finance" && tokens[1] != "employee") {
        output_invalid();
        continue;
      }
      continue;
    }

    output_invalid();
  }

  SaveState(users, books, finance_records);
  return 0;
}
