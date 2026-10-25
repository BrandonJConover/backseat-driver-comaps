#include "backseat/lines.hpp"

#include "backseat/lines_data.hpp"

#include <cctype>

namespace backseat
{
std::vector<std::string> const & LinesFor(std::string const & key, int tier)
{
  return LineData().at(key).at(static_cast<size_t>(tier));
}

std::string Fill(std::string const & tmpl, Vars const & vars)
{
  std::string out;
  out.reserve(tmpl.size());
  size_t i = 0;
  while (i < tmpl.size())
  {
    if (tmpl[i] == '{')
    {
      size_t j = i + 1;
      while (j < tmpl.size() && (std::isalnum(static_cast<unsigned char>(tmpl[j])) || tmpl[j] == '_'))
        ++j;
      if (j < tmpl.size() && tmpl[j] == '}' && j > i + 1)
      {
        std::string const key = tmpl.substr(i + 1, j - i - 1);
        std::string lower = key;
        std::string upper = key;
        for (auto & c : lower)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto & c : upper)
          c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        auto const it = vars.find(lower);
        if (it != vars.end())
        {
          std::string value = it->second;
          if (key == upper && key != lower)
            for (auto & c : value)
              c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          out += value;
          i = j + 1;
          continue;
        }
      }
    }
    out += tmpl[i++];
  }
  return out;
}
}  // namespace backseat
