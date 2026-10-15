#pragma once
#include "common.hpp"

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

// Certify deletion using actual mine-sharing witnesses, rather than assuming
// that every non-private flag is free. Failure or a search limit keeps v.
inline bool witnessDeletion(int v, const vector<vector<int>> &contacts,
                            const vector<vector<int>> &mines,
                            const vector<vector<int>> &bases,
                            const vector<uint8_t> &alive) {
    if (mines[v].size() > 63 || bases[v].size() > 63) return false;
    struct Near { int id; uint64_t mines = 0, bases = 0; };
    vector<Near> neighbors, witnesses;
    for (int u = 0; u < int(alive.size()); ++u) {
        if (!alive[u] || u == v) continue;
        Near item{u};
        for (size_t i = 0; i < mines[v].size(); ++i)
            if (binary_search(mines[u].begin(), mines[u].end(), mines[v][i]))
                item.mines |= uint64_t(1) << i;
        bool adjacent = binary_search(contacts[v].begin(), contacts[v].end(), u);
        if (!item.mines && !adjacent) continue;
        for (size_t i = 0; i < bases[v].size(); ++i)
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
        return binary_search(contacts[a].begin(), contacts[a].end(), b);
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
                        const vector<uint8_t> &alive) {
    auto difference = [](const vector<int> &a, const vector<int> &b) {
        vector<int> result;
        set_difference(a.begin(), a.end(), b.begin(), b.end(), back_inserter(result));
        return result;
    };
    auto savedMines = difference(mines[c], mines[d]);
    auto lostBases = difference(bases[c], bases[d]);
    auto addedMines = difference(mines[d], mines[c]);
    auto addedBases = difference(bases[d], bases[c]);
    if (lostBases.size() > 1 + savedMines.size() ||
        addedMines.size() + lostBases.size() > savedMines.size() + addedBases.size())
        return false;
    auto adjacent = [&](int a, int b) {
        return binary_search(contacts[a].begin(), contacts[a].end(), b);
    };
    vector<int> neighbors;
    for (int u : contacts[c])
        if (alive[u] && u != d && !adjacent(d, u)) neighbors.push_back(u);
    int visited = 0;
    for (bool swap : {false, true}) {
        int goodCount = int(savedMines.size() + (swap ? addedBases.size() : 0));
        int badCount = int(lostBases.size() + (swap ? addedMines.size() : 0));
        if (goodCount > 63 || badCount > 63) return false;
        int budget = swap ? 0 : 1;
        vector<uint64_t> good(alive.size()), bad(alive.size());
        vector<int> witnesses;
        auto mask = [](const vector<int> &items, const vector<int> &covered) {
            uint64_t result = 0;
            for (size_t i = 0; i < items.size(); ++i)
                if (binary_search(covered.begin(), covered.end(), items[i]))
                    result |= uint64_t(1) << i;
            return result;
        };
        for (int u = 0; u < int(alive.size()); ++u) {
            if (!alive[u] || u == c || u == d) continue;
            good[u] = mask(savedMines, mines[u]);
            bad[u] = mask(lostBases, bases[u]);
            if (swap) {
                good[u] |= mask(addedBases, bases[u]) << savedMines.size();
                bad[u] |= mask(addedMines, mines[u]) << lostBases.size();
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
