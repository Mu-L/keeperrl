#pragma once

#include "util.h"
#include "item_type.h"
#include "creature_factory.h"
#include "item_factory.h"
#include "furniture_list.h"
#include "inhabitants_info.h"
#include "building_id.h"
#include "building_info.h"
#include "name_generator_id.h"
#include "stair_key.h"
#include "map_layout_id.h"
#include "random_layout_id.h"
#include "layout_mapping_id.h"

struct StockpileInfo {
  ItemListId SERIAL(items);
  int SERIAL(count);
  optional<FurnitureType> SERIAL(furniture);
  SERIALIZE_ALL(NAMED(items), NAMED(count), NAMED(furniture))
};

RICH_ENUM(
  BuiltinLayoutId,
  VILLAGE,
  SMALL_VILLAGE,
  FORREST_VILLAGE,
  FOREST,
  EMPTY,
  CASTLE2,
  COTTAGE,
  FORREST_COTTAGE,
  TOWER,
  TEMPLE,
  MINETOWN,
  ANT_NEST,
  SMALL_MINETOWN,
  VAULT,
  CAVE,
  SPIDER_CAVE,
  ISLAND_VAULT,
  ISLAND_VAULT_DOOR,
  CEMETERY,
  MOUNTAIN_LAKE
);

RICH_ENUM(
  MapLayoutPredicate,
  OUTDOOR,
  MOUNTAIN,
  CAVE
);

namespace MapLayoutTypes {
struct Builtin {
  BuiltinLayoutId SERIAL(id);
  BuildingId SERIAL(buildingId);
  BuildingInfo buildingInfo;
  SERIALIZE_ALL(id, buildingId)
};

struct Predefined {
  MapLayoutId SERIAL(id);
  MapLayoutPredicate SERIAL(predicate) = MapLayoutPredicate::OUTDOOR;
  BuildingId SERIAL(buildingId);
  BuildingInfo buildingInfo;
  SERIALIZE_ALL(NAMED(id), OPTION(buildingId), OPTION(predicate))
};

struct RandomLayout {
  RandomLayoutId SERIAL(id);
  MapLayoutPredicate SERIAL(predicate) = MapLayoutPredicate::OUTDOOR;
  LayoutMappingId SERIAL(mapping);
  Vec2Range SERIAL(size);
  SERIALIZE_ALL(NAMED(id), NAMED(size), NAMED(mapping), OPTION(predicate))
};

MAKE_VARIANT2(LayoutType, Predefined, RandomLayout, Builtin);
}
using MapLayoutTypes::LayoutType;

class CollectiveBuilder;

struct CropsInfo {
  MapLayoutTypes::RandomLayout SERIAL(layout);
  Range SERIAL(count);
  int SERIAL(distance);
  SERIALIZE_ALL(layout, count, distance)
};

struct SettlementInfo {
  LayoutType SERIAL(type);
  InhabitantsInfo SERIAL(inhabitants);
  optional<InhabitantsInfo> SERIAL(corpses);
  optional<string> SERIAL(locationName);
  optional<NameGeneratorId> SERIAL(locationNameGen);
  TribeId SERIAL(tribe);
  optional<string> SERIAL(race);
  vector<StairKey> downStairs;
  vector<StairKey> upStairs;
  vector<StockpileInfo> SERIAL(stockpiles);
  optional<ItemType> SERIAL(lootItem);
  struct ShopInfo {
    Range SERIAL(count);
    ItemListId SERIAL(items);
    SERIALIZE_ALL(count, items)
  };
  vector<ShopInfo> SERIAL(shopItems);
  bool SERIAL(shopkeeperDead) = false;
  CollectiveBuilder* collective;
  optional<FurnitureListId> SERIAL(furniture);
  optional<FurnitureListId> SERIAL(outsideFeatures);
  bool SERIAL(closeToPlayer) = false;
  bool SERIAL(dontConnectCave) = false;
  bool SERIAL(dontBuildRoad) = false;
  int SERIAL(surroundWithResources) = 0;
  optional<FurnitureType> SERIAL(extraResources);
  optional<CropsInfo> SERIAL(crops);
  SERIALIZE_ALL(NAMED(type), OPTION(inhabitants), NAMED(corpses), NAMED(locationName), NAMED(locationNameGen), NAMED(tribe), NAMED(race), OPTION(stockpiles), NAMED(lootItem), OPTION(shopItems), OPTION(shopkeeperDead), OPTION(furniture), NAMED(outsideFeatures), OPTION(closeToPlayer), OPTION(dontConnectCave), OPTION(dontBuildRoad), OPTION(surroundWithResources), NAMED(extraResources), NAMED(crops))
};

static_assert(std::is_nothrow_move_constructible<SettlementInfo>::value, "T should be noexcept MoveConstructible");
