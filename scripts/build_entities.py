import argparse
import collections
import datetime
import difflib
import functools
import json
import re
import time
import unicodedata
import urllib.error
import urllib.request

USER_AGENT = "kparb-entities"
KALSHI_MARKETS_URL = "https://api.elections.kalshi.com/trade-api/v2/markets"
POLYMARKET_TEAMS_URL = "https://gamma-api.polymarket.com/teams"
POLYMARKET_EVENTS_URL = "https://gamma-api.polymarket.com/events/keyset"

MAJOR_LEAGUES = {
    "nba": {
        "kalshiSeriesPrefix": "KXNBA",
        "codes": ["ATL", "BKN", "BOS", "CHA", "CHI", "CLE", "DAL", "DEN", "DET", "GSW", "HOU", "IND", "LAC", "LAL", "MEM",
                  "MIA", "MIL", "MIN", "NOP", "NYK", "OKC", "ORL", "PHI", "PHX", "POR", "SAC", "SAS", "TOR", "UTA", "WAS"],
        "abbreviations": {},
        "prefixCityToName": True,
    },
    "nhl": {
        "kalshiSeriesPrefix": "KXNHL",
        "codes": ["ANA", "BOS", "BUF", "CAR", "CBJ", "CGY", "CHI", "COL", "DAL", "DET", "EDM", "FLA", "LA", "MIN", "MTL", "NJ",
                  "NSH", "NYI", "NYR", "OTT", "PHI", "PIT", "SEA", "SJ", "STL", "TB", "TOR", "UTA", "VAN", "VGK", "WPG", "WSH"],
        "abbreviations": {"CGY": "cal", "LA": "lak", "VGK": "las", "MTL": "mon", "UTA": "utah"},
        "prefixCityToName": True,
    },
    "nfl": {
        "kalshiSeriesPrefix": "KXNFL",
        "codes": ["ARI", "ATL", "BAL", "BUF", "CAR", "CHI", "CIN", "CLE", "DAL", "DEN", "DET", "GB", "HOU", "IND", "JAC", "KC",
                  "LAC", "LAR", "LV", "MIA", "MIN", "NE", "NO", "NYG", "NYJ", "PHI", "PIT", "SEA", "SF", "TB", "TEN", "WAS"],
        "abbreviations": {"JAC": "jax", "LAR": "la"},
        "prefixCityToName": False,
    },
    "mlb": {
        "kalshiSeriesPrefix": "KXMLB",
        "codes": ["ATH", "ATL", "AZ", "BAL", "BOS", "CHC", "CIN", "CLE", "COL", "CWS", "DET", "HOU", "KC", "LAA", "LAD", "MIA",
                  "MIL", "MIN", "NYM", "NYY", "PHI", "PIT", "SD", "SEA", "SF", "STL", "TB", "TEX", "TOR", "WSH"],
        "abbreviations": {"ATH": "oak", "AZ": "ari"},
        "prefixCityToName": False,
    },
}

EXCLUDED_SERIES = {"KXNBAGAME", "KXNHLGAME", "KXNFLGAME", "KXMLBGAME", "KXT20MATCH", "KXWT20MATCH", "KXTESTMATCH",
                   "KXODIMATCH", "KXWODIMATCH"}

CONFIRMED_LOW_SIMILARITY_LINKS = {
    ("KXLALIGAGAME", "ATH", "bil"), ("KXLALIGAGAME", "SAN", "rrc"), ("KXLIGAPORTUGALGAME", "VIT", "gui"),
    ("KXARGPREMDIVGAME", "RCU", "aae"), ("KXARGPREMDIVGAME", "CAS", "cas"), ("KXEREDIVISIEGAME", "NIJ", "nec"),
    ("KXUELGAME", "NIJ", "nec"), ("KXCOPADELREYGAME", "UDI", "iru"), ("KXUELGAME", "OL", "lyo"),
    ("KXNCAAMBGAME", "CSN", "csunr"), ("KXNCAAMBGAME", "UNCW", "ncw"), ("KXNCAAWBGAME", "UNCW", "ncw"),
    ("KXNCAAMBGAME", "UTM", "tmrt"), ("KXNCAAMBGAME", "FDU", "fairdk"), ("KXNCAAMBGAME", "ALBY", "albny"),
    ("KXNCAAWBGAME", "ALBY", "albny"), ("KXNCAAMBGAME", "CONN", "uconn"), ("KXNCAAWBGAME", "CONN", "uconn"),
    ("KXNCAAMBGAME", "MASS", "umass"), ("KXNCAAWBGAME", "MASS", "umass"), ("KXSAUDIPLGAME", "NEO", "neo"),
    ("KXPERLIGA1GAME", "ASO", "adt"), ("KXROML1GAME", "ARG", "fap"),
}

GENERIC_WORDS = {"fc", "cf", "sc", "bc", "kk", "ac", "afc", "club", "basket", "basketball", "the", "de", "team", "cd", "ca", "sk",
                 "bk", "fk", "nk", "cb", "as", "us", "ud", "sd", "rc", "calcio", "bbc", "basquete", "ss", "ssc", "futbol",
                 "football", "esports", "gaming", "reg", "time"}
MONTHS = {name: index + 1 for index, name in enumerate(["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"])}


def fetch_json(url):
    for attempt in range(8):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(request, timeout=60) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if error.code not in (429, 500, 502, 503, 504):
                raise
            time.sleep(2 + 2 * attempt)
    raise RuntimeError("giving up on " + url)


def fetch_polymarket_teams():
    teams, offset = [], 0
    while True:
        page = fetch_json(f"{POLYMARKET_TEAMS_URL}?limit=100&offset={offset}")
        teams.extend(page)
        offset += len(page)
        if len(page) < 100:
            return teams


def fetch_kalshi_game_markets(days):
    latest = int(time.time()) + days * 86400
    markets, cursor = [], ""
    while True:
        page = fetch_json(f"{KALSHI_MARKETS_URL}?limit=1000&status=open&mve_filter=exclude&max_close_ts={latest}&cursor={cursor}")
        markets.extend(market for market in page["markets"] if re.search(r"(GAME|MATCH)$", market["event_ticker"].split("-")[0]))
        cursor = page.get("cursor") or ""
        if not cursor:
            return markets


def fetch_polymarket_game_events(days):
    now = datetime.datetime.now(datetime.timezone.utc)
    stamp = lambda moment: moment.strftime("%Y-%m-%dT%H:%M:%SZ")
    base = (f"{POLYMARKET_EVENTS_URL}?closed=false&limit=100&end_date_min={stamp(now - datetime.timedelta(hours=12))}"
            f"&end_date_max={stamp(now + datetime.timedelta(days=days))}")
    events, cursor = [], ""
    while True:
        page = fetch_json(base + (f"&after_cursor={cursor}" if cursor else ""))
        for event in page.get("events", []):
            if len(event.get("teams") or []) == 2 and event.get("eventDate"):
                events.append({"slug": event["slug"], "eventDate": event["eventDate"], "teams": event["teams"]})
        cursor = page.get("next_cursor") or ""
        if not cursor or not page.get("events"):
            return events


@functools.lru_cache(None)
def core_words(text):
    ascii_text = unicodedata.normalize("NFKD", text or "").encode("ascii", "ignore").decode().lower()
    ascii_text = ascii_text.replace("st.", "state").replace("&", " and ")
    words = re.findall(r"[a-z0-9]+", ascii_text)
    return tuple([word for word in words if word not in GENERIC_WORDS] or words)


@functools.lru_cache(None)
def word_overlap(first, second):
    first_words, second_words = core_words(first), core_words(second)
    if not first_words or not second_words:
        return 0.0
    first_text, second_text = " ".join(first_words), " ".join(second_words)
    prefix = 0.95 if first_text.startswith(second_text) or second_text.startswith(first_text) else 0.0
    shared = len(set(first_words) & set(second_words)) / min(len(set(first_words)), len(set(second_words)))
    return max(shared, prefix)


@functools.lru_cache(None)
def name_similarity(first, second):
    first_words, second_words = core_words(first), core_words(second)
    if not first_words or not second_words:
        return 0.0
    ratio = difflib.SequenceMatcher(None, " ".join(first_words), " ".join(second_words)).ratio()
    return max(word_overlap(first, second), ratio)


def kalshi_event_date(event_ticker):
    match = re.match(r"(\d\d)([A-Z]{3})(\d\d)", event_ticker.split("-")[1])
    if not match or match.group(2) not in MONTHS:
        return None
    return datetime.date(2000 + int(match.group(1)), MONTHS[match.group(2)], int(match.group(3)))


def kalshi_team_name(market):
    return re.sub(r"^Reg Time: ", "", market.get("yes_sub_title") or "")


def group_kalshi_events(markets):
    events = collections.defaultdict(lambda: {"teams": {}})
    for market in markets:
        name = kalshi_team_name(market)
        code = market["ticker"].split("-")[-1]
        if code == "TIE" or name.lower() in ("tie", "draw/tie"):
            continue
        event = events[market["event_ticker"]]
        event["series"] = market["event_ticker"].split("-")[0]
        event["teams"][code] = name
    return [(ticker, event) for ticker, event in events.items()
            if len(event["teams"]) == 2 and kalshi_event_date(ticker) and event["series"] not in EXCLUDED_SERIES]


class PolymarketIndex:
    def __init__(self, teams, events):
        self.teams = {team["id"]: team for team in teams}
        self.events_by_date = collections.defaultdict(list)
        for event in events:
            event["league"] = event["slug"].split("-")[0]
            self.events_by_date[event["eventDate"]].append(event)

    def names_of(self, team):
        full = self.teams.get(team["id"], team)
        return tuple(name for name in {full.get("name"), full.get("alias"), team.get("name")} if name)

    def abbreviation_of(self, team):
        return (team.get("abbreviation") or self.teams.get(team["id"], {}).get("abbreviation") or "").lower()

    def events_near(self, date, leagues=None):
        for offset in (0, -1, 1):
            for event in self.events_by_date[(date + datetime.timedelta(days=offset)).isoformat()]:
                if leagues is None or event["league"] in leagues:
                    yield offset, event


def series_leagues(kalshi_events, polymarket):
    votes = collections.defaultdict(collections.Counter)
    for ticker, event in kalshi_events:
        names = list(event["teams"].values())
        for _, poly_event in polymarket.events_near(kalshi_event_date(ticker)):
            poly_names = [polymarket.names_of(team) for team in poly_event["teams"]]
            for first, second in ((0, 1), (1, 0)):
                first_score = max(word_overlap(names[0], name) for name in poly_names[first])
                second_score = max(word_overlap(names[1], name) for name in poly_names[second])
                if min(first_score, second_score) >= 0.99:
                    votes[event["series"]][poly_event["league"]] += 1
    best_series_for_league = {}
    for series, counter in votes.items():
        league, count = counter.most_common(1)[0]
        if count >= 2 and count > best_series_for_league.get(league, ("", 0))[1]:
            best_series_for_league[league] = (series, count)
    return {series: league for league, (series, _) in best_series_for_league.items()}


def ordered_codes(ticker, event):
    codes = list(event["teams"].keys())
    event_codes = ticker.split("-")[1][7:]
    if event_codes.endswith(codes[0]) and not event_codes.endswith(codes[1]):
        codes.reverse()
    return codes


def link_codes(kalshi_events, polymarket, league_of_series):
    links = collections.defaultdict(collections.Counter)
    kalshi_names = {}
    for ticker, event in kalshi_events:
        league = league_of_series.get(event["series"])
        if league is None:
            continue
        codes = ordered_codes(ticker, event)
        best = None
        for offset, poly_event in polymarket.events_near(kalshi_event_date(ticker), {league}):
            ordered = sorted(poly_event["teams"], key=lambda team: 0 if team.get("ordering") == "away" else 1)
            for assignment in (ordered, ordered[::-1]):
                scores = []
                for code, team in zip(codes, assignment):
                    similarity = max(name_similarity(event["teams"][code], name) for name in polymarket.names_of(team))
                    scores.append(max(similarity, 0.9 if polymarket.abbreviation_of(team) == code.lower() else 0.0))
                total = sum(scores) + (0.05 if assignment is ordered else 0.0) - 0.1 * abs(offset)
                if best is None or total > best[0]:
                    best = (total, scores, assignment)
        if best and (min(best[1]) >= 0.5 or max(best[1]) >= 0.85):
            for code, team in zip(codes, best[2]):
                links[(event["series"], code)][team["id"]] += 1
                kalshi_names[(event["series"], code)] = event["teams"][code]
    return links, kalshi_names


def is_acceptable_link(series, code, kalshi_name, team, polymarket):
    abbreviation = polymarket.abbreviation_of(team)
    similarity = max(name_similarity(kalshi_name, name) for name in polymarket.names_of(team))
    if similarity >= 0.5:
        return True
    lowercase_code = code.lower()
    if abbreviation and (abbreviation == lowercase_code or (min(len(abbreviation), len(lowercase_code)) >= 2
                                                             and (abbreviation.startswith(lowercase_code) or lowercase_code.startswith(abbreviation)))):
        return True
    return (series, code, abbreviation) in CONFIRMED_LOW_SIMILARITY_LINKS


def choose_team(series, code, kalshi_name, counter, polymarket):
    ranked = sorted(counter.items(), key=lambda item: (-item[1], -max(name_similarity(kalshi_name, name) for name in polymarket.names_of(polymarket.teams[item[0]]))))
    if len(ranked) > 1 and ranked[0][1] == ranked[1][1]:
        scored = sorted(ranked, key=lambda item: -max(name_similarity(kalshi_name, name) for name in polymarket.names_of(polymarket.teams[item[0]])))
        top = max(name_similarity(kalshi_name, name) for name in polymarket.names_of(polymarket.teams[scored[0][0]]))
        if top < 0.8:
            return None
        ranked = scored
    team = polymarket.teams.get(ranked[0][0])
    if team is None or not is_acceptable_link(series, code, kalshi_name, team, polymarket):
        return None
    return team


def clean_name(name):
    return " ".join((name or "").split())


def aliases_for(*names):
    aliases = []
    for name in map(clean_name, names):
        if not name:
            continue
        for variant in (name, re.sub(r"\bSt\.$", "State", name), re.sub(r"\bSt\.(?= )", "State", name)):
            variant = variant.strip()
            if len(variant) >= 2 and variant not in aliases:
                aliases.append(variant)
    return aliases


def entity_record(code, name, kalshi_name, team, verified):
    return {
        "code": code,
        "name": clean_name(name),
        "kalshiName": clean_name(kalshi_name),
        "polymarketAbbreviation": (team.get("abbreviation") or "").lower(),
        "polymarketName": clean_name(team.get("name")),
        "aliases": aliases_for(name, kalshi_name, team.get("name"), team.get("alias")),
        "verified": verified,
    }


def city_of(kalshi_name):
    return re.sub(r"\s+[A-Z]$", "", kalshi_name or "").strip()


def major_league_entities(kalshi_markets, polymarket_teams):
    kalshi_names = collections.defaultdict(dict)
    for market in kalshi_markets:
        series = market["event_ticker"].split("-")[0]
        kalshi_names[series][market["ticker"].split("-")[-1]] = kalshi_team_name(market)
    leagues = []
    for league_name, spec in MAJOR_LEAGUES.items():
        teams_by_abbreviation = {team["abbreviation"].lower(): team for team in polymarket_teams
                                 if team.get("league") == league_name and team.get("abbreviation")}
        names = kalshi_names.get(spec["kalshiSeriesPrefix"] + "GAME", {})
        entities = []
        for code in spec["codes"]:
            team = teams_by_abbreviation.get(spec["abbreviations"].get(code, code.lower()))
            if team is None:
                raise RuntimeError(f"{league_name} {code} is not in the Polymarket team list")
            kalshi_name = names.get(code, "")
            if not kalshi_name and team.get("alias") and team["name"].endswith(" " + team["alias"]):
                kalshi_name = team["name"][: -len(team["alias"])].strip()
            name = team["name"]
            if spec["prefixCityToName"] and kalshi_name:
                name = f"{city_of(kalshi_name)} {team['name']}"
            entities.append(entity_record(code, name, kalshi_name, team, True))
        leagues.append({"name": league_name, "kalshiSeriesPrefix": spec["kalshiSeriesPrefix"],
                        "polymarketSlugPrefix": league_name + "-", "entities": entities})
    return leagues


def linked_league_entities(kalshi_markets, polymarket_teams, polymarket_events):
    polymarket = PolymarketIndex(polymarket_teams, polymarket_events)
    kalshi_events = group_kalshi_events(kalshi_markets)
    league_of_series = series_leagues(kalshi_events, polymarket)
    links, kalshi_names = link_codes(kalshi_events, polymarket, league_of_series)
    leagues = {}
    for (series, code), counter in sorted(links.items()):
        team = choose_team(series, code, kalshi_names[(series, code)], counter, polymarket)
        if team is None:
            continue
        league_name = league_of_series[series]
        league = leagues.setdefault(league_name, {"name": league_name, "kalshiSeriesPrefix": re.sub(r"(GAME|MATCH)$", "", series),
                                                  "polymarketSlugPrefix": league_name + "-", "entities": []})
        league["entities"].append(entity_record(code, team["name"], kalshi_names[(series, code)], team, False))
    prefixes = [league["kalshiSeriesPrefix"] for league in leagues.values()]
    for series in sorted({event["series"] for _, event in kalshi_events}):
        prefix = re.sub(r"(GAME|MATCH)$", "", series)
        if prefix not in prefixes and any(prefix.startswith(other) for other in prefixes):
            leagues[prefix.lower()] = {"name": prefix.lower(), "kalshiSeriesPrefix": prefix, "polymarketSlugPrefix": "", "entities": []}
    return list(leagues.values())


def merge(existing, fresh):
    by_name = {league["name"]: league for league in existing}
    for league in fresh:
        current = by_name.setdefault(league["name"], {**league, "entities": []})
        entities = {entity["code"]: entity for entity in current["entities"]}
        for entity in league["entities"]:
            if entity["verified"] or not entities.get(entity["code"], {}).get("verified"):
                entities[entity["code"]] = entity
        current["entities"] = sorted(entities.values(), key=lambda entity: entity["code"])
    return sorted(by_name.values(), key=lambda league: league["name"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="config/entities.json")
    parser.add_argument("--days", type=int, default=10)
    arguments = parser.parse_args()
    try:
        with open(arguments.output) as file:
            existing = json.load(file).get("leagues", [])
    except FileNotFoundError:
        existing = []
    polymarket_teams = fetch_polymarket_teams()
    kalshi_markets = fetch_kalshi_game_markets(arguments.days)
    polymarket_events = fetch_polymarket_game_events(arguments.days)
    fresh = major_league_entities(kalshi_markets, polymarket_teams) + linked_league_entities(kalshi_markets, polymarket_teams, polymarket_events)
    leagues = merge(existing, fresh)
    with open(arguments.output, "w") as file:
        json.dump({"leagues": leagues}, file, indent=1, ensure_ascii=False)
        file.write("\n")
    print(f"{len(leagues)} leagues, {sum(len(league['entities']) for league in leagues)} entities written to {arguments.output}")


if __name__ == "__main__":
    main()
