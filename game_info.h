#pragma once

#include "unique_entity.h"
#include "item_action.h"
#include "village_action.h"
#include "view_id.h"
#include "player_message.h"
#include "entity_set.h"
#include "immigrant_auto_state.h"
#include "tutorial_highlight.h"
#include "hashing.h"
#include "attr_type.h"
#include "best_attack.h"
#include "villain_type.h"
#include "game_time.h"
#include "intrinsic_attack.h"
#include "team_member_action.h"
#include "team_order.h"
#include "view_object_modifier.h"
#include "creature_experience_info.h"
#include "tech_id.h"
#include "scripted_help_info.h"
#include "keybinding.h"
#include "player_info_action.h"

class PlayerMessage;
class SpecialTrait;
class Encyclopedia;

struct CreatureInfo {
  CreatureInfo(const Creature*);
  ViewIdList HASH(viewId);
  UniqueEntity<Creature>::Id HASH(uniqueId);
  string HASH(name);
  string HASH(stackName);
  BestAttack HASH(bestAttack);
  HASH_ALL(viewId, uniqueId, name, stackName, bestAttack)
};

static_assert(std::is_nothrow_move_constructible<CreatureInfo>::value, "T should be noexcept MoveConstructible");

struct ItemInfo {
  static ItemInfo get(const Creature*, const vector<Item*>&, const ContentFactory*);
  bool representsSteed() const;
  string HASH(name);
  string HASH(fullName);
  vector<string> HASH(description);
  int HASH(number);
  ViewIdList HASH(viewId);
  EnumSet<ViewObjectModifier> HASH(viewIdModifiers);
  vector<UniqueEntity<Item>::Id> HASH(ids);
  vector<ItemAction> HASH(actions);
  bool HASH(equiped);
  bool HASH(locked);
  bool HASH(pending);
  bool HASH(unavailable);
  enum IntrinsicAttackState { ACTIVE, INACTIVE };
  optional<IntrinsicAttackState> HASH(intrinsicAttackState);
  bool HASH(intrinsicExtraAttack);
  string HASH(unavailableReason);
  optional<EquipmentSlot> HASH(slot);
  optional<pair<CreatureInfo, int>> HASH(owner);
  enum Type {EQUIPMENT, CONSUMABLE, OTHER} HASH(type);
  optional<pair<ViewId, int>> HASH(price);
  optional<double> HASH(weight);
  bool HASH(tutorialHighlight);
  heap_optional<ItemInfo> HASH(ingredient);
  HASH_ALL(name, fullName, description, number, viewId, ids, actions, equiped, locked, pending, unavailable, slot, owner, type, price, unavailableReason, weight, tutorialHighlight, intrinsicAttackState, intrinsicExtraAttack, viewIdModifiers, ingredient)
};

static_assert(std::is_nothrow_move_constructible<ItemInfo>::value, "T should be noexcept MoveConstructible");

struct AttributeInfo {
  static vector<AttributeInfo> fromCreature(const ContentFactory*, const Creature*);
  string HASH(name);
  ViewId HASH(viewId);
  int HASH(value);
  int HASH(bonus);
  string HASH(help);
  HASH_ALL(name, viewId, value, bonus, help)
};

struct AvatarLevelInfo {
  int HASH(level);
  double HASH(progress);
  ViewIdList HASH(viewId);
  string HASH(title);
  int HASH(numAvailable);
  HASH_ALL(level, progress, viewId, title, numAvailable)
};

struct SpellInfo {
  string HASH(name);
  string HASH(symbol);
  optional<int> HASH(level);
  bool HASH(available) = true;
  vector<string> HASH(help);
  optional<TimeInterval> HASH(timeout);
  bool HASH(highlighted);
  HASH_ALL(name, symbol, help, timeout, available, level, highlighted)
};

struct SpellSchoolInfo {
  string HASH(name);
  string HASH(experienceType);
  vector<SpellInfo> HASH(spells);
  HASH_ALL(name, experienceType, spells)
};

class PlayerInfo {
  public:
  PlayerInfo(const Creature*, const ContentFactory*);
  string getFirstName() const;
  vector<AttributeInfo> HASH(attributes);
  BestAttack HASH(bestAttack);
  string HASH(firstName);
  string HASH(name);
  string HASH(groupName);
  string HASH(title);
  CreatureExperienceInfo HASH(experienceInfo);
  string description;
  int HASH(positionHash);
  struct Effect {
    string HASH(name);
    string HASH(help);
    bool HASH(bad);
    HASH_ALL(name, help, bad)
  };
  vector<Effect> HASH(effects);
  vector<SpellInfo> HASH(spells);
  vector<SpellSchoolInfo> HASH(spellSchools);
  vector<ItemInfo> HASH(lyingItems);
  vector<ItemInfo> HASH(inventory);
  vector<ItemInfo> HASH(intrinsicAttacks);
  int HASH(debt);
  vector<PlayerInfo> HASH(teamInfos);
  struct CommandInfo {
    string HASH(name);
    optional<Keybinding> HASH(keybinding);
    string HASH(description);
    bool HASH(active);
    bool HASH(tutorialHighlight) = false;
    HASH_ALL(name, keybinding, description, active, tutorialHighlight)
  };
  vector<CommandInfo> HASH(commands);
  struct MinionActivityInfo {
    MinionActivity HASH(task);
    bool HASH(inactive);
    bool HASH(current);
    bool HASH(locked);
    bool HASH(canLock);
    bool HASH(lockedForGroup);
    HASH_ALL(task, inactive, current, locked, canLock, lockedForGroup)
  };
  vector<MinionActivityInfo> HASH(minionTasks);
  AIType HASH(aiType);
  UniqueEntity<Creature>::Id HASH(creatureId);
  int HASH(moveCounter);
  ViewIdList HASH(viewId);
  bool HASH(isPlayerControlled);
  enum ControlMode {
    FULL,
    LEADER
  };
  optional<ControlMode> HASH(controlMode);
  using Action = PlayerInfoAction;
  optional<EnumSet<TeamOrder>> HASH(teamOrders);
  vector<Action> HASH(actions);
  vector<TeamMemberAction> HASH(teamMemberActions);
  optional<double> HASH(carryLimit);
  vector<ViewIdList> HASH(kills);
  vector<string> HASH(killTitles);
  bool HASH(canExitControlMode);
  struct EquipmentGroupInfo {
    ViewId HASH(viewId);
    string HASH(name);
    bool HASH(locked);
    HASH_ALL(viewId, name, locked);
  };
  vector<EquipmentGroupInfo> HASH(equipmentGroups);
  HASH_ALL(attributes, firstName, name, groupName, title, experienceInfo, positionHash, effects, spells, lyingItems, inventory, minionTasks, aiType, creatureId, viewId, actions, commands, debt, bestAttack, carryLimit, intrinsicAttacks, teamInfos, moveCounter, isPlayerControlled, controlMode, teamMemberActions, teamOrders, spellSchools, kills, killTitles, canExitControlMode, equipmentGroups)
};

struct ImmigrantCreatureInfo {
  string HASH(name);
  ViewIdList HASH(viewId);
  vector<AttributeInfo> HASH(attributes);
  vector<string> HASH(spellSchools);
  struct TrainingInfo {
    AttrType HASH(expType);
    int HASH(limit);
    ViewId HASH(attribute);
    HASH_ALL(expType, limit, attribute)
  };
  vector<TrainingInfo> HASH(trainingLimits);
  HASH_ALL(name, viewId, attributes, spellSchools, trainingLimits);
};

ImmigrantCreatureInfo getImmigrantCreatureInfo(const Creature*, const ContentFactory*);

struct ImmigrantDataInfo {
  ImmigrantDataInfo();
  STRUCT_DECLARATIONS(ImmigrantDataInfo)
  vector<string> HASH(requirements);
  vector<string> HASH(info);
  struct SpecialTraitInfo {
    string HASH(label);
    bool HASH(bad);
    HASH_ALL(label, bad)
  };
  vector<SpecialTraitInfo> HASH(specialTraits);
  optional<pair<ViewId, int>> HASH(cost);
  optional<int> HASH(count);
  ImmigrantCreatureInfo HASH(creature);
  optional<TimeInterval> HASH(timeLeft);
  int HASH(id);
  enum AutoState { AUTO_REJECT, AUTO_ACCEPT};
  optional<ImmigrantAutoState> HASH(autoState);
  optional<milliseconds> HASH(generatedTime);
  optional<Keybinding> HASH(keybinding);
  optional<TutorialHighlight> HASH(tutorialHighlight);
  size_t getHash() const;
};
static_assert(std::is_nothrow_move_constructible<ImmigrantDataInfo>::value, "T should be noexcept MoveConstructible");

class CollectiveInfo {
  public:
  CollectiveInfo() {}
  CollectiveInfo(const CollectiveInfo&) = delete;
  CollectiveInfo(CollectiveInfo&&) = default;
  CollectiveInfo& operator = (CollectiveInfo&&) = default;
  CollectiveInfo& operator = (const CollectiveInfo&) = delete;
  string HASH(warning);
  struct Button {
    ViewId HASH(viewId);
    string HASH(name);
    optional<pair<ViewId, int>> HASH(cost);
    string HASH(count);
    enum { ACTIVE, GRAY_CLICKABLE, INACTIVE} HASH(state);
    string HASH(help);
    optional<Keybinding> HASH(key);
    string HASH(groupName);
    bool HASH(hotkeyOpensGroup);
    optional<TutorialHighlight> HASH(tutorialHighlight);
    bool HASH(isBuilding);
    HASH_ALL(viewId, name, cost, count, state, help, key, groupName, hotkeyOpensGroup, tutorialHighlight, isBuilding)
  };
  vector<Button> HASH(buildings);
  string HASH(populationString);
  int HASH(minionCount);
  int HASH(minionLimit);
  string HASH(monsterHeader);
  vector<CreatureInfo> HASH(minions);
  struct CreatureGroup {
    UniqueEntity<Creature>::Id HASH(creatureId);
    string HASH(name);
    ViewIdList HASH(viewId);
    int HASH(count);
    bool HASH(highlight);
    HASH_ALL(creatureId, name, viewId, count, highlight)
  };
  vector<CreatureGroup> HASH(minionGroups);
  vector<CreatureGroup> HASH(automatonGroups);
  struct ChosenCreatureInfo {
    UniqueEntity<Creature>::Id HASH(chosenId);
    vector<PlayerInfo> HASH(creatures);
    optional<TeamId> HASH(teamId);
    HASH_ALL(chosenId, creatures, teamId)
  };
  optional<ChosenCreatureInfo> HASH(chosenCreature);
  vector<ImmigrantDataInfo> HASH(immigration);
  vector<ImmigrantDataInfo> HASH(allImmigration);
  struct QueuedItemInfo {
    double HASH(productionState);
    bool HASH(paid);
    ItemInfo HASH(itemInfo);
    optional<ImmigrantCreatureInfo> HASH(creatureInfo);
    struct UpgradeInfo {
      ViewId HASH(viewId);
      string HASH(name);
      int HASH(used);
      int HASH(count);
      vector<string> HASH(description);
      HASH_ALL(viewId, name, used, count, description)
    };
    vector<UpgradeInfo> HASH(upgrades);
    pair<string, int> HASH(maxUpgrades);
    int HASH(itemIndex);
    HASH_ALL(productionState, paid, itemInfo, creatureInfo, upgrades, maxUpgrades, itemIndex)
  };
  struct OptionInfo {
    ItemInfo HASH(itemInfo);
    optional<ImmigrantCreatureInfo> HASH(creatureInfo);
    pair<string, int> HASH(maxUpgrades);
    HASH_ALL(itemInfo, creatureInfo, maxUpgrades)
  };
  struct ChosenWorkshopInfo {
    vector<ViewId> HASH(resourceTabs);
    int HASH(chosenTab);
    string HASH(tabName);
    vector<OptionInfo> HASH(options);
    vector<QueuedItemInfo> HASH(queued);
    int HASH(index);
    string HASH(queuedTitle);
    bool HASH(allowChangeNumber);
    int HASH(minAttrValue);
    ViewId HASH(attr);
    HASH_ALL(index, options, queued, resourceTabs, chosenTab, tabName, queuedTitle, allowChangeNumber, minAttrValue, attr)
  };
  optional<ChosenWorkshopInfo> HASH(chosenWorkshop);
  struct LibraryInfo {
    struct TechInfo {
      string HASH(description);
      TechId HASH(id);
      struct UnlockInfo {
        ViewIdList HASH(viewId);
        string HASH(name);
        string HASH(type);
        HASH_ALL(viewId, name, type)
      };
      vector<UnlockInfo> HASH(unlocks);
      bool HASH(active);
      optional<TutorialHighlight> HASH(tutorialHighlight);
      HASH_ALL(description, id, active, unlocks, tutorialHighlight)
    };
    int HASH(currentProgress);
    int HASH(totalProgress);
    optional<string> HASH(warning);
    vector<TechInfo> HASH(available);
    vector<TechInfo> HASH(researched);
    HASH_ALL(currentProgress, totalProgress, warning, available, researched)
  };
  LibraryInfo HASH(libraryInfo);
  struct PromotionOption {
    ViewId HASH(viewId);
    string HASH(name);
    string HASH(description);
    HASH_ALL(viewId, name, description);
  };
  struct MinionPromotionInfo {
    ViewIdList HASH(viewId);
    string HASH(name);
    UniqueEntity<Creature>::Id HASH(id);
    vector<PromotionOption> HASH(promotions);
    vector<PromotionOption> HASH(options);
    bool HASH(canAdvance);
    HASH_ALL(viewId, name, id, promotions, options, canAdvance)
  };
  vector<MinionPromotionInfo> HASH(minionPromotions);
  int HASH(availablePromotions);
  struct Resource {
    ViewId HASH(viewId);
    int HASH(count);
    string HASH(name);
    optional<TutorialHighlight> HASH(tutorialHighlight);
    HASH_ALL(viewId, count, name, tutorialHighlight)
  };
  vector<Resource> HASH(numResource);
  AvatarLevelInfo HASH(avatarLevelInfo);
  struct Team {
    TeamId HASH(id);
    vector<UniqueEntity<Creature>::Id> HASH(members);
    bool HASH(active);
    bool HASH(highlight);
    HASH_ALL(id, members, active, highlight)
  };
  vector<Team> HASH(teams);
  const CreatureInfo* getMinion(UniqueEntity<Creature>::Id) const;
  bool hasMinion(UniqueEntity<Creature>::Id);
  int HASH(nextPayout);
  int HASH(payoutTimeRemaining);

  struct Task {
    string HASH(name);
    optional<UniqueEntity<Creature>::Id> HASH(creature);
    bool HASH(priority);
    HASH_ALL(name, creature, priority)
  };
  vector<Task> HASH(taskMap);

  struct NextWave {
    ViewIdList HASH(viewId);
    string HASH(attacker);
    TimeInterval HASH(numTurns);
    HASH_ALL(viewId, attacker, numTurns)
  };
  optional<NextWave> HASH(nextWave);
  enum class RebellionChance {
    LOW,
    MEDIUM,
    HIGH,
  };
  optional<RebellionChance> HASH(rebellionChance);
  HASH_ALL(warning, buildings, minionCount, minionLimit, monsterHeader, minions, minionGroups, automatonGroups, chosenCreature, numResource, teams, nextPayout, payoutTimeRemaining, taskMap, nextWave, chosenWorkshop, immigration, allImmigration, libraryInfo, minionPromotions, availablePromotions, rebellionChance, avatarLevelInfo, populationString)
};

class VillageInfo {
  public:
  struct Village {
    optional<string> HASH(name);
    string HASH(tribeName);
    ViewIdList HASH(viewId);
    VillainType HASH(type);
    enum Access { ACTIVE, INACTIVE, LOCATION, NO_LOCATION };
    Access HASH(access);
    bool HASH(isConquered);
    struct TriggerInfo {
      string HASH(name);
      double HASH(value);
      HASH_ALL(name, value)
    };
    optional<VillageAction> HASH(action);
    vector<TriggerInfo> HASH(triggers);
    UniqueEntity<Collective>::Id HASH(id);
    bool HASH(attacking);
    HASH_ALL(name, tribeName, access, isConquered, action, triggers, id, viewId, type, attacking)
  };
  int HASH(numMainVillains);
  int HASH(numConqueredMainVillains);
  set<pair<UniqueEntity<Collective>::Id, string>> HASH(dismissedInfos);
  vector<Village> HASH(villages);
  HASH_ALL(villages, numMainVillains, numConqueredMainVillains, dismissedInfos)
};

class GameSunlightInfo {
  public:
  string HASH(description);
  TimeInterval HASH(timeRemaining);
  HASH_ALL(description, timeRemaining)
};

class TutorialInfo {
  public:
  TutorialState HASH(state);
  optional<string> HASH(warning);
  bool HASH(canContinue);
  bool HASH(canGoBack);
  EnumSet<TutorialHighlight> HASH(highlights);
  vector<Vec2> HASH(highlightedSquaresHigh);
  vector<Vec2> HASH(highlightedSquaresLow);
  HASH_ALL(state, warning, canContinue, canGoBack, highlights, highlightedSquaresHigh, highlightedSquaresLow)
};

struct CurrentLevelInfo {
  string HASH(name);
  int HASH(levelDepth);
  vector<string> HASH(zLevels);
  HASH_ALL(name, levelDepth, zLevels)
};

/** Represents all the game information displayed around the map window.*/
class GameInfo {
  public:
  enum class InfoType { PLAYER, BAND, SPECTATOR} HASH(infoType) = InfoType::PLAYER;
  GlobalTime HASH(time);
  int HASH(modifiedSquares);
  int HASH(totalSquares);

  GameInfo() {}
  GameInfo(const GameInfo&) = delete;
  GameInfo(GameInfo&&) = default;
  GameInfo& operator = (const GameInfo&) = delete;
  GameInfo& operator = (GameInfo&&) = default;

  variant<CollectiveInfo, PlayerInfo> HASH(playerInfo);
  VillageInfo HASH(villageInfo);
  GameSunlightInfo HASH(sunlightInfo);
  optional<TutorialInfo> HASH(tutorial);
  optional<CurrentLevelInfo> HASH(currentLevel);
  const Encyclopedia* encyclopedia;
  bool HASH(isSingleMap) = false;
  vector<ScriptedHelpInfo> scriptedHelp; // this won't change during the game so don't hash
  vector<PlayerMessage> HASH(messageBuffer);
  bool HASH(takingScreenshot) = false;
  HASH_ALL(infoType, time, playerInfo, villageInfo, sunlightInfo, messageBuffer, modifiedSquares, totalSquares, tutorial, currentLevel, takingScreenshot, isSingleMap)
};

struct AutomatonPart;
class SpellSchoolId;
SpellSchoolInfo fillSpellSchool(const Creature*, SpellSchoolId, const ContentFactory*);
