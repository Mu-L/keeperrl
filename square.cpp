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

#include "square.h"
#include "level.h"
#include "creature.h"
#include "item.h"
#include "view_object.h"
#include "progress_meter.h"
#include "game.h"
#include "vision.h"
#include "view_index.h"
#include "inventory.h"
#include "tile_gas.h"
#include "tribe.h"
#include "view.h"
#include "game_event.h"
#include "fire.h"
#include "lasting_effect.h"
#include "furniture.h"
#include "content_factory.h"
#include "tile_gas_info.h"

template <class Archive> 
void Square::serialize(Archive& ar, const unsigned int version) { 
  ar(inventory, onFire);
  ar(creature, landingLink, tileGas);
  ar(lastViewer, viewIndex);
  ar(forbiddenTribe);
}

SERIALIZABLE(Square);

Square::Square() : viewIndex(new ViewIndex()) {
}

Square::~Square() {
}

void Square::putCreature(Creature* c) {
  CHECK(!creature);
  creature = c;
  setDirty(c->getPosition());
  if (auto game = c->getGame())
    game->addEvent(EventInfo::CreatureMoved{c});
}

void Square::removeCreature(Position pos) {
  setDirty(pos);
  CHECK(creature);
  creature = nullptr;
}

void Square::setLandingLink(optional<StairKey> key) {
  CHECK(!key || !landingLink);
  landingLink = key;
}

optional<StairKey> Square::getLandingLink() const {
  return landingLink;
}

void Square::onAddedToLevel(Position pos) const {
  if (!inventory->isEmpty())
    pos.getLevel()->addTickingSquare(pos.getCoord());
}

void Square::tick(Position pos) {
  PROFILE_BLOCK("Square::tick");
  setDirty(pos);
  if (!inventory->isEmpty()) {
    inventory->tick(pos, false);
    if (!pos.canEnterEmpty(MovementType(MovementTrait::WALK).setForced()) ||
        (creature && creature->isAffected(LastingEffect::IMMOBILE)))
      for (auto neighbor : pos.neighbors8(Random))
        if (neighbor.canEnterEmpty({MovementTrait::WALK})) {
          neighbor.dropItems(pos.removeItems(pos.getItems()));
          break;
        }
  }
  tileGas->tick(pos);
}

bool Square::itemLands(vector<Item*> item, const Attack& attack) const {
  if (creature) {
    if (item.size() > 1)
      creature->you(MsgType::MISS_THROWN_ITEM_PLURAL, item[0]->getPluralTheName(item.size()));
    else
      creature->you(MsgType::MISS_THROWN_ITEM, item[0]->getTheName());
  }
  return false;
}

void Square::onItemLands(Position pos, vector<PItem> item, const Attack& attack) {
  setDirty(pos);
  if (creature) {
    auto targetCreature = creature;
    if (auto steed = creature->getSteed())
      if (Random.roll(2))
        targetCreature = steed;
    item[0]->onHitCreature(targetCreature, attack, item.size());
    if (!item[0]->isDiscarded())
      dropItems(pos, std::move(item));
    return;
  }
  item[0]->onHitSquareMessage(pos, attack, item.size());
  if (!item[0]->isDiscarded())
    pos.dropItems(std::move(item));
}

void Square::addGas(Position pos, TileGasType type, double amount) {
  setDirty(pos);
  if (pos.canSeeThruIgnoringGas(VisionId::NORMAL)) {
    tileGas->addAmount(pos, type, amount);
    pos.getLevel()->addTickingSquare(pos.getCoord());
  }
}

void Square::addPermanentGas(TileGasType type, double amount) {
  tileGas->addPermanentAmount(type, amount);
}

double Square::getGasAmount(TileGasType type) const {
  return tileGas->getAmount(type);
}

bool Square::hasSunlightBlockingGasAmount() const {
  return tileGas->hasSunlightBlockingAmount();
}

void Square::getViewIndex(const ContentFactory* factory, ViewIndex& ret, const Creature* viewer) const {
  if ((!viewer && lastViewer) || (viewer && lastViewer == viewer->getUniqueId())) {
    ret = *viewIndex;
    return;
  }
  // viewer is null only in Spectator mode, so setting a random id to lastViewer is ok
  lastViewer = viewer ? viewer->getUniqueId() : Creature::Id();
  ret.modItemCounts() = inventory->getCounts();
  if (!getInventory().isEmpty()) {
    auto obj = getInventory().getItems().back()->getViewObject();
    for (Item* it : getInventory().getItems())
      if (it->getFire().isBurning()) {
        obj.setAttribute(ViewObject::Attribute::BURNING, double(it->getFire().getBurnState()) / 50);
        break;
      }
    ret.insert(std::move(obj));
  }
  CHECK(ret.getGasAmounts().empty());
  for (auto& type : factory->tileGasTypes) {
    auto amount = tileGas->getAmount(type.first);
    if (amount > 0)
      ret.addGasAmount(type.second.name, type.second.color.transparency(amount * 255));
  }
  *viewIndex = ret;
}

void Square::dropItem(Position pos, PItem item) {
  dropItems(pos, makeVec(std::move(item)));
}

void Square::dropItemsLevelGen(vector<PItem> items) {
  inventory->addItems(std::move(items));
}

void Square::dropItems(Position pos, vector<PItem> items) {
  setDirty(pos);
  pos.getLevel()->addTickingSquare(pos.getCoord());
  dropItemsLevelGen(std::move(items));
}

Creature* Square::getCreature() const {
  return creature;
}

bool Square::isOnFire() const {
  return onFire;
}

void Square::setOnFire(bool state) {
  onFire = state;
}

PItem Square::removeItem(Position pos, Item* it) {
  setDirty(pos);
  for (auto f : pos.getFurniture())
    f->onItemsRemoved(pos);
  return inventory->removeItem(it);
}

vector<PItem> Square::removeItems(Position pos, vector<Item*> it) {
  setDirty(pos);
  for (auto f : pos.getFurniture())
    f->onItemsRemoved(pos);
  return inventory->removeItems(it);
}

void Square::setDirty(Position pos) {
  pos.setNeedsRenderAndMemoryUpdate(true);
  lastViewer.reset();
}

void Square::forbidMovementForTribe(Position pos, TribeId tribe) {
  CHECK(!forbiddenTribe || forbiddenTribe == tribe);
  forbiddenTribe = tribe;
  pos.updateConnectivity();
  setDirty(pos);
}

void Square::allowMovementForTribe(Position pos, TribeId tribe) {
  CHECK(!forbiddenTribe || forbiddenTribe == tribe);
  forbiddenTribe = none;
  pos.updateConnectivity();
  setDirty(pos);
}

bool Square::isTribeForbidden(TribeId tribe) const {
  return forbiddenTribe == tribe;
}

optional<TribeId> Square::getForbiddenTribe() const {
  return forbiddenTribe;
}

const Inventory& Square::getInventory() const {
  return *inventory;
}

void Square::clearItemIndex(ItemIndex index) {
  inventory->clearIndex(index);
}
