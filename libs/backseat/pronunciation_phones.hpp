#pragma once

// Internal to libs/backseat: phone numbers read digit by digit in their groups (PhoneNumbers), used by
// ForSpeech() in pronunciation.cpp. Included only by pronunciation.cpp (internal linkage).
// Not public API; hosts include pronunciation.hpp.
//
// Without it "Call 512-555-0134" was "five hundred twelve-five hundred fifty five-one hundred thirty
// four", "1-800-555-0199" "one-eight hundred-..." and "Call 911" "nine hundred eleven" (loop B168,
// BI-148). Golden cases in backseat_tests/spoken_phone_numbers.json, shared with BookLyre (CROSS).
// Each digit is a word, 0 is "oh", groups are joined by a comma pause:
//   - North American numbers with separators: "512-555-0134", "512.555.0134", "(512) 555-0134",
//     "+1 512 555 0134", "1-800-555-0199" -> "one eight hundred, five five five, oh one nine nine".
//     The area code and exchange start 2-9; without parentheses the two separators match, and spaces
//     only count after "+1". A toll-free 8x00 area code is "eight hundred"; 888 stays "eight eight eight".
//   - Vanity numbers: "1-800-FLOWERS" -> "one eight hundred FLOWERS".
//   - Seven digits "555-0134" only right after a phone word (call, dial, text, phone, tel, fax, number).
//   - N11 codes "911", "411" after call/dial/text or before call/operator/dispatcher, and "9-1-1" anywhere;
//     "Porsche 911" is a number, not a phone code.
// Left as written: ranges ("3-5 miles", "200-1000"), years ("1914-1918"), scores ("21-14"), dates,
// I-405, ZIP+4, amounts. The output has no digits, so the later passes leave it alone (idempotent).

#include <cctype>
#include <string>
#include <string_view>

namespace backseat::pronunciation
{
namespace
{
namespace phones
{
constexpr char const * kDigitWords[] = {"oh", "one", "two", "three", "four",
                                        "five", "six", "seven", "eight", "nine"};
constexpr std::string_view kCallWords[] = {"call",  "calling", "called", "dial",   "dialed", "dialing",
                                           "dialled", "text",  "texting", "phone",  "tel",    "fax",
                                           "number", "ring",   "cell",   "mobile", "ph"};
constexpr std::string_view kN11After[] = {"call", "calls", "operator", "operators", "dispatcher",
                                          "dispatchers", "dispatch", "emergency"};

bool Dig(char c) { return c >= '0' && c <= '9'; }
bool Alnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

std::string Spell(std::string_view digits)
{
  std::string out;
  for (char c : digits)
  {
    if (!out.empty())
      out += ' ';
    out += kDigitWords[c - '0'];
  }
  return out;
}

std::string Area(std::string_view a)
{
  if (a[0] == '8' && a[1] == '0' && a[2] == '0')
    return "eight hundred";
  return Spell(a);
}

/// Counts n digits at i.
bool DigitsAt(std::string const & s, size_t i, size_t n)
{
  if (i + n > s.size())
    return false;
  for (size_t k = 0; k < n; ++k)
    if (!Dig(s[i + k]))
      return false;
  return true;
}

/// Nothing glued on the left: no letter, digit or number punctuation.
bool CleanLeft(std::string const & s, size_t i)
{
  if (i == 0)
    return true;
  char const c = s[i - 1];
  return !(Alnum(c) || c == '-' || c == '.' || c == '+' || c == '/' || c == '$' || c == ',' || c == ':' ||
           c == '#' || c == '_' || c == '@');
}

/// Nothing glued on the right: no letter or digit, and no "-5", ".5", ",5", ":5", "/5" continuation.
bool CleanRight(std::string const & s, size_t j)
{
  if (j >= s.size())
    return true;
  char const c = s[j];
  if (Alnum(c) || c == '_' || c == '@' || c == '%')
    return false;
  if ((c == '-' || c == '.' || c == ',' || c == ':' || c == '/') && j + 1 < s.size() && Alnum(s[j + 1]))
    return false;
  return true;
}

std::string Lower(std::string_view w)
{
  std::string out(w);
  for (auto & c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

/// The word just before i (skipping spaces, an opening "the", ':' or '.' after "Tel"), lowercased.
std::string WordBefore(std::string const & s, size_t i)
{
  size_t e = i;
  while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == ':' || s[e - 1] == '.'))
    --e;
  size_t b = e;
  while (b > 0 && std::isalpha(static_cast<unsigned char>(s[b - 1])))
    --b;
  return Lower(std::string_view(s).substr(b, e - b));
}

/// The word right after j (one space), lowercased.
std::string WordAfter(std::string const & s, size_t j)
{
  if (j >= s.size() || s[j] != ' ')
    return {};
  size_t b = j + 1, e = b;
  while (e < s.size() && std::isalpha(static_cast<unsigned char>(s[e])))
    ++e;
  return Lower(std::string_view(s).substr(b, e - b));
}

template <size_t N>
bool In(std::string const & w, std::string_view const (&list)[N])
{
  for (auto const & x : list)
    if (w == x)
      return true;
  return false;
}

bool CallWordBefore(std::string const & s, size_t i) { return In(WordBefore(s, i), kCallWords); }

/// A full North American number starting at i ("+1 ", "1-", "(512) ", "512-"); returns its end and
/// fills said, or npos.
size_t FullNumber(std::string const & s, size_t i, std::string & said)
{
  size_t k = i;
  std::string prefix;
  bool plusOne = false;
  if (s[k] == '+' && k + 1 < s.size() && s[k + 1] == '1')
  {
    plusOne = true;
    prefix = "plus one, ";
    k += 2;
    if (k < s.size() && (s[k] == ' ' || s[k] == '-' || s[k] == '.'))
      ++k;
  }
  else if (s[k] == '1' && k + 1 < s.size() && (s[k + 1] == '-' || s[k + 1] == ' ' || s[k + 1] == '.'))
  {
    prefix = "one ";
    k += 2;
  }
  size_t areaAt;
  char sep = 0;
  bool parens = false;
  if (k < s.size() && s[k] == '(')
  {
    if (!DigitsAt(s, k + 1, 3) || k + 4 >= s.size() || s[k + 4] != ')')
      return std::string::npos;
    parens = true;
    areaAt = k + 1;
    k += 5;
    if (k < s.size() && s[k] == ' ')
      ++k;
  }
  else
  {
    if (!DigitsAt(s, k, 3) || k + 3 >= s.size())
      return std::string::npos;
    areaAt = k;
    sep = s[k + 3];
    if (sep != '-' && sep != '.' && !(sep == ' ' && plusOne))
      return std::string::npos;
    k += 4;
  }
  if (!DigitsAt(s, k, 3) || k + 3 >= s.size())
    return std::string::npos;
  size_t const exchAt = k;
  char const sep2 = s[k + 3];
  if (parens ? (sep2 != '-' && sep2 != '.' && sep2 != ' ') : sep2 != sep)
    return std::string::npos;
  k += 4;
  if (!DigitsAt(s, k, 4))
    return std::string::npos;
  k += 4;
  if (s[areaAt] < '2' || s[exchAt] < '2' || !CleanRight(s, k))
    return std::string::npos;
  said = prefix + Area(std::string_view(s).substr(areaAt, 3)) + ", " +
         Spell(std::string_view(s).substr(exchAt, 3)) + ", " + Spell(std::string_view(s).substr(exchAt + 4, 4));
  return k;
}

/// "1-800-FLOWERS", "800-GOT-JUNK": a toll-free area code then capital letters.
size_t VanityNumber(std::string const & s, size_t i, std::string & said)
{
  size_t k = i;
  std::string prefix;
  if (s[k] == '1' && k + 1 < s.size() && (s[k + 1] == '-' || s[k + 1] == ' '))
  {
    prefix = "one ";
    k += 2;
  }
  if (!DigitsAt(s, k, 3) || s[k] != '8' || s[k + 1] != s[k + 2] || k + 3 >= s.size() || s[k + 3] != '-')
    return std::string::npos;
  std::string const area = Area(std::string_view(s).substr(k, 3));
  k += 4;
  std::string letters;
  size_t total = 0;
  while (k < s.size() && std::isupper(static_cast<unsigned char>(s[k])))
  {
    size_t e = k;
    while (e < s.size() && std::isupper(static_cast<unsigned char>(s[e])))
      ++e;
    if (!letters.empty())
      letters += ' ';
    letters.append(s, k, e - k);
    total += e - k;
    k = e;
    if (k + 1 < s.size() && s[k] == '-' && std::isupper(static_cast<unsigned char>(s[k + 1])))
      ++k;
    else
      break;
  }
  if (total < 4 || (k < s.size() && (Alnum(s[k]) || s[k] == '_')))
    return std::string::npos;
  said = prefix + area + " " + letters;
  return k;
}

/// "555-0134" after a phone word.
size_t LocalNumber(std::string const & s, size_t i, std::string & said)
{
  if (!DigitsAt(s, i, 3) || s[i] < '2' || i + 3 >= s.size() || s[i + 3] != '-' || !DigitsAt(s, i + 4, 4))
    return std::string::npos;
  size_t const k = i + 8;
  if (!CleanRight(s, k) || !CallWordBefore(s, i))
    return std::string::npos;
  said = Spell(std::string_view(s).substr(i, 3)) + ", " + Spell(std::string_view(s).substr(i + 4, 4));
  return k;
}

/// "911" after call/dial or before call/operator; "9-1-1" anywhere.
size_t N11(std::string const & s, size_t i, std::string & said)
{
  if (i + 4 < s.size() && s[i] >= '2' && s[i] <= '9' && s[i + 1] == '-' && s[i + 2] == '1' &&
      s[i + 3] == '-' && s[i + 4] == '1' && CleanRight(s, i + 5))
  {
    said = Spell(std::string_view(s).substr(i, 1)) + " one one";
    return i + 5;
  }
  if (!DigitsAt(s, i, 3) || s[i] < '2' || s[i + 1] != '1' || s[i + 2] != '1' || !CleanRight(s, i + 3))
    return std::string::npos;
  if (!CallWordBefore(s, i) && !In(WordAfter(s, i + 3), kN11After))
    return std::string::npos;
  said = Spell(std::string_view(s).substr(i, 1)) + " one one";
  return i + 3;
}
}  // namespace phones

// "Call 512-555-0134" -> "Call five one two, five five five, oh one three four", "dial 911" -> "dial
// nine one one".
std::string PhoneNumbers(std::string const & s)
{
  using namespace phones;
  std::string out;
  size_t copied = 0;
  for (size_t i = 0; i < s.size(); ++i)
  {
    char const c = s[i];
    if (!(Dig(c) || c == '+' || c == '('))
      continue;
    if (!CleanLeft(s, i))
    {
      // Skip the rest of a glued token ("A1", "3.14", "$555").
      while (i + 1 < s.size() && (Alnum(s[i + 1])))
        ++i;
      continue;
    }
    std::string said;
    size_t end = FullNumber(s, i, said);
    if (end == std::string::npos)
      end = VanityNumber(s, i, said);
    if (end == std::string::npos)
      end = LocalNumber(s, i, said);
    if (end == std::string::npos)
      end = N11(s, i, said);
    if (end == std::string::npos)
    {
      while (i + 1 < s.size() && Dig(s[i + 1]))
        ++i;
      continue;
    }
    out.append(s, copied, i - copied);
    out += said;
    copied = end;
    i = end - 1;
  }
  if (copied == 0)
    return s;
  out.append(s, copied, std::string::npos);
  return out;
}
}  // namespace
}  // namespace backseat::pronunciation
