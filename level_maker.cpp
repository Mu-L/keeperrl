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

#include "level_maker.h"
#include "item_factory.h"
#include "square.h"
#include "collective_builder.h"
#include "collective.h"
#include "shortest_path.h"
#include "creature.h"
#include "location.h"
#include "level_builder.h"
#include "square_factory.h"
#include "model.h"
#include "monster_ai.h"
#include "item.h"
#include "view_id.h"
#include "furniture_type.h"
#include "furniture_factory.h"
#include "furniture.h"

namespace {

void failGen() {
  throw LevelGenException();
}

void checkGen(bool b) {
  if (!b)
    failGen();
}

class Predicate {
  public:
  bool apply(LevelBuilder* builder, Vec2 pos) const {
    return predFun(builder, pos);
  }

  Vec2 getRandomPosition(LevelBuilder* builder, Rectangle area) {
    vector<Vec2> good;
    for (Vec2 v : area)
      if (apply(builder, v))
        good.push_back(v);
    if (good.empty())
      failGen();
    return builder->getRandom().choose(good);
  }

  static Predicate attrib(SquareAttrib attr) {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) { return builder->hasAttrib(pos, attr);});
  }

  static Predicate negate(Predicate p) {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) { return !p.apply(builder, pos);});
  }

  static Predicate type(SquareType t) {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) { return builder->getType(pos) == t;});
  }

  static Predicate type(vector<SquareType> t) {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) { return contains(t, builder->getType(pos));});
  }

  static Predicate alwaysTrue() {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) { return true;});
  }

  static Predicate alwaysFalse() {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) { return false;});
  }

  static Predicate andPred(Predicate p1, Predicate p2) {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) {
        return p1.apply(builder, pos) && p2.apply(builder, pos);});
  }

  static Predicate orPred(Predicate p1, Predicate p2) {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) {
        return p1.apply(builder, pos) || p2.apply(builder, pos);});
  }

  static Predicate canEnter(MovementType m) {
    return Predicate([=] (LevelBuilder* builder, Vec2 pos) { return builder->getSquare(pos)->canEnterEmpty(m);});
  }

  private:
  typedef function<bool(LevelBuilder*, Vec2)> PredFun;
  Predicate(PredFun fun) : predFun(fun) {}
  PredFun predFun;
};

class Empty : public LevelMaker {
  public:
  Empty(SquareType s, optional<SquareAttrib> attr = none) : square(s), attrib(attr) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (Vec2 v : area) {
      builder->putSquare(v, square, attrib);
    }
  }

  private:
  SquareType square;
  optional<SquareAttrib> attrib;
};

class RoomMaker : public LevelMaker {
  public:
  RoomMaker(int _numRooms,
            int _minSize, int _maxSize, 
            SquareType _wallType,
            optional<SquareType> _onType = none,
            LevelMaker* _roomContents = new Empty(SquareId::FLOOR),
            vector<LevelMaker*> _insideMakers = {},
            bool _diggableCorners = false) : 
      numRooms(_numRooms),
      minSize(_minSize),
      maxSize(_maxSize),
      wallType(_wallType),
      squareType(_onType),
      roomContents(_roomContents),
      insideMakers(toUniquePtr(_insideMakers)),
      diggableCorners(_diggableCorners) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    int spaceBetween = 0;
    Table<int> taken(area.right(), area.bottom());
    for (Vec2 v : area)
      taken[v] = squareType && *squareType != builder->getType(v);
    for (int i : Range(numRooms)) {
      Vec2 p, k;
      bool good;
      int cnt = 100;
      do {
        k = Vec2(builder->getRandom().get(minSize, maxSize), builder->getRandom().get(minSize, maxSize));
        p = Vec2(area.left() + spaceBetween + builder->getRandom().get(area.width() - k.x - 2 * spaceBetween),
                 area.top() + spaceBetween + builder->getRandom().get(area.height() - k.y - 2 * spaceBetween));
        good = true;
        for (Vec2 v : Rectangle(k.x + 2 * spaceBetween, k.y + 2 * spaceBetween))
          if (taken[p + v - Vec2(spaceBetween,spaceBetween)]) {
            good = false;
            break;
          }
      } while (!good && --cnt > 0);
      if (cnt == 0) {
        Debug() << "Placed only " << i << " rooms out of " << numRooms;
        break;
      }
      for (Vec2 v : Rectangle(k))
        taken[v + p] = 1;
      for (Vec2 v : Rectangle(k - Vec2(2, 2)))
        builder->putSquare(p + v + Vec2(1, 1), SquareId::FLOOR, SquareAttrib::ROOM);
      for (int i : Range(p.x, p.x + k.x)) {
        if (i == p.x || i == p.x + k.x - 1) {
          builder->putSquare(Vec2(i, p.y), wallType,
              !diggableCorners ? optional<SquareAttrib>(SquareAttrib::NO_DIG) : none);
          builder->putSquare(Vec2(i, p.y + k.y - 1), wallType,
              !diggableCorners ? optional<SquareAttrib>(SquareAttrib::NO_DIG) : none);
        } else {
          builder->putSquare(Vec2(i, p.y), wallType);
          builder->putSquare(Vec2(i, p.y + k.y - 1), wallType);
        }
      }
      for (int i : Range(p.y, p.y + k.y)) {
        if (i > p.y && i < p.y + k.y - 1) {
          builder->putSquare(Vec2(p.x, i), wallType);
          builder->putSquare(Vec2(p.x + k.x - 1, i), wallType);
        }
      }
      Rectangle inside(p.x + 1, p.y + 1, p.x + k.x - 1, p.y + k.y - 1);
      roomContents->make(builder, inside);
      if (i < insideMakers.size())
        insideMakers[i]->make(builder, inside);
      else
        for (Vec2 v : inside)
          builder->addAttrib(v, SquareAttrib::EMPTY_ROOM);
    }
  }

  private:
  int numRooms;
  int minSize;
  int maxSize;
  SquareType wallType;
  optional<SquareType> squareType;
  PLevelMaker roomContents;
  vector<PLevelMaker> insideMakers;
  bool diggableCorners;
};

class Connector : public LevelMaker {
  public:
  Connector(optional<FurnitureFactory> _door, double _doorProb, double _diggingCost = 3,
        Predicate pred = Predicate::canEnter({MovementTrait::WALK}), optional<SquareAttrib> setAttr = none)
      : door(_door), doorProb(_doorProb), diggingCost(_diggingCost), connectPred(pred), setAttrib(setAttr) {
  }
  double getValue(LevelBuilder* builder, Vec2 pos, Rectangle area) {
    if (builder->canNavigate(pos, {MovementTrait::WALK}))
      return 1;
    if (builder->hasAttrib(pos, SquareAttrib::NO_DIG))
      return ShortestPath::infinity;
    if (builder->hasAttrib(pos, SquareAttrib::LAKE))
      return 15;
    if (builder->hasAttrib(pos, SquareAttrib::RIVER))
      return 15;
    int numCorners = 0;
    int numTotal = 0;
    for (Vec2 v : Vec2::directions8())
      if ((pos + v).inRectangle(area) && builder->getSquare(pos + v)->canEnterEmpty({MovementTrait::WALK})) {
        if (abs(v.x) == abs(v.y))
          ++numCorners;
        ++numTotal;
      }
    if (numCorners == 1)
      return 1000;
    if (numTotal - numCorners > 1)
      return diggingCost + 5;
    return diggingCost;
  }

  void connect(LevelBuilder* builder, Vec2 p1, Vec2 p2, Rectangle area) {
    ShortestPath path(area,
        [builder, this, &area](Vec2 pos) { return getValue(builder, pos, area); }, 
        [] (Vec2 v) { return v.length4(); },
        Vec2::directions4(builder->getRandom()), p1 ,p2);
    Vec2 prev(-100, -100);
    for (Vec2 v = p2; v != p1; v = path.getNextMove(v)) {
      if (!builder->canNavigate(v, {MovementTrait::WALK})) {
        optional<SquareType> newType;
        optional<FurnitureFactory> furniture;
        SquareType oldType = builder->getType(v);
        if (isWall(oldType)) {
          newType = SquareId::FLOOR;
          if (!!door && oldType != SquareId::BLACK_WALL && builder->getRandom().rollD(doorProb))
            furniture = *door;
        } else
          switch (oldType.getId()) {
            case SquareId::ABYSS:
            case SquareId::WATER:
            case SquareId::WATER_WITH_DEPTH:
            case SquareId::MAGMA:
              if (!builder->getFurnitureType(v))
                furniture = FurnitureFactory(TribeId::getMonster(), FurnitureType::BRIDGE);
              break;
            case SquareId::MOUNTAIN:
            case SquareId::BLACK_WALL:
              newType = SquareId::FLOOR;
              break;
            default:
              FAIL << "Unhandled square type " << (int)builder->getType(v).getId();
          }
        if (newType)
          builder->putSquare(v, *newType, setAttrib);
        if (furniture)
          builder->putFurniture(v, *furniture);
      }
      if (!path.isReachable(v))
        failGen();
      prev = v;
    }
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    Vec2 p1, p2;
    vector<Vec2> points = filter(area.getAllSquares(), [&] (Vec2 v) { return connectPred.apply(builder, v);});
    if (points.size() < 2)
      return;
    for (int i : Range(30)) {
      p1 = builder->getRandom().choose(points);
      p2 = builder->getRandom().choose(points);
      if (p1 != p2)
        connect(builder, p1, p2, area);
    }
    auto dijkstraFun = [&] (Vec2 pos) {
      if (builder->getSquare(pos)->canEnterEmpty({MovementTrait::WALK}))
        return 1.;
      else
        return ShortestPath::infinity;};
    Table<bool> connected(area, false);
    while (1) {
      Dijkstra dijkstra(area, p1, 10000, dijkstraFun);
      for (Vec2 v : area)
        if (dijkstra.isReachable(v))
          connected[v] = true;
      bool found = false;
      for (Vec2 v : area)
        if (connectPred.apply(builder, v) && !connected[v]) {
          connect(builder, p1, v, area);
          p1 = v;
          found = true;
          break;
         }
      if (!found)
        break;
    }
  }
  
  private:
  optional<FurnitureFactory> door;
  double doorProb;
  double diggingCost;
  Predicate connectPred;
  optional<SquareAttrib> setAttrib;
};

namespace {
class Furnitures : public LevelMaker {
  public:
  Furnitures(Predicate pred, double _density, FurnitureFactory _factory, optional<SquareAttrib> setAttr = none): 
      factory(_factory), density(_density), predicate(pred), attr(setAttr) {
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    vector<Vec2> available;
    for (Vec2 v : area)
      if (predicate.apply(builder, v) && builder->canPutFurniture(v))
        available.push_back(v);
    for (int i : Range(available.size() * density)) {
      Vec2 pos = builder->getRandom().choose(available);
      builder->putFurniture(pos, factory);
      if (attr)
        builder->addAttrib(pos, *attr);
      removeElement(available, pos);
    }
  }

  private:
  FurnitureFactory factory;
  double density;
  Predicate predicate;
  optional<SquareAttrib> attr;
};
}

class Creatures : public LevelMaker {
  public:
  Creatures(CreatureFactory cf, int numC, MonsterAIFactory actorF, Predicate pred = Predicate::alwaysTrue()) :
      cfactory(cf), numCreature(numC), actorFactory(actorF), onPred(pred) {}

  Creatures(CreatureFactory cf, int numC, CollectiveBuilder* col, Predicate pred = Predicate::alwaysTrue()) :
      cfactory(cf), numCreature(numC), actorFactory(MonsterAIFactory::monster()), onPred(pred),
      collective(col) {}

  Creatures(CreatureFactory cf, int numC, Predicate pred = Predicate::alwaysTrue()) :
      cfactory(cf), numCreature(numC), onPred(pred) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    if (!actorFactory) {
      Location* loc = new Location();
      builder->addLocation(loc, area);
      actorFactory = MonsterAIFactory::stayInLocation(loc);
    }
    Table<char> taken(area.right(), area.bottom());
    for (int i : Range(numCreature)) {
      PCreature creature = cfactory.random(*actorFactory);
      Vec2 pos;
      int numTries = 100;
      do {
        pos = Vec2(builder->getRandom().get(area.left(), area.right()),
            builder->getRandom().get(area.top(), area.bottom()));
      } while (--numTries > 0 && (!builder->canPutCreature(pos, creature.get()) || (!onPred.apply(builder, pos))));
      checkGen(numTries > 0);
      if (collective) {
        collective->addCreature(creature.get());
        builder->addCollective(collective);
      }
      builder->putCreature(pos, std::move(creature));
      taken[pos] = 1;
    }
  }

  private:
  CreatureFactory cfactory;
  int numCreature;
  optional<MonsterAIFactory> actorFactory;
  Predicate onPred;
  CollectiveBuilder* collective = nullptr;
};

class Items : public LevelMaker {
  public:
  Items(ItemFactory _factory, SquareType _onType, int minc, int maxc) : 
      factory(_factory), onType(_onType), minItem(minc), maxItem(maxc) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    int numItem = builder->getRandom().get(minItem, maxItem);
    for (int i : Range(numItem)) {
      Vec2 pos;
      do {
        pos = Vec2(builder->getRandom().get(area.left(), area.right()), builder->getRandom().get(area.top(),
              area.bottom()));
      } while (builder->getType(pos) != onType);
      builder->putItems(pos, factory.random());
    }
  }

  private:
  ItemFactory factory;
  SquareType onType;
  int minItem;
  int maxItem;
};

class River : public LevelMaker {
  public:
  River(int _width, SquareType _squareType) : width(_width), squareType(_squareType){}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    int wind = 5;
    int middle = (area.left() + area.right()) / 2;
    int px = builder->getRandom().get(middle - wind, middle + width);
    int kx = px + builder->getRandom().get(-wind, wind); // builder->getRandom().get(area.left(), area.right()) - width;
    if (kx < 0)
      kx = 0;
    if (kx >= area.right() - width)
      kx = area.right() - width - 1;
    int tot = 5;
    for (int h : Range(tot)) {
      int height = area.top() * (tot - h) / tot + area.bottom() * h / tot;
      int height2 = area.top() * (tot - h - 1) / tot + area.bottom() * (h + 1) / tot;
      vector<Vec2> line = straightLine(px, height, kx, (h == tot - 1) ? area.bottom() : height2);
      for (Vec2 v : line)
        for (int i : Range(width))
          builder->putSquare(v + Vec2(i, 0), squareType, SquareAttrib::RIVER);
      px = kx;
      kx = px + builder->getRandom().get(-wind, wind);
      if (kx < 0)
        kx = 0;
      if (kx >= area.right() - width)
        kx = area.right() - width - 1;
    }
  }

  private:

  vector<Vec2> straightLine(int x0, int y0, int x1, int y1){
    Debug() << "Line " << x1 << " " << y0 << " " << x1 << " " << y1;
    int dx = x1 - x0;
    int dy = y1 - y0;
    vector<Vec2> ret{ Vec2(x0, y0)};
    if (abs(dx) > abs(dy)) {          // slope < 1
      double m = (double) dy / (double) dx;      // compute slope
      double b = y0 - m*x0;
      dx = (dx < 0) ? -1 : 1;
      while (x0+dx != x1) {
        x0 += dx;
        ret.push_back(Vec2(x0,(int)roundf(m*x0+b)));
      }
    } else
      if (dy != 0) {                              // slope >= 1
        double m = (double) dx / (double) dy;      // compute slope
        double b = x0 - m*y0;
        dy = (dy < 0) ? -1 : 1;
        while (y0+dy != y1) {
          y0 += dy;
          ret.push_back(Vec2((int)round(m*y0+b),y0));
        }
      }
    return ret;
  }

  int width;
  SquareType squareType;
};

class MountainRiver : public LevelMaker {
  public:
  MountainRiver(int num, SquareType waterType, SquareType sandType, Predicate startPred)
    : number(num), water(waterType), sand(sandType), startPredicate(startPred) {}

  optional<Vec2> fillLake(LevelBuilder* builder, set<Vec2>& waterTiles, Rectangle area, Vec2 pos) {
    vector<Vec2> ret;
    double height = builder->getHeightMap(pos);
    queue<Vec2> q;
    set<Vec2> visited {pos};
    map<Vec2, Vec2> predecessor {{ pos, pos}};
    q.push(pos);
    while (!q.empty()) {
      pos = q.front();
      q.pop();
      for (Vec2 v : pos.neighbors8(builder->getRandom()))
        if (v.inRectangle(area) && !visited.count(v)) {
          visited.insert(v);
          predecessor[v] = pos;
          if (fabs(builder->getHeightMap(v) - height) < 0.000001)
            q.push(v);
          else
          if (builder->getHeightMap(v) < height)
            ret.push_back(v);
        }
    }
    if (builder->getRandom().roll(predecessor.size()) || ret.empty()) {
      for (auto& elem : predecessor)
        if (!contains(ret, elem.first))
          waterTiles.insert(elem.first);
      if (ret.empty())
        return none;
      else
        return builder->getRandom().choose(ret);
    } else {
      Vec2 end = builder->getRandom().choose(ret);
      for (Vec2 v = predecessor.at(end);; v = predecessor.at(v)) {
        waterTiles.insert(v);
        if (v == predecessor.at(v))
          break;
      }
      return end;
    }
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    set<Vec2> allWaterTiles;
    for (int i : Range(number)) {
      set<Vec2> waterTiles;
      Vec2 pos = startPredicate.getRandomPosition(builder, area);
      int width = builder->getRandom().get(3, 6);
      while (1) {
        if (builder->getType(pos) == water)
          break;
        if (auto next = fillLake(builder, waterTiles, area, pos))
          pos = *next;
        else
          break;
      }
      for (Vec2 v : waterTiles)
        for (Vec2 v2 : Rectangle(v, v + Vec2(width, width)))
          allWaterTiles.insert(v2);
    }
    double depth = 0;
    for (set<Vec2> layer : Vec2::calculateLayers(allWaterTiles)) {
      for (Vec2 v : layer)
        if (v.inRectangle(area)) {
          if (contains({SquareId::WATER, SquareId::SAND}, builder->getType(v).getId()))
            continue;
          if (builder->getType(v) == SquareId::GRASS) {
            if (depth == 0)
              builder->putSquare(v, SquareId::SAND, SquareAttrib::RIVER);
            else
              builder->putSquare(v, {SquareId::WATER_WITH_DEPTH, depth}, SquareAttrib::RIVER);
          } else
            builder->putSquare(v, SquareId::WATER, SquareAttrib::RIVER);

        }
      depth += 0.6;
    }
  }

  private:
  int number;
  SquareType water;
  SquareType sand;
  Predicate startPredicate;
};

class Blob : public LevelMaker {
  public:
  Blob(double _insideRatio = 0.333) : insideRatio(_insideRatio) {}

  virtual void addSquare(LevelBuilder* builder, Vec2 pos, int edgeDist) = 0;

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    vector<Vec2> squares;
    Table<char> isInside(area, 0);
    Vec2 center((area.right() + area.left()) / 2, (area.bottom() + area.top()) / 2);
    squares.push_back(center);
    isInside[center] = 1;
    int maxSquares = area.width() * area.height() * insideRatio;
    int numSquares = 0;
    bool done = false;
    while (!done) {
      Vec2 pos = squares[builder->getRandom().get(squares.size())];
      for (Vec2 next : pos.neighbors4(builder->getRandom())) {
        if (next.inRectangle(area.minusMargin(1)) && !isInside[next]) {
          Vec2 proj = next - center;
          proj.y *= area.width();
          proj.y /= area.height();
          if (builder->getRandom().getDouble() <= 1. - proj.lengthD() / (area.width() / 2)) {
            isInside[next] = 1;
            squares.push_back(next);
            if (++numSquares >= maxSquares)
              done = true;
          }
          break;
        }
      }
    }
    queue<Vec2> q;
    int inf = 10000;
    Table<int> distance(area, inf);
    for (Vec2 v : isInside.getBounds())
      if (!isInside[v]) {
        distance[v] = 0;
        q.push(v);
      }
    while (!q.empty()) {
      Vec2 pos = q.front();
      q.pop();
      for (Vec2 v : pos.neighbors8())
        if (v.inRectangle(area) && distance[v] == inf) {
          distance[v] = distance[pos] + 1;
          q.push(v);
          addSquare(builder, v, distance[v]);
        }
    }
  }

  private:
  double insideRatio;
};

class UniformBlob : public Blob {
  public:
  UniformBlob(SquareType insideSquare, optional<SquareType> borderSquare = none,
      optional<SquareAttrib> _attrib = none, double insideRatio = 0.3333) : Blob(insideRatio),
      inside(insideSquare), border(borderSquare), attrib(_attrib) {}

  virtual void addSquare(LevelBuilder* builder, Vec2 pos, int edgeDist) override {
    if (edgeDist == 1 && border)
      builder->putSquare(pos, *border, attrib);
    else
      builder->putSquare(pos, inside, attrib);
  }

  private:
  SquareType inside;
  optional<SquareType> border;
  optional<SquareAttrib> attrib;
};

class FurnitureBlob : public Blob {
  public:
  FurnitureBlob(FurnitureFactory in, double insideRatio = 0.3333) : Blob(insideRatio), inside(in) {}

  virtual void addSquare(LevelBuilder* builder, Vec2 pos, int edgeDist) override {
    builder->putFurniture(pos, inside);
  }

  private:
  FurnitureFactory inside;
};

class Lake : public Blob {
  public:
  Lake(bool s = true) : sand(s) {}
  virtual void addSquare(LevelBuilder* builder, Vec2 pos, int edgeDist) override {
    if (sand && edgeDist == 1 && builder->getType(pos).getId() != SquareId::WATER)
      builder->putSquare(pos, SquareId::SAND, SquareAttrib::LAKE);
    else
      builder->putSquare(pos, {SquareId::WATER_WITH_DEPTH, double(edgeDist) / 2}, SquareAttrib::LAKE);
  }

  private:
  bool sand;
};

class RemoveFurniture : public LevelMaker {
  public:
  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (Vec2 v : area)
      builder->removeFurniture(v);
  }
};

struct BuildingInfo {
  SquareType wall;
  SquareType floorInside;
  SquareType floorOutside;
  optional<FurnitureFactory> door;
};

static BuildingInfo getBuildingInfo(SettlementInfo info) {
  switch (info.buildingId) {
    case BuildingId::WOOD:
      return CONSTRUCT(BuildingInfo,
          c.wall = SquareId::WOOD_WALL;
          c.floorInside = SquareId::FLOOR;
          c.floorOutside = SquareId::GRASS;
          c.door = FurnitureFactory(info.tribe, FurnitureType::DOOR);
      );
    case BuildingId::WOOD_CASTLE:
      return CONSTRUCT(BuildingInfo,
          c.wall = SquareId::WOOD_WALL;
          c.floorInside = SquareId::FLOOR;
          c.floorOutside = SquareId::MUD;
          c.door = FurnitureFactory(info.tribe, FurnitureType::DOOR);
      );
    case BuildingId::MUD: 
      return CONSTRUCT(BuildingInfo,
          c.wall = SquareId::MUD_WALL;
          c.floorInside = SquareId::MUD;
          c.floorOutside = SquareId::MUD;);
    case BuildingId::BRICK:
      return CONSTRUCT(BuildingInfo,
          c.wall = SquareId::CASTLE_WALL;
          c.floorInside = SquareId::FLOOR;
          c.floorOutside = SquareId::MUD;
          c.door = FurnitureFactory(info.tribe, FurnitureType::DOOR);
      );
    case BuildingId::DUNGEON:
      return CONSTRUCT(BuildingInfo,
          c.wall = SquareId::MOUNTAIN;
          c.floorInside = SquareId::FLOOR;
          c.floorOutside = SquareId::FLOOR;
          c.door = FurnitureFactory(info.tribe, FurnitureType::DOOR);
      );
  }
}

class Buildings : public LevelMaker {
  public:
  Buildings(int _minBuildings, int _maxBuildings,
      int _minSize, int _maxSize,
      BuildingInfo _building,
      bool _align,
      vector<LevelMaker*> _insideMakers,
      bool _roadConnection = true) :
    minBuildings(_minBuildings),
    maxBuildings(_maxBuildings),
    minSize(_minSize), maxSize(_maxSize),
    align(_align),
    building(_building),
    insideMakers(toUniquePtr(_insideMakers)),
    roadConnection(_roadConnection) {
      CHECK(insideMakers.size() <= minBuildings);
    }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    Table<bool> filled(area);
    int width = area.width();
    int height = area.height();
    for (Vec2 v : area)
      filled[v] =  0;
    int sizeVar = 1;
    int spaceBetween = 1;
    int alignHeight = 0;
    if (align) {
      alignHeight = height / 2 - 2 + builder->getRandom().get(5);
    }
    int nextw = -1;
    int numBuildings = builder->getRandom().get(minBuildings, maxBuildings);
    for (int i = 0; i < numBuildings; ++i) {
      bool spaceOk = true;
      int w, h, px, py;
      int cnt = 10000;
      bool buildingRow;
      do {
        buildingRow = builder->getRandom().get(2);
        spaceOk = true;
        w = builder->getRandom().get(minSize, maxSize);
        h = builder->getRandom().get(minSize, maxSize);
        if (nextw > -1 && nextw + w < area.right()) {
          px = nextw;
          nextw = -1;
        } else
          px = area.left() + builder->getRandom().get(width - w - 2 * spaceBetween + 1) + spaceBetween;
        if (!align)
          py = area.top() + builder->getRandom().get(height - h - 2 * spaceBetween + 1) + spaceBetween;
        else {
          py = area.top() + (buildingRow == 1 ? alignHeight - h - 1 : alignHeight + 2);
          if (py + h >= area.bottom() || py < area.top()) {
            spaceOk = false;
            continue;
          }
        }
        Vec2 tmp(px - spaceBetween, py - spaceBetween);
        for (Vec2 v : Rectangle(w + spaceBetween * 2 + 1, h + spaceBetween * 2 + 1))
          if (!(tmp + v).inRectangle(area) || filled[px + v.x - spaceBetween][py + v.y - spaceBetween]) {
            spaceOk = false;
            break;
          }
      } while (!spaceOk && --cnt > 0);
      if (cnt == 0) {
        if (i < minBuildings)
          failGen(); // "Failed to add " << minBuildings - i << " buildings out of " << minBuildings;
        else
          break;
      }
      if (builder->getRandom().roll(1))
        nextw = px + w;
      for (Vec2 v : Rectangle(w + 1, h + 1)) {
        filled[Vec2(px, py) + v] = true;
        builder->putSquare(Vec2(px, py) + v, building.wall);
        builder->setCoverOverride(Vec2(px, py) + v, true);
      }
      for (Vec2 v : Rectangle(w - 1, h - 1)) {
        builder->putSquare(Vec2(px + 1, py + 1) + v, building.floorInside, SquareAttrib::ROOM);
      }
      Vec2 doorLoc = align ? 
          Vec2(px + builder->getRandom().get(1, w),
               py + (buildingRow * h)) :
          getRandomExit(Random, Rectangle(px, py, px + w + 1, py + h + 1));
      builder->putSquare(doorLoc, building.floorInside);
      if (building.door)
        builder->putFurniture(doorLoc, *building.door);
      Rectangle inside(px + 1, py + 1, px + w, py + h);
      if (i < insideMakers.size()) 
        insideMakers[i]->make(builder, inside);
      else
        for (Vec2 v : inside)
          builder->addAttrib(v, SquareAttrib::EMPTY_ROOM);
    }
    if (align)
      for (Vec2 v : Rectangle(area.left() + area.width() / 3, area.top() + alignHeight,
            area.right() - area.width() / 3, area.top() + alignHeight + 2))
        builder->addAttrib(v, SquareAttrib::BUILDINGS_CENTER);
    if (roadConnection) {
      Vec2 pos = Vec2((area.left() + area.right()) / 2, area.top() + alignHeight);
      builder->removeFurniture(pos);
      builder->putFurniture(pos, FurnitureParams{FurnitureType::ROAD, TribeId::getMonster()});
      builder->addAttrib(pos, SquareAttrib::CONNECT_ROAD);
    }
  }

  private:
  int minBuildings;
  int maxBuildings;
  int minSize;
  int maxSize;
  bool align;
  BuildingInfo building;
  vector<PLevelMaker> insideMakers;
  bool roadConnection;
};

class BorderGuard : public LevelMaker {
  public:

  BorderGuard(LevelMaker* inside, SquareType _type = SquareId::BORDER_GUARD) : type(_type), insideMaker(inside) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (int i : Range(area.left(), area.right())) {
      builder->putSquare(Vec2(i, area.top()), type);
      builder->putSquare(Vec2(i, area.bottom() - 1), type);
    }
    for (int i : Range(area.top(), area.bottom())) {
      builder->putSquare(Vec2(area.left(), i), type);
      builder->putSquare(Vec2(area.right() - 1, i), type);
    }
    insideMaker->make(builder, Rectangle(area.left() + 1, area.top() + 1, area.right() - 1, area.bottom() - 1));
  }

  private:
  SquareType type;
  PLevelMaker insideMaker;

};

class MakerQueue : public LevelMaker {
  public:
  MakerQueue() = default;
  MakerQueue(vector<LevelMaker*> _makers) : makers(toUniquePtr(_makers)) {}

  void addMaker(LevelMaker* maker) {
    makers.push_back(PLevelMaker(maker));
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (auto& maker : makers)
      maker->make(builder, area);
  }

  private:
  vector<PLevelMaker> makers;
};

class PredicatePrecalc {
  public:
  PredicatePrecalc(const Predicate& predicate, LevelBuilder* builder, Rectangle area)
      : counts(Rectangle(area.topLeft(), area.bottomRight() + Vec2(1, 1))) {
    int px = counts.getBounds().left();
    int py = counts.getBounds().top();
    for (int x : Range(px, counts.getBounds().right()))
      counts[x][py] = 0;
    for (int y : Range(py, counts.getBounds().bottom()))
      counts[px][y] = 0;
    for (Vec2 v : Rectangle(area.topLeft() + Vec2(1, 1), counts.getBounds().bottomRight()))
      counts[v] = (predicate.apply(builder, v - Vec2(1, 1)) ? 1 : 0) +
        counts[v.x - 1][v.y] + counts[v.x][v.y - 1] -counts[v.x - 1][v.y - 1];
  }

  int getCount(Rectangle area) const {
    return counts[area.bottomRight()] + counts[area.topLeft()]
      -counts[area.bottomLeft()] - counts[area.topRight()];
  }

  private:
  Table<int> counts;
};


class RandomLocations : public LevelMaker {
  public:
  RandomLocations(const vector<LevelMaker*>& _insideMakers, const vector<pair<int, int>>& _sizes,
      Predicate pred, bool _separate = true)
        : insideMakers(toUniquePtr(_insideMakers)), sizes(_sizes), predicate(sizes.size(), pred),
          separate(_separate) {
        CHECK(insideMakers.size() == sizes.size());
        CHECK(predicate.size() == sizes.size());
      }

  RandomLocations(const vector<LevelMaker*>& _insideMakers, const vector<pair<int, int>>& _sizes,
      const vector<Predicate>& pred, bool _separate = true)
        : insideMakers(toUniquePtr(_insideMakers)), sizes(_sizes), predicate(pred.begin(), pred.end()),
          separate(_separate) {
    CHECK(insideMakers.size() == sizes.size());
    CHECK(pred.size() == sizes.size());
  }

  class LocationPredicate {
    public:
    LocationPredicate(Predicate main, Predicate sec, int minSec, int maxSec)
      // main and sec must be mutually exclusive!!!
        : predicate(main), second(sec), minSecond(minSec), maxSecond(maxSec) {
    }

    LocationPredicate(Predicate p) : predicate(p) {}

    class Precomputed {
      public:
      Precomputed(LevelBuilder* builder, Rectangle area, Predicate p1, Predicate p2, int minSec, int maxSec)
        : pred1(p1, builder, area), pred2(p2, builder, area), minSecond(minSec), maxSecond(maxSec) {
      }

      bool apply(Rectangle rect) const {
        int numFirst = pred1.getCount(rect);
        int numSecond = pred2.getCount(rect);
        return numSecond >= minSecond && numSecond < maxSecond && numSecond + numFirst == rect.width() * rect.height();
      }

      private:
      PredicatePrecalc pred1;
      PredicatePrecalc pred2;
      int minSecond;
      int maxSecond;
    };

    Precomputed precompute(LevelBuilder* builder, Rectangle area) {
      return Precomputed(builder, area, predicate, second, minSecond, maxSecond);
    }

    private:
    Predicate predicate;
    Predicate second = Predicate::alwaysFalse();
    int minSecond = 0;
    int maxSecond = 1;
  };

  RandomLocations(bool _separate = true) : separate(_separate) {}

  void add(LevelMaker* maker, Vec2 size, LocationPredicate pred) {
    insideMakers.emplace_back(maker);
    sizes.push_back({size.x, size.y});
    predicate.push_back(pred);
  }

  void setMinDistance(LevelMaker* m1, LevelMaker* m2, double dist) {
    minDistance[{m1, m2}] = dist;
    minDistance[{m2, m1}] = dist;
  }

  void setMaxDistance(LevelMaker* m1, LevelMaker* m2, double dist) {
    maxDistance[{m1, m2}] = dist;
    maxDistance[{m2, m1}] = dist;
  }

  void setMinDistanceLast(LevelMaker* m, double dist) {
    minDistance[{m, insideMakers.back().get()}]  = dist;
    minDistance[{insideMakers.back().get(), m}]  = dist;
  }

  void setMaxDistanceLast(LevelMaker* m, double dist) {
    maxDistance[{m, insideMakers.back().get()}] = dist;
    maxDistance[{insideMakers.back().get(), m}] = dist;
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    vector<LocationPredicate::Precomputed> precomputed;
    for (int i : All(insideMakers))
      precomputed.push_back(predicate[i].precompute(builder, area));
    for (int i : Range(3000))
      if (tryMake(builder, precomputed, area))
        return;
    failGen(); // "Failed to find free space for " << (int)sizes.size() << " areas";
  }

  bool tryMake(LevelBuilder* builder, vector<LocationPredicate::Precomputed>& precomputed, Rectangle area) {
    vector<Rectangle> occupied;
    vector<Rectangle> makerBounds;
    vector<LevelBuilder::Rot> maps;
    for (int i : All(insideMakers))
      maps.push_back(builder->getRandom().choose(
            LevelBuilder::CW0, LevelBuilder::CW1, LevelBuilder::CW2, LevelBuilder::CW3));
    for (int i : All(insideMakers)) {
      int width = sizes[i].first;
      int height = sizes[i].second;
      if (contains({LevelBuilder::CW1, LevelBuilder::CW3}, maps[i]))
        std::swap(width, height);
      CHECK(width <= area.width() && height <= area.height());
      int px;
      int py;
      int cnt = 1000;
      bool ok;
      do {
        ok = true;
        px = area.left() + builder->getRandom().get(area.width() - width);
        py = area.top() + builder->getRandom().get(area.height() - height);
        for (int j : Range(i))
          if ((maxDistance.count({insideMakers[j].get(), insideMakers[i].get()}) && 
                maxDistance[{insideMakers[j].get(), insideMakers[i].get()}] <
                  Vec2(px + width / 2, py + height / 2).dist8(occupied[j].middle())) ||
              minDistance[{insideMakers[j].get(), insideMakers[i].get()}] >
                  Vec2(px + width / 2, py + height / 2).dist8(occupied[j].middle())) {
            ok = false;
            break;
          }
        if (!precomputed[i].apply(Rectangle(px, py, px + width, py + height)))
          ok = false;
        else
          if (separate)
            for (Rectangle r : occupied)
              if (r.intersects(Rectangle(px, py, px + width, py + height))) {
                ok = false;
                break;
              }
      } while (!ok && --cnt > 0);
      if (cnt == 0)
        return false;
      occupied.push_back(Rectangle(px, py, px + width, py + height));
      makerBounds.push_back(Rectangle(px, py, px + sizes[i].first, py + sizes[i].second));
    }
    CHECK(insideMakers.size() == occupied.size());
    for (int i : All(insideMakers)) {
      builder->pushMap(makerBounds[i], maps[i]);
      insideMakers[i]->make(builder, makerBounds[i]);
      builder->popMap();
    }
    return true;
  }

  private:
  vector<PLevelMaker> insideMakers;
  vector<pair<int, int>> sizes;
  vector<LocationPredicate> predicate;
  bool separate;
  map<pair<LevelMaker*, LevelMaker*>, double> minDistance;
  map<pair<LevelMaker*, LevelMaker*>, double> maxDistance;
};

class Margin : public LevelMaker {
  public:
  Margin(int s, LevelMaker* in) : left(s), top(s), right(s), bottom(s), inside(in) {}
  Margin(int _left, int _top, int _right, int _bottom, LevelMaker* in) 
      :left(_left) ,top(_top), right(_right), bottom(_bottom), inside(in) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    CHECK(area.width() > left + right && area.height() > top + bottom);
    inside->make(builder, Rectangle(
          area.left() + left,
          area.top() + top,
          area.right() - right,
          area.bottom() - bottom));
  }

  private:
  int left, top, right, bottom;
  LevelMaker* inside;
};

void addAvg(int x, int y, const Table<double>& wys, double& avg, int& num) {
  Vec2 pos(x, y);
  if (pos.inRectangle(wys.getBounds())) {
    avg += wys[pos];
    ++num;
  } 
}

Table<double> genNoiseMap(RandomGen& random, Rectangle area, vector<int> cornerLevels, double varianceMult) {
  int width = 1;
  while (width < area.width() - 1 || width < area.height() - 1)
    width *= 2;
  width /= 2;
  ++width;
  Table<double> wys(width, width);
  wys[0][0] = cornerLevels[0];
  wys[width - 1][0] = cornerLevels[1];
  wys[width - 1][width - 1] = cornerLevels[2];
  wys[0][width - 1] = cornerLevels[3];
  wys[(width - 1) / 2][(width - 1) / 2] = cornerLevels[4];

  double variance = 0.5;
  double heightDiff = 0.1;
  for (int a = width - 1; a >= 2; a /= 2) {
    if (a < width - 1)
      for (Vec2 pos1 : Rectangle((width - 1) / a, (width - 1) / a)) {
        Vec2 pos = pos1 * a;
        double avg = (wys[pos] + wys[pos.x + a][pos.y] + wys[pos.x][pos.y + a] + wys[pos.x + a][pos.y + a]) / 4;
        wys[pos.x + a / 2][pos.y + a / 2] =
            avg + variance * (random.getDouble() * 2 - 1);
      }
    for (Vec2 pos1 : Rectangle((width - 1) / a, (width - 1) / a + 1)) {
      Vec2 pos = pos1 * a;
      double avg = 0;
      int num = 0;
      addAvg(pos.x + a / 2, pos.y - a / 2, wys, avg, num);
      addAvg(pos.x, pos.y, wys, avg, num);
      addAvg(pos.x + a, pos.y, wys, avg, num);
      addAvg(pos.x + a / 2, pos.y + a / 2, wys, avg, num);
      wys[pos.x + a / 2][pos.y] =
          avg / num + variance * (random.getDouble() * 2 - 1);
    }
    for (Vec2 pos1 : Rectangle((width - 1) / a + 1, (width - 1) / a)) {
      Vec2 pos = pos1 * a;
      double avg = 0;
      int num = 0;
      addAvg(pos.x - a / 2, pos.y + a / 2, wys, avg, num);
      addAvg(pos.x, pos.y, wys, avg, num);
      addAvg(pos.x, pos.y + a , wys, avg, num);
      addAvg(pos.x + a / 2, pos.y + a / 2, wys, avg, num);
      wys[pos.x][pos.y + a / 2] =
          avg / num + variance * (random.getDouble() * 2 - 1);
    }
    variance *= varianceMult;
  }
  Table<double> ret(area);
  Vec2 offset(area.left(), area.top());
  for (Vec2 v : area) {
    Vec2 lv((v.x - offset.x) * width / area.width(), (v.y - offset.y) * width / area.height());
    ret[v] = wys[lv];
  }
  return ret;
}

void raiseLocalMinima(Table<double>& t) {
  Vec2 minPos = t.getBounds().topLeft();
  for (Vec2 v : t.getBounds())
    if (t[v] < t[minPos])
      minPos = v;
  Table<bool> visited(t.getBounds(), false);
  auto comparator = [&](const Vec2& v1, const Vec2& v2) { return t[v1] > t[v2];};
  priority_queue<Vec2, vector<Vec2>, decltype(comparator)> q(comparator);
  q.push(minPos);
  visited[minPos] = true;
  while (!q.empty()) {
    Vec2 pos = q.top();
    q.pop();
    for (Vec2 v : pos.neighbors4())
      if (v.inRectangle(t.getBounds()) && !visited[v]) {
        if (t[v] < t[pos])
          t[v] = t[pos];
        q.push(v);
        visited[v] = true;
      }
  }
}

vector<double> sortedValues(const Table<double>& t) {
  vector<double> values;
  for (Vec2 v : t.getBounds()) {
    values.push_back(t[v]);
  }
  sort(values.begin(), values.end());
  return values;
}

class Mountains : public LevelMaker {
  public:
  Mountains(vector<double> r, double varianceM, vector<int> _cornerLevels, vector<SquareType> _types) 
      : ratio(r), cornerLevels(_cornerLevels), types(_types), varianceMult(varianceM) {
    CHECK(r.size() == 5);
  }


  virtual void make(LevelBuilder* builder, Rectangle area) override {
    Table<double> wys = genNoiseMap(builder->getRandom(), area, cornerLevels, varianceMult);
    raiseLocalMinima(wys);
    vector<double> values = sortedValues(wys);
    double cutOffValSand = values[(int)(ratio[0] * double(values.size() - 1))];
    double cutOffValLand = values[(int)(ratio[1] * double(values.size() - 1))];
    double cutOffValHill = values[(int)(ratio[2] * double(values.size() - 1))];
    double cutOffVal = values[(int)(ratio[3] * double(values.size() - 1))];
    double cutOffValSnow = values[(int)(ratio[4] * double(values.size() - 1))];
    int gCnt = 0, mCnt = 0, hCnt = 0, lCnt = 0, sCnt = 0, wCnt = 0;
    for (Vec2 v : area) {
      builder->setHeightMap(v, wys[v]);
      if (wys[v] >= cutOffValSnow) {
        builder->putSquare(v, types[0], SquareAttrib::GLACIER);
        builder->setSunlight(v, 0.0);
        ++gCnt;
      }
      else if (wys[v] >= cutOffVal) {
        builder->putSquare(v, types[1], SquareAttrib::MOUNTAIN);
        builder->setSunlight(v, 1. - (wys[v] - cutOffVal) / (cutOffValSnow - cutOffVal));
        ++mCnt;
      }
      else if (wys[v] >= cutOffValHill) {
        builder->putSquare(v, types[2], SquareAttrib::HILL);
        ++hCnt;
      }
      else if (wys[v] >= cutOffValLand) {
        builder->putSquare(v, types[3]);
        builder->addAttrib(v, SquareAttrib::LOWLAND);
        ++lCnt;
      }
      else if (wys[v] >= cutOffValSand) {
        builder->putSquare(v, types[4]);
        builder->addAttrib(v, SquareAttrib::LOWLAND);
        ++sCnt;
      }
      else {
        builder->addAttrib(v, SquareAttrib::LAKE);
        ++wCnt;
      }
    }
    Debug() << "Terrain distribution " << gCnt << " glacier, " << mCnt << " mountain, " << hCnt << " hill, " << lCnt << " lowland, " << wCnt << " water, " << sCnt << " sand";
  }

  private:
  vector<double> ratio;
  vector<int> cornerLevels;
  vector<SquareType> types;
  double varianceMult;
};

class Roads : public LevelMaker {
  public:
  Roads(SquareType roadSquare) : square(roadSquare) {}

  bool makeBridge(LevelBuilder* builder, Vec2 pos) {
    return !builder->canNavigate(pos, {MovementTrait::WALK}) && builder->canNavigate(pos, {MovementTrait::SWIM});
  }

  double getValue(LevelBuilder* builder, Vec2 pos) {
    if ((!builder->getSquare(pos)->canEnterEmpty(MovementType({MovementTrait::WALK, MovementTrait::SWIM})) &&
         !builder->hasAttrib(pos, SquareAttrib::ROAD_CUT_THRU)) ||
        builder->hasAttrib(pos, SquareAttrib::NO_ROAD))
      return ShortestPath::infinity;
    SquareType type = builder->getType(pos);
    if (makeBridge(builder, pos))
      return 10;
    if (builder->getFurnitureType(pos) == FurnitureType::ROAD || builder->getFurnitureType(pos) == FurnitureType::BRIDGE)
      return 1;
    return 1 + pow(1 + builder->getHeightMap(pos), 2);
  }

  FurnitureType getRoadType(LevelBuilder* builder, Vec2 pos) {
    if (makeBridge(builder, pos))
      return FurnitureType::BRIDGE;
    else
      return FurnitureType::ROAD;
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    vector<Vec2> points;
    for (Vec2 v : area)
      if (builder->hasAttrib(v, SquareAttrib::CONNECT_ROAD)) {
        points.push_back(v);
        Debug() << "Connecting point " << v;
      }
    for (int ind : Range(1, points.size())) {
      Vec2 p1 = points[ind];
      Vec2 p2 = points[ind - 1];
      ShortestPath path(area,
          [=](Vec2 pos) { return (pos == p1 || pos == p2) ? 1 : getValue(builder, pos); }, 
          [] (Vec2 v) { return v.length4(); },
          Vec2::directions4(builder->getRandom()), p1, p2);
      Vec2 prev(-1, -1);
      for (Vec2 v = p2; v != p1; v = path.getNextMove(v)) {
        if (!path.isReachable(v))
          failGen();
        auto roadType = getRoadType(builder, v);
        if (v != p2 && v != p1 && builder->getFurnitureType(v) != roadType) {
          builder->removeFurniture(v);
          builder->putFurniture(v, FurnitureParams{roadType, TribeId::getMonster()});
        }
        prev = v;
      }
    }
  }

  private:
  SquareType square;
};

class StartingPos : public LevelMaker {
  public:

  StartingPos(Predicate pred, StairKey key) : predicate(pred), stairKey(key) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (Vec2 pos : area)
      if (predicate.apply(builder, pos))
        builder->modSquare(pos)->setLandingLink(stairKey);
  }

  private:
  Predicate predicate;
  StairKey stairKey;
};

class TransferPos : public LevelMaker {
  public:

  TransferPos(Predicate pred, StairKey key, int w) : predicate(pred), stairKey(key), width(w) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    bool found = false;
    for (Vec2 pos : area)
      if (((pos.x - area.left() < width) || (pos.y - area.top() < width) ||
          (area.right() - pos.x <= width) || (area.bottom() - pos.y <= width)) &&
          predicate.apply(builder, pos)) {
        builder->modSquare(pos)->setLandingLink(stairKey);
        found = true;
      }
    checkGen(found);
  }

  private:
  Predicate predicate;
  StairKey stairKey;
  int width;
};

class Forrest : public LevelMaker {
  public:
  Forrest(double _ratio, double _density, SquareType _onType, FurnitureFactory _factory) 
      : ratio(_ratio), density(_density), factory(_factory), onType(_onType) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    Table<double> wys = genNoiseMap(builder->getRandom(), area, {0, 0, 0, 0, 0}, 0.65);
    vector<double> values = sortedValues(wys);
    double cutoff = values[values.size() * ratio];
    for (Vec2 v : area)
      if (builder->getType(v) == onType && wys[v] < cutoff) {
        if (builder->getRandom().getDouble() <= density)
          builder->putFurniture(v, factory);
        builder->addAttrib(v, SquareAttrib::FORREST);
      }
  }

  private:
  double ratio;
  double density;
  FurnitureFactory factory;
  SquareType onType;
};

class LocationMaker : public LevelMaker {
  public:
  LocationMaker(Location* l) : location(l) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    builder->addLocation(location, area);
    for (Vec2 v : area)
      builder->addAttrib(v, SquareAttrib::LOCATION);
  }
  
  private:
  Location* location;
};

class AddSquaresToCollective : public LevelMaker {
  public:
  AddSquaresToCollective(CollectiveBuilder* c) : collective(c) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    collective->addSquares(builder->toGlobalCoordinates(area).getAllSquares());
  }
  
  private:
  CollectiveBuilder* collective;
};

class ForEachSquare : public LevelMaker {
  public:
  ForEachSquare(function<void(LevelBuilder*, Vec2 pos)> f, Predicate _onPred = Predicate::alwaysTrue())
    : fun(f), onPred(_onPred) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (Vec2 v : area)
      if (onPred.apply(builder, v))
        fun(builder, v);
  }
  
  protected:
  function<void(LevelBuilder*, Vec2 pos)> fun;
  Predicate onPred;
};

class AddAttrib : public ForEachSquare {
  public:
  AddAttrib(SquareAttrib attr, Predicate onPred = Predicate::alwaysTrue()) 
      : ForEachSquare([attr](LevelBuilder* b, Vec2 pos) { b->addAttrib(pos, attr); }, onPred) {}
};

class RemoveAttrib : public ForEachSquare {
  public:
  RemoveAttrib(SquareAttrib attr, Predicate onPred = Predicate::alwaysTrue()) 
    : ForEachSquare([attr](LevelBuilder* b, Vec2 pos) { b->removeAttrib(pos, attr); }, onPred) {}
};

enum class StairDirection {
  UP, DOWN
};

class Stairs : public LevelMaker {
  public:
  Stairs(StairDirection dir, StairKey k, Predicate onPred, optional<SquareAttrib> _setAttr = none)
    : direction(dir), key(k), onPredicate(onPred), setAttr(_setAttr) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    vector<Vec2> allPos;
    for (Vec2 v : area)
      if (onPredicate.apply(builder, v) && builder->canPutFurniture(v))
        allPos.push_back(v);
    checkGen(allPos.size() > 0);
    Vec2 pos = allPos[builder->getRandom().get(allPos.size())];
    auto type = direction == StairDirection::DOWN ? FurnitureType::DOWN_STAIRS : FurnitureType::UP_STAIRS;
    builder->putFurniture(pos, FurnitureParams{type, TribeId::getHostile()});
    builder->setLandingLink(pos, key);
  }

  private:
  StairDirection direction;
  StairKey key;
  Predicate onPredicate;
  optional<SquareAttrib> setAttr;
};

class ShopMaker : public LevelMaker {
  public:
  ShopMaker(ItemFactory _factory, TribeId _tribe, int _numItems, BuildingInfo _building)
      : factory(_factory), tribe(_tribe), numItems(_numItems), building(_building) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    Location *loc = new Location();
    builder->addLocation(loc, area);
    PCreature shopkeeper = CreatureFactory::getShopkeeper(loc, tribe);
    vector<Vec2> pos;
    for (Vec2 v : area)
      if (builder->getSquare(v)->canEnterEmpty({MovementTrait::WALK}) && builder->getType(v) == building.floorInside)
        pos.push_back(v);
    builder->putCreature(pos[builder->getRandom().get(pos.size())], std::move(shopkeeper));
    builder->putFurniture(pos[builder->getRandom().get(pos.size())], FurnitureParams{FurnitureType::TORCH, tribe});
    for (int i : Range(numItems)) {
      Vec2 v = pos[builder->getRandom().get(pos.size())];
      builder->putItems(v, factory.random());
    }
  }

  private:
  ItemFactory factory;
  TribeId tribe;
  int numItems;
  BuildingInfo building;
};

class LevelExit : public LevelMaker {
  public:
  LevelExit(optional<FurnitureFactory> _exit, optional<SquareAttrib> _attrib = none, int _minCornerDist = 1)
      : exit(_exit), attrib(_attrib), minCornerDist(_minCornerDist) {}
  
  virtual void make(LevelBuilder* builder, Rectangle area) override {
    Vec2 pos = getRandomExit(builder->getRandom(), area, minCornerDist);
    builder->putSquare(pos, SquareId::FLOOR, attrib);
    if (exit)
      builder->putFurniture(pos, *exit);
  }

  private:
  optional<FurnitureFactory> exit;
  optional<SquareAttrib> attrib;
  int minCornerDist;
};

class Division : public LevelMaker {
  public:
  Division(double _vRatio, double _hRatio,
      LevelMaker* _upperLeft, LevelMaker* _upperRight, LevelMaker* _lowerLeft, LevelMaker* _lowerRight,
      optional<SquareType> _wall = none) : vRatio(_vRatio), hRatio(_hRatio),
      upperLeft(_upperLeft), upperRight(_upperRight), lowerLeft(_lowerLeft), lowerRight(_lowerRight), wall(_wall) {}

  Division(double _hRatio, LevelMaker* _left, LevelMaker* _right, optional<SquareType> _wall = none) 
      : vRatio(-1), hRatio(_hRatio), upperLeft(_left), upperRight(_right), wall(_wall) {}

  Division(bool, double _vRatio, LevelMaker* _top, LevelMaker* _bottom, optional<SquareType> _wall = none) 
      : vRatio(_vRatio), hRatio(-1), upperLeft(_top), lowerLeft(_bottom), wall(_wall) {}

  void makeHorizDiv(LevelBuilder* builder, Rectangle area) {
    int hDiv = area.left() + min(area.width() - 1, max(1, (int) (hRatio * area.width())));
    if (upperLeft)
      upperLeft->make(builder, Rectangle(area.left(), area.top(), hDiv, area.bottom()));
    if (upperRight)
      upperRight->make(builder, Rectangle(hDiv + (wall ? 1 : 0), area.top(), area.right(), area.bottom()));
    if (wall)
      for (int i : Range(area.top(), area.bottom()))
        builder->putSquare(Vec2(hDiv, i), *wall);
  }

  void makeVertDiv(LevelBuilder* builder, Rectangle area) {
    int vDiv = area.top() + min(area.height() - 1, max(1, (int) (vRatio * area.height())));
    if (upperLeft)
      upperLeft->make(builder, Rectangle(area.left(), area.top(), area.right(), vDiv));
    if (lowerLeft)
      lowerLeft->make(builder, Rectangle(area.left(), vDiv + (wall ? 1 : 0), area.right(), area.bottom()));
    if (wall)
      for (int i : Range(area.left(), area.right()))
        builder->putSquare(Vec2(i, vDiv), *wall);
  }

  void makeDiv(LevelBuilder* builder, Rectangle area) {
    int vDiv = area.top() + min(area.height() - 1, max(1, (int) (vRatio * area.height())));
    int hDiv = area.left() + min(area.width() - 1, max(1, (int) (hRatio * area.width())));
    int wallSpace = wall ? 1 : 0;
    if (upperLeft)
      upperLeft->make(builder, Rectangle(area.left(), area.top(), hDiv, vDiv));
    if (upperRight)
      upperRight->make(builder, Rectangle(hDiv + wallSpace, area.top(), area.right(), vDiv));
    if (lowerLeft)
      lowerLeft->make(builder, Rectangle(area.left(), vDiv + wallSpace, hDiv, area.bottom()));
    if (lowerRight)
      lowerRight->make(builder, Rectangle(hDiv + wallSpace, vDiv + wallSpace, area.right(), area.bottom()));
    if (wall) {
      for (int i : Range(area.top(), area.bottom()))
        builder->putSquare(Vec2(hDiv, i), *wall);
      for (int i : Range(area.left(), area.right()))
        builder->putSquare(Vec2(i, vDiv), *wall);
    }
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    if (vRatio < 0)
      makeHorizDiv(builder, area);
    else if (hRatio < 0)
      makeVertDiv(builder, area);
    else
      makeDiv(builder, area);
  }

  private:
  double vRatio, hRatio;
  LevelMaker *upperLeft = nullptr;
  LevelMaker *upperRight = nullptr;
  LevelMaker *lowerLeft = nullptr;
  LevelMaker *lowerRight = nullptr;
  optional<SquareType> wall;
};

class AreaCorners : public LevelMaker {
  public:
  AreaCorners(LevelMaker* _maker, Vec2 _size, vector<LevelMaker*> _insideMakers)
      : maker(_maker), size(_size), insideMakers(_insideMakers) {}

  vector<Rectangle> getCorners(Rectangle area) {
    return {
      Rectangle(area.topLeft(), area.topLeft() + size),
      Rectangle(area.topRight() - Vec2(size.x, 0), area.topRight() + Vec2(0, size.y)),
      Rectangle(area.bottomLeft() - Vec2(0, size.y), area.bottomLeft() + Vec2(size.x, 0)),
      Rectangle(area.bottomRight() - size, area.bottomRight())};
  }

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    vector<Rectangle> corners = builder->getRandom().permutation(getCorners(area));
    for (int i : All(corners)) {
      maker->make(builder, corners[i]);
      if (i < insideMakers.size())
        insideMakers[i]->make(builder, corners[i]);
    }
  }

  private:
  LevelMaker* maker;
  Vec2 size;
  vector<LevelMaker*> insideMakers;
};

class CastleExit : public LevelMaker {
  public:
  CastleExit(TribeId _guardTribe, BuildingInfo _building, CreatureId _guardId)
    : guardTribe(_guardTribe), building(_building), guardId(_guardId) {}
  
  virtual void make(LevelBuilder* builder, Rectangle area) override {
    Vec2 loc(area.right() - 1, area.middle().y);
    builder->putSquare(loc + Vec2(2, 0), building.floorInside);
    if (building.door)
      builder->putFurniture(loc + Vec2(2, 0), *building.door);
    builder->addAttrib(loc + Vec2(2, 0), SquareAttrib::CONNECT_ROAD);
    vector<Vec2> walls { Vec2(1, -2), Vec2(2, -2), Vec2(2, -1), Vec2(2, 1), Vec2(2, 2), Vec2(1, 2)};
    for (Vec2 v : walls)
      builder->putSquare(loc + v, building.wall);
    vector<Vec2> floor { Vec2(1, -1), Vec2(1, 0), Vec2(1, 1), Vec2(0, -1), Vec2(0, 0), Vec2(0, 1) };
    for (Vec2 v : floor)
      builder->putSquare(loc + v, building.floorInside);
    vector<Vec2> guardPos { Vec2(1, 1), Vec2(1, -1) };
    for (Vec2 pos : guardPos) {
      Location* guard = new Location();
      builder->addLocation(guard, Rectangle(loc + pos, loc + pos + Vec2(1, 1)));
      builder->putCreature(loc + pos, CreatureFactory::fromId(guardId, guardTribe,
            MonsterAIFactory::stayInLocation(guard, false)));
    }
  }

  private:
  TribeId guardTribe;
  BuildingInfo building;
  CreatureId guardId;
};

class AddMapBorder : public LevelMaker {
  public:
  AddMapBorder(int w) : width(w) {}

  virtual void make(LevelBuilder* builder, Rectangle area) override {
    for (Vec2 v : area)
      if (!v.inRectangle(area.minusMargin(width)))
        builder->setUnavailable(v);
  }

  private:
  int width;
};

}

static MakerQueue* stockpileMaker(StockpileInfo info) {
  SquareType square {SquareId::CUSTOM_FLOOR, CustomFloorInfo{ViewId::STOCKPILE1, "stockpile"}};
  ItemFactory items;
  switch (info.type) {
    case StockpileInfo::GOLD: items = ItemFactory::singleType(ItemId::GOLD_PIECE); break;
    case StockpileInfo::MINERALS: items = ItemFactory::minerals(); break;
  }
  MakerQueue* queue = new MakerQueue();
  queue->addMaker(new Empty(square));
  queue->addMaker(new Items(items, square, info.number, info.number + 1));
  return queue;
}

PLevelMaker LevelMaker::cryptLevel(RandomGen& random, SettlementInfo info) {
  MakerQueue* queue = new MakerQueue();
  BuildingInfo building = getBuildingInfo(info);
  queue->addMaker(new Empty(SquareId::MOUNTAIN));
  queue->addMaker(new LocationMaker(info.location));
  queue->addMaker(new RoomMaker(random.get(8, 15), 3, 5, SquareId::MOUNTAIN,
        SquareType(SquareId::MOUNTAIN)));
  queue->addMaker(new Connector(building.door, 0.75));
  if (info.furniture)
    queue->addMaker(new Furnitures(Predicate::attrib(SquareAttrib::EMPTY_ROOM), 0.3, *info.furniture));
  for (StairKey key : info.downStairs)
    queue->addMaker(new Stairs(StairDirection::DOWN, key, Predicate::type(SquareId::FLOOR)));
  for (StairKey key : info.upStairs)
    queue->addMaker(new Stairs(StairDirection::UP, key, Predicate::type(SquareId::FLOOR)));
  if (info.creatures)
    queue->addMaker(new Creatures(*info.creatures, info.numCreatures, info.collective));
  queue->addMaker(new Items(ItemFactory::dungeon(), SquareId::FLOOR, 5, 10));
  return PLevelMaker(new BorderGuard(queue, SquareId::MOUNTAIN));
}

PLevelMaker LevelMaker::mazeLevel(RandomGen& random, SettlementInfo info) {
  MakerQueue* queue = new MakerQueue();
  BuildingInfo building = getBuildingInfo(info);
  queue->addMaker(new Empty(SquareId::MOUNTAIN));
  queue->addMaker(new RoomMaker(random.get(8, 15), 3, 5, SquareId::MOUNTAIN));
  queue->addMaker(new Connector(building.door, 0.75));
  if (info.furniture)
    queue->addMaker(new Furnitures(Predicate::attrib(SquareAttrib::EMPTY_ROOM), 0.3, *info.furniture));
  for (StairKey key : info.downStairs)
    queue->addMaker(new Stairs(StairDirection::DOWN, key, Predicate::type(SquareId::FLOOR)));
  for (StairKey key : info.upStairs)
    queue->addMaker(new Stairs(StairDirection::UP, key, Predicate::type(SquareId::FLOOR)));
  if (info.creatures)
    queue->addMaker(new Creatures(*info.creatures, info.numCreatures));
  queue->addMaker(new Items(ItemFactory::dungeon(), SquareId::FLOOR, 5, 10));
  return PLevelMaker(new BorderGuard(queue, SquareId::MOUNTAIN));
}

LevelMaker* hatchery(CreatureFactory factory, int numCreatures) {
  MakerQueue* queue = new MakerQueue();
  queue->addMaker(new Empty(SquareId::MUD));
  queue->addMaker(new Creatures(factory, numCreatures));
  return queue;
}

MakerQueue* getElderRoom(SettlementInfo info) {
  BuildingInfo building = getBuildingInfo(info);
  MakerQueue* elderRoom = new MakerQueue();
  if (info.elderLoot)
    elderRoom->addMaker(new Items(ItemFactory::singleType(*info.elderLoot), building.floorInside, 1, 2));
  return elderRoom;
}

MakerQueue* village2(RandomGen& random, SettlementInfo info) {
  BuildingInfo building = getBuildingInfo(info);
  MakerQueue* queue = new MakerQueue();
  queue->addMaker(new LocationMaker(info.location));
  vector<LevelMaker*> insideMakers {getElderRoom(info)};
  for (auto& elem : info.stockpiles)
    insideMakers.push_back(stockpileMaker(elem));
  if (info.shopFactory)
    insideMakers.push_back(new ShopMaker(*info.shopFactory, info.tribe, random.get(8, 16), building));
  queue->addMaker(new Buildings(6, 10, 3, 4, building, false, insideMakers));
  if (info.furniture)
    queue->addMaker(new Furnitures(Predicate::attrib(SquareAttrib::EMPTY_ROOM), 0.3, *info.furniture));
  if (info.outsideFeatures)
    queue->addMaker(new Furnitures(Predicate::type(building.floorOutside), 0.01, *info.outsideFeatures));
  if (info.creatures)
    queue->addMaker(new Creatures(*info.creatures, info.numCreatures, info.collective,
          Predicate::type(building.floorOutside)));
  if (info.neutralCreatures)
    queue->addMaker(
        new Creatures(info.neutralCreatures->first, info.neutralCreatures->second, 
          Predicate::type(building.floorOutside)));
  return queue;
}

MakerQueue* village(RandomGen& random, SettlementInfo info) {
  BuildingInfo building = getBuildingInfo(info);
  MakerQueue* queue = new MakerQueue();
  queue->addMaker(new LocationMaker(info.location));
  queue->addMaker(new UniformBlob(building.floorOutside, none, none, 0.6));
  vector<LevelMaker*> insideMakers {
 //     hatchery(CreatureFactory::singleType(info.tribe, CreatureId::PIG), random.get(2, 5)),
      getElderRoom(info)};
  if (info.shopFactory)
    insideMakers.push_back(new ShopMaker(*info.shopFactory, info.tribe, random.get(8, 16), building));
  for (auto& elem : info.stockpiles)
    insideMakers.push_back(stockpileMaker(elem));
  queue->addMaker(new Buildings(4, 8, 3, 7, building, true, insideMakers));
  if (info.furniture)
    queue->addMaker(new Furnitures(Predicate::attrib(SquareAttrib::EMPTY_ROOM), 0.3, *info.furniture));
  if (info.outsideFeatures)
    queue->addMaker(new Furnitures(Predicate::andPred(
        Predicate::type(building.floorOutside),
        Predicate::attrib(SquareAttrib::BUILDINGS_CENTER)), 0.2, *info.outsideFeatures, SquareAttrib::NO_ROAD));
  if (info.creatures)
    queue->addMaker(new Creatures(*info.creatures, info.numCreatures, info.collective, 
          Predicate::type(building.floorOutside)));
  if (info.neutralCreatures)
    queue->addMaker(
        new Creatures(info.neutralCreatures->first, info.neutralCreatures->second, 
          Predicate::type(building.floorOutside)));
  return queue;
}

MakerQueue* cottage(SettlementInfo info) {
  BuildingInfo building = getBuildingInfo(info);
  MakerQueue* queue = new MakerQueue();
  queue->addMaker(new Empty(building.floorOutside));
  MakerQueue* room = getElderRoom(info);
  for (StairKey key : info.upStairs)
    room->addMaker(new Stairs(StairDirection::UP, key, Predicate::type(building.floorInside), none));
  for (StairKey key : info.downStairs)
    room->addMaker(new Stairs(StairDirection::DOWN, key, Predicate::type(building.floorInside), none));
  if (info.furniture)
    room->addMaker(new Furnitures(Predicate::type(building.floorInside), 0.3, *info.furniture));
  if (info.outsideFeatures)
    room->addMaker(new Furnitures(Predicate::type(building.floorOutside), 0.1, *info.outsideFeatures));
  queue->addMaker(new Buildings(1, 2, 5, 7, building, false, {room}, false));
  queue->addMaker(new LocationMaker(info.location));
  if (info.creatures)
    queue->addMaker(new Creatures(*info.creatures, info.numCreatures, info.collective, 
          Predicate::type(building.floorOutside)));
  if (info.neutralCreatures)
    queue->addMaker(
        new Creatures(info.neutralCreatures->first, info.neutralCreatures->second, 
          Predicate::type(building.floorOutside)));
   return queue;
}

MakerQueue* forrestCottage(SettlementInfo info) {
  BuildingInfo building = getBuildingInfo(info);
  MakerQueue* queue = new MakerQueue();
  MakerQueue* room = getElderRoom(info);
  for (StairKey key : info.upStairs)
    room->addMaker(new Stairs(StairDirection::UP, key, Predicate::type(building.floorInside), none));
  for (StairKey key : info.downStairs)
    room->addMaker(new Stairs(StairDirection::DOWN, key, Predicate::type(building.floorInside), none));
  if (info.furniture)
    room->addMaker(new Furnitures(Predicate::type(building.floorInside), 0.3, *info.furniture));
  if (info.outsideFeatures)
    room->addMaker(new Furnitures(Predicate::type(building.floorOutside), 0.1, *info.outsideFeatures));
  queue->addMaker(new Buildings(1, 3, 3, 4, building, false, {room}, false));
  queue->addMaker(new LocationMaker(info.location));
  if (info.creatures)
    queue->addMaker(new Creatures(*info.creatures, info.numCreatures, info.collective, 
          Predicate::type(building.floorOutside)));
  if (info.neutralCreatures)
    queue->addMaker(
        new Creatures(info.neutralCreatures->first, info.neutralCreatures->second, 
          Predicate::type(building.floorOutside)));
   return queue;
}

MakerQueue* castle(RandomGen& random, SettlementInfo info) {
  BuildingInfo building = getBuildingInfo(info);
  LevelMaker* castleRoom = new BorderGuard(new Empty(building.floorInside, SquareAttrib::EMPTY_ROOM),
      building.wall);
  MakerQueue* leftSide = new MakerQueue();
  leftSide->addMaker(new Division(true, random.getDouble(0.5, 0.5),
      new Margin(1, -1, -1, 1, castleRoom), new Margin(1, 1, -1, -1, castleRoom)));
  leftSide->addMaker(getElderRoom(info));
  MakerQueue* inside = new MakerQueue();
  inside->addMaker(new Empty(building.floorOutside));
  vector<LevelMaker*> insideMakers;
  if (info.shopFactory)
    insideMakers.push_back(new ShopMaker(*info.shopFactory, info.tribe, random.get(8, 16), building));
  inside->addMaker(new Division(random.getDouble(0.25, 0.4), leftSide,
        new Buildings(1, 3, 3, 6, building, false, insideMakers, false), building.wall));
  MakerQueue* insidePlusWall = new MakerQueue();
  insidePlusWall->addMaker(new BorderGuard(inside, building.wall));
  MakerQueue* queue = new MakerQueue();
  int insideMargin = 2;
  queue->addMaker(new Margin(insideMargin, new LocationMaker(info.location)));
  queue->addMaker(new Margin(insideMargin, insidePlusWall));
  vector<LevelMaker*> cornerMakers;
  for (auto& elem : info.stockpiles)
    cornerMakers.push_back(new Margin(1, stockpileMaker(elem)));
  queue->addMaker(new AreaCorners(new BorderGuard(new Empty(building.floorInside, SquareAttrib::CASTLE_CORNER),
          building.wall), Vec2(5, 5), cornerMakers));
  queue->addMaker(new Margin(insideMargin, new Connector(building.door, 1, 18)));
  queue->addMaker(new Margin(insideMargin, new CastleExit(info.tribe, building, *info.guardId)));
  if (info.creatures)
    queue->addMaker(new Creatures(*info.creatures, info.numCreatures, info.collective, 
          Predicate::type(building.floorOutside)));
  if (info.neutralCreatures)
    queue->addMaker(
        new Creatures(info.neutralCreatures->first, info.neutralCreatures->second, 
          Predicate::type(building.floorOutside)));
  if (info.outsideFeatures)
    inside->addMaker(new Furnitures(Predicate::type(building.floorOutside), 0.03, *info.outsideFeatures));
  if (info.furniture)
    inside->addMaker(new Furnitures(Predicate::attrib(SquareAttrib::EMPTY_ROOM), 0.35, *info.furniture));
  for (StairKey key : info.downStairs)
    queue->addMaker(new Stairs(StairDirection::DOWN, key, Predicate::andPred(
          Predicate::attrib(SquareAttrib::CASTLE_CORNER),
          Predicate::type(building.floorInside)), none));
  queue->addMaker(new StartingPos(Predicate::type(SquareId::MUD), StairKey::heroSpawn()));
  queue->addMaker(new AddAttrib(SquareAttrib::NO_DIG, Predicate::type(building.wall)));
  return queue;
}

MakerQueue* castle2(RandomGen& random, SettlementInfo info) {
  BuildingInfo building = getBuildingInfo(info);
  MakerQueue* inside = new MakerQueue();
  inside->addMaker(new Empty(building.floorOutside));
  vector<LevelMaker*> insideMakers {new MakerQueue({
      getElderRoom(info),
      stockpileMaker(getOnlyElement(info.stockpiles))})};
  inside->addMaker(new Buildings(1, 2, 3, 4, building, false, insideMakers, false));
  MakerQueue* insidePlusWall = new MakerQueue();
  insidePlusWall->addMaker(new BorderGuard(inside, building.wall));
  MakerQueue* queue = new MakerQueue();
  int insideMargin = 2;
  queue->addMaker(new Margin(insideMargin, new LocationMaker(info.location)));
  queue->addMaker(new Margin(insideMargin, insidePlusWall));
  queue->addMaker(new Margin(insideMargin, new Connector(building.door, 1, 18)));
  queue->addMaker(new Margin(insideMargin, new CastleExit(info.tribe, building, *info.guardId)));
  if (info.outsideFeatures)
    queue->addMaker(new Furnitures(Predicate::type(building.floorOutside), 0.05, *info.outsideFeatures));
  if (info.creatures)
    queue->addMaker(new Creatures(*info.creatures, info.numCreatures, info.collective, 
          Predicate::type(building.floorOutside)));
  if (info.neutralCreatures)
    queue->addMaker(
        new Creatures(info.neutralCreatures->first, info.neutralCreatures->second, 
          Predicate::type(building.floorOutside)));
   return queue;
}

LevelMaker* makeLake() {
  MakerQueue* queue = new MakerQueue();
  Location* loc = new Location();
  queue->addMaker(new LocationMaker(loc));
  queue->addMaker(new Lake());
  queue->addMaker(new Margin(10, new RandomLocations(
          {new UniformBlob(SquareId::GRASS, SquareType(SquareId::SAND))}, {{15, 15}},
          Predicate::type(SquareId::WATER))));
  return queue;
}

static LevelMaker* tower(RandomGen& random, SettlementInfo info, bool withExit) {
  BuildingInfo building = getBuildingInfo(info);
  MakerQueue* queue = new MakerQueue();
  queue->addMaker(new Empty(building.wall));
  if (withExit)
    queue->addMaker(new LevelExit(*building.door, none, 2));
  queue->addMaker(new Margin(1, new Empty(building.floorInside)));
  queue->addMaker(new Margin(1, new AddAttrib(SquareAttrib::ROOM)));
  queue->addMaker(new RemoveAttrib(SquareAttrib::ROAD_CUT_THRU));
  queue->addMaker(new LocationMaker(info.location));
  LevelMaker* downStairs = nullptr;
  for (StairKey key : info.downStairs)
    downStairs = new Stairs(StairDirection::DOWN, key, Predicate::type(building.floorInside));
  LevelMaker* upStairs = nullptr;
  for (StairKey key : info.upStairs)
    upStairs = new Stairs(StairDirection::UP, key, Predicate::type(building.floorInside));
  if (info.creatures)
    queue->addMaker(new Creatures(*info.creatures, info.numCreatures, info.collective, 
          Predicate::type(building.floorInside)));
  queue->addMaker(new Division(0.5, 0.5, upStairs, nullptr, nullptr, downStairs));
  if (info.furniture)
    queue->addMaker(new Furnitures(Predicate::type(building.floorInside), 0.5, *info.furniture));
  return queue;
}

PLevelMaker LevelMaker::towerLevel(RandomGen& random, SettlementInfo info) {
  return PLevelMaker(tower(random, info, false));
}

Vec2 getSize(RandomGen& random, SettlementType type) {
  switch (type) {
    case SettlementType::WITCH_HOUSE:
    case SettlementType::CEMETERY:
    case SettlementType::SWAMP: return {random.get(12, 16), random.get(12, 16)};
    case SettlementType::COTTAGE: return {random.get(8, 10), random.get(8, 10)};
    case SettlementType::FORREST_COTTAGE: return {15, 15};
    case SettlementType::FOREST: return {18, 13};
    case SettlementType::VILLAGE2: return {20, 20};
    case SettlementType::VILLAGE:
    case SettlementType::ANT_NEST:
    case SettlementType::CASTLE: return {30, 20};
    case SettlementType::CASTLE2: return {15, 15};
    case SettlementType::MINETOWN: return {30, 20};
    case SettlementType::SMALL_MINETOWN: return {15, 15};
    case SettlementType::CAVE: return {12, 12};
    case SettlementType::SPIDER_CAVE: return {12, 12};
    case SettlementType::VAULT: return {10, 10};
    case SettlementType::TOWER: return {5, 5};
    case SettlementType::ISLAND_VAULT_DOOR:
    case SettlementType::ISLAND_VAULT: return {6, 6};
  }
}

RandomLocations::LocationPredicate getSettlementPredicate(SettlementType type) {
  switch (type) {
    case SettlementType::FOREST:
    case SettlementType::FORREST_COTTAGE:
    case SettlementType::VILLAGE2:
      return Predicate::andPred(
          Predicate::negate(Predicate::attrib(SquareAttrib::RIVER)),
          Predicate::attrib(SquareAttrib::FORREST));
    case SettlementType::CAVE:
      return RandomLocations::LocationPredicate(
          Predicate::type(SquareId::MOUNTAIN), Predicate::attrib(SquareAttrib::HILL), 5, 15);
    case SettlementType::VAULT:
    case SettlementType::ANT_NEST:
    case SettlementType::SMALL_MINETOWN:
    case SettlementType::MINETOWN:
      return Predicate::type(SquareId::MOUNTAIN);
    case SettlementType::SPIDER_CAVE:
      return RandomLocations::LocationPredicate(
          Predicate::type(SquareId::MOUNTAIN), Predicate::andPred(
            Predicate::negate(Predicate::attrib(SquareAttrib::LOCATION)),
            Predicate::attrib(SquareAttrib::CONNECTOR)), 1, 2);
    case SettlementType::ISLAND_VAULT:
      return Predicate::attrib(SquareAttrib::MOUNTAIN);
    case SettlementType::ISLAND_VAULT_DOOR:
      return RandomLocations::LocationPredicate(
          Predicate::attrib(SquareAttrib::MOUNTAIN), Predicate::attrib(SquareAttrib::RIVER), 10, 30);
    default:
      return Predicate::andPred(Predicate::attrib(SquareAttrib::LOWLAND),
          Predicate::negate(Predicate::attrib(SquareAttrib::RIVER)));
  }
}

static MakerQueue* genericMineTownMaker(RandomGen& random, SettlementInfo info, int numCavern, int maxCavernSize,
    int numRooms, int minRoomSize, int maxRoomSize, bool connect) {
  BuildingInfo building = getBuildingInfo(info);
  MakerQueue* queue = new MakerQueue();
  LevelMaker* cavern = new UniformBlob(building.floorInside);
  vector<LevelMaker*> vCavern;
  vector<pair<int, int>> sizes;
  for (int i : Range(numCavern)) {
    sizes.push_back(make_pair(random.get(5, maxCavernSize), random.get(5, maxCavernSize)));
    vCavern.push_back(cavern);
  }
  queue->addMaker(new RandomLocations(vCavern, sizes, Predicate::alwaysTrue(), false));
  vector<LevelMaker*> roomInsides;
  if (info.shopFactory)
    roomInsides.push_back(new ShopMaker(*info.shopFactory, info.tribe, random.get(8, 16), building));
  for (auto& elem : info.stockpiles)
    roomInsides.push_back(stockpileMaker(elem));
  queue->addMaker(new RoomMaker(numRooms, minRoomSize, maxRoomSize, building.wall, none,
      new Empty(building.floorInside, ifTrue(connect, SquareAttrib::CONNECT_CORRIDOR)), roomInsides));
  queue->addMaker(new Connector(building.door, 0));
  Predicate featurePred = Predicate::andPred(
      Predicate::attrib(SquareAttrib::EMPTY_ROOM),
      Predicate::type(building.floorInside));
  for (StairKey key : info.downStairs)
    queue->addMaker(new Stairs(StairDirection::DOWN, key, featurePred));
  for (StairKey key : info.upStairs)
    queue->addMaker(new Stairs(StairDirection::UP, key, featurePred));
  if (info.furniture)
    queue->addMaker(new Furnitures(featurePred, 0.3, *info.furniture));
  if (info.outsideFeatures)
    queue->addMaker(new Furnitures(Predicate::type(building.floorInside), 0.09, *info.outsideFeatures));
  if (info.creatures)
    queue->addMaker(new Creatures(*info.creatures, info.numCreatures, info.collective));
  if (info.neutralCreatures)
    queue->addMaker(
        new Creatures(info.neutralCreatures->first, info.neutralCreatures->second, 
          Predicate::type(building.floorOutside)));
  queue->addMaker(new LocationMaker(info.location));
  return queue;
}

static MakerQueue* mineTownMaker(RandomGen& random, SettlementInfo info) {
  return genericMineTownMaker(random, info, 10, 12, random.get(5, 7), 6, 8, true);
}

static MakerQueue* antNestMaker(RandomGen& random, SettlementInfo info) {
  MakerQueue* ret = genericMineTownMaker(random, info, 4, 6, random.get(5, 7), 3, 4, false);
  ret->addMaker(new AddAttrib(SquareAttrib::NO_DIG));
  return ret;
}

static MakerQueue* smallMineTownMaker(RandomGen& random, SettlementInfo info) {
  return genericMineTownMaker(random, info, 2, 7, random.get(3, 5), 5, 7, true);
}

static MakerQueue* vaultMaker(SettlementInfo info, bool connection) {
  MakerQueue* queue = new MakerQueue();
  BuildingInfo building = getBuildingInfo(info);
  if (connection)
    queue->addMaker(new UniformBlob(building.floorOutside, none, SquareAttrib::CONNECT_CORRIDOR));
  else
    queue->addMaker(new UniformBlob(building.floorOutside));
  if (info.creatures)
    queue->addMaker(new Creatures(*info.creatures, info.numCreatures, info.collective));
  if (info.shopFactory)
    queue->addMaker(new Items(*info.shopFactory, building.floorOutside, 16, 20));
  if (info.neutralCreatures)
    queue->addMaker(
        new Creatures(info.neutralCreatures->first, info.neutralCreatures->second, 
          Predicate::type(building.floorOutside)));
  queue->addMaker(new Margin(1, new LocationMaker(info.location)));
  return queue;
}

static MakerQueue* spiderCaveMaker(SettlementInfo info) {
  MakerQueue* queue = new MakerQueue();
  BuildingInfo building = getBuildingInfo(info);
  MakerQueue* inside = new MakerQueue();
  inside->addMaker(new UniformBlob(building.floorOutside, none, SquareAttrib::CONNECT_CORRIDOR));
  if (info.creatures)
    inside->addMaker(new Creatures(*info.creatures, info.numCreatures, info.collective));
  if (info.shopFactory)
    inside->addMaker(new Items(*info.shopFactory, building.floorOutside, 5, 10));
  queue->addMaker(new Margin(3, inside));
  queue->addMaker(new LocationMaker(info.location));
  queue->addMaker(new Connector(none, 0));
  return queue;
}

static LevelMaker* islandVaultMaker(RandomGen& random, SettlementInfo info, bool door) {
  BuildingInfo building = getBuildingInfo(info);
  MakerQueue* inside = new MakerQueue();
  inside->addMaker(new LocationMaker(info.location));
  Predicate featurePred = Predicate::type(building.floorInside);
  if (!info.stockpiles.empty())
    inside->addMaker(stockpileMaker(getOnlyElement(info.stockpiles)));
  else
    inside->addMaker(new Empty(building.floorInside));
  for (StairKey key : info.downStairs)
    inside->addMaker(new Stairs(StairDirection::DOWN, key, featurePred));
  for (StairKey key : info.upStairs)
    inside->addMaker(new Stairs(StairDirection::UP, key, featurePred));
  MakerQueue* buildingMaker = new MakerQueue({
      new Empty(building.wall),
      new AddAttrib(SquareAttrib::NO_DIG),
      new RemoveAttrib(SquareAttrib::CONNECT_CORRIDOR),
      new Margin(1, inside),
      });
  if (door)
    buildingMaker->addMaker(new LevelExit(FurnitureFactory(TribeId::getMonster(), FurnitureType::DOOR)));
  return new MakerQueue({
        new Empty(SquareId::WATER),
        new Margin(1, buildingMaker)});
}

static MakerQueue* dragonCaveMaker(SettlementInfo info) {
  MakerQueue* queue = vaultMaker(info, true);
/*  queue->addMaker(new RandomLocations({new CreatureAltarMaker(info.collective)}, {{1, 1}},
      {Predicate::type(SquareId::HILL)}));*/
  return queue;
}

PLevelMaker LevelMaker::mineTownLevel(RandomGen& random, SettlementInfo info) {
  MakerQueue* queue = new MakerQueue();
  queue->addMaker(new Empty(SquareId::MOUNTAIN));
  queue->addMaker(mineTownMaker(random, info));
  return PLevelMaker(new BorderGuard(queue, SquareId::MOUNTAIN));
}

static void addResources(RandomGen& random, RandomLocations* locations, int count, int countHere,
    int minSize, int maxSize, int maxDist, int maxDist2, SquareType type, LevelMaker* hereMaker) {
  for (int i : Range(count)) {
    locations->add(new UniformBlob(type, none, SquareAttrib::NO_DIG),
        {random.get(minSize, maxSize), random.get(minSize, maxSize)}, 
        Predicate::type(SquareId::MOUNTAIN));
    if (i < countHere)
      locations->setMaxDistanceLast(hereMaker, maxDist);
    else {
      locations->setMaxDistanceLast(hereMaker, maxDist2);
    }
  }
}

MakerQueue* cemetery(SettlementInfo info) {
  BuildingInfo building = getBuildingInfo(info);
  MakerQueue* queue = new MakerQueue({
          new LocationMaker(info.location),
          new Margin(1, new Buildings(1, 2, 2, 3, building, false, {}, false)),
          new Furnitures(Predicate::type(SquareId::GRASS), 0.15,
              FurnitureFactory(info.tribe, FurnitureType::GRAVE))});
  for (StairKey key : info.downStairs)
    queue->addMaker(new Stairs(StairDirection::DOWN, key, Predicate::type(building.floorInside)));
  if (info.creatures)
    queue->addMaker(new Creatures(*info.creatures, info.numCreatures, info.collective));
  return queue;
}

static LevelMaker* emptyCollective(SettlementInfo info) {
  return new MakerQueue({
      new LocationMaker(info.location),
      new Creatures(*info.creatures, info.numCreatures, info.collective)});
}

LevelMaker* swamp(SettlementInfo info) {
  MakerQueue* queue = new MakerQueue({
      new Lake(false),
      new LocationMaker(info.location)});
  if (info.creatures)
    queue->addMaker(new Creatures(*info.creatures, info.numCreatures, info.collective));
  return queue;
}

static LevelMaker* getMountains(BiomeId id) {
  switch (id) {
    case BiomeId::GRASSLAND:
    case BiomeId::FORREST:
      return new Mountains({0.0, 0.0, 0.78, 0.83, 0.95}, 0.45, {0, 1, 0, 0, 0},
            {SquareId::MOUNTAIN, SquareId::MOUNTAIN, SquareId::HILL, SquareId::GRASS, SquareId::SAND});
    case BiomeId::MOUNTAIN:
      return new Mountains({0.0, 0.0, 0.15, 0.25, 0.75}, 0.45, {0, 1, 0, 0, 0},
            {SquareId::MOUNTAIN, SquareId::MOUNTAIN, SquareId::HILL, SquareId::GRASS, SquareId::SAND});
  }
}

static LevelMaker* getForrest(BiomeId id) {
  FurnitureFactory vegetationLow(TribeId::getHostile(),
      {{FurnitureType::CANIF_TREE, 2}, {FurnitureType::BUSH, 1 }});
  FurnitureFactory vegetationHigh(TribeId::getHostile(),
      {{FurnitureType::DECID_TREE, 2}, {FurnitureType::BUSH, 1 }});
  switch (id) {
    case BiomeId::MOUNTAIN:
      return new MakerQueue({
          new Forrest(0.7, 0.5, SquareId::GRASS, vegetationLow),
          new Forrest(0.8, 0.5, SquareId::HILL, vegetationHigh)});
    case BiomeId::GRASSLAND:
      return new MakerQueue({
          new Forrest(0.3, 0.25, SquareId::GRASS, vegetationLow),
          new Forrest(0.8, 0.25, SquareId::HILL, vegetationHigh)});
    case BiomeId::FORREST:
      return new MakerQueue({
          new Forrest(0.8, 0.5, SquareId::GRASS, vegetationLow),
          new Forrest(0.8, 0.5, SquareId::HILL, vegetationHigh)});
  }
}

static LevelMaker* getForrestCreatures(CreatureFactory factory, int levelWidth, BiomeId biome) {
  int div;
  switch (biome) {
    case BiomeId::FORREST: div = 2000; break;
    case BiomeId::GRASSLAND:
    case BiomeId::MOUNTAIN: div = 7000; break;
  }
  return new Creatures(factory, levelWidth * levelWidth / div, MonsterAIFactory::wildlifeNonPredator());
}

PLevelMaker LevelMaker::topLevel(RandomGen& random, CreatureFactory forrestCreatures,
    vector<SettlementInfo> settlements, int width, bool keeperSpawn, BiomeId biomeId) {
  MakerQueue* queue = new MakerQueue();
  RandomLocations* locations = new RandomLocations();
  RandomLocations* locations2 = new RandomLocations();
  LevelMaker* startingPos = nullptr;
  if (keeperSpawn) {
    startingPos = new StartingPos(Predicate::alwaysTrue(), StairKey::keeperSpawn());
    locations->add(startingPos, Vec2(4, 4), RandomLocations::LocationPredicate(
          Predicate::attrib(SquareAttrib::HILL), Predicate::attrib(SquareAttrib::MOUNTAIN), 1, 8));
  }
  struct CottageInfo {
    LevelMaker* maker;
    CollectiveBuilder* collective;
    TribeId tribe;
  };
  vector<CottageInfo> cottages;
  for (SettlementInfo settlement : settlements) {
    LevelMaker* queue = nullptr;
    switch (settlement.type) {
      case SettlementType::VILLAGE: queue = village(random, settlement); break;
      case SettlementType::VILLAGE2: queue = village2(random, settlement); break;
      case SettlementType::CASTLE:
          queue = castle(random, settlement);
          break;
      case SettlementType::CASTLE2: queue = castle2(random, settlement); break;
      case SettlementType::COTTAGE:
          queue = cottage(settlement);
          cottages.push_back({queue, settlement.collective, settlement.tribe});
          break;
      case SettlementType::FORREST_COTTAGE:
          queue = forrestCottage(settlement);
          break;
      case SettlementType::TOWER:
          queue = tower(random, settlement, true);
          break;
      case SettlementType::WITCH_HOUSE:
          queue = cottage(settlement);
          break;
      case SettlementType::FOREST:
          queue = emptyCollective(settlement);
          break;
      case SettlementType::MINETOWN:
          queue = mineTownMaker(random, settlement);
          break;
      case SettlementType::ANT_NEST:
          queue = antNestMaker(random, settlement);
          break;
      case SettlementType::SMALL_MINETOWN:
          queue = smallMineTownMaker(random, settlement); break;
          break;
      case SettlementType::VAULT:
          queue = vaultMaker(settlement, false);
          if (keeperSpawn)
            locations->setMaxDistance(startingPos, queue, width / 3);
          break;
      case SettlementType::ISLAND_VAULT:
          queue = islandVaultMaker(random, settlement, false);
          break;
      case SettlementType::ISLAND_VAULT_DOOR:
          queue = islandVaultMaker(random, settlement, true);
          break;
      case SettlementType::CAVE:
          queue = dragonCaveMaker(settlement); break;
          break;
      case SettlementType::SPIDER_CAVE:
          queue = spiderCaveMaker(settlement); break;
          break;
      case SettlementType::CEMETERY:
          queue = cemetery(settlement); break;
          break;
      case SettlementType::SWAMP:
          queue = swamp(settlement); break;
          break;
    }
    if (settlement.type == SettlementType::SPIDER_CAVE)
      locations2->add(queue, getSize(random, settlement.type), getSettlementPredicate(settlement.type));
    else {
      if (keeperSpawn) {
        if (settlement.closeToPlayer) {
          locations->setMinDistance(startingPos, queue, 25);
          locations->setMaxDistance(startingPos, queue, 60);
        } else
          locations->setMinDistance(startingPos, queue, 70);
      }
      locations->add(queue, getSize(random, settlement.type), getSettlementPredicate(settlement.type));
    }
  }
  Predicate lowlandPred = Predicate::andPred(
      Predicate::attrib(SquareAttrib::LOWLAND),
      Predicate::negate(Predicate::attrib(SquareAttrib::RIVER)));
  for (auto& cottage : cottages)
    for (int i : Range(random.get(1, 3))) {
      locations->add(new MakerQueue({
            new RemoveFurniture(),
            new FurnitureBlob(FurnitureFactory(cottage.tribe, FurnitureType::CROPS)),
            new AddSquaresToCollective(cottage.collective)}),
          {random.get(7, 12), random.get(7, 12)},
          lowlandPred);
      locations->setMaxDistanceLast(cottage.maker, 13);
    }
  if (biomeId == BiomeId::GRASSLAND || biomeId == BiomeId::FORREST)
    for (int i : Range(random.get(0, 3)))
      locations->add(new Lake(), {random.get(20, 30), random.get(20, 30)}, Predicate::attrib(SquareAttrib::LOWLAND));
  if (biomeId == BiomeId::MOUNTAIN)
    for (int i : Range(random.get(3, 6))) {
      locations->add(new UniformBlob(SquareId::WATER, none, SquareAttrib::LAKE), 
          {random.get(10, 30), random.get(10, 30)}, Predicate::type(SquareId::MOUNTAIN));
  //  locations->setMaxDistanceLast(startingPos, i == 0 ? 25 : 60);
  }
/*  for (int i : Range(random.get(3, 5))) {
    locations->add(new UniformBlob(SquareId::FLOOR, none), 
        {random.get(5, 12), random.get(5, 12)}, Predicate::type(SquareId::MOUNTAIN));
 //   locations->setMaxDistanceLast(startingPos, i == 0 ? 25 : 40);
  }*/
  int maxDist = 1000;
  int maxDist2 = 1000;
  int mapBorder = 30;
  int locationMargin = 10;
  queue->addMaker(new Empty(SquareId::WATER));
  queue->addMaker(getMountains(biomeId));
  queue->addMaker(new MountainRiver(1, SquareId::WATER, SquareId::SAND,
          Predicate::type(SquareId::MOUNTAIN)));
  queue->addMaker(new AddAttrib(SquareAttrib::CONNECT_CORRIDOR, Predicate::attrib(SquareAttrib::LOWLAND)));
  queue->addMaker(new AddAttrib(SquareAttrib::CONNECT_CORRIDOR, Predicate::attrib(SquareAttrib::HILL)));
  queue->addMaker(getForrest(biomeId));
  queue->addMaker(new Margin(mapBorder + locationMargin, locations));
  queue->addMaker(new Margin(mapBorder, new Roads(SquareId::FLOOR)));
  queue->addMaker(new Margin(mapBorder,
        new TransferPos(Predicate::canEnter(MovementTrait::WALK), StairKey::transferLanding(), 2)));
  queue->addMaker(new Margin(mapBorder, new Connector(none, 0, 5,
          Predicate::andPred(Predicate::canEnter({MovementTrait::WALK}),
          Predicate::attrib(SquareAttrib::CONNECT_CORRIDOR)), SquareAttrib::CONNECTOR)));
  queue->addMaker(new Margin(mapBorder + locationMargin, locations2));
  queue->addMaker(new Items(ItemFactory::mushrooms(), SquareId::GRASS, width / 10, width / 5));
  queue->addMaker(new AddMapBorder(mapBorder));
  queue->addMaker(getForrestCreatures(forrestCreatures, width - 2 * mapBorder, biomeId));
  return PLevelMaker(new BorderGuard(queue));
}

Vec2 LevelMaker::getRandomExit(RandomGen& random, Rectangle rect, int minCornerDist) {
  CHECK(rect.width() > 2 * minCornerDist && rect.height() > 2 * minCornerDist);
  int w1 = random.get(2);
  int w2 = random.get(2);
  int d1 = random.get(minCornerDist, rect.width() - minCornerDist);
  int d2 = random.get(minCornerDist, rect.height() - minCornerDist);
  return Vec2(
        rect.left() + d1 * w1 + (1 - w1) * w2 * (rect.width() - 1),
        rect.top() + d2 * (1 - w1) + w1 * w2 * (rect.height() - 1));
}

class SpecificArea : public LevelMaker {
  public:
  SpecificArea(Rectangle a, LevelMaker* m) : area(a), maker(m) {}

  virtual void make(LevelBuilder* builder, Rectangle) override {
    maker->make(builder, area);
  }

  private:
  Rectangle area;
  LevelMaker* maker;
};

PLevelMaker LevelMaker::splashLevel(CreatureFactory heroLeader, CreatureFactory heroes, CreatureFactory monsters,
    CreatureFactory imps, const string& splashPath) {
  MakerQueue* queue = new MakerQueue();
  queue->addMaker(new Empty(SquareId::BLACK_FLOOR));
  Rectangle leaderSpawn(
          Level::getSplashVisibleBounds().right() + 1, Level::getSplashVisibleBounds().middle().y,
          Level::getSplashVisibleBounds().right() + 2, Level::getSplashVisibleBounds().middle().y + 1);
  Rectangle heroSpawn(
          Level::getSplashVisibleBounds().right() + 2, Level::getSplashVisibleBounds().middle().y - 1,
          Level::getSplashBounds().right(), Level::getSplashVisibleBounds().middle().y + 2);
  Rectangle monsterSpawn1(
          Level::getSplashVisibleBounds().left(), 0,
          Level::getSplashVisibleBounds().right(), Level::getSplashVisibleBounds().top() - 1);
  Rectangle monsterSpawn2(
          Level::getSplashVisibleBounds().left(), Level::getSplashVisibleBounds().bottom() + 2,
          Level::getSplashVisibleBounds().right(), Level::getSplashBounds().bottom());
  queue->addMaker(new SpecificArea(leaderSpawn, new Creatures(heroLeader, 1,MonsterAIFactory::splashHeroes(true))));
  queue->addMaker(new SpecificArea(heroSpawn, new Creatures(heroes, 22, MonsterAIFactory::splashHeroes(false))));
  queue->addMaker(new SpecificArea(monsterSpawn1, new Creatures(monsters, 17, MonsterAIFactory::splashMonsters())));
  queue->addMaker(new SpecificArea(monsterSpawn2, new Creatures(monsters, 17, MonsterAIFactory::splashMonsters())));
  queue->addMaker(new SpecificArea(monsterSpawn1, new Creatures(imps, 15,
          MonsterAIFactory::splashImps(splashPath))));
  queue->addMaker(new SpecificArea(monsterSpawn2, new Creatures(imps, 15,
          MonsterAIFactory::splashImps(splashPath))));
  return PLevelMaker(new BorderGuard(queue, SquareId::BLACK_WALL));
}


static LevelMaker* underground(RandomGen& random, CreatureFactory waterFactory, CreatureFactory lavaFactory) {
  MakerQueue* queue = new MakerQueue();
  if (random.roll(1)) {
    vector<LevelMaker*> vCavern;
    vector<pair<int, int>> sizes;
    int minSize = random.get(5, 15);
    int maxSize = minSize + random.get(3, 10);
    for (int i : Range(sqrt(random.get(4, 100)))) {
      int size = random.get(minSize, maxSize);
      sizes.push_back(make_pair(size, size));
   /*   if (random.roll(4))
        queue->addMaker(new Items(ItemFactory::mushrooms(), SquareId::PATH, 2, 5));*/
      vCavern.push_back(new UniformBlob(SquareId::FLOOR));
    }
    queue->addMaker(new RandomLocations(vCavern, sizes, Predicate::alwaysTrue(), false));
  }
  switch (random.get(1, 3)) {
    case 1: queue->addMaker(new River(3, random.choose(SquareId::WATER, SquareId::MAGMA)));
            break;
    case 2:{
          int numLakes = sqrt(random.get(1, 100));
          SquareType lakeType = random.choose(SquareId::WATER, SquareId::MAGMA);
          vector<pair<int, int>> sizes;
          vector<LevelMaker*> makers;
          for (int i : Range(numLakes)) {
            int size = random.get(6, 20);
            sizes.emplace_back(size, size);
            makers.emplace_back(new UniformBlob(lakeType, none, SquareAttrib::LAKE));
          }
          queue->addMaker(new RandomLocations(makers, sizes, Predicate::alwaysTrue(), false));
          if (lakeType == SquareId::WATER) {
            queue->addMaker(new Creatures(waterFactory, 1, MonsterAIFactory::monster(),
                  Predicate::type(SquareType(SquareId::WATER))));
          }
          if (lakeType == SquareId::MAGMA) {
            queue->addMaker(new Creatures(lavaFactory, random.get(1, 4),
                  MonsterAIFactory::monster(), Predicate::type(SquareType(SquareId::MAGMA))));
          }
           break;
      }
    default: break;
  }
  return queue;
}

PLevelMaker LevelMaker::roomLevel(RandomGen& random, CreatureFactory roomFactory, CreatureFactory waterFactory,
    CreatureFactory lavaFactory, vector<StairKey> up, vector<StairKey> down, FurnitureFactory furniture) {
  MakerQueue* queue = new MakerQueue();
  queue->addMaker(new Empty(SquareId::MOUNTAIN));
  queue->addMaker(underground(random, waterFactory, lavaFactory));
  queue->addMaker(new RoomMaker(random.get(8, 15), 4, 7, SquareId::MOUNTAIN,
        SquareType(SquareId::MOUNTAIN), new Empty(SquareId::FLOOR)));
  queue->addMaker(new Connector(FurnitureFactory(TribeId::getHostile(), FurnitureType::DOOR), 0.5));
  queue->addMaker(new Furnitures(Predicate::attrib(SquareAttrib::EMPTY_ROOM), 0.05, furniture));
  for (StairKey key : down)
    queue->addMaker(new Stairs(StairDirection::DOWN, key, Predicate::type(SquareId::FLOOR)));
  for (StairKey key : up)
    queue->addMaker(new Stairs(StairDirection::UP, key, Predicate::type(SquareId::FLOOR)));
  queue->addMaker(new Creatures(roomFactory, random.get(10, 15), MonsterAIFactory::monster()));
  queue->addMaker(new Items(ItemFactory::dungeon(), SquareId::FLOOR, 5, 10));
  return PLevelMaker(new BorderGuard(queue, SquareId::BLACK_WALL));
}

class SokobanMaker : public LevelMaker {
  public:
  SokobanMaker(CreatureFactory boulders, StairKey hole) : boulderFactory(boulders), holeKey(hole) {}

  virtual void make(LevelBuilder* builder1, Rectangle area) override {
    builder = builder1;
    builder->setNoDiagonalPassing();
    Vec2 start;
    int roomRadius = 1;
    for (int x : area.getXRange().shorten(roomRadius).reverse())
      for (int y : builder->getRandom().permutation(area.getYRange().shorten(roomRadius)))
        if (builder->getType(Vec2(x, y)) == SquareId::FLOOR) {
          start = Vec2(x, y);
          goto found;
        }
found:
    int length = 7;
    middleLine = start.x;
    workArea = Rectangle(area.topLeft(), Vec2(start.x + length, area.bottom()));
    for (int i : Range(1, length + 1)) {
      Vec2 pos = start + Vec2(i, 0);
      builder->putSquare(pos, SquareId::FLOOR);
      boulders.push_back(pos);
    }
    builder->putSquare(start + Vec2(length + 1, 0), SquareId::FLOOR);
    builder->putFurniture(start + Vec2(length + 1, 0), FurnitureParams{FurnitureType::DOOR, TribeId::getHostile()});
    for (Vec2 v : Rectangle::centered(start + Vec2(length + roomRadius + 2, 0), roomRadius))
      builder->putSquare(v, SquareId::FLOOR, SquareAttrib::SOKOBAN_PRIZE);
    set<int> visited;
    Vec2 curPos = start;
    if (!moveBoulder(1500, curPos, visited))
      failGen();
    for (Vec2 pos : boulders) {
      if (pos.x > middleLine)
        failGen();
      builder->putCreature(pos, boulderFactory.random());
    }
    for (int i : Range(1, length + 1))
      builder->putSquare(start + Vec2(i, 0), {SquareId::SOKOBAN_HOLE, holeKey});
    builder->addAttrib(curPos, SquareAttrib::SOKOBAN_ENTRY);
  }

  private:

  int getHash(const vector<Vec2>& boulders, Vec2 curPos) {
    return combineHash(boulders);// + std::hash<Vec2>()(curPos);
  }

  bool isFree(Vec2 pos) {
    return pos.inRectangle(workArea) && !contains(boulders, pos) && (builder->getType(pos) == SquareId::FLOOR);
  }

  bool moveBoulder(int minDepth, Vec2& curPos, set<int>& visited) {
    if (visited.size() > maxVisited)
      failGen();
    BfSearch bfSearch(workArea, curPos, [&](Vec2 pos) {
        return builder->getType(pos) == SquareId::FLOOR && !contains(boulders, pos);}, Vec2::directions4());
    for (Vec2 pos : builder->getRandom().permutation(bfSearch.getAllReachable())) {
      for (Vec2 v : Vec2::directions4(builder->getRandom()))
        if (auto ind = findElement(boulders, pos + v)) {
          optional<Vec2> dest;
          for (int i = 1; isFree(pos - v * i) && !builder->getRandom().roll(4); ++i)
            dest = pos - v * i;
          if (!dest)
            continue;
          CHECK(!contains(boulders, *dest + v));
          boulders[*ind] = *dest + v;
          Vec2 prevPos = curPos;
          curPos = *dest;
          int hash = getHash(boulders, curPos);
          if (!visited.count(hash)) {
            visited.insert(hash);
            if (moveBoulder(minDepth - 1, curPos, visited))
              return true;
          }
          boulders[*ind] = pos + v;
          curPos = prevPos;
        }
    }
    return minDepth <= 0;
  }

  int middleLine;
  LevelBuilder* builder;
  Rectangle workArea;
  const int maxVisited = 15000;
  vector<Vec2> boulders;
  CreatureFactory boulderFactory;
  StairKey holeKey;
};

PLevelMaker LevelMaker::sokobanLevel(RandomGen& random, SettlementInfo info) {
  MakerQueue* queue = new MakerQueue();
  queue->addMaker(new Empty(SquareId::MOUNTAIN));
  RandomLocations* locations = new RandomLocations();
  for (int i = 0; i < 3; ++i) {
    locations->add(new Empty(SquareId::FLOOR), Vec2(2, random.get(2, 6)), Predicate::alwaysTrue());
  }
  for (int i = 0; i < 1; ++i) {
    locations->add(new Margin(1, new Empty(SquareId::FLOOR)), Vec2(4, random.get(4, 6)), Predicate::alwaysTrue());
  }
  queue->addMaker(new Division(0.5, locations, nullptr));
  queue->addMaker(new Connector(none, 0, 100));
  queue->addMaker(new SokobanMaker(info.neutralCreatures->first, getOnlyElement(info.downStairs)));
  queue->addMaker(new Stairs(StairDirection::DOWN, getOnlyElement(info.downStairs),
        Predicate::attrib(SquareAttrib::SOKOBAN_ENTRY)));
  queue->addMaker(new LocationMaker(info.location));
  queue->addMaker(new Creatures(*info.creatures, info.numCreatures, info.collective,
        Predicate::attrib(SquareAttrib::SOKOBAN_PRIZE)));
  return PLevelMaker(new BorderGuard(queue, SquareId::MOUNTAIN));
}

PLevelMaker LevelMaker::quickLevel(RandomGen&) {
  MakerQueue* queue = new MakerQueue();
  queue->addMaker(new Empty(SquareId::GRASS));
  queue->addMaker(new Mountains({0.0, 0.0, 0.6, 0.68, 0.95}, 0.45, {0, 1, 0, 0, 0},
        {SquareId::MOUNTAIN, SquareId::MOUNTAIN, SquareId::HILL, SquareId::GRASS, SquareId::SAND}));
/*  queue->addMaker(new RandomLocations(
          {new Empty(SquareId::FLOOR)}, {{2, 2}},
          Predicate::type(SquareId::MOUNTAIN)));*/
  queue->addMaker(new StartingPos(Predicate::type(SquareId::GRASS), StairKey::keeperSpawn()));
  queue->addMaker(new Margin(2,
        new TransferPos(Predicate::type(SquareId::GRASS), StairKey::transferLanding(), 2)));
  queue->addMaker(new AddMapBorder(2));
  return PLevelMaker(new BorderGuard(queue, SquareId::BLACK_WALL));
}

PLevelMaker LevelMaker::emptyLevel(RandomGen&) {
  MakerQueue* queue = new MakerQueue();
  queue->addMaker(new Empty(SquareId::GRASS));
  return PLevelMaker(new BorderGuard(queue, SquareId::BLACK_WALL));
}
