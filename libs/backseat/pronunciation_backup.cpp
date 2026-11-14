#include "backseat/pronunciation_backup.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace backseat::pronunciation
{
namespace
{
// Just enough JSON for a pronunciation file: objects, arrays, strings (with \u escapes and
// surrogate pairs), numbers, true/false/null. Any error ends the parse with nullopt.
struct Json
{
  enum class Kind
  {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
  } kind = Kind::Null;
  bool flag = false;
  std::string text;  // string value, or the number as written
  std::vector<Json> items;
  std::map<std::string, Json> fields;
};

class Parser
{
public:
  explicit Parser(std::string const & s) : m_s(s) {}

  std::optional<Json> Document()
  {
    auto v = Value(0);
    Space();
    if (!v || m_i != m_s.size())
      return std::nullopt;
    return v;
  }

private:
  static constexpr int kMaxDepth = 32;

  void Space()
  {
    while (m_i < m_s.size() && (m_s[m_i] == ' ' || m_s[m_i] == '\t' || m_s[m_i] == '\n' || m_s[m_i] == '\r'))
      ++m_i;
  }

  bool Eat(char c)
  {
    Space();
    if (m_i < m_s.size() && m_s[m_i] == c)
    {
      ++m_i;
      return true;
    }
    return false;
  }

  bool Word(char const * w)
  {
    std::string const word(w);
    if (m_s.compare(m_i, word.size(), word) != 0)
      return false;
    m_i += word.size();
    return true;
  }

  static void Utf8(uint32_t cp, std::string & out)
  {
    if (cp < 0x80)
      out += static_cast<char>(cp);
    else if (cp < 0x800)
    {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  std::optional<uint32_t> Hex4()
  {
    if (m_i + 4 > m_s.size())
      return std::nullopt;
    uint32_t v = 0;
    for (int k = 0; k < 4; ++k)
    {
      char const c = m_s[m_i++];
      v <<= 4;
      if (c >= '0' && c <= '9')
        v |= c - '0';
      else if (c >= 'a' && c <= 'f')
        v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        v |= c - 'A' + 10;
      else
        return std::nullopt;
    }
    return v;
  }

  std::optional<std::string> String()
  {
    if (!Eat('"'))
      return std::nullopt;
    std::string out;
    while (m_i < m_s.size())
    {
      char const c = m_s[m_i++];
      if (c == '"')
        return out;
      if (static_cast<unsigned char>(c) < 0x20)
        return std::nullopt;
      if (c != '\\')
      {
        out += c;
        continue;
      }
      if (m_i >= m_s.size())
        return std::nullopt;
      switch (char const e = m_s[m_i++])
      {
      case '"':
      case '\\':
      case '/': out += e; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      case 't': out += '\t'; break;
      case 'u':
      {
        auto cp = Hex4();
        if (!cp)
          return std::nullopt;
        if (*cp >= 0xD800 && *cp < 0xDC00 && m_s.compare(m_i, 2, "\\u") == 0)
        {
          m_i += 2;
          auto const low = Hex4();
          if (!low || *low < 0xDC00 || *low > 0xDFFF)
            return std::nullopt;
          cp = 0x10000 + ((*cp - 0xD800) << 10) + (*low - 0xDC00);
        }
        else if (*cp >= 0xD800 && *cp <= 0xDFFF)
          cp = 0xFFFD;  // a lone surrogate
        Utf8(*cp, out);
        break;
      }
      default: return std::nullopt;
      }
    }
    return std::nullopt;
  }

  std::optional<Json> Value(int depth)
  {
    if (depth > kMaxDepth)
      return std::nullopt;
    Space();
    if (m_i >= m_s.size())
      return std::nullopt;
    Json v;
    char const c = m_s[m_i];
    if (c == '"')
    {
      auto s = String();
      if (!s)
        return std::nullopt;
      v.kind = Json::Kind::String;
      v.text = std::move(*s);
    }
    else if (c == '{')
    {
      ++m_i;
      v.kind = Json::Kind::Object;
      if (Eat('}'))
        return v;
      do
      {
        auto key = String();
        if (!key || !Eat(':'))
          return std::nullopt;
        auto item = Value(depth + 1);
        if (!item)
          return std::nullopt;
        v.fields[*key] = std::move(*item);
      } while (Eat(','));
      if (!Eat('}'))
        return std::nullopt;
    }
    else if (c == '[')
    {
      ++m_i;
      v.kind = Json::Kind::Array;
      if (Eat(']'))
        return v;
      do
      {
        auto item = Value(depth + 1);
        if (!item)
          return std::nullopt;
        v.items.push_back(std::move(*item));
      } while (Eat(','));
      if (!Eat(']'))
        return std::nullopt;
    }
    else if (Word("true") || Word("false"))
    {
      v.kind = Json::Kind::Bool;
      v.flag = c == 't';
    }
    else if (Word("null"))
      v.kind = Json::Kind::Null;
    else if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
    {
      auto const start = m_i++;
      while (m_i < m_s.size() && (std::isdigit(static_cast<unsigned char>(m_s[m_i])) || m_s[m_i] == '.' ||
                                  m_s[m_i] == 'e' || m_s[m_i] == 'E' || m_s[m_i] == '+' || m_s[m_i] == '-'))
        ++m_i;
      v.kind = Json::Kind::Number;
      v.text = m_s.substr(start, m_i - start);
    }
    else
      return std::nullopt;
    return v;
  }

  std::string const & m_s;
  size_t m_i = 0;
};

std::string Field(Json const & o, char const * key)
{
  auto const it = o.fields.find(key);
  return it != o.fields.end() && it->second.kind == Json::Kind::String ? it->second.text : "";
}

// One line of the SetUserWords list: no tabs or breaks inside, no space at the ends.
std::string OneLine(std::string s)
{
  for (auto & c : s)
    if (c == '\t' || c == '\n' || c == '\r')
      c = ' ';
  auto const first = s.find_first_not_of(' ');
  if (first == std::string::npos)
    return "";
  return s.substr(first, s.find_last_not_of(' ') - first + 1);
}

std::string Lower(std::string s)
{
  for (auto & c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

void Quote(std::string const & s, std::string & out)
{
  out += '"';
  for (char const c : s)
  {
    switch (c)
    {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (static_cast<unsigned char>(c) < 0x20)
      {
        char buf[8];
        std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
        out += buf;
      }
      else
        out += c;
    }
  }
  out += '"';
}

uint64_t Fnv(std::string const & s, uint64_t h)
{
  for (unsigned char const c : s)
    h = (h ^ c) * 1099511628211ULL;
  return h;
}

// A UUID-shaped id (Swift decodes it as UUID) that stays the same for the same row, so two
// exports of an unchanged list are identical.
std::string StableId(size_t index, std::string const & word)
{
  auto const key = std::to_string(index) + '\t' + word;
  uint64_t const a = Fnv(key, 14695981039346656037ULL);
  uint64_t const b = Fnv(key, a ^ 0x9E3779B97F4A7C15ULL);
  char buf[40];
  std::snprintf(buf, sizeof buf, "%08X-%04X-4%03X-%04X-%012llX", static_cast<unsigned>(a >> 32),
                static_cast<unsigned>((a >> 16) & 0xFFFF), static_cast<unsigned>(a & 0xFFF),
                static_cast<unsigned>(0x8000 | ((b >> 48) & 0x3FFF)),
                static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFULL));
  return buf;
}
}  // namespace

std::string FromBackupJson(std::string const & json)
{
  auto const root = Parser(json).Document();
  if (!root || root->kind != Json::Kind::Object)
    return "";
  auto const entries = root->fields.find("entries");
  if (entries == root->fields.end() || entries->second.kind != Json::Kind::Array)
    return "";
  std::string lines;
  std::set<std::string> seen;
  for (auto const & e : entries->second.items)
  {
    if (e.kind != Json::Kind::Object)
      continue;
    auto const pattern = e.fields.find("isPattern");
    if (pattern != e.fields.end() && pattern->second.kind == Json::Kind::Bool && pattern->second.flag)
      continue;
    auto const word = OneLine(Field(e, "word"));
    auto const said = OneLine(Field(e, "pronunciation"));
    if (word.empty() || said.empty() || !seen.insert(Lower(word)).second)
      continue;
    lines += word + '\t' + said + '\n';
  }
  return lines;
}

std::string ToBackupJson(std::string const & lines, std::string const & name, long long unixSeconds)
{
  std::string out = "{\"name\":";
  Quote(name, out);
  out += ",\"exportDate\":" + std::to_string(unixSeconds - 978307200LL) + ",\"entries\":[";
  size_t count = 0;
  size_t start = 0;
  while (start < lines.size())
  {
    auto end = lines.find('\n', start);
    if (end == std::string::npos)
      end = lines.size();
    auto const line = lines.substr(start, end - start);
    start = end + 1;
    auto const tab = line.find('\t');
    if (tab == std::string::npos)
      continue;
    auto const word = OneLine(line.substr(0, tab));
    auto const said = OneLine(line.substr(tab + 1));
    if (word.empty() || said.empty())
      continue;
    out += count ? ",{\"id\":" : "{\"id\":";
    Quote(StableId(count, word), out);
    out += ",\"word\":";
    Quote(word, out);
    out += ",\"pronunciation\":";
    Quote(said, out);
    out += ",\"isPattern\":false,\"source\":\"manual\"}";
    ++count;
  }
  out += "],\"bookTitles\":[]}";
  return out;
}
}  // namespace backseat::pronunciation
