// Direct tests for the UTF-8 helpers under Cyrillic() and Kana() (pronunciation_text.hpp), which
// had no tests of their own. A stray or truncated byte used to swallow the letters after it (a
// lone 0x80 ate the next three bytes) once the line also held Cyrillic or kana; now each bad byte
// becomes one U+FFFD and everything after it is kept.
#include "check.hpp"

#include "backseat/pronunciation.hpp"
#include "backseat/pronunciation_text.hpp"

#include <cstdio>
#include <string>

using namespace backseat::pronunciation;
using backseat::pronunciation::detail::Decode;
using backseat::pronunciation::detail::Encode;
using backseat::pronunciation::detail::Letter;

namespace
{
std::string const kBad = "\xEF\xBF\xBD";  // U+FFFD in UTF-8

// Code points as "U+0061 U+FFFD", so a failure prints something readable.
std::string Hex(std::u32string const & s)
{
  std::string out;
  char buf[16];
  for (char32_t c : s)
  {
    std::snprintf(buf, sizeof buf, "%sU+%04X", out.empty() ? "" : " ", static_cast<unsigned>(c));
    out += buf;
  }
  return out;
}
}  // namespace

TEST(ScriptsDecodeRoundTripsValidText)
{
  for (std::string const s : {"", "Main St", "café", "Улица Ленина", "ちょっと待って", "😀 ok", "\xC2\x80"})
    CHECK_EQUAL(Encode(Decode(s)), s);
  CHECK_EQUAL(Hex(Decode("aé€😀")), Hex(U"aé€😀"));
  CHECK_EQUAL(Decode("😀").size(), size_t{1});
}

TEST(ScriptsDecodeStrayByteKeepsTheLettersAfterIt)
{
  CHECK_EQUAL(Hex(Decode("\x80Main")), Hex(U"�Main"));
  CHECK_EQUAL(Hex(Decode("a\xFFz")), Hex(U"a�z"));
  CHECK_EQUAL(Hex(Decode("a\xF8xyz")), Hex(U"a�xyz"));
  // Latin-1 é (0xE9) is a 3-byte lead with no continuation bytes after it.
  CHECK_EQUAL(Hex(Decode("caf\xE9 St")), Hex(U"caf� St"));
  // A lead followed by ASCII no longer forms a letter: 0xD0 'A' used to decode as Cyrillic Ё.
  CHECK_EQUAL(Hex(Decode("\xD0" "A")), Hex(U"�A"));
}

TEST(ScriptsDecodeTruncatedTail)
{
  CHECK_EQUAL(Hex(Decode("ok\xE2\x82")), Hex(U"ok��"));
  CHECK_EQUAL(Hex(Decode("\xF0\x9F\x98")), Hex(U"���"));
  CHECK_EQUAL(Hex(Decode("\xC3")), Hex(U"�"));
}

TEST(ScriptsDecodeKeepsSurrogatesForJni)
{
  // JNI hands over modified UTF-8 (CESU-8): an emoji is two 3-byte surrogates. They decode as two
  // code points and are written back byte for byte, as before.
  std::string const cesu = "\xED\xA0\xBD\xED\xB8\x80";
  CHECK_EQUAL(Decode(cesu).size(), size_t{2});
  CHECK_EQUAL(Encode(Decode(cesu)), cesu);
}

TEST(ScriptsCodePointsPastUnicodeBecomeReplacement)
{
  CHECK_EQUAL(Hex(Decode("\xF7\xBF\xBF\xBF")), Hex(U"�"));
  CHECK_EQUAL(Encode(std::u32string(1, char32_t{0x110000})), kBad);
  CHECK_EQUAL(Encode(std::u32string(1, char32_t{0x10FFFF})), std::string("\xF4\x8F\xBF\xBF"));
}

TEST(ScriptsLetterIsAsciiOnly)
{
  CHECK(Letter(U'a'));
  CHECK(Letter(U'Z'));
  CHECK(!Letter(U'é'));
  CHECK(!Letter(U'Л'));
  CHECK(!Letter(U'5'));
  CHECK(!Letter(U' '));
}

TEST(ScriptsCyrillicWithBadByteKeepsTheRest)
{
  std::string const clean = Cyrillic("Улица Ленина, then Main St");
  std::string const bad = Cyrillic("Улица Ленина, then \x80Main St");
  CHECK(clean.find("Main St") != std::string::npos);
  CHECK(bad.find("Main St") != std::string::npos);
  CHECK_EQUAL(bad.substr(0, bad.find(kBad)), clean.substr(0, clean.find("Main")));
}

TEST(ScriptsKanaWithBadByteKeepsTheRest)
{
  std::string const bad = Kana("ちょっと\xC3 wait");
  CHECK(bad.find(" wait") != std::string::npos);
  CHECK(bad.find(kBad) != std::string::npos);
}

TEST(ScriptsNoScriptLetterTextIsReturnedUntouched)
{
  // Without Cyrillic or kana the original bytes come back as they were, bad bytes included.
  for (std::string const s : {"caf\xE9 St", "\x80Main", "plain"})
  {
    CHECK_EQUAL(Cyrillic(s), s);
    CHECK_EQUAL(Kana(s), s);
  }
}
