#include "check.hpp"

#include "backseat/history.hpp"
#include "backseat/place_search.hpp"
#include "backseat/voice_command.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace backseat;

TEST(SpokenPlacesLoseTheirFiller)
{
  CHECK_EQUAL(SearchableQuery("Um, a place called Joe's near me, please."), std::string("joe's"));
  CHECK_EQUAL(SearchableQuery("the Starbucks nearby"), std::string("starbucks"));
  CHECK_EQUAL(SearchableQuery("uh Target or something"), std::string("target"));
  CHECK_EQUAL(SearchableQuery("Joe’s Pizza"), std::string("joe's pizza"));
  CHECK_EQUAL(SearchableQuery("'Joe's'"), std::string("joe's"));
  CHECK_EQUAL(SearchableQuery("  "), std::string(""));
  // Nothing left to take off.
  CHECK_EQUAL(SearchableQuery("please"), std::string("please"));
  CHECK_EQUAL(SearchableQuery("the"), std::string("the"));
}

TEST(SpelledOutLettersAreOneWord)
{
  CHECK_EQUAL(SearchableQuery("C V S pharmacy"), std::string("cvs pharmacy"));
  CHECK_EQUAL(SearchableQuery("c.v.s."), std::string("cvs"));
  CHECK_EQUAL(SearchableQuery("I H O P"), std::string("ihop"));
  // One letter alone stays: "a" was the article and is gone, "j" stays.
  CHECK_EQUAL(SearchableQuery("a J crew"), std::string("j crew"));
}

TEST(SpokenAddressesGetDigits)
{
  CHECK_EQUAL(SearchableQuery("one two three Main Street"), std::string("123 main street"));
  CHECK_EQUAL(SearchableQuery("one twenty three main street"), std::string("123 main street"));
  CHECK_EQUAL(SearchableQuery("fifteen oh five Elm Ave"), std::string("1505 elm ave"));
  CHECK_EQUAL(SearchableQuery("fifteen o five elm ave"), std::string("1505 elm ave"));
  CHECK_EQUAL(SearchableQuery("twenty one hundred Dickson Street"), std::string("2100 dickson street"));
  CHECK_EQUAL(SearchableQuery("four hundred twelve College Avenue"), std::string("412 college avenue"));
  CHECK_EQUAL(SearchableQuery("forty two oak drive"), std::string("42 oak drive"));
  CHECK_EQUAL(SearchableQuery("forty second street"), std::string("42nd street"));
  CHECK_EQUAL(SearchableQuery("Fifth Avenue"), std::string("5th avenue"));
  CHECK_EQUAL(SearchableQuery("twenty first street"), std::string("21st street"));
  CHECK_EQUAL(SearchableQuery("one twelve east twenty third street"), std::string("112 east 23rd street"));
  // Already digits: unchanged, and so is a second pass.
  CHECK_EQUAL(SearchableQuery("123 Main St."), std::string("123 main st"));
  CHECK_EQUAL(SearchableQuery(SearchableQuery("one twenty three main street")), std::string("123 main street"));
}

TEST(NamesWithNumbersKeepTheirWords)
{
  CHECK_EQUAL(SearchableQuery("Five Guys"), std::string("five guys"));
  CHECK_EQUAL(SearchableQuery("seven eleven"), std::string("seven eleven"));
  // One number word before a street is part of its name.
  CHECK_EQUAL(SearchableQuery("Five Points Road"), std::string("five points road"));
  CHECK_EQUAL(SearchableQuery("twelve oaks lane"), std::string("twelve oaks lane"));
  // "Second" isn't a street ordinal without a street after it.
  CHECK_EQUAL(SearchableQuery("second chance thrift"), std::string("second chance thrift"));
}

TEST(EmptySearchesAreRetriedOtherWays)
{
  auto has = [](std::vector<std::string> const & v, std::string const & s)
  { return std::find(v.begin(), v.end(), s) != v.end(); };
  auto v = QueryVariants("starbucks on main");
  CHECK(!v.empty() && v[0] == "starbucks");
  CHECK(has(QueryVariants("star bucks"), "starbucks"));
  CHECK(has(QueryVariants("wal mart"), "walmart"));
  CHECK(has(QueryVariants("mcdonalds"), "mcdonald's"));
  CHECK(has(QueryVariants("applebees grill"), "applebee's grill"));
  CHECK(has(QueryVariants("joe's pizza"), "joes pizza"));
  CHECK(has(QueryVariants("five guys"), "5 guys"));
  CHECK(has(QueryVariants("seven eleven"), "7 eleven"));
  CHECK(has(QueryVariants("5th avenue"), "fifth avenue"));
  for (std::string const q : {"starbucks on main", "star bucks", "mcdonalds", "x", "", "123 main street"})
  {
    auto const variants = QueryVariants(q);
    CHECK(variants.size() <= 4);
    CHECK(!has(variants, q));
    CHECK(!has(variants, ""));
  }
  // "On" at the end isn't a qualifier.
  CHECK(!has(QueryVariants("hold on"), "hold"));
}

TEST(WordsCloseForgivesTheRecognizersSpelling)
{
  CHECK(WordsClose("chipotlay", "chipotle"));
  CHECK(WordsClose("chiptole", "chipotle"));
  CHECK(WordsClose("mcdonalds", "mcdonald's"));
  CHECK(WordsClose("jimy", "jimmy"));
  CHECK(WordsClose("filadelphia", "philadelphia"));  // "ph" is "f"
  CHECK(WordsClose("philly", "phily"));
  CHECK(WordsClose("starbuck", "starbucks"));
  CHECK(!WordsClose("cafe", "care"));
  CHECK(!WordsClose("shop", "shoe"));
  CHECK(WordsClose("lutheran", "latheran"));
  CHECK(!WordsClose("target", "garget"));
  CHECK(!WordsClose("walmart", "walgreens"));
  CHECK(!WordsClose("", "a"));
}

TEST(PlaceMatchesForgivesOneMisspelledWord)
{
  CHECK(PlaceMatches("chipotlay", "Chipotle Mexican Grill"));
  CHECK(PlaceMatches("mcdonalds", "McDonald's"));
  CHECK(PlaceMatches("chipotlay grill", "Chipotle Mexican Grill"));
  // The old rules still hold.
  CHECK(!PlaceMatches("peace Lutheran", "Latheron Place"));
  CHECK(PlaceMatches("walmart", "Wal-Mart Supercenter"));
  CHECK(!PlaceMatches("coffee", "Starbucks"));
  CHECK(!PlaceMatches("cafe", "Care Clinic"));
}

TEST(SearchResultsPutTheNamedAndFamiliarFirst)
{
  PastPlace usual;
  usual.destination = "Starbucks";
  usual.to = {36.0700, -94.1600};
  usual.trips = 6;
  std::vector<PlaceCandidate> found = {
      {"Pizza Hut", 300, LatLon{36.06, -94.15}},
      {"Joe's Pizza", 1200, LatLon{36.08, -94.17}},
      {"Joe's Pizza", 900, LatLon{36.09, -94.18}},
  };
  // The name asked for beats the nearer place that isn't it.
  auto order = RankSearchResults("joe's pizza", found, {});
  CHECK_EQUAL(order.size(), size_t(3));
  CHECK_EQUAL(order[0], size_t(2));
  CHECK_EQUAL(order[1], size_t(1));
  CHECK_EQUAL(order[2], size_t(0));

  // The Starbucks they always go to comes before a nearer one.
  std::vector<PlaceCandidate> coffee = {
      {"Starbucks", 400, LatLon{36.10, -94.20}},
      {"Starbucks", 2500, LatLon{36.0701, -94.1601}},
      {"Starbucks", 900, std::nullopt},
  };
  order = RankSearchResults("starbucks", coffee, {usual});
  CHECK_EQUAL(order[0], size_t(1));
  CHECK_EQUAL(order[1], size_t(0));
  CHECK_EQUAL(order[2], size_t(2));

  // "Gas station": no name says it, so familiar then nearest.
  std::vector<PlaceCandidate> gas = {{"Shell", 800, LatLon{1, 1}}, {"Casey's", 300, LatLon{2, 2}}};
  order = RankSearchResults("gas station", gas, {});
  CHECK_EQUAL(order[0], size_t(1));
  // A bad distance goes last instead of breaking the sort.
  gas.push_back({"Exxon", -1, std::nullopt});
  gas.push_back({"Exxon", std::nan(""), std::nullopt});
  order = RankSearchResults("gas station", gas, {});
  CHECK_EQUAL(order.size(), size_t(4));
  CHECK_EQUAL(order[0], size_t(1));
  CHECK(RankSearchResults("x", {}, {usual}).empty());
}

TEST(PastPlacesAnswerWhenTheMapSearchCant)
{
  PastPlace grandma;
  grandma.destination = "Grandma's House";
  grandma.to = {36.2, -94.1};
  grandma.trips = 3;
  PastPlace joes;
  joes.destination = "Joe's Pizza";
  joes.to = {36.1, -94.2};
  joes.trips = 1;
  PastPlace joes2 = joes;
  joes2.to = {36.3, -94.3};
  joes2.trips = 4;
  std::vector<PastPlace> past = {grandma, joes, joes2};
  auto hit = PastPlaceNamed("grandmas house", past);
  CHECK(hit && hit->destination == "Grandma's House");
  hit = PastPlaceNamed("joe's pizza", past);
  CHECK(hit && hit->trips == 4);
  CHECK(!PastPlaceNamed("the dentist", past));
  CHECK(!PastPlaceNamed("", past));
  CHECK(!PastPlaceNamed("grandma's house", {}));
  PastPlace unnamed;
  unnamed.trips = 9;
  CHECK(!PastPlaceNamed("anything", {unnamed}));
}

TEST(SearchKeepsLettersOutsideAscii)
{
  // A destination from the voice model can be accented or in another script; its letters must reach the
  // map search, not turn into spaces ("São Paulo" was "so paulo", "東京駅 cafe" just "cafe").
  CHECK_EQUAL(SearchableQuery("São Paulo"), std::string("s\xC3\xA3o paulo"));
  CHECK_EQUAL(SearchableQuery("東京駅 cafe"), std::string("\xE6\x9D\xB1\xE4\xBA\xAC\xE9\xA7\x85 cafe"));
  CHECK_EQUAL(SearchableQuery("the Café Tacuba nearby"), std::string("caf\xC3\xA9 tacuba"));
}
