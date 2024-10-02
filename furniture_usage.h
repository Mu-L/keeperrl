#pragma once

#include "util.h"
#include "effect.h"

class Position;
class Furniture;
class Creature;

RICH_ENUM(
  BuiltinUsageId,
  KEEPER_BOARD,
  TIE_UP,
  TRAIN,
  ARCHERY_RANGE,
  STUDY,
  PORTAL,
  DEMON_RITUAL,
  PRAY
);

struct UsageEffect {
  string SERIAL(usageVerb);
  Effect SERIAL(effect);
  SERIALIZE_ALL(usageVerb, effect)
};

MAKE_VARIANT2(FurnitureUsageType, BuiltinUsageId, UsageEffect);

class FurnitureUsage {
  public:
  static void handle(FurnitureUsageType, Position, const Furniture*, Creature*);
  static bool canHandle(FurnitureUsageType, const Creature*);
  static string getUsageQuestion(FurnitureUsageType, string furnitureName);
};
