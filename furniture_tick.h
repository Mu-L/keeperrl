#pragma once

#include "util.h"
#include "effect.h"
#include "pretty_archive.h"

class Position;
class Furniture;

namespace FurnitureTickTypes {

struct MeteorShower : EmptyStruct<struct MeteorShowerTag> {};
struct Pit : EmptyStruct<struct PitTag> {};

struct Trap {
  int SERIAL(maxDistance);
  array<Effect, 4> SERIAL(effects); // {south, east, north, west}
  SERIALIZE_ALL(maxDistance, effects)
};

#define VARIANT_TYPES_LIST\
  X(Effect, 0)\
  X(MeteorShower, 1)\
  X(Pit, 2)\
  X(Trap, 4)

#define VARIANT_NAME FurnitureTickType

#include "gen_variant.h"
#include "gen_variant_serialize.h"
inline
#include "gen_variant_serialize_pretty.h"

#undef VARIANT_TYPES_LIST
#undef VARIANT_NAME

}

class FurnitureTickType : public FurnitureTickTypes::FurnitureTickType {
  public:
  using FurnitureTickTypes::FurnitureTickType::FurnitureTickType;
  void handle(Position, Furniture*);
};
