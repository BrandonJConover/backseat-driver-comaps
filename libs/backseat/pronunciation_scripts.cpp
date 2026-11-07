// Cyrillic and Japanese kana romanised for the English voice (Cyrillic(), Kana()).
// Split out of pronunciation.cpp unchanged (loop B65); ForSpeech() calls both.

#include "backseat/pronunciation.hpp"
#include "backseat/pronunciation_text.hpp"

#include <map>
#include <string>

namespace backseat::pronunciation
{
using detail::Decode;
using detail::Encode;
using detail::Letter;

// MARK: - Cyrillic

namespace
{
bool CyrillicLetter(char32_t c)
{
  return c >= U'Ѐ' && c <= U'ӿ';
}

char32_t CyrillicLower(char32_t c)
{
  if (c >= U'А' && c <= U'Я')
    return c + 0x20;
  if (c >= U'Ѐ' && c <= U'Џ')
    return c + 0x50;
  if (c >= U'Ѡ' && c <= U'ӿ' && (c % 2) == 0)
    return c + 1;
  return c;
}

/// Reader-style romanisation, the same table as BookLyre's text/spoken_foreign_scripts.py (Russian,
/// Ukrainian, Belarusian), plus the Serbian and Macedonian letters map names use.
char const * CyrillicRoman(char32_t lower)
{
  switch (lower)
  {
  case U'а': return "a";   case U'б': return "b";  case U'в': return "v";  case U'г': return "g";
  case U'д': return "d";   case U'е': return "e";  case U'ё': return "yo"; case U'ж': return "zh";
  case U'з': return "z";   case U'и': return "i";  case U'й': return "y";  case U'к': return "k";
  case U'л': return "l";   case U'м': return "m";  case U'н': return "n";  case U'о': return "o";
  case U'п': return "p";   case U'р': return "r";  case U'с': return "s";  case U'т': return "t";
  case U'у': return "u";   case U'ф': return "f";  case U'х': return "kh"; case U'ц': return "ts";
  case U'ч': return "ch";  case U'ш': return "sh"; case U'щ': return "shch";
  case U'ы': return "y";   case U'э': return "e";  case U'ю': return "yu"; case U'я': return "ya";
  case U'і': return "i";   case U'ї': return "yi"; case U'є': return "ye"; case U'ґ': return "g";
  case U'ў': return "w";
  case U'ј': return "y";   case U'љ': return "ly"; case U'њ': return "ny"; case U'ћ': return "ch";
  case U'ђ': return "j";   case U'џ': return "j";  case U'ѓ': return "gy"; case U'ќ': return "ky";
  case U'ѕ': return "dz";
  default: return "";
  }
}

bool CyrillicVowel(char32_t lower)
{
  return std::u32string_view(U"аеёиоуыэюяіїє").find(lower) != std::u32string_view::npos;
}

std::string CyrillicWord(std::u32string const & word)
{
  std::string out;
  bool upper = true;
  char32_t prev = 0;
  for (char32_t c : word)
  {
    char32_t const low = CyrillicLower(c);
    upper = upper && low != c;
    // "е" starting a word or after a vowel or sign is "ye": Елена Yelena, моё moyo.
    if (low == U'е' && (!prev || CyrillicVowel(prev) || prev == U'ъ' || prev == U'ь'))
      out += "ye";
    else
      out += CyrillicRoman(low);
    prev = low;
  }
  if (out.empty())
    return out;
  // A vowelless acronym in capitals is spelled ("СССР" is "S, S, S, R"); a shouted word reads as a word.
  if (upper && word.size() > 1 && out.find_first_of("aeiouy") == std::string::npos)
  {
    std::string spelled;
    for (char c : out)
      spelled += std::string(spelled.empty() ? "" : "-") + static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return spelled;
  }
  if (CyrillicLower(word[0]) != word[0])
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}
}  // namespace

std::string Cyrillic(std::string const & text)
{
  std::u32string const in = Decode(text);
  bool any = false;
  for (char32_t c : in)
    any = any || CyrillicLetter(c);
  if (!any)
    return text;
  std::u32string out;
  for (size_t i = 0; i < in.size();)
  {
    if (!CyrillicLetter(in[i]))
    {
      out += in[i++];
      continue;
    }
    size_t end = i;
    std::u32string word;
    for (; end < in.size() && (CyrillicLetter(in[end]) || in[end] == U'́'); ++end)
      if (in[end] != U'́')
        word += in[end];
    // A token that mixes Latin and Cyrillic letters ("Мoscow") is a look-alike spelling: leave it.
    bool const mixed = (i > 0 && Letter(in[i - 1])) || (end < in.size() && Letter(in[end]));
    if (mixed)
      out.append(in, i, end - i);
    else
      out += Decode(CyrillicWord(word));
    i = end;
  }
  return Encode(out);
}

// MARK: - Kana

namespace
{
bool KanaLetter(char32_t c)
{
  return (c >= U'ぁ' && c <= U'ゖ') || (c >= U'ァ' && c <= U'ヺ') || c == U'ー';
}

bool Ideograph(char32_t c)
{
  return (c >= U'㐀' && c <= U'鿿') || (c >= U'豈' && c <= U'﫿');
}

/// Katakana is hiragana + 0x60; fold it so one table covers both.
char32_t Hiragana(char32_t c)
{
  return (c >= U'ァ' && c <= U'ヶ') ? c - 0x60 : c;
}

/// Hepburn for one kana, the same table as BookLyre's text/spoken_foreign_scripts.py.
std::string KanaRoman(char32_t c)
{
  static std::map<char32_t, char const *> const table = {
      {U'あ', "a"},  {U'い', "i"},   {U'う', "u"},   {U'え', "e"},  {U'お', "o"},
      {U'か', "ka"}, {U'き', "ki"},  {U'く', "ku"},  {U'け', "ke"}, {U'こ', "ko"},
      {U'が', "ga"}, {U'ぎ', "gi"},  {U'ぐ', "gu"},  {U'げ', "ge"}, {U'ご', "go"},
      {U'さ', "sa"}, {U'し', "shi"}, {U'す', "su"},  {U'せ', "se"}, {U'そ', "so"},
      {U'ざ', "za"}, {U'じ', "ji"},  {U'ず', "zu"},  {U'ぜ', "ze"}, {U'ぞ', "zo"},
      {U'た', "ta"}, {U'ち', "chi"}, {U'つ', "tsu"}, {U'て', "te"}, {U'と', "to"},
      {U'だ', "da"}, {U'ぢ', "ji"},  {U'づ', "zu"},  {U'で', "de"}, {U'ど', "do"},
      {U'な', "na"}, {U'に', "ni"},  {U'ぬ', "nu"},  {U'ね', "ne"}, {U'の', "no"},
      {U'は', "ha"}, {U'ひ', "hi"},  {U'ふ', "fu"},  {U'へ', "he"}, {U'ほ', "ho"},
      {U'ば', "ba"}, {U'び', "bi"},  {U'ぶ', "bu"},  {U'べ', "be"}, {U'ぼ', "bo"},
      {U'ぱ', "pa"}, {U'ぴ', "pi"},  {U'ぷ', "pu"},  {U'ぺ', "pe"}, {U'ぽ', "po"},
      {U'ま', "ma"}, {U'み', "mi"},  {U'む', "mu"},  {U'め', "me"}, {U'も', "mo"},
      {U'や', "ya"}, {U'ゆ', "yu"},  {U'よ', "yo"},
      {U'ら', "ra"}, {U'り', "ri"},  {U'る', "ru"},  {U'れ', "re"}, {U'ろ', "ro"},
      {U'わ', "wa"}, {U'ゐ', "i"},   {U'ゑ', "e"},   {U'を', "o"},  {U'ん', "n"},  {U'ゔ', "vu"},
      {U'ぁ', "a"},  {U'ぃ', "i"},   {U'ぅ', "u"},   {U'ぇ', "e"},  {U'ぉ', "o"},
  };
  auto const it = table.find(Hiragana(c));
  return it == table.end() ? std::string() : std::string(it->second);
}

char YoonVowel(char32_t c)
{
  switch (Hiragana(c))
  {
  case U'ゃ': return 'a';
  case U'ゅ': return 'u';
  case U'ょ': return 'o';
  default: return 0;
  }
}

char SmallVowel(char32_t c)
{
  switch (Hiragana(c))
  {
  case U'ぁ': return 'a';
  case U'ぃ': return 'i';
  case U'ぅ': return 'u';
  case U'ぇ': return 'e';
  case U'ぉ': return 'o';
  default: return 0;
  }
}

/// Drops "o"+"o"/"u" and "uu" left to right without overlap, like Python's re.sub.
std::string CollapseLong(std::string const & s)
{
  std::string a;
  for (size_t i = 0; i < s.size(); ++i)
  {
    a += s[i];
    if (s[i] == 'o' && i + 1 < s.size() && (s[i + 1] == 'o' || s[i + 1] == 'u'))
      ++i;
  }
  std::string b;
  for (size_t i = 0; i < a.size(); ++i)
  {
    b += a[i];
    if (a[i] == 'u' && i + 1 < a.size() && a[i + 1] == 'u')
      ++i;
  }
  return b;
}

std::string KanaWord(std::u32string const & run)
{
  // Greetings where は is a particle, spelled as said.
  if (run == U"こんにちは")
    return "konnichiwa";
  if (run == U"こんばんは")
    return "konbanwa";
  std::string out;
  bool twice = false;
  for (size_t i = 0; i < run.size(); ++i)
  {
    char32_t const c = run[i];
    char32_t const next = i + 1 < run.size() ? run[i + 1] : 0;
    if (c == U'っ' || c == U'ッ')
    {
      twice = true;
      continue;
    }
    if (c == U'ー')
      continue;
    std::string syl = KanaRoman(c);
    if (char const v = YoonVowel(next); v && syl.size() >= 2 && syl.back() == 'i')
    {
      // shi+ya sha, chi+ya cha, ji+ya ja; ki+ya kya.
      std::string const stem = syl.substr(0, syl.size() - 1);
      syl = (stem == "sh" || stem == "ch" || stem == "j" ? stem : stem + "y") + v;
      ++i;
    }
    else if (char const v = SmallVowel(next); v && !syl.empty())
    {
      // Katakana loan sounds: ティ ti, ファ fa, ウィ wi, チェ che, ヴァ va.
      std::string stem = syl.substr(0, syl.size() - 1);
      if (syl == "fu" || syl == "vu" || syl == "u")
        stem = syl == "fu" ? "f" : syl == "vu" ? "v" : "w";
      else if (syl == "te" || syl == "de")
        stem = syl.substr(0, 1);
      syl = stem + v;
      ++i;
    }
    if (twice && !syl.empty() && std::string_view("aeiou").find(syl[0]) == std::string_view::npos)
      syl = (syl.rfind("ch", 0) == 0 ? std::string("t") : std::string(1, syl[0])) + syl;
    twice = false;
    out += syl;
  }
  return CollapseLong(out);
}
}  // namespace

std::string Kana(std::string const & text)
{
  std::u32string const in = Decode(text);
  bool any = false;
  for (char32_t c : in)
    any = any || KanaLetter(c);
  if (!any)
    return text;
  std::u32string out;
  for (size_t i = 0; i < in.size();)
  {
    if (!KanaLetter(in[i]))
    {
      out += in[i++];
      continue;
    }
    size_t end = i;
    while (end < in.size() && KanaLetter(in[end]))
      ++end;
    std::string word = KanaWord(in.substr(i, end - i));
    if (word.empty())
    {
      out.append(in, i, end - i);
      i = end;
      continue;
    }
    // Kanji stay as they are; keep the romaji from running into them: "ちょっと待って" "chotto 待 tte".
    if (i > 0 && Ideograph(in[i - 1]))
      word = " " + word;
    if (end < in.size() && Ideograph(in[end]))
      word += " ";
    out += Decode(word);
    i = end;
  }
  return Encode(out);
}
}  // namespace backseat::pronunciation
