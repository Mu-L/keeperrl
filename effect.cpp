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
#include "effect.h"
#include "controller.h"
#include "creature.h"
#include "level.h"
#include "creature_factory.h"
#include "creature_group.h"
#include "item.h"
#include "view_object.h"
#include "view_id.h"
#include "game.h"
#include "model.h"
#include "monster_ai.h"
#include "attack.h"
#include "player_message.h"
#include "equipment.h"
#include "creature_attributes.h"
#include "creature_name.h"
#include "position_map.h"
#include "sound.h"
#include "attack_level.h"
#include "attack_type.h"
#include "body.h"
#include "game_event.h"
#include "item_class.h"
#include "furniture_factory.h"
#include "furniture.h"
#include "movement_set.h"
#include "weapon_info.h"
#include "fx_name.h"
#include "draw_line.h"
#include "monster.h"
#include "spell_map.h"
#include "content_factory.h"
#include "effect_type.h"
#include "minion_equipment_type.h"
#include "health_type.h"
#include "collective.h"
#include "collective_control.h"
#include "immigration.h"
#include "immigrant_info.h"
#include "furniture_entry.h"
#include "territory.h"
#include "vision.h"
#include "workshop_type.h"
#include "automaton_part.h"
#include "minion_trait.h"
#include "unlocks.h"
#include "view.h"
#include "player.h"
#include "tile_gas.h"
#include "tile_gas_info.h"
#include "buff_info.h"
#include "zlevel.h"
#include "special_trait.h"

namespace {
struct DefaultType {
  template <typename T>
  DefaultType(const T&) {}
  template <typename T>
  DefaultType(T&) {}
};
}

template <typename T>
static double getSteedChance() {
  return 0;
}

static bool isConsideredInDanger(const Creature* c) {
  if (auto intent = c->getLastCombatIntent())
    return (intent->time > *c->getGlobalTime() - 5_visible);
  return false;
}

static void summonFX(Position pos) {
  auto color = Color(240, 146, 184);
  pos.getGame()->addEvent(EventInfo::FX{pos, {FXName::SPAWN, color}});
}

vector<Creature*> Effect::summonCreatures(Position pos, vector<PCreature> creatures, TimeInterval delay) {
  vector<Creature*> ret;
  for (int i : All(creatures))
    if (auto v = pos.getLevel()->getClosestLanding({pos}, creatures[i].get())) {
      ret.push_back(creatures[i].get());
      v->addCreature(std::move(creatures[i]), delay);
      summonFX(*v);
    }
  return ret;
}

vector<Creature*> Effect::summon(Creature* c, CreatureId id, int num, optional<TimeInterval> ttl, TimeInterval delay,
    optional<Position> position) {
  vector<PCreature> creatures;
  for (int i : Range(num))
    creatures.push_back(c->getGame()->getContentFactory()->getCreatures().fromId(id, c->getTribeId(),
        MonsterAIFactory::summoned(c)));
  auto ret = summonCreatures(position.value_or(c->getPosition()), std::move(creatures), delay);
  for (auto summoned : ret) {
    summoned->setCombatExperience(c->getCombatExperience(false, false));
    if (ttl)
      summoned->addEffect(LastingEffect::SUMMONED, *ttl, false);
  }
  return ret;
}

vector<Creature*> Effect::summon(Position pos, CreatureGroup& factory, int num, optional<TimeInterval> ttl,
    TimeInterval delay) {
  vector<PCreature> creatures;
  for (int i : Range(num))
    creatures.push_back(factory.random(&pos.getGame()->getContentFactory()->getCreatures(), MonsterAIFactory::monster()));
  auto ret = summonCreatures(pos, std::move(creatures), delay);
  for (auto c : ret)
    if (ttl)
      c->addEffect(LastingEffect::SUMMONED, *ttl, false);
  return ret;
}

static bool enhanceArmor(Creature* c, int mod, TStringId verb1, TStringId verb2) {
  for (EquipmentSlot slot : Random.permutation(getKeys(Equipment::slotTitles)))
    for (Item* item : c->getEquipment().getSlotItems(slot))
      if (item->getClass() == ItemClass::ARMOR) {
        c->verb(verb1, verb2, item->getName());
        if (item->getModifier(AttrType("DEFENSE")) > 0 || mod > 0)
          item->addModifier(AttrType("DEFENSE"), mod);
        return true;
      }
  return false;
}

static bool enhanceWeapon(Creature* c, int mod, TStringId verb1, TStringId verb2) {
  auto items = c->getEquipment().getSlotItems(EquipmentSlot::WEAPON);
  items.append(c->getEquipment().getSlotItems(EquipmentSlot::RANGED_WEAPON));
  if (!items.empty()) {
    auto item = Random.choose(items);
    c->verb(verb1, verb2, item->getName());
    auto attr = [&] {
      if (item->getEquipmentSlot() == EquipmentSlot::RANGED_WEAPON)
        return AttrType("RANGED_DAMAGE");
      return item->getWeaponInfo().meleeAttackAttr;
    }();
    item->addModifier(attr, mod);
    return true;
  }
  return false;
}

static bool summon(Creature* summoner, CreatureId id, Range count, bool hostile, optional<TimeInterval> ttl) {
  if (hostile) {
    CreatureGroup f = CreatureGroup::singleType(TribeId::getHostile(), id);
    return !Effect::summon(summoner->getPosition(), f, Random.get(count), ttl, 1_visible).empty();
  } else
    return !Effect::summon(summoner, id, Random.get(count), ttl, 1_visible).empty();
}

static int getPrice(const Effects::Escape&, const ContentFactory*) {
  return 12;
}

static bool isGasHarmful(Position pos, const Creature* c) {
  for (auto& elem : pos.getGame()->getContentFactory()->tileGasTypes)
    if (pos.getGasAmount(elem.first) > 0.0 && elem.second.effect &&
        elem.second.effect->shouldAIApply(c, c->getPosition()) < 0)
      return true;
  return false;
}

static bool applyToCreature(const Effects::Escape& e, Creature* c, Creature*) {
  PROFILE_BLOCK("Escape::applyToCreature");
  Rectangle area = Rectangle::centered(Vec2(0, 0), 12);
  PositionMap<int> weight;
  queue<Position> q;
  auto addDanger = [&] (Position pos) {
    q.push(pos);
    weight.set(pos, 0);
  };
  for (Position v : c->getPosition().getRectangle(area)) {
    if (v.isBurning())
      addDanger(v);
    else if (auto other = v.getCreature())
      if (other->isEnemy(c))
        addDanger(v);
  }
  if (isGasHarmful(c->getPosition(), c))
    addDanger(c->getPosition());
  while (!q.empty()) {
    Position v = q.front();
    q.pop();
    for (Position w : v.neighbors8())
      if (w.canEnterEmpty({MovementTrait::WALK}) && !weight.contains(w)) {
        weight.set(w, weight.getOrFail(v) + 1);
        q.push(w);
      }
  }
  vector<Position> good;
  int maxW = 0;
  auto movementType = c->getMovementType();
  for (Position v : c->getPosition().getRectangle(area)) {
    if (!v.canEnter(c) || v.isBurning() || isGasHarmful(v, c) ||
        !v.isConnectedTo(c->getPosition(), movementType) || *v.dist8(c->getPosition()) > e.maxDist)
      continue;
    if (auto weightV = weight.getValueMaybe(v)) {
      if (*weightV == maxW)
        good.push_back(v);
      else if (*weightV > maxW) {
        good = {v};
        maxW = *weightV;
      }
    }
  }
  if (maxW < 2) {
    c->message(TStringId("SPELL_DIDNT_WORK"));
    return false;
  }
  CHECK(!good.empty());
  c->you(MsgType::TELE_DISAPPEAR);
  c->getPosition().moveCreature(Random.choose(good), true);
  c->you(MsgType::TELE_APPEAR);
  return true;
}

static optional<MinionEquipmentType> getMinionEquipmentType(const Effects::Escape&) {
  return MinionEquipmentType::COMBAT_ITEM;
}

static TString getName(const Effects::Escape&, const ContentFactory*) {
  return TStringId("ESCAPE_EFFECT_NAME");
}

static TString getDescription(const Effects::Escape&, const ContentFactory*) {
  return TStringId("ESCAPE_EFFECT_DESCRIPTION");
}

static TString getName(const Effects::Teleport&, const ContentFactory*) {
  return TStringId("TELEPORT_EFFECT_NAME");
}

static TString getDescription(const Effects::Teleport&, const ContentFactory*) {
  return TStringId("TELEPORT_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::Teleport&, Position pos, Creature* attacker) {
  if (attacker->getPosition().canMoveCreature(pos)) {
    attacker->you(MsgType::TELE_DISAPPEAR);
    attacker->getPosition().moveCreature(pos, true);
    attacker->you(MsgType::TELE_APPEAR);
    return true;
  }
  return false;
}

static TString getName(const Effects::SetPhylactery&, const ContentFactory*) {
  return TStringId("PHYLACTERY_EFFECT_NAME");
}

static TString getDescription(const Effects::SetPhylactery&, const ContentFactory*) {
  return TStringId("PHYLACTERY_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::SetPhylactery&, Position pos, Creature* attacker) {
  if (attacker)
    if (auto f = pos.getFurniture(FurnitureLayer::MIDDLE)) {
      attacker->setPhylactery(pos, f->getType());
      return true;
    }
  return false;
}

static TString getName(const Effects::AddTechnology& t, const ContentFactory* f) {
  return f->technology.techs.at(t).name.value_or(TString(string(t.data())));
}

static TString getDescription(const Effects::AddTechnology& t, const ContentFactory* f) {
  auto name = f->technology.techs.at(t).name.value_or(TString(string(t.data())));
  return TSentence("TECH_EFFECT_DESCRIPTION", std::move(name));
}

static bool apply(const Effects::AddTechnology& t, Position pos, Creature* attacker) {
  pos.getGame()->addEvent(EventInfo::TechbookRead{t});
  return true;
}

static TString getName(const Effects::Jump&, const ContentFactory*) {
  return TStringId("JUMP_EFFECT_NAME");
}

static TString getDescription(const Effects::Jump&, const ContentFactory*) {
  return TStringId("JUMP_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::Jump&, Position pos, Creature* attacker) {
  auto from = attacker->getPosition();
  if (pos.canEnter(attacker)) {
    for (auto v : drawLine(from, pos))
      if (v != from && !v.canEnter(MovementType({MovementTrait::WALK, MovementTrait::FLY})))
        return false;
    attacker->displace(attacker->getPosition().getDir(pos));
    return true;
  }
  return false;
}

static bool canAutoAssignMinionEquipment(const Effects::NoAutoAssign& e) {
  return false;
}

static optional<MinionEquipmentType> getMinionEquipmentType(const Effects::EquipmentType& e) {
  return e.type;
}

static bool applyToCreature(const Effects::Lasting& e, Creature* c, Creature*) {
  return addEffect(e.lastingEffect, c, e.duration);
}

static void scale(Effects::Lasting& e, double value, const ContentFactory* f) {
  if (e.duration)
    *e.duration *= value;
  else
    e.duration = LastingEffects::getDuration(*e.lastingEffect.getValueMaybe<LastingEffect>()) * value;
}

/*static bool isOffensive(const Effects::Lasting& e) {
  return LastingEffects::isConsideredBad(e.lastingEffect);
}*/

static int getPrice(const Effects::Lasting& e, const ContentFactory* factory) {
  return getPrice(e.lastingEffect, factory);
}

static bool isConsideredHostile(const Effects::Lasting& e, const Creature* victim) {
  return isConsideredBad(e.lastingEffect, victim->getGame()->getContentFactory());
}

static optional<MinionEquipmentType> getMinionEquipmentType(const Effects::Lasting& e) {
  return MinionEquipmentType::COMBAT_ITEM;
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::Lasting& e, const Creature* victim, bool isEnemy) {
  if (isAffected(victim, e.lastingEffect))
    return 0;
  return shouldAIApply(e.lastingEffect, isEnemy, victim);
}

static Color getColor(const Effects::Lasting& e, const ContentFactory* f) {
  return getColor(e.lastingEffect, f);
}

static TString getName(const Effects::Lasting& e, const ContentFactory* f) {
  return getName(e.lastingEffect, f);
}

static TString getDescription(const Effects::Lasting& e, const ContentFactory* f) {
  auto ret = getDescription(e.lastingEffect, f);
  auto duration = !!e.duration
      ? *e.duration
      : LastingEffects::getDuration(*e.lastingEffect.getValueMaybe<LastingEffect>());
  return TSentence("BUFF_EFFECT_DESCRIPTION", std::move(ret), TString(duration));
}

static bool applyToCreature(const Effects::RemoveLasting& e, Creature* c, Creature*) {
  return removeEffect(e.lastingEffect, c);
}

static TString getName(const Effects::RemoveLasting& e, const ContentFactory* f) {
  return TSentence("REMOVE_BUFF_NAME", getName(e.lastingEffect, f));
}

static TString getDescription(const Effects::RemoveLasting& e, const ContentFactory* f) {
  return TSentence("REMOVE_BUFF_DESCRIPTION", getName(e.lastingEffect, f));
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::RemoveLasting& e, const Creature* victim, bool isEnemy) {
  if (!isAffected(victim, e.lastingEffect))
    return 0;
  return -shouldAIApply(e.lastingEffect, isEnemy, victim);
}

static bool applyToCreature(const Effects::IncreaseAttr& e, Creature* c, Creature*) {
  if (auto game = c->getGame())
    c->verb(e.get(TStringId("YOUR_ATTR_IMPROVES"), TStringId("YOUR_ATTR_WANES")),
        e.get(TStringId("HIS_ATTR_IMPROVES"), TStringId("HIS_ATTR_WANES")),
        game->getContentFactory()->attrInfo.at(e.attr).name);
  c->getAttributes().increaseBaseAttr(e.attr, e.amount);
  return true;
}

static TString getName(const Effects::IncreaseAttr& e, const ContentFactory* f) {
  return TSentence(e.get(TStringId("ATTR_BOOST_NAME"), TStringId("ATTR_LOSS_NAME")), f->attrInfo.at(e.attr).name);
}

static void scale(Effects::IncreaseAttr& e, double value, const ContentFactory* f) {
  e.amount *= value;
}

static TString getDescription(const Effects::IncreaseAttr& e, const ContentFactory* f) {
  return TSentence(e.get(TStringId("ATTR_BOOST_DESCRIPTION"), TStringId("ATTR_LOSS_DESCRIPTION")),
      f->attrInfo.at(e.attr).name, TString(abs(e.amount)));
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::IncreaseAttr& e, const Creature* victim, bool isEnemy) {
  return e.amount;
}

TStringId Effects::IncreaseAttr::get(TStringId ifIncrease, TStringId ifDecrease) const {
  if (amount > 0)
    return ifIncrease;
  else
    return ifDecrease;
}

static bool applyToCreature(const Effects::AddExperience& e, Creature* c, Creature* attacker) {
  c->setCombatExperience(c->getCombatExperience(false, false) + e.amount);
  c->you(MsgType::ARE, TStringId("MORE_EXPERIENCED"));
  return true;
}

static TString getName(const Effects::AddExperience& e, const ContentFactory*) {
  return TStringId("ADD_EXPERIENCE_NAME");
}

static TString getDescription(const Effects::AddExperience& e, const ContentFactory*) {
  return TStringId("ADD_EXPERIENCE_DESCRIPTION");
}

static const char* get(const Effects::SpecialAttr& a, const char* ifIncrease, const char* ifDecrease) {
  if (a.value > 0)
    return ifIncrease;
  else
    return ifDecrease;
}

static bool applyToCreature(const Effects::SpecialAttr& e, Creature* c, Creature*) {
  auto factory = c->getGame()->getContentFactory();
  c->verb(TStringId(get(e, "YOUR_SPECIAL_ATTR_IMPROVES", "YOUR_SPECIAL_ATTR_WANES")),
      TStringId(get(e, "HIS_SPECIAL_ATTR_IMPROVES", "HIS_SPECIAL_ATTR_WANES")),
      factory->attrInfo.at(e.attr).name, e.predicate.getName(factory));
  c->getAttributes().specialAttr[e.attr].push_back(make_pair(e.value, e.predicate));
  return true;
}

static TString getName(const Effects::SpecialAttr& e, const ContentFactory* f) {
  return TSentence(TStringId(get(e, "SPECIAL_ATTR_BOOST_NAME", "SPECIAL_ATTR_LOSS_NAME")),
      f->attrInfo.at(e.attr).name, e.predicate.getName(f));
}

static TString getDescription(const Effects::SpecialAttr& e, const ContentFactory* f) {
  return TSentence(TStringId(get(e, "SPECIAL_ATTR_BOOST_DESCRIPTION", "SPECIAL_ATTR_LOSS_DESCRIPTION")),
      {f->attrInfo.at(e.attr).name, TString(e.value), e.predicate.getName(f)});
}

static const char* get(const Effects::IncreaseMaxLevel& e, const char* inc, const char* dec) {
  if (e.value > 0)
    return inc;
  return dec;
}

static bool applyToCreature(const Effects::IncreaseMaxLevel& e, Creature* c, Creature*) {
  auto f = c->getGame()->getContentFactory();
  c->verb(TStringId(get(e, "YOUR_TRAINING_LIMIT_IMPROVES", "YOUR_TRAINING_LIMIT_WANES")),
      TStringId(get(e, "HIS_TRAINING_LIMIT_IMPROVES", "HIS_TRAINING_LIMIT_WANES")),
      f->attrInfo.at(e.type).name);
  c->getAttributes().increaseMaxExpLevel(e.type, e.value);
  return true;
}

static TString getName(const Effects::IncreaseMaxLevel& e, const ContentFactory* f) {
  return TSentence("TRAINING_LIMIT_EFFECT_NAME", f->attrInfo.at(e.type).name);
}

static TString getDescription(const Effects::IncreaseMaxLevel& e, const ContentFactory* f) {
  return TSentence(TStringId(get(e, "TRAINING_LIMIT_BUFF_DESCRIPTION", "TRAINING_LIMIT_DEBUFF_DESCRIPTION")),
      f->attrInfo.at(e.type).name, TString(std::fabs(e.value)));
}

static bool applyToCreature(const Effects::IncreaseLevel& e, Creature* c, Creature*) {
  c->increaseExpLevel(e.type, e.value);
  return true;
}

static TString getName(const Effects::IncreaseLevel& e, const ContentFactory* f) {
  return TSentence("TRAINING_EFFECT_NAME", f->attrInfo.at(e.type).name);
}

static TString getDescription(const Effects::IncreaseLevel& e, const ContentFactory* f) {
  return TSentence(TStringId("TRAINING_EFFECT_DESCRIPTION"),
      f->attrInfo.at(e.type).name, TString(std::fabs(e.value)));
}

static bool applyToCreature(const Effects::AddCompanion& e, Creature* c, Creature*) {
  c->getAttributes().companions.push_back(e);
  return true;
}

static TString getName(const Effects::AddCompanion& e, const ContentFactory*) {
  return TStringId("COMPANION_EFFECT_NAME");
}

static TString getDescription(const Effects::AddCompanion& e, const ContentFactory* f) {
  return TSentence("COMPANION_EFFECT_DESCRIPTION", f->getCreatures().getName(e.creatures[0]));
}

static bool applyToCreature(const Effects::Permanent& e, Creature* c, Creature*) {
  return addPermanentEffect(e.lastingEffect, c);
}

static int getPrice(const Effects::Permanent& e, const ContentFactory* f) {
  return getPrice(e.lastingEffect, f) * 30;
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::Permanent& e, const Creature* victim, bool isEnemy) {
  if (isAffectedPermanently(victim, e.lastingEffect))
    return 0;
  return shouldAIApply(e.lastingEffect, isEnemy, victim);
}

static TString getName(const Effects::Permanent& e, const ContentFactory* f) {
  return TSentence("PERMANENT_LASTING_NAME", getName(e.lastingEffect, f));
}

static TString getDescription(const Effects::Permanent& e, const ContentFactory* f) {
  return TSentence("PERMANENT_LASTING_DESCRIPTION", getDescription(e.lastingEffect, f));
}

static Color getColor(const Effects::Permanent& e, const ContentFactory* f) {
  return getColor(e.lastingEffect, f);
}

static bool applyToCreature(const Effects::RemovePermanent& e, Creature* c, Creature*) {
  return removePermanentEffect(e.lastingEffect, c);
}

static TString getName(const Effects::RemovePermanent& e, const ContentFactory* f) {
  return TSentence("REMOVE_PERMANENT_LASTING_NAME", getName(e.lastingEffect, f));
}

static TString getDescription(const Effects::RemovePermanent& e, const ContentFactory* f) {
  return TSentence("REMOVE_PERMANENT_LASTING_DESCRIPTION", getName(e.lastingEffect, f));
}

static bool applyToCreature(const Effects::Alarm& e, Creature* c, Creature*) {
  c->getGame()->addEvent(EventInfo::Alarm{c->getPosition(), e.silent});
  return true;
}

static TString getName(const Effects::Alarm&, const ContentFactory*) {
  return TStringId("ALARM_EFFECT_NAME");
}

static TString getDescription(const Effects::Alarm&, const ContentFactory*) {
  return TStringId("ALARM_EFFECT_DESCRIPTION");
}

static TString getName(const Effects::Acid&, const ContentFactory*) {
  return TStringId("ACID_EFFECT_NAME");
}

static bool isOffensive(const Effects::Acid&) {
  return true;
}

static int getPrice(const Effects::Acid&, const ContentFactory*) {
  return 8;
}

static TString getDescription(const Effects::Acid&, const ContentFactory*) {
  return TStringId("ACID_EFFECT_DESCRIPTION");
}

static Color getColor(const Effects::Acid&, const ContentFactory* f) {
  return Color::YELLOW;
}

static bool apply(const Effects::Acid& a, Position pos, Creature* attacker) {
  return pos.acidDamage(a.amount.value_or(attacker ? attacker->getAttr(AttrType("ACID_DAMAGE")) : 10), attacker);
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::Acid&, const Creature* victim, bool isEnemy) {
  return isEnemy ? 1 : -1;
}

static bool isConsideredHostile(const Effects::Acid&, const Creature* victim) {
  return !victim->isAffected(BuffId("ACID_RESISTANT"));
}

static TString getName(const Effects::EatCorpse&, const ContentFactory*) {
  return TStringId("EAT_CORPSE_EFFECT_NAME");
}

static TString getDescription(const Effects::EatCorpse&, const ContentFactory*) {
  return TStringId("EAT_CORPSE_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::EatCorpse& a, Position pos, Creature* attacker) {
  for (auto& item : pos.getItems())
    if (auto info = item->getCorpseInfo()) {
      auto it = pos.removeItem(item);
      attacker->verb(TStringId("YOU_EAT"), TStringId("EATS"), TString(it->getTheName()));
      return true;
    }
  return false;
}

static TString getName(const Effects::Banish&, const ContentFactory*) {
  return TStringId("BANISH_EFFECT_NAME");
}

static TString getDescription(const Effects::Banish&, const ContentFactory*) {
  return TStringId("BANISH_EFFECT_DESCRIPTION");
}

static Collective* getCollective(Creature* c) {
  if (auto game = c->getGame())
    for (auto col : game->getCollectives())
      if (col->getCreatures().contains(c))
        return col;
  return nullptr;
}

static bool applyToCreature(const Effects::Banish& a, Creature *c, Creature* attacker) {
  if (auto col = getCollective(c)) {
    col->banishCreature(c);
    return true;
  }
  return false;
}

static bool apply(const Effects::Summon& e, Position pos, Creature* attacker) {
  if (auto c = pos.getCreature())
    return !Effect::summon(c, e.creature, Random.get(e.count), e.ttl.map([](int v) { return TimeInterval(v); }), 1_visible).empty();
  else {
    auto tribe = [&] {
      if (attacker)
        return attacker->getTribeId();
      if (auto col = pos.getCollective())
        return col->getTribeId();
      return TribeId::getHostile();
    }();
    CreatureGroup f = CreatureGroup::singleType(tribe, e.creature);
    return !Effect::summon(pos, f, Random.get(e.count), e.ttl.map([](int v) { return TimeInterval(v); }), 1_visible).empty();
  }
}

optional<Position> Effect::getSummonAwayPosition(Creature* c) {
  auto level = c->getPosition().getModel()->getGroundLevel();
  for (int i : Range(100)) {
    Position pos(level->getBounds().random(Random), level);
    if (pos.isCovered() || c->canSee(pos) || !pos.canEnter(MovementTrait::WALK) || !!pos.getCollective())
      continue;
    return pos;
  }
  return none;
}

static bool apply(const Effects::SummonAway& e, Position pos, Creature* attacker) {
  if (auto c = pos.getCreature()) {
    auto summoned = Effect::summon(c, e.creature, Random.get(e.count),
        none, 1_visible, Effect::getSummonAwayPosition(c));
    if (e.ttl)
      for (auto other : summoned)
        other->addEffect(BuffId("WILL_BANISH"), TimeInterval(*e.ttl));
    if (auto col = getCollective(c))
      for (auto other : summoned)
        col->addCreature(other, e.traits);
    return !summoned.empty();
  }
  return false;
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::Summon&, const Creature* victim, bool isEnemy) {
  return isConsideredInDanger(victim) ? 1 : 0;
}

static TString getName(const Effects::Summon& e, const ContentFactory* f) {
  return TSentence("SUMMON_EFFECT_NAME", e.count.getEnd() > 2
      ? f->getCreatures().getNamePlural(e.creature)
      : f->getCreatures().getName(e.creature));
}

static bool isOffensive(const Effects::Summon&) {
  return true;
}

static TString getDescription(const Effects::Summon& e, const ContentFactory* f) {
  if (e.count.getEnd() > 2) {
    if (e.count.getLength() == 1)
      return TSentence("SUMMON_MULTIPLE_EFFECT_DESCRIPTION", f->getCreatures().getNamePlural(e.creature),
          TString(e.count.getStart()));
    else
      return TSentence("SUMMON_RANGE_EFFECT_DESCRIPTION", {f->getCreatures().getNamePlural(e.creature),
          TString(e.count.getStart()), TString(e.count.getEnd() - 1)});
  } else
    return TSentence("SUMMON_SINGLE_EFFECT_DESCRIPTION", f->getCreatures().getName(e.creature));
}

static bool applyToCreature(const Effects::AddAutomatonPart& e, Creature* c, Creature*) {
  c->addAutomatonPart(e);
  return true;
}

static TString getName(const Effects::AddAutomatonPart& e, const ContentFactory* f) {
  return TStringId("AUTOMATON_PART_EFFECT_NAME");
}

static TString getDescription(const Effects::AddAutomatonPart& e, const ContentFactory* f) {
  return TStringId("AUTOMATON_PART_EFFECT_DESCRIPTION");
}

static TString getName(const Effects::SummonEnemy& e, const ContentFactory* f) {
  return TSentence("SUMMON_HOSTILE_EFFECT_NAME", f->getCreatures().getName(e.creature));
}

static TString getDescription(const Effects::SummonEnemy& e, const ContentFactory* f) {
  if (e.count.getEnd() > 2) {
    if (e.count.getLength() == 1)
      return TSentence("SUMMON_HOSTILE_MULTIPLE_EFFECT_DESCRIPTION", f->getCreatures().getNamePlural(e.creature),
          TString(e.count.getStart()));
    else
      return TSentence("SUMMON_HOSTILE_RANGE_EFFECT_DESCRIPTION", {f->getCreatures().getNamePlural(e.creature),
          TString(e.count.getStart()), TString(e.count.getEnd() - 1)});
  } else
    return TSentence("SUMMON_HOSTILE_SINGLE_EFFECT_DESCRIPTION", f->getCreatures().getName(e.creature));
}

static bool apply(const Effects::SummonEnemy& summon, Position pos, Creature*) {
  CreatureGroup f = CreatureGroup::singleType(TribeId::getHostile(), summon.creature);
  auto ret = Effect::summon(pos, f, Random.get(summon.count),
      summon.ttl.map([](int v) { return TimeInterval(v); }), 1_visible);
  for (auto c : ret)
    c->setCombatExperience(pos.getModelDifficulty());
  return !ret.empty();
}

static bool applyToCreature(const Effects::SummonElement&, Creature* c, Creature*) {
  auto id = CreatureId("AIR_ELEMENTAL");
  for (Position p : c->getPosition().getRectangle(Rectangle::centered(3)))
    for (auto f : p.getFurniture())
      if (auto elem = f->getSummonedElement())
        id = *elem;
  return ::summon(c, id, Range(1, 2), false, 100_visible);
}

static TString getName(const Effects::SummonElement&, const ContentFactory*) {
  return TStringId("SUMMON_ELEMENT_EFFECT_NAME");
}

static bool isOffensive(const Effects::SummonElement&) {
  return true;
}

static TString getDescription(const Effects::SummonElement&, const ContentFactory*) {
  return TStringId("SUMMON_ELEMENT_EFFECT_DESCRIPTION");
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::SummonElement&, const Creature* victim, bool isEnemy) {
  return isConsideredInDanger(victim) ? 1 : 0;
}

static bool applyToCreature(const Effects::Deception&, Creature* c, Creature*) {
  vector<PCreature> creatures;
  for (int i : Range(Random.get(3, 7)))
    creatures.push_back(CreatureFactory::getIllusion(c));
  return !Effect::summonCreatures(c->getPosition(), std::move(creatures)).empty();
}

static TString getName(const Effects::Deception&, const ContentFactory*) {
  return TStringId("DECEPTION_EFFECT_NAME");
}

static bool isOffensive(const Effects::Deception&) {
  return true;
}

static TString getDescription(const Effects::Deception&, const ContentFactory*) {
  return TStringId("DECEPTION_EFFECT_DESCRIPTION");
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::Deception&, const Creature* victim, bool isEnemy) {
  return isConsideredInDanger(victim) ? 1 : 0;
}

static void airBlast(Creature* attacker, Position origin, Position position, Position target) {
  CHECK(target != origin);
  Vec2 direction = origin.getDir(target);
  constexpr int maxDistance = 4;
  while (direction.length8() < maxDistance * 3)
    direction += origin.getDir(target);
  auto trajectory = drawLine(Vec2(0, 0), direction);
  for (int i : All(trajectory))
    if (trajectory[i] == origin.getDir(position)) {
      trajectory = trajectory.getSubsequence(i + 1, maxDistance);
      for (auto& v : trajectory)
        v = v - origin.getDir(position);
      break;
    }
  CHECK(trajectory.size() == maxDistance);
  if (Creature* c = position.getCreature()) {
    optional<Position> target;
    for (auto& v : trajectory)
      if (position.canMoveCreature(v))
        target = position.plus(v);
      else
        break;
    if (target) {
      c->displace(c->getPosition().getDir(*target));
      c->you(MsgType::ARE, TStringId("THROWN_BACK"));
    }
    c->addEffect(LastingEffect::COLLAPSED, 2_visible);
  }
  for (auto& stack : Item::stackItems(origin.getGame()->getContentFactory(), position.getItems())) {
    position.throwItem(
        position.removeItems(stack),
        Attack(attacker, Random.choose<AttackLevel>(),
          stack[0]->getWeaponInfo().attackType, 15, AttrType("DAMAGE")), maxDistance,
          position.plus(trajectory.back()), VisionId::NORMAL);
  }
  for (auto furniture : position.modFurniture())
    if (furniture->canDestroy(DestroyAction::Type::BASH))
      furniture->destroy(position, DestroyAction::Type::BASH);
}

static bool applyToCreature(const Effects::CircularBlast&, Creature* c, Creature* attacker) {
  for (Vec2 v : Vec2::directions8(Random))
    airBlast(attacker, c->getPosition(), c->getPosition().plus(v), c->getPosition().plus(v * 10));
  c->addFX({FXName::CIRCULAR_BLAST});
  return true;
}

static TString getName(const Effects::CircularBlast&, const ContentFactory*) {
  return TStringId("CIRCULAR_BLAST_EFFECT_NAME");
}

static bool isOffensive(const Effects::CircularBlast&) {
  return true;
}

static TString getDescription(const Effects::CircularBlast&, const ContentFactory*) {
  return TStringId("CIRCULAR_BLAST_EFFECT_DESCRIPTION");
}

const char* Effects::Enhance::typeAsString(const char* weapon, const char* armor) const {
  switch (type) {
    case EnhanceType::WEAPON:
      return weapon;
    case EnhanceType::ARMOR:
      return armor;
  }
}

const char* Effects::Enhance::amountAs(const char* positive, const char* negative) const {
  return amount > 0 ? positive : negative;
}

static bool applyToCreature(const Effects::Enhance& e, Creature* c, Creature*) {
  switch (e.type) {
    case Effects::EnhanceType::WEAPON:
      return enhanceWeapon(c, e.amount, TStringId(e.amountAs("YOUR_WEAPON_IS_IMPROVED", "YOUR_WEAPON_DEGRADES")),
          TStringId(e.amountAs("HIS_WEAPON_IS_IMPROVED", "HIS_WEAPON_DEGRADES")));
    case Effects::EnhanceType::ARMOR:
      return enhanceArmor(c, e.amount, TStringId(e.amountAs("YOUR_ARMOR_IS_IMPROVED", "YOUR_ARMOR_DEGRADES")),
          TStringId(e.amountAs("HIS_ARMOR_IS_IMPROVED", "HIS_ARMOR_DEGRADES")));
  }
}

static TString getName(const Effects::Enhance& e, const ContentFactory*) {
  return TStringId(e.typeAsString(e.amountAs("WEAPON_ENCHANTMENT_NAME", "WEAPON_DEGRADATION_NAME"),
      e.amountAs("ARMOR_ENCHANTMENT_NAME", "ARMOR_DEGRADATION_NAME")));
}

static TString getDescription(const Effects::Enhance& e, const ContentFactory*) {
  return TStringId(e.typeAsString(e.amountAs("WEAPON_ENCHANTMENT_DESCRIPTION", "WEAPON_DEGRADATION_DESCRIPTION"),
      e.amountAs("ARMOR_ENCHANTMENT_DESCRIPTION", "ARMOR_DEGRADATION_DESCRIPTION")));
}

static bool applyToCreature(const Effects::DestroyEquipment&, Creature* c, Creature*) {
  auto equipped = c->getEquipment().getAllEquipped();
  if (!equipped.empty()) {
    Item* dest = Random.choose(equipped);
    c->verb(TStringId("YOUR_ITEM_CRUMBLES"), TStringId("HIS_ITEM_CRUMBLES"), dest->getName());
    c->steal({dest});
    return true;
  }
  return false;
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::DestroyEquipment&, const Creature* victim, bool isEnemy) {
  return isEnemy ? 1 : -1;
}

static bool isConsideredHostile(const Effects::DestroyEquipment&, const Creature*) {
  return true;
}

static TString getName(const Effects::DestroyEquipment&, const ContentFactory*) {
  return TStringId("EQUIPMENT_DESTRUCTION_EFFECT_NAME");
}

static bool isOffensive(const Effects::DestroyEquipment&) {
  return true;
}

static TString getDescription(const Effects::DestroyEquipment&, const ContentFactory*) {
  return TStringId("EQUIPMENT_DESTRUCTION_EFFECT_DESCRIPTION");
}

static TString getName(const Effects::DestroyWalls&, const ContentFactory*) {
  return TStringId("DESTROY_WALLS_EFFECT_NAME");
}

static TString getDescription(const Effects::DestroyWalls&, const ContentFactory*) {
  return TStringId("DESTROY_WALLS_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::DestroyWalls& m, Position pos, Creature* attacker) {
  bool res = false;
  for (auto furniture : pos.modFurniture())
    if (furniture->canDestroy(m.action)) {
      furniture->destroy(pos, m.action, attacker);
      res = true;
    }
  return res;
}

static TString getName(const Effects::RemoveFurniture& e, const ContentFactory* c) {
  return TSentence("REMOVE_FURNITURE_EFFECT_NAME", c->furniture.getData(e.type).getName());
}

static TString getDescription(const Effects::RemoveFurniture& e, const ContentFactory* c) {
  return TSentence("REMOVE_FURNITURE_EFFECT_DESCRIPTION", c->furniture.getData(e.type).getName());
}

static bool apply(const Effects::RemoveFurniture& e, Position pos, Creature*) {
  if (auto f = pos.getFurniture(e.type)) {
    pos.removeFurniture(f->getLayer());
    return true;
  }
  return false;
}

static bool applyToCreature(const Effects::Heal& e, Creature* c, Creature*) {
  if (c->getBody().canHeal(e.healthType, c->getGame()->getContentFactory())) {
    bool res = false;
    res |= c->heal(e.amount);
    res |= c->removeEffect(BuffId("BLEEDING"));
    if (e.amount > 0.5)
      c->addFX(FXInfo(FXName::CIRCULAR_SPELL, Color::LIGHT_GREEN));
    return res;
  } else
    return false;
}

static int getPrice(const Effects::Heal, const ContentFactory*) {
  return 8;
}

static bool applyToCreature(const Effects::Bleed& e, Creature* c, Creature*) {
  c->getBody().bleed(c, e.amount);
  if (c->getBody().getHealth() <= 0) {
    c->you(MsgType::DIE_OF, e.deathReason);
    c->dieWithLastAttacker();
  }
  return true;
}

static bool isConsideredHostile(const Effects::Bleed&, const Creature* victim) {
  return true;
}

static TString getName(const Effects::Bleed&, const ContentFactory*) {
  return TStringId("BLEEDING_EFFECT_NAME");
}

static bool isOffensive(const Effects::Bleed&) {
  return true;
}

static TString getDescription(const Effects::Bleed&, const ContentFactory*) {
  return TStringId("BLEEDING_EFFECT_DESCRIPTION");
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::Bleed& e, const Creature* victim, bool isEnemy) {
  return isEnemy ? 1 : -1;
}

static optional<MinionEquipmentType> getMinionEquipmentType(const Effects::Heal& e) {
  if (e.healthType == HealthType::FLESH)
    return MinionEquipmentType::HEALING;
  else
    return MinionEquipmentType::MATERIALIZATION;
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::Heal& e, const Creature* victim, bool isEnemy) {
  auto& body = victim->getBody();
  if (body.canHeal(e.healthType, victim->getGame()->getContentFactory()) &&
      (body.getHealth() < 0.5 || isConsideredInDanger(victim)))
    return isEnemy ? -1 : 1;
  return 0;
}

static TString getName(const Effects::Heal& e, const ContentFactory*) {
  switch (e.healthType) {
    case HealthType::FLESH: return TStringId("HEALING_EFFECT_NAME");
    case HealthType::SPIRIT: return TStringId("MATERIALIZATION_EFFECT_NAME");
  }
}

static TString getDescription(const Effects::Heal& e, const ContentFactory*) {
  switch (e.healthType) {
    case HealthType::FLESH: return TStringId("HEALING_EFFECT_DESCRIPTION");
    case HealthType::SPIRIT: return TStringId("MATERIALIZATION_EFFECT_DESCRIPTION");
  }
}

static Color getColor(const Effects::Heal& e, const ContentFactory* f) {
  switch (e.healthType) {
    case HealthType::FLESH: return Color::RED;
    case HealthType::SPIRIT: return Color::PURPLE;
  }
}

static TString getName(const Effects::SetFurnitureOnFire&, const ContentFactory*) {
  return TStringId("SET_FURNITURE_ON_FIRE_EFFECT_NAME");
}

static bool isOffensive(const Effects::SetFurnitureOnFire&) {
  return true;
}

static int getPrice(const Effects::SetFurnitureOnFire&, const ContentFactory*) {
  return 12;
}

static TString getDescription(const Effects::SetFurnitureOnFire&, const ContentFactory*) {
  return TStringId("SET_FURNITURE_ON_FIRE_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::SetFurnitureOnFire& a, Position pos, Creature* attacker) {
  bool ret = false;
  for (auto& f : pos.getFurniture())
    if (f->getFire())
      ret |= pos.modFurniture(f->getLayer())->fireDamage(pos, true);
  return ret;
}

static TString getName(const Effects::ClaimTile&, const ContentFactory*) {
  return TStringId("CLAIM_TILE_EFFECT_NAME");
}

static TString getDescription(const Effects::ClaimTile&, const ContentFactory*) {
  return TStringId("CLAIM_TILE_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::ClaimTile& a, Position pos, Creature* attacker) {
  if (auto col = pos.getGame()->getPlayerCollective())
    if (!col->getTerritory().contains(pos) && col->canClaimSquare(pos)) {
      col->claimSquare(pos);
      return true;
    }
  return false;
}

static TString getName(const Effects::Fire&, const ContentFactory*) {
  return TStringId("FIRE_EFFECT_NAME");
}

static bool isOffensive(const Effects::Fire&) {
  return true;
}

static int getPrice(const Effects::Fire&, const ContentFactory*) {
  return 12;
}

static TString getDescription(const Effects::Fire&, const ContentFactory*) {
  return TStringId("FIRE_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::Fire& a, Position pos, Creature* attacker) {
  pos.getGame()->addEvent(EventInfo::FX{pos, {FXName::FIREBALL_SPLASH}});
  return pos.fireDamage(a.amount.value_or(attacker ? attacker->getAttr(AttrType("FIRE_DAMAGE")) : 10), attacker);
}

static optional<ViewId> getProjectile(const Effects::Fire&) {
  return ViewId("fireball");
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::Fire&, const Creature* victim, bool isEnemy) {
  if (!victim->isAffected(BuffId("FIRE_IMMUNITY")))
    return isEnemy ? 1 : -1;
  return 0;
}

static bool isConsideredHostile(const Effects::Fire&, const Creature* victim) {
  return !victim->isAffected(BuffId("FIRE_IMMUNITY"));
}

static TString getName(const Effects::Ice&, const ContentFactory*) {
  return TStringId("ICE_EFFECT_NAME");
}

static bool isOffensive(const Effects::Ice&) {
  return true;
}

static TString getDescription(const Effects::Ice&, const ContentFactory*) {
  return TStringId("ICE_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::Ice& a, Position pos, Creature* attacker) {
  return pos.iceDamage(a.amount.value_or(attacker ? attacker->getAttr(AttrType("COLD_DAMAGE")) : 10), attacker);
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::Ice&, const Creature* victim, bool isEnemy) {
  if (!victim->isAffected(BuffId("COLD_IMMUNITY")))
    return isEnemy ? 1 : -1;
  return 0;
}

static bool isConsideredHostile(const Effects::Ice&, const Creature* victim) {
  return !victim->isAffected(BuffId("COLD_IMMUNITY"));
}

static TString getName(const Effects::ReviveCorpse&, const ContentFactory*) {
  return TStringId("REVIVE_CORPSE_EFFECT_NAME");
}

static bool isOffensive(const Effects::ReviveCorpse&) {
  return true;
}

static TString getDescription(const Effects::ReviveCorpse&, const ContentFactory*) {
  return TStringId("REVIVE_CORPSE_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::ReviveCorpse& effect, Position pos, Creature* attacker) {
  for (auto& item : pos.getItems())
    if (auto info = item->getCorpseInfo())
      if (info->canBeRevived)
        for (auto& dead : pos.getModel()->getDeadCreatures())
          if (dead->getUniqueId() == info->victim) {
            for (auto id : effect.summoned) {
              auto summoned = Effect::summon(attacker, id, 1, TimeInterval(effect.ttl));
              if (!summoned.empty()) {
                for (auto& c : summoned) {
//                  c->getName().addBarePrefix(dead->getName().bare());
                  attacker->verb(TStringId("YOU_HAVE_REVIVED"), TStringId("HAS_REVIVED"), c->getName().a());
                }
                pos.removeItems({item});
                return true;
              }
            }
            attacker->message(TSentence("THE_SPELL_FAILED"));
            return false;
          }
  return false;
}

static TString getName(const Effects::EmitGas& m, const ContentFactory* f) {
  return f->tileGasTypes.at(m.type).name;
}

static bool isOffensive(const Effects::EmitGas&) {
  return true;
}

static Color getColor(const Effects::EmitGas& e, const ContentFactory* f) {
  return f->tileGasTypes.at(e.type).color;
}

static TString getDescription(const Effects::EmitGas& m, const ContentFactory* f) {
  if (auto info = getReferenceMaybe(f->tileGasTypes, m.type))
    return TSentence("EMIT_GAS_DESCRIPTION", info->name);
  else {
    string out;
    for (auto& elem : f->tileGasTypes)
      out.append(" "_s + elem.first.data());
    FATAL << m.type.data() << " not found (" << out << ")";
    fail();
  }
}

static bool apply(const Effects::EmitGas& m, Position pos, Creature*) {
  pos.addGas(m.type, m.amount);
  auto& info = pos.getGame()->getContentFactory()->tileGasTypes.at(m.type);
  pos.globalMessage(TSentence("GAS_RELEASED", info.name));
  pos.unseenMessage(TStringId("GAS_RELEASED_UNSEEN"));
  return true;
}

static optional<MinionEquipmentType> getMinionEquipmentType(const Effects::EmitGas& e) {
  return MinionEquipmentType::COMBAT_ITEM;
}

static EffectAIIntent shouldAIApply(const Effects::EmitGas& e, const Creature* caster, Position pos) {
  auto factory = pos.getGame()->getContentFactory();
  if (pos.getGasAmount(e.type) > 0.3)
    return 0;
  auto& effect = factory->tileGasTypes.at(e.type).effect;
  return effect ? effect->shouldAIApply(caster, pos) : 0;
}

static TString getName(const Effects::PlaceFurniture& e, const ContentFactory* c) {
  return c->furniture.getData(e.furniture).getName();
}

static TString getDescription(const Effects::PlaceFurniture& e, const ContentFactory* c) {
  return TSentence("CREATE_FURNITURE_DESCRIPTION", getName(e, c));
}

static bool apply(const Effects::PlaceFurniture& summon, Position pos, Creature* attacker) {
  auto f = pos.getGame()->getContentFactory()->furniture.getFurniture(summon.furniture,
      attacker ? attacker->getTribeId() : TribeId::getMonster());
  auto layer = f->getLayer();
  auto ref = f.get();
  pos.addFurniture(std::move(f));
  if (pos.getFurniture(layer) == ref)
    ref->onConstructedBy(pos, attacker);
  return true;
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::PlaceFurniture& f, const Creature* victim, bool isEnemy) {
  return victim->getGame()->getContentFactory()->furniture.getData(f.furniture).isHostileSpell() &&
      isConsideredInDanger(victim) ? 1 : 0;
}

static TString getName(const Effects::ModifyFurniture& e, const ContentFactory* c) {
  return c->furniture.getData(e.furniture).getName();
}

static TString getDescription(const Effects::ModifyFurniture& e, const ContentFactory* c) {
  return TSentence("CREATE_FURNITURE_DESCRIPTION", getName(e, c));
}

static bool apply(const Effects::ModifyFurniture& summon, Position pos, Creature*) {
  auto data = pos.getGame()->getContentFactory()->furniture.getData(summon.furniture);
  if (auto f = pos.modFurniture(data.getLayer())) {
    auto keepType = f->getType();
    auto keepTribe = f->getTribe();
    *f = data;
    f->setType(keepType);
    f->setTribe(keepTribe);
    return true;
  }
  return false;
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::ModifyFurniture& f, const Creature* victim, bool isEnemy) {
  return victim->getGame()->getContentFactory()->furniture.getData(f.furniture).isHostileSpell() &&
      isConsideredInDanger(victim) ? 1 : 0;
}

static TString getName(const Effects::DropItems&, const ContentFactory* c) {
  return TSentence("CREATE_ITEMS_NAME");
}

static TString getDescription(const Effects::DropItems&, const ContentFactory* c) {
  return TSentence("CREATE_ITEMS_DESCRIPTION");
}

static bool apply(const Effects::DropItems& effect, Position pos, Creature*) {
  pos.dropItems(effect.type.get(Random.get(effect.count), pos.getGame()->getContentFactory()));
  return true;
}

static TString getName(const Effects::DropItemList&, const ContentFactory* c) {
  return TSentence("CREATE_ITEMS_NAME");
}

static TString getDescription(const Effects::DropItemList&, const ContentFactory* c) {
  return TSentence("CREATE_ITEMS_DESCRIPTION");
}

static bool apply(const Effects::DropItemList& listId, Position pos, Creature*) {
  auto factory = pos.getGame()->getContentFactory();
  auto list = factory->itemFactory.get(listId);
  pos.dropItems(list.random(factory, pos.getModelDifficulty()));
  return true;
}

template <>
double getSteedChance<Effects::Damage>() {
  return 0.5;
}

static bool applyToCreature(const Effects::Damage& e, Creature* c, Creature* attacker) {
  CHECK(attacker) << "Unknown attacker";
  int value = attacker->getAttr(e.attr) + attacker->getSpecialAttr(e.attr, c);
  bool result = c->takeDamage(Attack(attacker, Random.choose<AttackLevel>(), e.attackType, value, e.attr));
  if (auto& fx = c->getGame()->getContentFactory()->attrInfo.at(e.attr).meleeFX)
    c->addFX(*fx);
  return result;
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::Damage&, const Creature* victim, bool isEnemy) {
  return isEnemy ? 1 : -1;
}

static optional<FXInfo> getProjectileFX(const Effects::Damage&) {
  return {FXName::MAGIC_MISSILE};
}

static optional<ViewId> getProjectile(const Effects::Damage&) {
  return ViewId("force_bolt");
}

static bool isConsideredHostile(const Effects::Damage&, const Creature*) {
  return true;
}

static TString getName(const Effects::Damage& e, const ContentFactory* f) {
  return f->attrInfo.at(e.attr).name;
}

static bool isOffensive(const Effects::Damage&) {
  return true;
}

static TString getDescription(const Effects::Damage& e, const ContentFactory* f) {
  return TSentence("DAMAGE_EFFECT_DESCRIPTION", f->attrInfo.at(e.attr).name);
}

template <>
double getSteedChance<Effects::FixedDamage>() {
  return 0.5;
}

static bool applyToCreature(const Effects::FixedDamage& e, Creature* c, Creature*) {
  bool result = c->takeDamage(Attack(nullptr, Random.choose<AttackLevel>(), e.attackType, e.value, e.attr));
  if (auto& fx = c->getGame()->getContentFactory()->attrInfo.at(e.attr).meleeFX)
    c->addFX(*fx);
  return result;
}

static TString getName(const Effects::FixedDamage& e, const ContentFactory* f) {
  return TSentence("FIXED_DAMAGE_EFFECT_NAME", f->attrInfo.at(e.attr).name, TString(e.value));
}

static bool isOffensive(const Effects::FixedDamage&) {
  return true;
}

static TString getDescription(const Effects::FixedDamage& e, const ContentFactory* f) {
  return TSentence("FIXED_DAMAGE_EFFECT_DESCRIPTION", f->attrInfo.at(e.attr).name, TString(e.value));
}

static bool applyToCreature(const Effects::InjureBodyPart& e, Creature* c, Creature* attacker) {
  if (c->getBody().injureBodyPart(c, e.part, false)) {
    c->you(MsgType::DIE);
    c->dieWithAttacker(attacker);
  }
  return false;
}

static TString getName(const Effects::InjureBodyPart& e, const ContentFactory*) {
  return TSentence("INJURE_BODY_PART_EFFECT_NAME", ::getName(e.part));
}

static bool isOffensive(const Effects::InjureBodyPart&) {
  return true;
}

static TString getDescription(const Effects::InjureBodyPart& e, const ContentFactory*) {
  return TSentence("INJURE_BODY_PART_EFFECT_DESCRIPTION", ::getName(e.part));
}

static bool applyToCreature(const Effects::LoseBodyPart& e, Creature* c, Creature* attacker) {
  if (c->getBody().injureBodyPart(c, e.part, true)) {
    c->you(MsgType::DIE);
    c->dieWithAttacker(attacker);
  }
  return false;
}

static TString getName(const Effects::LoseBodyPart& e, const ContentFactory*) {
  return TSentence("LOSE_BODY_PART_EFFECT_NAME", ::getName(e.part));
}

static bool isOffensive(const Effects::LoseBodyPart&) {
  return true;
}

static TString getDescription(const Effects::LoseBodyPart& e, const ContentFactory*) {
  return TSentence("LOSE_BODY_PART_EFFECT_DESCRIPTION", ::getName(e.part));
}

static bool applyToCreature(const Effects::AddBodyPart& p, Creature* c, Creature* attacker) {
  c->getBody().addBodyPart(p.part, p.count);
  if (p.attack) {
    c->getBody().addIntrinsicAttack(p.part, IntrinsicAttack{*p.attack, true});
    c->getBody().initializeIntrinsicAttack(c->getGame()->getContentFactory());
  }
  return true;
}

static TString getName(const Effects::AddBodyPart& e, const ContentFactory*) {
  if (e.count > 1)
    return TSentence("EXTRA_BODY_PARTS_EFFECT_NAME", makePlural(::getName(e.part)));
  else
    return TSentence("EXTRA_BODY_PART_EFFECT_NAME", ::getName(e.part));
}

static TString getDescription(const Effects::AddBodyPart& e, const ContentFactory*) {
  if (e.count > 1)
    return TSentence("EXTRA_BODY_PARTS_EFFECT_DESCRIPTION", makePlural(::getName(e.part)));
  else
    return TSentence("EXTRA_BODY_PART_EFFECT_DESCRIPTION", ::getName(e.part));
}

static bool applyToCreature(const Effects::AddIntrinsicAttack& p, Creature* c, Creature* attacker) {
  c->getBody().addIntrinsicAttack(p.part, IntrinsicAttack{p.attack, true});
  c->getBody().initializeIntrinsicAttack(c->getGame()->getContentFactory());
  return true;
}

static TString getName(const Effects::AddIntrinsicAttack& e, const ContentFactory*) {
  return TSentence("ADD_INTRINSIC_ATTACK_EFFECT_NAME", ::getName(e.part));
}

static TString getDescription(const Effects::AddIntrinsicAttack& e, const ContentFactory*) {
  return TSentence("ADD_INTRINSIC_ATTACK_EFFECT_DESCRIPTION", ::getName(e.part));
}

static bool applyToCreature(const Effects::MakeHumanoid&, Creature* c, Creature*) {
  bool ret = !c->getBody().isHumanoid();
  c->getBody().setHumanoid(true);
  return ret;
}

static TString getName(const Effects::MakeHumanoid&, const ContentFactory*) {
  return TSentence("MAKE_HUMANOID_EFFECT_NAME");
}

static TString getDescription(const Effects::MakeHumanoid&, const ContentFactory*) {
  return TSentence("MAKE_HUMANOID_EFFECT_DESCRIPTION");
}

static bool applyToCreature(const Effects::RegrowBodyPart& e, Creature* c, Creature*) {
  return c->getBody().healBodyParts(c, e.maxCount);
}

static TString getName(const Effects::RegrowBodyPart&, const ContentFactory*) {
  return TSentence("REGROW_BODY_PART_EFFECT_NAME");
}

static TString getDescription(const Effects::RegrowBodyPart&, const ContentFactory*) {
  return TSentence("REGROW_BODY_PART_EFFECT_DESCRIPTION");
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::RegrowBodyPart&, const Creature* victim, bool isEnemy) {
  for (auto part : ENUM_ALL(BodyPart))
    if (victim->getBody().numLost(part) + victim->getBody().numInjured(part) > 0)
      return isEnemy ? -1 : 1;
  return 0;
}

static TString getDescription(const Effects::Area& e, const ContentFactory* factory) {
  return TSentence("AREA_EFFECT_DESCRIPTION", TString(e.radius), e.effect->getDescription(factory));
}

static TString getName(const Effects::Area& e, const ContentFactory* factory) {
  return TSentence("AREA_EFFECT_NAME", e.effect->getName(factory));
}

static bool apply(const Effects::Area& area, Position pos, Creature* attacker) {
  bool res = false;
  for (auto v : pos.getRectangle(Rectangle::centered(area.radius)))
    res |= area.effect->apply(v, attacker);
  return res;
}

template <typename Range>
EffectAIIntent considerArea(const Creature* caster, const Range& range, const Effect& effect) {
  auto allRes = 0;
  auto badRes = 0;
  for (auto v : range) {
    auto res = effect.shouldAIApply(caster, v);
    if (res < 0)
      badRes += res;
    else
      allRes += res;
  }
  return badRes < 0 ? badRes : allRes;
};

static EffectAIIntent shouldAIApply(const Effects::Area& a, const Creature* caster, Position pos) {
  return considerArea(caster, pos.getRectangle(Rectangle::centered(a.radius)), *a.effect);
}

static TString getName(const Effects::CustomArea& e, const ContentFactory* f) {
  return TSentence("CUSTOM_AREA_EFFECT_NAME", e.effect->getName(f));
}

static TString getDescription(const Effects::CustomArea& e, const ContentFactory* factory) {
  return TSentence("CUSTOM_AREA_EFFECT_DESCRIPTION", e.effect->getDescription(factory));
}

static Vec2 rotate(Vec2 v, Vec2 r) {
  return Vec2(v.x * r.x - v.y * r.y, v.x * r.y + v.y * r.x);
}

vector<Position> Effects::CustomArea::getTargetPos(const Creature* attacker, Position targetPos) const {
  Vec2 orientation = Vec2(1, 0);
  if (attacker)
    orientation = attacker->getPosition().getDir(targetPos).getBearing();
  vector<Position> ret;
  for (auto v : positions)
    ret.push_back(targetPos.plus(rotate(v, orientation)));
  return ret;
}

static EffectAIIntent shouldAIApply(const Effects::CustomArea& a, const Creature* caster, Position pos) {
  return considerArea(caster, a.getTargetPos(caster, pos), *a.effect);
}

static bool apply(const Effects::CustomArea& area, Position pos, Creature* attacker) {
  bool res = false;
  for (auto& v : area.getTargetPos(attacker, pos))
    res |= area.effect->apply(v, attacker);
  return res;
}

static bool applyToCreature(const Effects::Suicide& e, Creature* c, Creature* attacker) {
  if (!c->isAffected(BuffId("INVULNERABLE"))) {
    c->you(e.message);
    c->dieWithAttacker(attacker);
    return true;
  }
  return false;
}

static int getPrice(const Effects::Suicide&, const ContentFactory*) {
  return 8;
}

static bool canAutoAssignMinionEquipment(const Effects::Suicide&) {
  return false;
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::Suicide&, const Creature* victim, bool isEnemy) {
  return isEnemy ? 1 : -1;
}

static TString getName(const Effects::Suicide&, const ContentFactory*) {
  return TSentence("SUICIDE_EFFECT_NAME");
}

static TString getDescription(const Effects::Suicide&, const ContentFactory*) {
  return TSentence("SUICIDE_EFFECT_DESCRIPTION");
}

static bool applyToCreature(const Effects::Wish&, Creature* c, Creature* attacker) {
  c->getController()->grantWish(
      (attacker ? capitalFirst(TSentence("GRANTS_YOU_A_WISH", attacker->getName().the()))
          : TSentence("YOU_ARE_GRANTED_A_WISH")));
  return true;
}

static EffectAIIntent shouldAIApply(const Effects::Wish&, const Creature* caster, Position pos) {
  auto victim = pos.getCreature();
  if (victim && victim->isPlayer() && !caster->isEnemy(victim))
    return 1;
  else
    return -1;
}

static TString getName(const Effects::Wish&, const ContentFactory*) {
  return TSentence("WISH_EFFECT_NAME");
}

static TString getDescription(const Effects::Wish&, const ContentFactory*) {
  return TSentence("WISH_EFFECT_DESCRIPTION");
}

static TString combineNames(const ContentFactory* f, const vector<Effect>& effects) {
  return combineWithCommas(effects.transform([f](auto& e) { return e.getName(f); }));
}

static TString getName(const Effects::Chain& e, const ContentFactory* f) {
  return combineNames(f, e.effects);
}

static TString getDescription(const Effects::Chain& e, const ContentFactory* f) {
  return TString();
}

static bool apply(const Effects::Chain& chain, Position pos, Creature* attacker) {
  bool res = false;
  for (auto& e : chain.effects)
    res |= e.apply(pos, attacker);
  return res;
}

static bool applyToCreaturePreference(const Effects::Chain& chain, Creature* c, Creature* attacker) {
  bool res = false;
  for (auto& e : chain.effects)
    res |= e.applyToCreature(c, attacker);
  return res;
}

static optional<MinionEquipmentType> getMinionEquipmentType(const Effects::Chain& c) {
  for (auto& e : c.effects)
    if (auto t = e.getMinionEquipmentType())
      return t;
  return none;
}

static bool canAutoAssignMinionEquipment(const Effects::Chain& c) {
  for (auto& e : c.effects)
    if (!e.canAutoAssignMinionEquipment())
      return false;
  return true;
}

static bool isOffensive(const Effects::Chain& c) {
  for (auto& e : c.effects)
    if (e.isOffensive())
      return true;
  return false;
}

static void scale(Effects::Chain& e, double value, const ContentFactory* f) {
  for (auto& e : e.effects)
    e.scale(value, f);
}

static EffectAIIntent shouldAIApply(const Effects::Chain& chain, const Creature* caster, Position pos) {
  auto allRes = 0;
  auto badRes = 0;
  for (auto& e : chain.effects) {
    auto res = e.shouldAIApply(caster, pos);
    if (res < 0)
      badRes += res;
    else
      allRes += res;
  }
  return badRes < 0 ? badRes : allRes;
}

static bool apply(const Effects::ChainUntilFail& chain, Position pos, Creature* attacker) {
  bool res = false;
  for (auto& e : chain.effects) {
    auto b = e.apply(pos, attacker);
    res |= b;
    if (!b)
      break;
  }
  return res;
}

static TString getName(const Effects::FirstSuccessful& e, const ContentFactory* f) {
  return combineNames(f, e.effects);
}

static TString getDescription(const Effects::FirstSuccessful& e, const ContentFactory* f) {
  return TString();
}

static bool apply(const Effects::FirstSuccessful& chain, Position pos, Creature* attacker) {
  for (auto& e : chain.effects)
    if (e.apply(pos, attacker))
      return true;
  return false;
}

static TString getName(const Effects::ChooseRandom& e, const ContentFactory* f) {
  return e.effects[0].getName(f);
}

static TString getDescription(const Effects::ChooseRandom& e, const ContentFactory* f) {
  return e.effects[0].getDescription(f);
}

static bool apply(const Effects::ChooseRandom& r, Position pos, Creature* attacker) {
  return r.effects[Random.get(r.effects.size())].apply(pos, attacker);
}

static TString getName(const Effects::ChooseRandomUntilSuccessful& e, const ContentFactory* f) {
  return e.effects[0].getName(f);
}

static TString getDescription(const Effects::ChooseRandomUntilSuccessful& e, const ContentFactory* f) {
  return e.effects[0].getDescription(f);
}

static bool apply(const Effects::ChooseRandomUntilSuccessful& r, Position pos, Creature* attacker) {
  for (auto e : Random.permutation(r.effects))
    if (e.apply(pos, attacker))
      return true;
  return false;
}

static TString getName(const Effects::Message&, const ContentFactory*) {
  return TStringId("MESSAGE_EFFECT_NAME");
}

static TString getDescription(const Effects::Message& e, const ContentFactory*) {
  return e.text;
}

static bool apply(const Effects::Message& msg, Position pos, Creature*) {
  pos.globalMessage(PlayerMessage(msg.text, msg.priority));
  return true;
}

static TString getName(const Effects::UnseenMessage&, const ContentFactory*) {
  return TStringId("MESSAGE_EFFECT_NAME");
}

static TString getDescription(const Effects::UnseenMessage& e, const ContentFactory*) {
  return e.text;
}

static bool apply(const Effects::UnseenMessage& msg, Position pos, Creature*) {
  pos.unseenMessage(PlayerMessage(msg.text, msg.priority));
  return true;
}

static TString getName(const Effects::CreatureMessage&, const ContentFactory*) {
  return TStringId("MESSAGE_EFFECT_NAME");
}

static TString getDescription(const Effects::CreatureMessage&, const ContentFactory*) {
  return TString();
}

static bool applyToCreature(const Effects::CreatureMessage& e, Creature* c, Creature*) {
  if (e.secondPerson.text.contains<TSentence>())
    c->verb(e.secondPerson.text.getReferenceMaybe<TSentence>()->id,
        e.thirdPerson.text.getReferenceMaybe<TSentence>()->id, e.priority);
  return true;
}

static TString getName(const Effects::PlayerMessage&, const ContentFactory*) {
  return TStringId("MESSAGE_EFFECT_NAME");
}

static TString getDescription(const Effects::PlayerMessage&, const ContentFactory*) {
  return TString();
}

static bool applyToCreature(const Effects::PlayerMessage& e, Creature* c, Creature*) {
  c->privateMessage(::PlayerMessage(e.text, e.priority));
  return true;
}

static bool applyToCreature(const Effects::GrantAbility& e, Creature* c, Creature*) {
  bool ret = !c->getSpellMap().contains(e.id);
  c->getSpellMap().add(*c->getGame()->getContentFactory()->getCreatures().getSpell(e.id), AttrType("DAMAGE"), 0);
  return ret;
}

static TString getName(const Effects::GrantAbility& e, const ContentFactory* f) {
  return TSentence("GRANT_ABILITY_EFFECT_NAME", f->getCreatures().getSpell(e.id)->getName(f));
}

static TString getDescription(const Effects::GrantAbility& e, const ContentFactory* f) {
  return TSentence("GRANT_ABILITY_EFFECT_DESCRIPTION", f->getCreatures().getSpell(e.id)->getName(f));
}

static bool applyToCreature(const Effects::AddSpellSchool& id, Creature* c, Creature*) {
  if (!c->getAttributes().getSpellSchools().contains(id)) {
    auto& creatures = c->getGame()->getContentFactory()->getCreatures();
    auto& school = creatures.getSpellSchools().at(id);
    for (auto& spell : school.spells)
      c->getSpellMap().add(*creatures.getSpell(spell.first), school.expType, spell.second);
    c->getAttributes().addSpellSchool(id);
    return true;
  }
  return false;
}

static TString getName(const Effects::AddSpellSchool& id, const ContentFactory* f) {
  auto name = f->getCreatures().getSpellSchools().at(id).name.value_or(TString(string(id.data())));
  return TSentence("SPELL_SCHOOL_EFFECT_NAME", std::move(name));
}

static TString getDescription(const Effects::AddSpellSchool& id, const ContentFactory* f) {
  auto name = f->getCreatures().getSpellSchools().at(id).name.value_or(TString(string(id.data())));
  return TSentence("SPELL_SCHOOL_EFFECT_DESCRIPTION", std::move(name));
}

static bool applyToCreature(const Effects::Polymorph& e, Creature* c, Creature*) {
  auto& factory = c->getGame()->getContentFactory()->getCreatures();
  auto attributes = [&] {
    if (e.into)
      return factory.getAttributesFromId(*e.into);
    for (auto id : Random.permutation(factory.getAllCreatures())) {
      auto attr = factory.getAttributesFromId(id);
      if (attr.getBody().getMaterial() == BodyMaterialId("FLESH") && !attr.isAffectedPermanently(LastingEffect::PLAGUE))
        return attr;
    }
    fail();
  }();
  auto spells = factory.getSpellMap(attributes);
  auto origName = c->getName().the();
  if (auto timeout = e.timeout) { // make a copy as it's possible e is invalidated in pushAttributes()
    c->pushAttributes(std::move(attributes), std::move(spells));
    c->addEffect(LastingEffect::POLYMORPHED, *timeout);
  } else {
    c->setAttributes(std::move(attributes), std::move(spells));
    for (auto& elem : c->specialTraits)
      applySpecialTrait(*c->getGlobalTime(), elem, c, c->getGame()->getContentFactory());
  }
  c->secondPerson(PlayerMessage(TSentence("YOU_POLYMORPH_INTO", c->getName().a()), MessagePriority::HIGH));
  c->thirdPerson(PlayerMessage(TSentence("POLYMORPHS_INTO", origName, c->getName().a())));
  summonFX(c->getPosition());
  return true;
}

static TString getName(const Effects::Polymorph& e, const ContentFactory* f) {
  return !!e.timeout ? TStringId("TEMPORARY_POLYMORPH_EFFECT_NAME") : TStringId("PERMANENT_POLYMORPH_EFFECT_NAME");
}

static TString getDescription(const Effects::Polymorph& e, const ContentFactory* f) {
  if (e.into)
    return TSentence("POLYMORPH_INTO_EFFECT_DESCRIPTION", f->getCreatures().getName(*e.into));
  else
    return TStringId("RANDOM_POLYMORPH_EFFECT_DESCRIPTION");
}

static bool applyToCreature(const Effects::SetCreatureName& e, Creature* c, Creature*) {
  c->getName().setBare(e.value);
  return true;
}

static TString getName(const Effects::SetCreatureName& e, const ContentFactory* f) {
  return TStringId("CHANGE_NAME_EFFECT_NAME");
}

static TString getDescription(const Effects::SetCreatureName& e, const ContentFactory* f) {
  return TSentence("CHANGE_NAME_EFFECT_DESCRIPTION", e.value);
}

static bool applyToCreature(const Effects::SetViewId& e, Creature* c, Creature*) {
  c->setViewId(e.value);
  return true;
}

static TString getName(const Effects::SetViewId& e, const ContentFactory* f) {
  return TStringId("CHANGE_SPRITE_EFFECT_NAME");
}

static TString getDescription(const Effects::SetViewId& e, const ContentFactory* f) {
  return TStringId("CHANGE_SPRITE_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::UI& e, Position pos, Creature*) {
  auto view = pos.getGame()->getView();
  if (auto c = pos.getCreature())
    if (c->isPlayer())
      view->updateView(dynamic_cast<Player*>(c->getController()), true);
  view->scriptedUI(e.id, e.data);
  return true;
}

static TString getName(const Effects::UI& e, const ContentFactory* f) {
  return TStringId("UI_EFFECT_NAME");
}

static TString getDescription(const Effects::UI& e, const ContentFactory* f) {
  return TStringId("UI_EFFECT_DESCRIPTION");
}

static bool applyToCreature(const Effects::RemoveAbility& e, Creature* c, Creature*) {
  auto id = e.id;
  if (id.data() == "SPELLS_ENNOBLEMENT_EFFECT"_s)
    id = SpellId("ennoblement");
  if (id.data() == "SPELLS_BYPASS_ALLIES_EFFECT"_s)
    id = SpellId("bypass allies");
  bool ret = c->getSpellMap().contains(id);
  c->getSpellMap().remove(id);
  return ret;
}

static TString getName(const Effects::RemoveAbility& e, const ContentFactory* f) {
  return TSentence("REMOVE_ABILITY_EFFECT_NAME", f->getCreatures().getSpell(e.id)->getName(f));
}

static TString getDescription(const Effects::RemoveAbility& e, const ContentFactory* f) {
  return TSentence("REMOVE_ABILITY_EFFECT_DESCRIPTION", f->getCreatures().getSpell(e.id)->getName(f));
}

static TString getName(const Effects::Caster& e, const ContentFactory* f) {
  return e.effect->getName(f);
}

static TString getDescription(const Effects::Caster& e, const ContentFactory* f) {
  return e.effect->getDescription(f);
}

static bool apply(const Effects::Caster& e, Position, Creature* attacker) {
  if (attacker)
    return e.effect->apply(attacker->getPosition(), attacker);
  return false;
}

template <>
double getSteedChance<Effects::ApplyToSteed>() {
  return 1.0;
}

static bool applyToCreature(const Effects::ApplyToSteed& e, Creature* c, Creature* attacker) {
  return e.effect->applyToCreature(c, attacker);
}

static TString getName(const Effects::GenericModifierEffect& e, const ContentFactory* f) {
  return e.effect->getName(f);
}

static TString getDescription(const Effects::GenericModifierEffect& e, const ContentFactory* f) {
  return e.effect->getDescription(f);
}

static bool apply(const Effects::GenericModifierEffect& e, Position pos, Creature* attacker) {
  return e.effect->apply(pos, attacker);
}

static EffectAIIntent shouldAIApply(const Effects::GenericModifierEffect& e, const Creature* caster, Position pos) {
  return e.effect->shouldAIApply(caster, pos);
}

static optional<FXInfo> getProjectileFX(const Effects::GenericModifierEffect& e) {
  return e.effect->getProjectileFX();
}

static optional<ViewId> getProjectile(const Effects::GenericModifierEffect& e) {
  return e.effect->getProjectile();
}

static int getPrice(const Effects::GenericModifierEffect& e, const ContentFactory* f) {
  return e.effect->getPrice(f);
}

static void scale(Effects::GenericModifierEffect& e, double value, const ContentFactory* f) {
  e.effect->scale(value, f);
}

static bool canAutoAssignMinionEquipment(const Effects::GenericModifierEffect& e) {
  return e.effect->canAutoAssignMinionEquipment();
}

static bool isOffensive(const Effects::GenericModifierEffect& e) {
  return e.effect->isOffensive();
}

static optional<MinionEquipmentType> getMinionEquipmentType(const Effects::GenericModifierEffect& e) {
  return e.effect->getMinionEquipmentType();
}

static Color getColor(const Effects::GenericModifierEffect& e, const ContentFactory* f) {
  return e.effect->getColor(f);
}

static TString getDescription(const Effects::Chance& e, const ContentFactory* f) {
  return TSentence("CHANCE_EFFECT_DESCRIPTION", e.effect->getDescription(f));
}

static bool apply(const Effects::Chance& e, Position pos, Creature* attacker) {
  if (Random.chance(e.value))
    return e.effect->apply(pos, attacker);
  return false;
}

static bool applyToCreature(const Effects::DoubleTrouble&, Creature* c, Creature*) {
  auto factory = c->getGame()->getContentFactory();
  PCreature copy = factory->getCreatures().makeCopy(c);
  auto ttl = 50_visible;
  for (auto& item : c->getEquipment().getItems())
    if (!item->getResourceId() && !item->isDiscarded() && !item->getEffect()) {
      auto itemCopy = item->getCopy(factory);
      itemCopy->setTimeout(c->getGame()->getGlobalTime() + ttl + 10_visible);
      copy->take(std::move(itemCopy), factory);
    }
  auto cRef = Effect::summonCreatures(c->getPosition(), makeVec(std::move(copy)));
  if (!cRef.empty()) {
    cRef[0]->addEffect(LastingEffect::SUMMONED, ttl, false);
    c->message(::PlayerMessage(TStringId("DOUBLE_TROUBLE_MESSAGE"), MessagePriority::HIGH));
    return true;
  } else {
    c->message(::PlayerMessage(TStringId("THE_SPELL_FAILED"), MessagePriority::HIGH));
    return false;
  }
}

static TString getName(const Effects::DoubleTrouble&, const ContentFactory*) {
  return TStringId("DOUBLE_TROUBLE_EFFECT_NAME");
}

static bool isOffensive(const Effects::DoubleTrouble&) {
  return true;
}

static TString getDescription(const Effects::DoubleTrouble&, const ContentFactory*) {
  return TStringId("DOUBLE_TROUBLE_EFFECT_DESCRIPTION");
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::DoubleTrouble&, const Creature* victim, bool isEnemy) {
  return isConsideredInDanger(victim) ? 1 : 0;
}

static TString getName(const Effects::Blast&, const ContentFactory*) {
  return TStringId("AIR_BLAST_EFFECT_NAME");
}

static bool isOffensive(const Effects::Blast&) {
  return true;
}

static TString getDescription(const Effects::Blast&, const ContentFactory*) {
  return TStringId("AIR_BLAST_EFFECT_DESCRIPTION");
}

static optional<FXInfo> getProjectileFX(const Effects::Blast&) {
  return {FXName::AIR_BLAST};
}

static optional<ViewId> getProjectile(const Effects::Blast&) {
  return ViewId("air_blast");
}

static bool apply(const Effects::Blast&, Position pos, Creature* attacker) {
  constexpr int range = 4;
  CHECK(attacker);
  vector<Position> trajectory;
  auto origin = attacker->getPosition().getCoord();
  for (auto& v : drawLine(origin, pos.getCoord()))
    if (v != origin && v.dist8(origin) <= range) {
      trajectory.push_back(Position(v, pos.getLevel()));
      if (trajectory.back().isDirEffectBlocked(attacker->getVision().getId()))
        break;
    }
  for (int i : All(trajectory).reverse())
    airBlast(attacker, attacker->getPosition(), trajectory[i], pos);
  return true;
}

static TString getName(const Effects::DirectedBlast&, const ContentFactory*) {
  return TStringId("DIRECTED_BLAST_EFFECT_NAME");
}

static bool isOffensive(const Effects::DirectedBlast&) {
  return true;
}

static TString getDescription(const Effects::DirectedBlast&, const ContentFactory*) {
  return TStringId("DIRECTED_BLAST_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::DirectedBlast& b, Position pos, Creature*) {
  vector<Position> trajectory;
  for (auto v = pos.plus(b.dir); v != pos.plus(b.dir * (b.length + 1)); v = v.plus(b.dir)) {
    trajectory.push_back(v);
    if (trajectory.back().isDirEffectBlocked(VisionId::NORMAL))
      break;
  }
  for (int i : All(trajectory).reverse())
    airBlast(nullptr, pos, trajectory[i], pos.plus(b.dir * (b.length + 1)));
  pos.getGame()->addEvent(
      EventInfo::Projectile{{FXName::AIR_BLAST}, ViewId("air_blast"), pos, pos.plus(b.dir * b.length), SoundId("SPELL_BLAST")});
  return true;
}

static bool pullCreature(Creature* victim, const vector<Position>& trajectory) {
  auto victimPos = victim->getPosition();
  optional<Position> target;
  for (auto& v : trajectory) {
    if (v == victimPos)
      break;
    if (v.canEnter(victim)) {
      if (!target)
        target = v;
    } else
      target = none;
  }
  if (target) {
    victim->addEffect(LastingEffect::COLLAPSED, 2_visible);
    victim->displace(victim->getPosition().getDir(*target));
    return true;
  }
  return false;
}

static TString getName(const Effects::Pull&, const ContentFactory*) {
  return TStringId("PULL_EFFECT_NAME");
}

static bool isOffensive(const Effects::Pull&) {
  return true;
}

static TString getDescription(const Effects::Pull&, const ContentFactory*) {
  return TStringId("PULL_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::Pull&, Position pos, Creature* attacker) {
  CHECK(attacker);
  vector<Position> trajectory = drawLine(attacker->getPosition(), pos);
  for (auto& pos : trajectory)
    if (auto c = pos.getCreature())
      return pullCreature(c, trajectory);
  return false;
}

static optional<FXInfo> getProjectileFX(const Effects::Pull&) {
  return FXInfo{FXName::AIR_BLAST}.setReversed();
}

static bool applyToCreature(const Effects::Shove&, Creature* c, Creature* attacker) {
  CHECK(attacker);
  auto origin = attacker->getPosition();
  auto dir = origin.getDir(c->getPosition());
  if (dir.length8() == 1 && c->getPosition().canMoveCreature(dir)) {
    c->displace(dir);
    c->you(MsgType::ARE, TStringId("THROWN_BACK"));
    c->addEffect(LastingEffect::COLLAPSED, 2_visible);
    return true;
  }
  return false;
}

static TString getName(const Effects::Shove&, const ContentFactory*) {
  return TStringId("SHOVE_EFFECT_NAME");
}

static bool isOffensive(const Effects::Shove&) {
  return true;
}

static TString getDescription(const Effects::Shove&, const ContentFactory*) {
  return TStringId("SHOVE_EFFECT_DESCRIPTION");
}

static bool applyToCreature(const Effects::SwapPosition&, Creature* c, Creature* attacker) {
  CHECK(attacker);
  auto origin = attacker->getPosition();
  auto dir = origin.getDir(c->getPosition());
  if (dir.length8() != 1) {
    attacker->privateMessage(TSentence("YOU_CANT_SWAP_POSITION_WITH", c->getName().the()));
    return false;
  } else if (attacker->canSwapPositionWithEnemy(c)) {
    attacker->swapPosition(dir, false);
    attacker->verb(TStringId("YOU_SWAP_POSITION"), TStringId("SWAPS_POSITION"), TString(c->getName().the()));
    return true;
  } else {
    attacker->privateMessage(TSentence("RESISTS_SWAP_POSITION", c->getName().the()));
    return false;
  }
}

static TString getName(const Effects::SwapPosition&, const ContentFactory*) {
  return TStringId("SWAP_POSITION_EFFECT_NAME");
}

static TString getDescription(const Effects::SwapPosition&, const ContentFactory*) {
  return TStringId("SWAP_POSITION_EFFECT_DESCRIPTION");
}

static TString getName(const Effects::TriggerTrap&, const ContentFactory*) {
  return TStringId("TRIGGER_TRAP_EFFECT_NAME");
}

static TString getDescription(const Effects::TriggerTrap&, const ContentFactory*) {
  return TStringId("TRIGGER_TRAP_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::TriggerTrap&, Position pos, Creature* attacker) {
  for (auto furniture : pos.getFurniture())
    if (auto& entry = furniture->getEntryType())
      if (auto trapInfo = entry->entryData.getReferenceMaybe<FurnitureEntry::Trap>()) {
        pos.globalMessage(TSentence("A_TRAP_IS_TRIGGERED",trapInfo->effect.getName(pos.getGame()->getContentFactory())));
        auto effect = trapInfo->effect;
        pos.removeFurniture(furniture);
        effect.apply(pos, attacker);
        return true;
      }
  return false;
}

static TString getName(const Effects::AnimateItems& m, const ContentFactory*) {
  switch (m.type) {
    case Effects::AnimatedItemType::CORPSE:
      return TStringId("ANIMATE_CORPSES_EFFECT_NAME");
    case Effects::AnimatedItemType::WEAPON:
      return TStringId("ANIMATE_WEAPONS_EFFECT_NAME");
  }
}

static bool isOffensive(const Effects::AnimateItems&) {
  return true;
}

static TString getDescription(const Effects::AnimateItems& e, const ContentFactory*) {
  switch (e.type) {
    case Effects::AnimatedItemType::CORPSE:
      return TSentence("ANIMATE_CORPSES_EFFECT_DESCRIPTION", TString(e.maxCount));
    case Effects::AnimatedItemType::WEAPON:
      return TSentence("ANIMATE_WEAPONS_EFFECT_DESCRIPTION", TString(e.maxCount));
  }
}

static vector<Item*> getItemsToAnimate(const Effects::AnimateItems& m, Position pos) {
  switch (m.type) {
    case Effects::AnimatedItemType::CORPSE:
      return pos.getItems().filter([](auto it) { return it->getClass() == ItemClass::CORPSE; });
    case Effects::AnimatedItemType::WEAPON:
      return pos.getItems(ItemIndex::WEAPON);
  }
}

static bool apply(const Effects::AnimateItems& m, Position pos, Creature* attacker) {
  vector<pair<Position, Item*>> candidates;
  for (auto v : pos.getRectangle(Rectangle::centered(m.radius)))
    for (auto item : getItemsToAnimate(m, v))
      candidates.push_back(make_pair(v, item));
  candidates = Random.permutation(candidates);
  bool res = false;
  for (int i : Range(min(m.maxCount, candidates.size()))) {
    auto v = candidates[i].first;
    auto creature = pos.getGame()->getContentFactory()->getCreatures().
        getAnimatedItem(pos.getGame()->getContentFactory(), v.removeItem(candidates[i].second), attacker->getTribeId(),
            attacker->getAttr(AttrType("SPELL_DAMAGE")));
    for (auto c : Effect::summonCreatures(v, makeVec(std::move(creature)))) {
      c->addEffect(LastingEffect::SUMMONED, TimeInterval{Random.get(m.time)}, false);
      c->effectFlags.insert("animated");
      res = true;
    }
  }
  return res;
}

static EffectAIIntent shouldAIApply(const Effects::AnimateItems& m, const Creature* caster, Position pos) {
  if (caster && isConsideredInDanger(caster)) {
    int totalWeapons = 0;
    for (auto v : pos.getRectangle(Rectangle::centered(m.radius))) {
      totalWeapons += getItemsToAnimate(m, v).size();
      if (totalWeapons >= m.maxCount / 2)
        return 1;
    }
  }
  return 0;
}

static TString getName(const Effects::Audience&, const ContentFactory*) {
  return TStringId("AUDIENCE_EFFECT_NAME");
}

static bool isOffensive(const Effects::Audience&) {
  return true;
}

static TString getDescription(const Effects::Audience&, const ContentFactory*) {
  return TStringId("AUDIENCE_EFFECT_DESCRIPTION");
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::Audience&, const Creature* victim, bool isEnemy) {
  return isConsideredInDanger(victim) ? 1 : 0;
}

static bool tryTeleporting(Position pos, Creature* enemy, optional<int> maxDistance) {
  bool result = false;
  if (!enemy->getStatus().contains(CreatureStatus::CIVILIAN) && !enemy->getStatus().contains(CreatureStatus::PRISONER)) {
    auto distance = enemy->getPosition().dist8(pos);
    if ((!maxDistance || *maxDistance >= distance.value_or(10000)) &&
        (distance.value_or(4) > 3 || !pos.canSee(enemy->getPosition(), Vision())))
      if (auto landing = pos.getLevel()->getClosestLanding({pos}, enemy)) {
        enemy->getPosition().moveCreature(*landing, true);
        result = true;
        enemy->removeEffect(LastingEffect::SLEEP);
      }
  }
  return result;
};

static bool apply(const Effects::Audience& a, Position pos, Creature* attacker) {
  auto collective = [&]() -> Collective* {
    for (auto col : pos.getGame()->getCollectives())
      if (col->getTerritory().contains(pos))
        return col;
    for (auto col : pos.getGame()->getCollectives())
      if (col->getTerritory().getStandardExtended().contains(pos))
        return col;
    return nullptr;
  }();
  bool wasTeleported = false;
  if (collective) {
    for (auto enemy : copyOf(collective->getCreatures(MinionTrait::FIGHTER)))
      wasTeleported |= tryTeleporting(pos, enemy, a.maxDistance);
    if (collective != collective->getGame()->getPlayerCollective())
      for (auto l : collective->getLeaders())
        wasTeleported |= tryTeleporting(pos, l, a.maxDistance);
  } else
    for (auto enemy : pos.getLevel()->getAllCreatures())
      wasTeleported |= tryTeleporting(pos, enemy, a.maxDistance);
  if (wasTeleported) {
    if (attacker)
      attacker->privateMessage(PlayerMessage(setSubjectGender(TSentence("AUDIENCE_SUMMONED"),
          attacker->getName().name), MessagePriority::HIGH));
    else
      pos.globalMessage(PlayerMessage(TStringId("AUDIENCE_SUMMONED2"), MessagePriority::HIGH));
    return true;
  }
  pos.globalMessage(TStringId("NOTHING_HAPPENS"));
  return false;
}

static TString getName(const Effects::SummonMinions&, const ContentFactory*) {
  return TStringId("SUMMON_MINIONS_EFFECT_NAME");
}

static bool isOffensive(const Effects::SummonMinions&) {
  return true;
}

static TString getDescription(const Effects::SummonMinions&, const ContentFactory*) {
  return TStringId("SUMMON_MINIONS_EFFECT_DESCRIPTION");
}

static EffectAIIntent shouldAIApplyToCreature(const Effects::SummonMinions&, const Creature* victim, bool isEnemy) {
  return isConsideredInDanger(victim) ? 1 : 0;
}

static bool applyToCreature(const Effects::SummonMinions& a, Creature* c, Creature* attacker) {
  bool success = false;
  if (auto collective = getCollective(c)) {
    for (auto minion : collective->getCreatures(MinionTrait::FIGHTER))
      success |= tryTeleporting(c->getPosition(), minion, none);
  }
  if (success)
    c->privateMessage(TStringId("SUMMON_MINIONS_MESSAGE"));
  else
    c->privateMessage(TStringId("NOTHING_HAPPENS"));
  return success;
}

static TString getName(const Effects::SoundEffect&, const ContentFactory*) {
  return TStringId("SOUND_EFFECT_EFFECT_NAME");
}

static TString getDescription(const Effects::SoundEffect&, const ContentFactory*) {
  return TStringId("SOUND_EFFECT_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::SoundEffect& e, Position pos, Creature*) {
  pos.addSound(e.sound);
  return true;
}

static bool applyToCreature(const Effects::ColorVariant& e, Creature* c, Creature*) {
  auto& obj = c->modViewObject();
  obj.setId(obj.id().withColor(e.color));
  return true;
}

static TString getName(const Effects::ColorVariant&, const ContentFactory*) {
  return TStringId("COLOR_VARIANT_EFFECT_NAME");
}

static TString getDescription(const Effects::ColorVariant&, const ContentFactory*) {
  return TStringId("COLOR_VARIANT_EFFECT_DESCRIPTION");
}

static TString getName(const Effects::Fx&, const ContentFactory*) {
  return TStringId("VISUAL_EFFECT_NAME");
}

static TString getDescription(const Effects::Fx&, const ContentFactory*) {
  return TStringId("VISUAL_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::Fx& fx, Position pos, Creature*) {
  if (auto game = pos.getGame())
    game->addEvent(EventInfo::FX{pos, fx.info});
  return true;
}

static TString getName(const Effects::SetFlag& e, const ContentFactory*) {
  return TStringId("SET_FLAG_EFFECT_NAME");
}

static TString getDescription(const Effects::SetFlag& e, const ContentFactory*) {
  return TStringId("SET_FLAG_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::SetFlag& e, Position pos, Creature*) {
  if (auto game = pos.getGame()) {
    if (e.value) {
      if (!game->effectFlags.count(e.name)) {
        game->effectFlags.insert(e.name);
        return true;
      }
    } else
    if (game->effectFlags.count(e.name)) {
      game->effectFlags.erase(e.name);
      return true;
    }
  }
  return false;
}

static TString getName(const Effects::SetCreatureFlag& e, const ContentFactory*) {
  return TStringId("SET_FLAG_EFFECT_NAME");
}

static TString getDescription(const Effects::SetCreatureFlag& e, const ContentFactory*) {
  return TStringId("SET_FLAG_EFFECT_DESCRIPTION");
}

static bool applyToCreature(const Effects::SetCreatureFlag& e, Creature* c, Creature*) {
  if (e.value) {
    if (!c->effectFlags.count(e.name)) {
      c->effectFlags.insert(e.name);
      return true;
    }
  } else
  if (c->effectFlags.count(e.name)) {
    c->effectFlags.erase(e.name);
    return true;
  }
  return false;
}

static TString getName(const Effects::Unlock& e, const ContentFactory*) {
  return TStringId("UNLOCK_EFFECT_NAME");
}

static TString getDescription(const Effects::Unlock& e, const ContentFactory*) {
  return TStringId("UNLOCK_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::Unlock& e, Position pos, Creature*) {
  if (auto game = pos.getGame())
    game->getUnlocks()->unlock(e.id);
  return true;
}

static TString getName(const Effects::Achievement& e, const ContentFactory* f) {
  return TStringId("ACHIEVEMENT_EFFECT_NAME");
}

static TString getDescription(const Effects::Achievement& e, const ContentFactory* f) {
  return TStringId("ACHIEVEMENT_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::Achievement& e, Position pos, Creature*) {
  if (auto game = pos.getGame())
    game->achieve(e);
  return true;
}

static TString getName(const Effects::Analytics& e, const ContentFactory*) {
  return TStringId("ANALYTICS_EFFECT_NAME");
}

static TString getDescription(const Effects::Analytics& e, const ContentFactory*) {
  return TStringId("ANALYTICS_EFFECT_DESCRIPTION");
}

static bool apply(const Effects::Analytics& e, Position pos, Creature*) {
  if (auto game = pos.getGame())
    game->addAnalytics(e.name, e.value);
  return false;
}

static TString getName(const Effects::Stairs&, const ContentFactory*) {
  return TStringId("STAIRS_EFFECT_NAME");
}

static TString getDescription(const Effects::Stairs&, const ContentFactory*) {
  return TStringId("STAIRS_EFFECT_DESCRIPTION");
}

static bool applyToCreature(const Effects::Stairs&, Creature* c, Creature* attacker) {
  if (auto link = c->getPosition().getLandingLink()) {
    c->getLevel()->changeLevel(*link, c);
    return true;
  } else {
    c->message(TStringId("BAD_STAIRS_MESSAGE"));
    return false;
  }
}

static TString getName(const Effects::AllCreatures& e, const ContentFactory* f) {
  return e.effect->getName(f);
}

static TString getDescription(const Effects::AllCreatures& e, const ContentFactory* f) {
  return e.effect->getDescription(f);
}

static bool applyToCreature(const Effects::AllCreatures& e, Creature* c, Creature* attacker) {
  bool res = false;
  for (auto other : c->getPosition().getModel()->getAllCreatures())
    res |= e.effect->applyToCreature(other, attacker);
  return res;
}

static TString getName(const Effects::AddMinionTrait& trait, const ContentFactory*) {
  return TStringId("MINION_TRAIT_EFFECT_NAME");
}

static TString getDescription(const Effects::AddMinionTrait& trait, const ContentFactory*) {
return TStringId("MINION_TRAIT_EFFECT_NAME");
}

static bool applyToCreature(const Effects::AddMinionTrait& trait, Creature* c, Creature* attacker) {
  if (auto col = getCollective(c))
    return col->setTrait(c, trait);
  return false;
}

static TString getName(const Effects::RemoveMinionTrait& e, const ContentFactory*) {
  return TStringId("REMOVE_MINION_TRAIT_EFFECT_NAME");
}

static TString getDescription(const Effects::RemoveMinionTrait& e, const ContentFactory*) {
  return TStringId("REMOVE_MINION_TRAIT_EFFECT_NAME");
}

static bool applyToCreature(const Effects::RemoveMinionTrait& e, Creature* c, Creature* attacker) {
  if (auto col = getCollective(c))
    return col->removeTrait(c, e.trait);
  return false;
}

static TString getName(const Effects::SetMinionActivity& activity, const ContentFactory*) {
  return TStringId("MINION_ACTIVITY_EFFECT_NAME");
}

static TString getDescription(const Effects::SetMinionActivity& activity, const ContentFactory*) {
  return TStringId("MINION_ACTIVITY_EFFECT_DESCRIPTION");
}

static bool applyToCreature(const Effects::SetMinionActivity& activity, Creature* c, Creature* attacker) {
  if (auto col = getCollective(c)) {
    col->setMinionActivity(c, activity);
    return true;
  }
  return false;
}

static TString getName(const Effects::CollectiveMessage& msg, const ContentFactory*) {
  return TStringId("MESSAGE_EFFECT_NAME");
}

static TString getDescription(const Effects::CollectiveMessage& msg, const ContentFactory*) {
  return msg.msg;
}

static bool applyToCreature(const Effects::CollectiveMessage& msg, Creature* c, Creature* attacker) {
  if (auto col = getCollective(c)) {
    col->getControl()->addMessage(msg.msg);
    return true;
  }
  return false;
}

static TString getName(const Effects::TakeItems& e, const ContentFactory*) {
  return TStringId("TAKE_ITEMS_EFFECT_NAME");
}

static TString getDescription(const Effects::TakeItems& e, const ContentFactory*) {
  return TStringId("TAKE_ITEMS_EFFECT_DESCRIPTION");
}

static bool applyToCreature(const Effects::TakeItems& e, Creature* c, Creature* attacker) {
  auto items = c->getEquipment().removeItems(
      c->getEquipment().getItems(ItemIndex::RUNE).filter(
      [&] (auto& it){ return it->getIngredientType() == e.ingredient; }),
      c);
  if (items.empty())
    return false;
  if (attacker)
    attacker->takeItems(std::move(items), c);
  return true;
}

static bool canAutoAssignMinionEquipment(const Effects::Filter& f) {
  return f.effect->canAutoAssignMinionEquipment();
}

static optional<MinionEquipmentType> getMinionEquipmentType(const Effects::Filter& f) {
  return f.effect->getMinionEquipmentType();
}

static bool apply(const Effects::Filter& e, Position pos, Creature* attacker) {
  return e.predicate.apply(pos, attacker) && e.effect->apply(pos, attacker);
}

static bool applyToCreaturePreference(const Effects::Filter& e, Creature* c, Creature* attacker) {
  return e.predicate.apply(c, attacker) && e.effect->applyToCreature(c, attacker);
}

static optional<FXInfo> getProjectileFX(const Effects::Filter& e) {
  return e.effect->getProjectileFX();
}

static optional<ViewId> getProjectile(const Effects::Filter& e) {
  return e.effect->getProjectile();
}

static EffectAIIntent shouldAIApply(const Effects::Filter& e, const Creature* caster, Position pos) {
  if (e.predicate.apply(pos, caster))
    return e.effect->shouldAIApply(caster, pos);
  return 0;
}

static TString getName(const Effects::Filter& e, const ContentFactory* f) {
  return e.effect->getName(f);
}

static TString getDescription(const Effects::Filter& e, const ContentFactory* f) {
  return e.effect->getDescription(f);
}

static bool apply(const Effects::ReturnFalse& e, Position pos, Creature* attacker) {
  e.effect->apply(pos, attacker);
  return false;
}

static TString getDescription(const Effects::Description& e, const ContentFactory*) {
  return e.text;
}

static TString getName(const Effects::Name& e, const ContentFactory*) {
  return e.text;
}

static EffectAIIntent shouldAIApply(const Effects::Name& e, const Creature* caster, Position pos) {
  PROFILE_BLOCK(e.text.c_str());
  return e.effect->shouldAIApply(caster, pos);
}

static bool sameAIIntent(EffectAIIntent a, EffectAIIntent b) {
  return (a > 0 && b > 0) || (a < 0 && b < 0) || (a == 0 && b == 0);
}

static EffectAIIntent shouldAIApply(const Effects::AI& e, const Creature* caster, Position pos) {
  auto origRes = e.effect->shouldAIApply(caster, pos);
  if (!caster->isPlayer() && sameAIIntent(origRes, e.from) && e.predicate.apply(pos, caster))
    return e.to;
  return origRes;
}

static int getPrice(const Effects::Price& e, const ContentFactory*) {
  return e.value;
}

Effect::Effect(const EffectType& t) noexcept : effect(t) {
}

Effect::Effect(Effect&&) noexcept = default;

Effect::Effect(const Effect&) noexcept = default;

Effect::Effect() noexcept {
}

Effect::~Effect() {
}

Effect& Effect::operator =(Effect&&) = default;

Effect& Effect::operator =(const Effect&) = default;

template <typename T>
static bool isConsideredHostile(const T&, const Creature*) {
  return false;
}

template <typename T, REQUIRE(applyToCreature(TVALUE(const T&), TVALUE(Creature*), TVALUE(Creature*)))>
static bool apply1(const T& t, Position pos, Creature* attacker, int) {
  if (auto c = pos.getCreature()) {
    if (auto steed = c->getSteed())
      if (Random.chance(getSteedChance<T>()))
        return applyToCreature(t, steed, attacker);
    return applyToCreature(t, c, attacker);
  }
  return false;
}

template <typename T, REQUIRE(apply(TVALUE(const T&), TVALUE(Position), TVALUE(Creature*)))>
static bool apply1(const T& t, Position pos, Creature* attacker, double) {
  return apply(t, pos, attacker);
}

template <typename T, REQUIRE(applyToCreature(TVALUE(const T&), TVALUE(Creature*), TVALUE(Creature*)))>
static bool applyToCreature1(const T& t, Creature* c, Creature* attacker, int) {
  return applyToCreature(t, c, attacker);
}

template <typename T, REQUIRE(applyToCreaturePreference(TVALUE(const T&), TVALUE(Creature*), TVALUE(Creature*)))>
static bool applyToCreature1(const T& t, Creature* c, Creature* attacker, int) {
  return applyToCreaturePreference(t, c, attacker);
}
template <typename T, REQUIRE(apply(TVALUE(const T&), TVALUE(Position), TVALUE(Creature*)))>
static bool applyToCreature1(const T& t, Creature* c, Creature* attacker, double) {
  return apply(t, c->getPosition(), attacker);
}

bool Effect::apply(Position pos, Creature* attacker) const {
  if (auto c = pos.getCreature()) {
    if (attacker)
      effect->visit([&](const auto& e) {
        if (isConsideredHostile(e, c))
          c->onAttackedBy(attacker);
      });
  }
  return effect->visit<bool>([&](const auto& e) { return ::apply1(e, pos, attacker, 1); });
}

bool Effect::applyToCreature(Creature* c, Creature* attacker) const {
  return effect->visit<bool>([&](const auto& e) { return ::applyToCreature1(e, c, attacker, 1); });
}

TString Effect::getName(const ContentFactory* f) const {
  return effect->visit<TString>([&](const auto& elem) { return ::getName(elem, f); });
}

TString Effect::getDescription(const ContentFactory* f) const {
  return effect->visit<TString>([&](const auto& elem) { return ::getDescription(elem, f); });
}

/* Unimplemented: Teleport, EnhanceArmor, EnhanceWeapon, Suicide, IncreaseAttr, IncreaseSkill, IncreaseWorkshopSkill
      EmitPoisonGas, CircularBlast, Alarm, DoubleTrouble,
      PlaceFurniture, InjureBodyPart, LooseBodyPart, RegrowBodyPart, DestroyWalls,
      ReviveCorpse, Blast, Shove, SwapPosition, AddAutomatonParts, AddBodyPart, MakeHumanoid */

static EffectAIIntent shouldAIApply(const DefaultType& m, const Creature* caster, Position pos) {
  return 0;
}

template <typename T, REQUIRE(shouldAIApplyToCreature(TVALUE(const T&), TVALUE(const Creature*), TVALUE(bool)))>
static EffectAIIntent shouldAIApply(const T& elem, const Creature* caster, Position pos) {
  auto victim = pos.getCreature();
  if (victim && !caster->canSee(victim))
    victim = nullptr;
  if (victim && !victim->isAffected(LastingEffect::STUNNED))
    return shouldAIApplyToCreature(elem, victim, caster->isEnemy(victim));
  return 0;
}

EffectAIIntent Effect::shouldAIApply(const Creature* caster, Position pos) const {
  PROFILE;
  return effect->visit<EffectAIIntent>([&](const auto& e) {
    PROFILE_BLOCK(typeid(e).name());
    return ::shouldAIApply(e, caster, pos);
  });
}

static optional<FXInfo> getProjectileFX(const DefaultType&) {
  return none;
}

optional<FXInfo> Effect::getProjectileFX() const {
  return effect->visit<optional<FXInfo>>([&](const auto& e) { return ::getProjectileFX(e); });
}

static optional<ViewId> getProjectile(const DefaultType&) {
  return none;
}

optional<ViewId> Effect::getProjectile() const {
  return effect->visit<optional<ViewId>>([&](const auto& elem) { return ::getProjectile(elem); } );
}

vector<Effect> Effect::getWishedForEffects(const ContentFactory* factory) {
  vector<Effect> allEffects {
       Effect(Effects::Escape{}),
       Effect(Effects::Heal{HealthType::FLESH}),
       Effect(Effects::Heal{HealthType::SPIRIT}),
       Effect(Effects::Ice{}),
       Effect(Effects::Fire{}),
       Effect(Effects::DestroyEquipment{}),
       Effect(Effects::Area{2, Effect(Effects::DestroyWalls{DestroyAction::Type::BOULDER})}),
       Effect(Effects::Enhance{Effects::EnhanceType::WEAPON, 2}),
       Effect(Effects::Enhance{Effects::EnhanceType::ARMOR, 2}),
       Effect(Effects::Enhance{Effects::EnhanceType::WEAPON, -2}),
       Effect(Effects::Enhance{Effects::EnhanceType::ARMOR, -2}),
       Effect(Effects::CircularBlast{}),
       Effect(Effects::Deception{}),
       Effect(Effects::SummonElement{}),
       Effect(Effects::Acid{}),
       Effect(Effects::DoubleTrouble{})
  };
  for (auto effect : ENUM_ALL(LastingEffect))
    if (LastingEffects::canWishFor(effect)) {
      allEffects.push_back(EffectType(Effects::Lasting{none, effect}));
      allEffects.push_back(EffectType(Effects::Permanent{effect}));
      allEffects.push_back(EffectType(Effects::RemoveLasting{effect}));
    }
  for (auto& buff : factory->buffs)
    if (buff.second.canWishFor) {
      allEffects.push_back(EffectType(Effects::Lasting{20_visible, buff.first}));
      allEffects.push_back(EffectType(Effects::Permanent{buff.first}));
      allEffects.push_back(EffectType(Effects::RemoveLasting{buff.first}));
    }
  for (auto& attr : factory->attrInfo)
    allEffects.push_back(EffectType(Effects::IncreaseAttr{attr.first, attr.second.wishedItemIncrease}));
  return allEffects;
}

static optional<MinionEquipmentType> getMinionEquipmentType(const DefaultType&) {
  return none;
}

optional<MinionEquipmentType> Effect::getMinionEquipmentType() const {
  return effect->visit<optional<MinionEquipmentType>>([](const auto& elem) { return ::getMinionEquipmentType(elem); } );
}

static bool canAutoAssignMinionEquipment(const DefaultType&) { return true; }

bool Effect::canAutoAssignMinionEquipment() const {
  return effect->visit<bool>([](const auto& elem) { return ::canAutoAssignMinionEquipment(elem); });
}

static bool isOffensive(const DefaultType&) { return false; }

bool Effect::isOffensive() const {
  return effect->visit<bool>([](const auto& elem) { return ::isOffensive(elem); });
}

template <typename T, REQUIRE(getColor(TVALUE(const T&), TVALUE(const ContentFactory*)))>
static Color getColor(const T& t, const Effect& effect, const ContentFactory* f) {
  return getColor(t, f);
}

static Color getColor(const DefaultType& e, const Effect& effect, const ContentFactory* f) {
  int h = int(combineHash(effect.getName(f)));
  return Color(h % 256, ((h / 256) % 256), ((h / 256 / 256) % 256));
}

Color Effect::getColor(const ContentFactory* f) const {
  return effect->visit<Color>([f, this](const auto& elem) { return ::getColor(elem, *this, f); });
}

static int getPrice(const DefaultType& e, const ContentFactory*) {
  return 30;
}

int Effect::getPrice(const ContentFactory* f) const {
  return effect->visit<int>([f](const auto& elem) { return ::getPrice(elem, f); });
}

static void scale(const DefaultType& e, double, const ContentFactory*) {
}

void Effect::scale(double value, const ContentFactory* f) {
  effect->visit<void>([f, value](auto& elem) { ::scale(elem, value, f); });
}

SERIALIZE_DEF(Effect, effect)

#define VARIANT_TYPES_LIST EFFECT_TYPES_LIST
#define VARIANT_NAME EffectType
namespace Effects {
#include "gen_variant_serialize.h"
}

#include "pretty_archive.h"
template void Effect::serialize(PrettyInputArchive&, unsigned);

void Effects::Lasting::serialize(PrettyInputArchive& ar1, const unsigned int) {
  int d;
  if (ar1.readMaybe(d))
    duration = TimeInterval(d);
  ar1(lastingEffect);
  if (lastingEffect.contains<BuffId>() && !duration)
    ar1.error("Buff duration is required");
}

namespace Effects {
#define DEFAULT_ELEM "Chain"
template<>
#include "gen_variant_serialize_pretty.h"
}
#undef DEFAULT_ELEM
#undef VARIANT_TYPES_LIST
#undef VARIANT_NAME
