#pragma once

#include "lasting_effect.h"
#include "attr_type.h"
#include "effect.h"
#include "msg_type.h"
#include "fx_variant_name.h"
#include "color.h"
#include "special_attr.h"

struct YouMessage {
  MsgType SERIAL(type);
  string SERIAL(message);
  SERIALIZE_ALL(type, message)
};

struct VerbMessage {
  string SERIAL(secondPerson);
  string SERIAL(thirdPerson);
  string SERIAL(message);
  SERIALIZE_ALL(secondPerson, thirdPerson, message)
};

using BuffMessageInfo = variant<YouMessage, VerbMessage>;

void applyMessage(const BuffMessageInfo&, const Creature*);
void serialize(PrettyInputArchive&, BuffMessageInfo&, const unsigned int);

struct BuffInfo {
  string SERIAL(name);
  optional<BuffMessageInfo> SERIAL(addedMessage);
  optional<BuffMessageInfo> SERIAL(removedMessage);
  optional<Effect> SERIAL(startEffect);
  optional<Effect> SERIAL(tickEffect);
  optional<Effect> SERIAL(endEffect);
  string SERIAL(description);
  string SERIAL(adjective);
  bool SERIAL(consideredBad) = false;
  bool SERIAL(combatConsumable) = false;
  bool SERIAL(stacks) = false;
  bool SERIAL(canAbsorb) = true;
  bool SERIAL(canWishFor) = true;
  bool SERIAL(inheritsFromSteed) = false;
  optional<double> SERIAL(efficiencyMultiplier);
  int SERIAL(price) = 50;
  Color SERIAL(color);
  optional<string> SERIAL(hatedGroupName);
  double SERIAL(defenseMultiplier) = 1.0;
  optional<AttrType> SERIAL(defenseMultiplierAttr);
  FXVariantName SERIAL(fx) = FXVariantName::BUFF_RED;
  optional<pair<AttrType, AttrType>> SERIAL(modifyDamageAttr);
  optional<SpecialAttr> SERIAL(specialAttr);
  optional<CreaturePredicate> SERIAL(hiddenPredicate);
  template <class Archive>
  void serialize(Archive& ar1, const unsigned int);
};

CEREAL_CLASS_VERSION(BuffInfo, 2)