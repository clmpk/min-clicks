#pragma once
#include "common.hpp"
#ifdef __BMI2__
#include <immintrin.h>
#endif

namespace minclicks::detail {

// An adversary selects witnesses covering saved items (gain), but must also
// pay for the lost items those same witnesses cover. Ignore witness/witness
// incompatibilities: this enlarges the adversary's choices and is conservative.
inline bool witnessesWithinBudget(vector<pair<uint64_t, uint64_t>> &costly,
                                  uint64_t freeGain, int delta, int &visited) {
    if (delta > 0) return false;
    for (auto &item : costly) item.first &= ~freeGain;
    sort(costly.begin(), costly.end());
    costly.erase(unique(costly.begin(), costly.end()), costly.end());
    vector<uint64_t> suffix(costly.size() + 1);
    for (size_t i = costly.size(); i-- > 0;)
        suffix[i] = suffix[i + 1] | costly[i].first;
    auto violates = [&](auto &&search, size_t i, uint64_t gain,
                        uint64_t loss) -> bool {
        if (delta + popcount(gain) - popcount(loss) > 0) return true;
        if (delta + popcount(gain | suffix[i]) - popcount(loss) <= 0)
            return false;
        if (++visited > 20000) return true;
        return search(search, i + 1, gain | costly[i].first,
                      loss | costly[i].second) || search(search, i + 1, gain, loss);
    };
    return !violates(violates, 0, 0, 0);
}

// Physical overlap does not change during reduction. Build a row lazily from
// inverted item users, and reuse it for every later deletion/swap certificate.
struct CoverageCache {
    const vector<vector<int>> &mines, &bases, &mineUsers, &baseUsers;
    vector<vector<uint64_t>> mineRows, baseRows;
    const vector<vector<uint64_t>> *contactBits;
    CoverageCache(const vector<vector<int>> &m, const vector<vector<int>> &b,
                  const vector<vector<int>> &mu, const vector<vector<int>> &bu,
                  const vector<vector<uint64_t>> *links = nullptr)
        : mines(m), bases(b), mineUsers(mu), baseUsers(bu),
          mineRows(m.size()), baseRows(m.size()), contactBits(links) {}
    bool adjacent(int a, int b, const vector<vector<int>> &contacts) const {
        if (contactBits) return ((*contactBits)[a][b / 64] >> (b % 64)) & 1;
        return binary_search(contacts[a].begin(), contacts[a].end(), b);
    }
    const vector<uint64_t> &row(int v, bool mine) {
        auto &result = (mine ? mineRows : baseRows)[v];
        if (result.empty()) {
            result.resize(mines.size());
            const auto &items = (mine ? mines : bases)[v];
            const auto &users = mine ? mineUsers : baseUsers;
            for (size_t i = 0; i < items.size(); ++i)
                for (int u : users[items[i]]) result[u] |= uint64_t(1) << i;
        }
        return result;
    }
};

// Preserve the original sorted difference's bit numbering. Large original
// scopes can still have small differences: retain the direct fallback for them.
struct CoverageProjection {
    const vector<int> *items = nullptr;
    const vector<vector<int>> *scopes = nullptr;
    const vector<uint64_t> *overlaps = nullptr;
    uint64_t selected = 0;
    CoverageProjection() = default;
    CoverageProjection(const vector<uint64_t> &row, uint64_t selected)
        : overlaps(&row), selected(selected) {}
    CoverageProjection(const vector<int> &items, const vector<vector<int>> &scopes,
                       int owner, bool mine, CoverageCache *cache)
        : items(&items), scopes(&scopes) {
        if (cache && scopes[owner].size() <= 63) {
            overlaps = &cache->row(owner, mine);
            for (int item : items)
                selected |= uint64_t(1) << (lower_bound(scopes[owner].begin(), scopes[owner].end(), item) - scopes[owner].begin());
        }
    }
    uint64_t mask(int u) const {
        uint64_t result = 0;
        if (overlaps) {
#ifdef __BMI2__
            return _pext_u64((*overlaps)[u], selected);
#else
            uint64_t hits = (*overlaps)[u], bit = 1;
            for (uint64_t scan = selected; scan; scan &= scan - 1, bit <<= 1)
                if (hits & (scan & -scan)) result |= bit;
#endif
        } else {
            for (size_t i = 0; i < items->size(); ++i)
                if (binary_search((*scopes)[u].begin(), (*scopes)[u].end(), (*items)[i]))
                    result |= uint64_t(1) << i;
        }
        return result;
    }
};

// Certify deletion using actual mine-sharing witnesses, rather than assuming
// that every non-private flag is free. Failure or a search limit keeps v.
inline bool witnessDeletion(int v, const vector<vector<int>> &contacts,
                            const vector<vector<int>> &mines,
                            const vector<vector<int>> &bases,
                            const vector<uint8_t> &alive, CoverageCache *cache = nullptr) {
    if (mines[v].size() > 63 || bases[v].size() > 63) return false;
    struct Near { int id; uint64_t mines = 0, bases = 0; };
    vector<Near> neighbors, witnesses;
    const vector<uint64_t> *mineHits = cache ? &cache->row(v, true) : nullptr;
    const vector<uint64_t> *baseHits = cache ? &cache->row(v, false) : nullptr;
    for (int u = 0; u < int(alive.size()); ++u) {
        if (!alive[u] || u == v) continue;
        Near item{u};
        if (mineHits) item.mines = (*mineHits)[u];
        else for (size_t i = 0; i < mines[v].size(); ++i)
            if (binary_search(mines[u].begin(), mines[u].end(), mines[v][i]))
                item.mines |= uint64_t(1) << i;
        bool adjacent = cache ? cache->adjacent(v, u, contacts) :
            binary_search(contacts[v].begin(), contacts[v].end(), u);
        if (!item.mines && !adjacent) continue;
        if (baseHits) item.bases = (*baseHits)[u];
        else for (size_t i = 0; i < bases[v].size(); ++i)
            if (binary_search(bases[u].begin(), bases[u].end(), bases[v][i]))
                item.bases |= uint64_t(1) << i;
        if (adjacent) neighbors.push_back(item);
        if (item.mines) witnesses.push_back(item);
    }
    uint64_t allMines = (uint64_t(1) << mines[v].size()) - 1;
    uint64_t allBases = (uint64_t(1) << bases[v].size()) - 1;
    vector<int> chosen;
    int visited = 0;
    auto adjacent = [&](int a, int b) {
        return cache ? cache->adjacent(a, b, contacts) :
            binary_search(contacts[a].begin(), contacts[a].end(), b);
    };
    auto check = [&](auto &&self, size_t start, uint64_t mineHits,
                     uint64_t baseHits) -> bool {
        if (++visited > 20000) return false;
        uint64_t missingMines = allMines & ~mineHits;
        uint64_t missingBases = allBases & ~baseHits;
        uint64_t freeMines = 0;
        vector<pair<uint64_t, uint64_t>> costly;
        for (const auto &w : witnesses) {
            uint64_t gain = w.mines & missingMines;
            if (!gain || (chosen.empty() && adjacent(v, w.id))) continue;
            int touches = 0;
            bool selected = false;
            for (int i : chosen) {
                selected |= neighbors[i].id == w.id;
                touches += adjacent(neighbors[i].id, w.id);
            }
            if (selected || touches > 1) continue;
            uint64_t loss = w.bases & missingBases;
            if (loss) costly.emplace_back(gain, loss);
            else freeMines |= gain;
        }
        // Deleting v saves one chord and its old component, then pays one
        // seed for each remaining piece and a click for each lost 3BV unit.
        int delta = int(chosen.size()) - 2 + popcount(missingBases) -
                    popcount(missingMines & ~freeMines);
        if (!witnessesWithinBudget(costly, freeMines, delta, visited)) return false;
        for (size_t i = start; i < neighbors.size(); ++i) {
            bool independent = true;
            for (int j : chosen)
                if (adjacent(neighbors[i].id, neighbors[j].id)) {
                    independent = false;
                    break;
                }
            if (!independent) continue;
            chosen.push_back(int(i));
            bool safe = self(self, i + 1, mineHits | neighbors[i].mines,
                             baseHits | neighbors[i].bases);
            chosen.pop_back();
            if (!safe) return false;
        }
        return true;
    };
    return check(check, 0, 0, 0);
}

// Replacing c by an adjacent d may trade flags, coverage, and connectivity.
// Check both d absent (swap) and d already selected (delete c).
inline bool witnessSwap(int c, int d, const vector<vector<int>> &contacts,
                        const vector<vector<int>> &mines,
                        const vector<vector<int>> &bases,
                        const vector<uint8_t> &alive, CoverageCache *cache = nullptr) {
    auto difference = [](const vector<int> &a, const vector<int> &b) {
        vector<int> result;
        set_difference(a.begin(), a.end(), b.begin(), b.end(), back_inserter(result));
        return result;
    };
    vector<int> savedMines, lostBases, addedMines, addedBases;
    CoverageProjection saved, lost, addedBase, addedMine;
    int savedCount, lostCount, addedMineCount, addedBaseCount;
    if (cache && mines[c].size() <= 63 && mines[d].size() <= 63 &&
        bases[c].size() <= 63 && bases[d].size() <= 63) {
        const auto &mc = cache->row(c, true), &md = cache->row(d, true);
        const auto &bc = cache->row(c, false), &bd = cache->row(d, false);
        auto missing = [](size_t size, uint64_t shared) {
            return ((uint64_t(1) << size) - 1) & ~shared;
        };
        uint64_t sm = missing(mines[c].size(), mc[d]);
        uint64_t lb = missing(bases[c].size(), bc[d]);
        uint64_t am = missing(mines[d].size(), md[c]);
        uint64_t ab = missing(bases[d].size(), bd[c]);
        savedCount = popcount(sm); lostCount = popcount(lb);
        addedMineCount = popcount(am); addedBaseCount = popcount(ab);
        saved = CoverageProjection(mc, sm); lost = CoverageProjection(bc, lb);
        addedBase = CoverageProjection(bd, ab); addedMine = CoverageProjection(md, am);
    } else {
        savedMines = difference(mines[c], mines[d]);
        lostBases = difference(bases[c], bases[d]);
        addedMines = difference(mines[d], mines[c]);
        addedBases = difference(bases[d], bases[c]);
        savedCount = int(savedMines.size()); lostCount = int(lostBases.size());
        addedMineCount = int(addedMines.size()); addedBaseCount = int(addedBases.size());
        saved = CoverageProjection(savedMines, mines, c, true, cache);
        lost = CoverageProjection(lostBases, bases, c, false, cache);
        addedBase = CoverageProjection(addedBases, bases, d, false, cache);
        addedMine = CoverageProjection(addedMines, mines, d, true, cache);
    }
    if (lostCount > 1 + savedCount ||
        addedMineCount + lostCount > savedCount + addedBaseCount)
        return false;
    auto adjacent = [&](int a, int b) {
        return cache ? cache->adjacent(a, b, contacts) :
            binary_search(contacts[a].begin(), contacts[a].end(), b);
    };
    vector<int> neighbors;
    for (int u : contacts[c])
        if (alive[u] && u != d && !adjacent(d, u)) neighbors.push_back(u);
    int visited = 0;
    for (bool swap : {false, true}) {
        int goodCount = savedCount + (swap ? addedBaseCount : 0);
        int badCount = lostCount + (swap ? addedMineCount : 0);
        if (goodCount > 63 || badCount > 63) return false;
        int budget = swap ? 0 : 1;
        vector<uint64_t> good(alive.size()), bad(alive.size());
        vector<int> witnesses;
        for (int u = 0; u < int(alive.size()); ++u) {
            if (!alive[u] || u == c || u == d) continue;
            good[u] = saved.mask(u);
            bad[u] = lost.mask(u);
            if (swap) {
                good[u] |= addedBase.mask(u) << savedCount;
                bad[u] |= addedMine.mask(u) << lostCount;
            }
            if (good[u]) witnesses.push_back(u);
        }
        uint64_t allGood = (uint64_t(1) << goodCount) - 1;
        uint64_t allBad = (uint64_t(1) << badCount) - 1;
        vector<int> chosen;
        auto check = [&](auto &&self, size_t start, uint64_t hitGood,
                         uint64_t hitBad) -> bool {
            if (++visited > 20000) return false;
            uint64_t missingGood = allGood & ~hitGood, missingBad = allBad & ~hitBad;
            uint64_t freeGood = 0;
            vector<pair<uint64_t, uint64_t>> costly;
            for (int u : witnesses) {
                uint64_t gain = good[u] & missingGood;
                if (!gain || find(chosen.begin(), chosen.end(), u) != chosen.end()) continue;
                int touches = 0;
                for (int i : chosen) touches += adjacent(i, u);
                if (touches > (adjacent(d, u) ? 0 : 1)) continue;
                uint64_t loss = bad[u] & missingBad;
                if (loss) costly.emplace_back(gain, loss);
                else freeGood |= gain;
            }
            int delta = int(chosen.size()) + popcount(missingBad) -
                        popcount(missingGood & ~freeGood) - budget;
            if (!witnessesWithinBudget(costly, freeGood, delta, visited)) return false;
            for (size_t i = start; i < neighbors.size(); ++i) {
                int u = neighbors[i];
                bool independent = true;
                for (int j : chosen) if (adjacent(u, j)) { independent = false; break; }
                if (!independent) continue;
                chosen.push_back(u);
                bool safe = self(self, i + 1, hitGood | good[u], hitBad | bad[u]);
                chosen.pop_back();
                if (!safe) return false;
            }
            return true;
        };
        if (!check(check, 0, 0, 0)) return false;
    }
    return true;
}

} // namespace minclicks::detail
