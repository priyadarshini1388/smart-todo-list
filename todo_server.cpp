// =============================================================================
//  Smart To-Do List  -  C++ REST backend (single file, SQLite storage)
//
//  Build on Windows (MSYS2 UCRT64 g++, run from any terminal once its bin is on PATH):
//      g++ -std=c++17 -O2 todo_server.cpp -lsqlite3 -lws2_32 -o todo_server.exe
//      (one-time:  pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sqlite3)
//
//  Build on Linux / macOS:
//      g++ -std=c++17 -O2 -pthread todo_server.cpp -lsqlite3 -o todo_server
//
//  Run:
//      todo_server.exe [port=8080] [db_file=todo.db] [index_html=index.html]
//
//  Then open http://localhost:8080  (the server also serves index.html)
//
//  REST API
//      GET    /api/health
//      GET    /api/tasks?q=&status=&priority=&category=&sort=
//                 status   : all | active | completed | overdue | today
//                 priority : low | medium | high
//                 sort     : smart | due | priority | newest | oldest | title
//      POST   /api/tasks                 body: {title, description, priority, category, due_date, completed}
//      GET    /api/tasks/{id}
//      PUT    /api/tasks/{id}            body: any subset of the fields above
//      PATCH  /api/tasks/{id}/toggle     flips the completed flag
//      DELETE /api/tasks/{id}
//      DELETE /api/tasks?completed=true  removes every completed task
//      GET    /api/stats
//
//  No third-party libraries other than SQLite: HTTP and JSON are handled here.
// =============================================================================

#include <sqlite3.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
  #endif
  typedef SOCKET socket_t;
  #define CLOSESOCK closesocket
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <signal.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  typedef int socket_t;
  #define CLOSESOCK close
  #ifndef INVALID_SOCKET
    #define INVALID_SOCKET (-1)
  #endif
#endif
#ifndef MSG_NOSIGNAL
  #define MSG_NOSIGNAL 0
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std;

static sqlite3* g_db = nullptr;
static mutex g_mu;                       // serialises all DB access
static string g_indexPath = "index.html";

// -----------------------------------------------------------------------------
//  Small string helpers
// -----------------------------------------------------------------------------
static string trim(const string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}
static string lower(string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
static string urlDecode(const string& s) {
    string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) &&
                 isxdigit((unsigned char)s[i + 2])) {
            o += (char)stoi(s.substr(i + 1, 2), nullptr, 16);
            i += 2;
        } else o += s[i];
    }
    return o;
}

// -----------------------------------------------------------------------------
//  Minimal JSON support (output escaping + parser for flat objects)
// -----------------------------------------------------------------------------
static string jesc(const string& s) {
    string o;
    o.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += (char)c;
        }
    }
    return o;
}
static string jstr(const string& s) { return "\"" + jesc(s) + "\""; }

struct JVal {
    enum Type { Null, Bool, Num, Str };
    Type t = Null;
    bool b = false;
    double n = 0;
    string s;
};
typedef map<string, JVal> JObj;

class JParser {
    const string& s;
    size_t i = 0;
    void ws() { while (i < s.size() && isspace((unsigned char)s[i])) i++; }
    static void utf8(string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
        } else {
            o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F));
            o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& v) {
        if (i + 4 > s.size()) return false;
        v = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return false;
        }
        return true;
    }
    bool str(string& o) {
        if (i >= s.size() || s[i] != '"') return false;
        i++;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { o += c; continue; }
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case '"': case '\\': case '/': o += e; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        unsigned lo;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(o, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

public:
    explicit JParser(const string& x) : s(x) {}
    // Parses a flat object ({"key": string|number|bool|null, ...}).
    bool parse(JObj& out) {
        ws();
        if (i >= s.size() || s[i] != '{') return false;
        i++; ws();
        if (i < s.size() && s[i] == '}') return true;
        while (true) {
            ws();
            string k;
            if (!str(k)) return false;
            ws();
            if (i >= s.size() || s[i] != ':') return false;
            i++; ws();
            if (i >= s.size()) return false;
            JVal v;
            char c = s[i];
            if (c == '"') { v.t = JVal::Str; if (!str(v.s)) return false; }
            else if (s.compare(i, 4, "true") == 0) { v.t = JVal::Bool; v.b = true; i += 4; }
            else if (s.compare(i, 5, "false") == 0) { v.t = JVal::Bool; v.b = false; i += 5; }
            else if (s.compare(i, 4, "null") == 0) { i += 4; }
            else if (c == '-' || isdigit((unsigned char)c)) {
                char* end;
                v.n = strtod(s.c_str() + i, &end);
                size_t len = (size_t)(end - (s.c_str() + i));
                if (len == 0) return false;
                i += len;
                v.t = JVal::Num;
            } else return false;     // nested objects / arrays are not supported
            out[k] = v;
            ws();
            if (i >= s.size()) return false;
            if (s[i] == ',') { i++; continue; }
            if (s[i] == '}') { i++; return true; }
            return false;
        }
    }
};

// -----------------------------------------------------------------------------
//  SQLite helpers
// -----------------------------------------------------------------------------
struct Stmt {
    sqlite3_stmt* s = nullptr;
    explicit Stmt(const string& sql) {
        if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &s, nullptr) != SQLITE_OK)
            throw runtime_error(sqlite3_errmsg(g_db));
    }
    ~Stmt() { sqlite3_finalize(s); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    void bindText(int i, const string& v) { sqlite3_bind_text(s, i, v.c_str(), -1, SQLITE_TRANSIENT); }
    void bindInt(int i, sqlite3_int64 v) { sqlite3_bind_int64(s, i, v); }
    bool step() {
        int r = sqlite3_step(s);
        if (r == SQLITE_ROW) return true;
        if (r == SQLITE_DONE) return false;
        throw runtime_error(sqlite3_errmsg(g_db));
    }
    string text(int c) {
        const unsigned char* p = sqlite3_column_text(s, c);
        return p ? string((const char*)p) : string();
    }
    sqlite3_int64 i64(int c) { return sqlite3_column_int64(s, c); }
    bool isNull(int c) { return sqlite3_column_type(s, c) == SQLITE_NULL; }
};

static void execSql(const string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(g_db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        string m = err ? err : "sqlite error";
        sqlite3_free(err);
        throw runtime_error(m);
    }
}
static sqlite3_int64 scalar(const string& sql) {
    Stmt st(sql);
    return st.step() ? st.i64(0) : 0;
}

static const char* TASK_COLS =
    "id,title,description,priority,category,due_date,completed,created_at,completed_at";

static string taskJson(Stmt& st) {
    ostringstream o;
    o << "{\"id\":" << st.i64(0)
      << ",\"title\":" << jstr(st.text(1))
      << ",\"description\":" << jstr(st.text(2))
      << ",\"priority\":" << jstr(st.text(3))
      << ",\"category\":" << jstr(st.text(4))
      << ",\"due_date\":" << jstr(st.text(5))
      << ",\"completed\":" << (st.i64(6) ? "true" : "false")
      << ",\"created_at\":" << jstr(st.text(7))
      << ",\"completed_at\":" << (st.isNull(8) ? string("null") : jstr(st.text(8)))
      << "}";
    return o.str();
}

static void initDb(const string& path) {
    if (sqlite3_open(path.c_str(), &g_db) != SQLITE_OK)
        throw runtime_error(string("Cannot open database: ") + sqlite3_errmsg(g_db));
    sqlite3_busy_timeout(g_db, 5000);
    execSql("PRAGMA journal_mode=WAL;");
    execSql("PRAGMA foreign_keys=ON;");
    execSql(
        "CREATE TABLE IF NOT EXISTS tasks ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " title TEXT NOT NULL,"
        " description TEXT NOT NULL DEFAULT '',"
        " priority TEXT NOT NULL DEFAULT 'medium' CHECK(priority IN ('low','medium','high')),"
        " category TEXT NOT NULL DEFAULT 'General',"
        " due_date TEXT NOT NULL DEFAULT '',"
        " completed INTEGER NOT NULL DEFAULT 0,"
        " created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),"
        " completed_at TEXT);"
        "CREATE INDEX IF NOT EXISTS idx_tasks_due ON tasks(due_date);"
        "CREATE INDEX IF NOT EXISTS idx_tasks_completed ON tasks(completed);");
}

// -----------------------------------------------------------------------------
//  Task model + validation
// -----------------------------------------------------------------------------
struct Task {
    sqlite3_int64 id = 0;
    string title, description, priority = "medium", category = "General", due_date;
    bool completed = false;
};

static bool validDate(const string& d) {
    if (d.empty()) return true;
    if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
    for (int k : {0, 1, 2, 3, 5, 6, 8, 9})
        if (!isdigit((unsigned char)d[k])) return false;
    int m = stoi(d.substr(5, 2)), dd = stoi(d.substr(8, 2));
    return m >= 1 && m <= 12 && dd >= 1 && dd <= 31;
}

// Overlay the fields present in `o` on `t`, then validate.
static bool applyJson(Task& t, const JObj& o, string& err) {
    auto getS = [&](const char* k, string& dst) -> bool {
        auto it = o.find(k);
        if (it == o.end()) return true;
        if (it->second.t == JVal::Null) { dst.clear(); return true; }
        if (it->second.t != JVal::Str) { err = string("'") + k + "' must be a string"; return false; }
        dst = it->second.s;
        return true;
    };
    if (!getS("title", t.title) || !getS("description", t.description) || !getS("priority", t.priority) ||
        !getS("category", t.category) || !getS("due_date", t.due_date))
        return false;
    auto it = o.find("completed");
    if (it != o.end()) {
        if (it->second.t == JVal::Bool) t.completed = it->second.b;
        else if (it->second.t == JVal::Num) t.completed = it->second.n != 0;
        else { err = "'completed' must be a boolean"; return false; }
    }
    t.title = trim(t.title);
    t.description = trim(t.description);
    t.priority = lower(trim(t.priority));
    t.category = trim(t.category);
    t.due_date = trim(t.due_date);
    if (t.category.empty()) t.category = "General";
    if (t.title.empty()) { err = "Title is required"; return false; }
    if (t.title.size() > 200) { err = "Title must be 200 characters or fewer"; return false; }
    if (t.description.size() > 2000) { err = "Description is too long (max 2000)"; return false; }
    if (t.category.size() > 40) { err = "Category must be 40 characters or fewer"; return false; }
    if (t.priority != "low" && t.priority != "medium" && t.priority != "high") {
        err = "Priority must be low, medium or high";
        return false;
    }
    if (!validDate(t.due_date)) { err = "due_date must be YYYY-MM-DD"; return false; }
    return true;
}

// -----------------------------------------------------------------------------
//  HTTP plumbing
// -----------------------------------------------------------------------------
struct Req {
    string method, path, body;
    map<string, string> query, headers;
};
struct Resp {
    int status = 200;
    string body;
    string ctype = "application/json; charset=utf-8";
};

static Resp jsonResp(int status, const string& body) { Resp r; r.status = status; r.body = body; return r; }
static Resp errResp(int status, const string& msg) { return jsonResp(status, "{\"error\":" + jstr(msg) + "}"); }

static const char* statusText(int s) {
    switch (s) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        default:  return "Internal Server Error";
    }
}

static bool readRequest(socket_t fd, Req& r) {
    string buf;
    char tmp[4096];
    size_t hend;
    while ((hend = buf.find("\r\n\r\n")) == string::npos) {
        int n = (int)recv(fd, tmp, (int)sizeof tmp, 0);
        if (n <= 0) return false;
        buf.append(tmp, (size_t)n);
        if (buf.size() > 65536) return false;
    }
    istringstream hs(buf.substr(0, hend));
    string line;
    getline(hs, line);
    istringstream rl(line);
    string target, version;
    rl >> r.method >> target >> version;
    if (r.method.empty() || target.empty()) return false;
    while (getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t c = line.find(':');
        if (c == string::npos) continue;
        r.headers[lower(trim(line.substr(0, c)))] = trim(line.substr(c + 1));
    }
    size_t q = target.find('?');
    r.path = urlDecode(target.substr(0, q));
    if (q != string::npos) {
        istringstream qs(target.substr(q + 1));
        string kv;
        while (getline(qs, kv, '&')) {
            size_t e = kv.find('=');
            string k = urlDecode(kv.substr(0, e));
            string v = e == string::npos ? "" : urlDecode(kv.substr(e + 1));
            r.query[k] = v;
        }
    }
    r.body = buf.substr(hend + 4);
    size_t cl = 0;
    auto it = r.headers.find("content-length");
    if (it != r.headers.end()) cl = (size_t)strtoull(it->second.c_str(), nullptr, 10);
    if (cl > 1024 * 1024) return false;
    while (r.body.size() < cl) {
        int n = (int)recv(fd, tmp, (int)sizeof tmp, 0);
        if (n <= 0) return false;
        r.body.append(tmp, (size_t)n);
    }
    return true;
}

static void sendAll(socket_t fd, const string& data) {
    size_t off = 0;
    while (off < data.size()) {
        int n = (int)send(fd, data.data() + off, (int)min<size_t>(data.size() - off, 1 << 20), MSG_NOSIGNAL);
        if (n <= 0) return;
        off += (size_t)n;
    }
}

static void writeResponse(socket_t fd, const Resp& r) {
    ostringstream h;
    h << "HTTP/1.1 " << r.status << " " << statusText(r.status) << "\r\n"
      << "Content-Type: " << r.ctype << "\r\n"
      << "Content-Length: " << r.body.size() << "\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Access-Control-Allow-Methods: GET, POST, PUT, PATCH, DELETE, OPTIONS\r\n"
      << "Access-Control-Allow-Headers: Content-Type\r\n"
      << "Cache-Control: no-store\r\n"
      << "Connection: close\r\n\r\n";
    sendAll(fd, h.str() + r.body);
}

// -----------------------------------------------------------------------------
//  API handlers
// -----------------------------------------------------------------------------
static Resp getTask(sqlite3_int64 id) {
    Stmt st(string("SELECT ") + TASK_COLS + " FROM tasks WHERE id=?1");
    st.bindInt(1, id);
    if (!st.step()) return errResp(404, "Task not found");
    return jsonResp(200, taskJson(st));
}

static Resp listTasks(const Req& r) {
    auto q = [&](const char* k) { auto it = r.query.find(k); return it == r.query.end() ? string() : it->second; };
    vector<string> where, binds;
    string search = trim(q("q"));
    if (!search.empty()) {
        where.push_back("(title LIKE ? OR description LIKE ? OR category LIKE ?)");
        for (int i = 0; i < 3; i++) binds.push_back("%" + search + "%");
    }
    string status = lower(q("status"));
    if (status == "active") where.push_back("completed=0");
    else if (status == "completed") where.push_back("completed=1");
    else if (status == "overdue")
        where.push_back("(completed=0 AND due_date<>'' AND due_date<date('now','localtime'))");
    else if (status == "today") where.push_back("(completed=0 AND due_date=date('now','localtime'))");
    string pr = lower(q("priority"));
    if (!pr.empty()) { where.push_back("priority=?"); binds.push_back(pr); }
    string cat = q("category");
    if (!cat.empty()) { where.push_back("category=? COLLATE NOCASE"); binds.push_back(cat); }

    string sort = lower(q("sort"));
    string order;
    const string prank = "CASE priority WHEN 'high' THEN 0 WHEN 'medium' THEN 1 ELSE 2 END";
    const string dueRank = "CASE WHEN due_date='' THEN 1 ELSE 0 END, due_date";
    if (sort == "due") order = "completed, " + dueRank + ", " + prank + ", id DESC";
    else if (sort == "priority") order = "completed, " + prank + ", " + dueRank + ", id DESC";
    else if (sort == "newest") order = "id DESC";
    else if (sort == "oldest") order = "id ASC";
    else if (sort == "title") order = "completed, title COLLATE NOCASE";
    else  // "smart": open tasks first; overdue, then due today, then priority, then soonest due date
        order = "completed, "
                "CASE WHEN due_date<>'' AND due_date<date('now','localtime') THEN 0 "
                "     WHEN due_date=date('now','localtime') THEN 1 ELSE 2 END, " +
                prank + ", " + dueRank + ", id DESC";

    string sql = string("SELECT ") + TASK_COLS + " FROM tasks";
    for (size_t i = 0; i < where.size(); i++) sql += (i ? " AND " : " WHERE ") + where[i];
    sql += " ORDER BY " + order + " LIMIT 1000";

    Stmt st(sql);
    for (size_t i = 0; i < binds.size(); i++) st.bindText((int)i + 1, binds[i]);
    string out = "[";
    bool first = true;
    while (st.step()) {
        if (!first) out += ",";
        first = false;
        out += taskJson(st);
    }
    return jsonResp(200, out + "]");
}

static Resp createTask(const Req& r) {
    JObj o;
    if (!JParser(r.body).parse(o)) return errResp(400, "Invalid JSON body");
    Task t;
    string err;
    if (!applyJson(t, o, err)) return errResp(400, err);
    Stmt st("INSERT INTO tasks(title,description,priority,category,due_date,completed,completed_at) "
            "VALUES(?1,?2,?3,?4,?5,?6, CASE WHEN ?6=1 THEN datetime('now','localtime') ELSE NULL END)");
    st.bindText(1, t.title); st.bindText(2, t.description); st.bindText(3, t.priority);
    st.bindText(4, t.category); st.bindText(5, t.due_date); st.bindInt(6, t.completed ? 1 : 0);
    st.step();
    Resp res = getTask(sqlite3_last_insert_rowid(g_db));
    res.status = 201;
    return res;
}

static Resp updateTask(sqlite3_int64 id, const Req& r) {
    JObj o;
    if (!JParser(r.body).parse(o)) return errResp(400, "Invalid JSON body");
    Task t;
    {   // load existing values first so PUT can be partial
        Stmt st("SELECT title,description,priority,category,due_date,completed FROM tasks WHERE id=?1");
        st.bindInt(1, id);
        if (!st.step()) return errResp(404, "Task not found");
        t.title = st.text(0); t.description = st.text(1); t.priority = st.text(2);
        t.category = st.text(3); t.due_date = st.text(4); t.completed = st.i64(5) != 0;
    }
    string err;
    if (!applyJson(t, o, err)) return errResp(400, err);
    Stmt st("UPDATE tasks SET title=?1,description=?2,priority=?3,category=?4,due_date=?5,completed=?6,"
            "completed_at=CASE WHEN ?6=1 THEN COALESCE(completed_at,datetime('now','localtime')) ELSE NULL END "
            "WHERE id=?7");
    st.bindText(1, t.title); st.bindText(2, t.description); st.bindText(3, t.priority);
    st.bindText(4, t.category); st.bindText(5, t.due_date); st.bindInt(6, t.completed ? 1 : 0);
    st.bindInt(7, id);
    st.step();
    return getTask(id);
}

static Resp toggleTask(sqlite3_int64 id) {
    Stmt st("UPDATE tasks SET completed=1-completed,"
            "completed_at=CASE WHEN completed=0 THEN datetime('now','localtime') ELSE NULL END WHERE id=?1");
    st.bindInt(1, id);
    st.step();
    if (sqlite3_changes(g_db) == 0) return errResp(404, "Task not found");
    return getTask(id);
}

static Resp deleteTask(sqlite3_int64 id) {
    Stmt st("DELETE FROM tasks WHERE id=?1");
    st.bindInt(1, id);
    st.step();
    if (sqlite3_changes(g_db) == 0) return errResp(404, "Task not found");
    return jsonResp(200, "{\"deleted\":" + to_string(id) + "}");
}

static Resp clearCompleted() {
    execSql("DELETE FROM tasks WHERE completed=1");
    return jsonResp(200, "{\"deleted\":" + to_string(sqlite3_changes(g_db)) + "}");
}

static Resp stats() {
    const string today = "date('now','localtime')";
    ostringstream o;
    sqlite3_int64 total = scalar("SELECT COUNT(*) FROM tasks");
    sqlite3_int64 done = scalar("SELECT COUNT(*) FROM tasks WHERE completed=1");
    o << "{\"total\":" << total << ",\"completed\":" << done << ",\"pending\":" << (total - done)
      << ",\"overdue\":" << scalar("SELECT COUNT(*) FROM tasks WHERE completed=0 AND due_date<>'' AND due_date<" + today)
      << ",\"due_today\":" << scalar("SELECT COUNT(*) FROM tasks WHERE completed=0 AND due_date=" + today)
      << ",\"completed_today\":" << scalar("SELECT COUNT(*) FROM tasks WHERE completed=1 AND date(completed_at)=" + today)
      << ",\"high_pending\":" << scalar("SELECT COUNT(*) FROM tasks WHERE completed=0 AND priority='high'")
      << ",\"categories\":[";
    Stmt st("SELECT category,COUNT(*),SUM(completed) FROM tasks GROUP BY category COLLATE NOCASE ORDER BY category COLLATE NOCASE");
    bool first = true;
    while (st.step()) {
        if (!first) o << ",";
        first = false;
        o << "{\"name\":" << jstr(st.text(0)) << ",\"count\":" << st.i64(1) << ",\"done\":" << st.i64(2) << "}";
    }
    o << "]}";
    return jsonResp(200, o.str());
}

static Resp serveIndex() {
    ifstream f(g_indexPath, ios::binary);
    if (!f) return errResp(404, "index.html not found next to the server (looked for '" + g_indexPath + "')");
    Resp r;
    r.ctype = "text/html; charset=utf-8";
    r.body.assign(istreambuf_iterator<char>(f), istreambuf_iterator<char>());
    return r;
}

static bool parseId(const string& s, sqlite3_int64& id) {
    if (s.empty() || s.size() > 18) return false;
    for (char c : s) if (!isdigit((unsigned char)c)) return false;
    id = atoll(s.c_str());
    return true;
}

static Resp route(const Req& r) {
    const string& m = r.method;
    const string& p = r.path;
    if (m == "OPTIONS") { Resp x; x.status = 204; return x; }

    if (p == "/" || p == "/index.html") {
        if (m != "GET") return errResp(405, "Method not allowed");
        return serveIndex();
    }
    if (p == "/api/health") return jsonResp(200, "{\"status\":\"ok\"}");

    lock_guard<mutex> lock(g_mu);
    if (p == "/api/stats") {
        if (m != "GET") return errResp(405, "Method not allowed");
        return stats();
    }
    if (p == "/api/tasks") {
        if (m == "GET") return listTasks(r);
        if (m == "POST") return createTask(r);
        if (m == "DELETE") {
            auto it = r.query.find("completed");
            if (it != r.query.end() && it->second == "true") return clearCompleted();
            return errResp(400, "Use DELETE /api/tasks?completed=true or DELETE /api/tasks/{id}");
        }
        return errResp(405, "Method not allowed");
    }
    const string prefix = "/api/tasks/";
    if (p.compare(0, prefix.size(), prefix) == 0) {
        string rest = p.substr(prefix.size());
        bool toggle = false;
        const string suffix = "/toggle";
        if (rest.size() > suffix.size() && rest.compare(rest.size() - suffix.size(), suffix.size(), suffix) == 0) {
            toggle = true;
            rest.resize(rest.size() - suffix.size());
        }
        sqlite3_int64 id;
        if (!parseId(rest, id)) return errResp(400, "Invalid task id");
        if (toggle) {
            if (m != "PATCH" && m != "POST") return errResp(405, "Method not allowed");
            return toggleTask(id);
        }
        if (m == "GET") return getTask(id);
        if (m == "PUT" || m == "PATCH") return updateTask(id, r);
        if (m == "DELETE") return deleteTask(id);
        return errResp(405, "Method not allowed");
    }
    return errResp(404, "Not found");
}

static void handleClient(socket_t fd) {
#ifdef _WIN32
    DWORD tv = 10000;                       // milliseconds on Windows
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
#else
    timeval tv{10, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
#endif
    Req req;
    try {
        if (!readRequest(fd, req)) {
            writeResponse(fd, errResp(400, "Bad request"));
        } else {
            Resp res;
            try { res = route(req); }
            catch (const exception& e) { res = errResp(500, e.what()); }
            writeResponse(fd, res);
            cout << req.method << " " << req.path << " -> " << res.status << "\n";
        }
    } catch (...) {}
    CLOSESOCK(fd);
}

// -----------------------------------------------------------------------------
//  main
// -----------------------------------------------------------------------------
int main(int argc, char** argv) {
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    string dbPath = argc > 2 ? argv[2] : "todo.db";
    if (argc > 3) g_indexPath = argv[3];
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { cerr << "WSAStartup failed\n"; return 1; }
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    try { initDb(dbPath); }
    catch (const exception& e) { cerr << e.what() << "\n"; return 1; }

    socket_t srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) { cerr << "Cannot create socket\n"; return 1; }
#ifndef _WIN32
    int yes = 1;   // on Windows SO_REUSEADDR would allow port hijacking, so it is skipped
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // local only; use INADDR_ANY to expose on the network
    addr.sin_port = htons((uint16_t)port);
    if (::bind(srv, (sockaddr*)&addr, (int)sizeof addr) != 0) {
        cerr << "Cannot bind to port " << port << " (is it already in use? try another port)\n";
        return 1;
    }
    if (::listen(srv, 64) != 0) { cerr << "listen failed\n"; return 1; }

    cout << "Smart To-Do server running on http://localhost:" << port << "  (db: " << dbPath << ")" << endl;
    while (true) {
        socket_t fd = ::accept(srv, nullptr, nullptr);
        if (fd == INVALID_SOCKET) continue;
        thread(handleClient, fd).detach();
    }
}
