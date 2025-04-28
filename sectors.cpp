/* Copyright (C) 2013-2014 Michal Brzozowski (rusolis@poczta.fm)

   This file is part of KeeperRL.

   KeeperRL is free software; you can redistribute it and/or modify it under the terms of the
   GNU General Public License as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   KeeperRL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
   even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along with this program.
   If not, see http://www.gnu.org/licenses/ . */

#include "stdafx.h"
#include "sectors.h"
#include "level.h"
#include <limits>

SERIALIZE_DEF(Sectors, bounds, sectors, allPos, extraConnections)

SERIALIZATION_CONSTRUCTOR_IMPL(Sectors)

Sectors::Sectors(Rectangle b, ExtraConnections con) : bounds(b), sectors(bounds, -1), extraConnections(std::move(con)) {
}

Sectors::Sectors(Rectangle b) : Sectors(b, ExtraConnections(b)) {}

bool Sectors::same(Vec2 v, Vec2 w) const {
  return contains(v) && sectors[v] == sectors[w];
}

bool Sectors::contains(Vec2 v) const {
  return sectors[v] > -1;
}

bool Sectors::add(Vec2 pos) {
  if (contains(pos))
    return false;
  set<int> neighbors;
  for (Vec2 v : getNeighbors(pos))
    if (v.inRectangle(bounds) && contains(v))
      neighbors.insert(sectors[v]);
  if (neighbors.size() == 0)
    setSector(pos, getNewSector());
  else if (neighbors.size() == 1)
    setSector(pos, *neighbors.begin());
  else {
    int largest = -1;
    for (int elem : neighbors)
      if (largest == -1 || allPos[largest].size() < allPos[elem].size())
        largest = elem;
    join(pos, largest);
  }
  return true;
}

void Sectors::setSector(Vec2 pos, SectorId sector) {
  CHECK(sectors[pos] != sector);
  if (contains(pos))
    allPos[sectors[pos]].erase(pos);
  sectors[pos] = sector;
  allPos[sector].insert(pos);
}

Sectors::SectorId Sectors::getNewSector() {
  allPos.emplace_back(0);
  CHECK(allPos.size() < std::numeric_limits<SectorId>::max());
  return allPos.size() - 1;
}

int Sectors::getNumSectors() const {
  int ret = 0;
  for (auto& elem : allPos)
    if (elem.size() > 0)
      ++ret;
  return ret;
}

void Sectors::join(Vec2 pos1, SectorId sector) {
  queue<Vec2> q;
  q.push(pos1);
  setSector(pos1, sector);
  while (!q.empty()) {
    Vec2 pos = q.front();
    q.pop();
    for (Vec2 v : getNeighbors(pos))
      if (v.inRectangle(bounds) && sectors[v] > -1 && sectors[v] != sector) {
        setSector(v, sector);
        q.push(v);
      }
  }
}

static DirtyTable<int> bfsTable(Level::getMaxBounds(), -1);

vector<Vec2> Sectors::getDisjoint(Vec2 pos) const {
  vector<queue<Vec2>> queues;
  bfsTable.clear();
  int numNeighbor = 0;
  for (Vec2 v : getNeighbors(pos))
    if (v.inRectangle(bounds) && contains(v) && !bfsTable.isDirty(v)) {
        bfsTable.setValue(v, numNeighbor++);
        queues.emplace_back();
        queues.back().push(v);
      }
  if (numNeighbor == 0)
    return {};
  DisjointSets sets(numNeighbor);
  int lastNeighbor = -1;
  while (1) {
    vector<int> activeQueues;
    for (auto& q : queues)
      if (!q.empty()) {
        Vec2 v = q.front();
        int myNum = bfsTable.getValue(v);
        activeQueues.push_back(myNum);
        lastNeighbor = myNum;
        q.pop();
        for (Vec2 w : getNeighbors(v))
          if (w.inRectangle(bounds) && contains(w) && w != pos) {
            if (!bfsTable.isDirty(w)) {
              bfsTable.setValue(w, myNum);
              q.push(w);
            } else
              sets.join(bfsTable.getDirtyValue(w), myNum);
          }
      }
    if (sets.same(activeQueues)) {
      break;
    }
  }
  int maxSector = allPos.size() - 1;
  vector<Vec2> ret;
  for (Vec2 v : getNeighbors(pos))
    if (v.inRectangle(bounds) && sectors[v] <= maxSector && contains(v) &&
          !sets.same(bfsTable.getDirtyValue(v), lastNeighbor))
      ret.push_back(v);
  return ret;
}

vector<Vec2> Sectors::getNeighbors(Vec2 pos) const {
  auto ret = pos.neighbors8();
  if (auto con = extraConnections[pos])
    ret.push_back(*con);
  return ret;
}

void Sectors::addExtraConnection(Vec2 pos1, Vec2 pos2) {
  if (contains(pos1) && contains(pos2)) {
    auto sector1 = sectors[pos1];
    auto sector2 = sectors[pos2];
    if (sector1 != sector2) {
      if (allPos[sector1].size() > allPos[sector2].size())
        join(pos2, sector1);
      else
        join(pos1, sector2);
    }
  }
  CHECK(!extraConnections[pos1] || extraConnections[pos1] == pos2);
  CHECK(!extraConnections[pos2] || extraConnections[pos2] == pos1);
  extraConnections[pos1] = pos2;
  extraConnections[pos2] = pos1;
}

void Sectors::removeExtraConnection(Vec2 pos1, Vec2 pos2) {
  extraConnections[pos1] = none;
  extraConnections[pos2] = none;
  join(pos1, getNewSector());
}

const Sectors::ExtraConnections Sectors::getExtraConnections() const {
  return extraConnections;
}

Sectors::SectorId Sectors::getLargest() const {
  PROFILE;
  int ret = 0;
  for (int i : All(allPos))
    if (allPos[i].size() > allPos[ret].size())
      ret = i;
  return SectorId(ret);
}

const Sectors::PosSet& Sectors::getWholeSector(SectorId id) const {
  return allPos[id];
}

optional<Sectors::SectorId> Sectors::getSector(Vec2 v) const {
  auto id = sectors[v];
  if (id >= 0)
    return id;
  else
    return none;
}

bool Sectors::remove(Vec2 pos) {
  if (!contains(pos))
    return false;
  allPos[sectors[pos]].erase(pos);
  sectors[pos] = -1;
  for (Vec2 v : getDisjoint(pos))
    join(v, getNewSector());
  return true;
}

void Sectors::dump() {
  for (int i : Range(bounds.height())) {
    for (int j : Range(bounds.width()))
      std::cout << sectors[j][i] << " ";
    std::cout << endl;
  }
  std::cout << endl;
}
