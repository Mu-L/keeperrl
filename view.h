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

#pragma once

#include "enums.h"
#include "util.h"
#include "debug.h"
#include "animation_id.h"
#include "gender.h"
#include "fx_info.h"
#include "creature_experience_info.h"
#include "enum_variant.h"
#include "unique_entity.h"
#include "view_id.h"
#include "campaign_menu_index.h"
#include "keybinding.h"

class CreatureView;
class Level;
class Jukebox;
class ProgressMeter;
class PlayerInfo;
struct ItemInfo;
struct CreatureInfo;
class Sound;
class Campaign;
class Options;
class RetiredGames;
class ScrollPosition;
class FilePath;
class ModInfo;
class UserInput;
struct Color;
struct ScriptedUIData;
struct ScriptedUIState;
namespace fx {
  class FXRenderer;
}
class FXViewManager;

enum class CampaignActionId {
  CANCEL,
  REROLL_MAP,
  UPDATE_MAP,
  CONFIRM,
  UPDATE_OPTION,
  SET_POSITION,
  CHANGE_WORLD_MAP
};

enum class PassableInfo {
  PASSABLE,
  NON_PASSABLE,
  STOPS_HERE,
  UNKNOWN
};

class CampaignAction : public EnumVariant<CampaignActionId, TYPES(OptionId, string, Vec2, int),
  ASSIGN(Vec2, CampaignActionId::SET_POSITION),
  ASSIGN(OptionId, CampaignActionId::UPDATE_OPTION),
  ASSIGN(int, CampaignActionId::CHANGE_WORLD_MAP)> {
    using EnumVariant::EnumVariant;
};

namespace RetiredChoices {
using Confirm = EmptyStruct<struct ConfirmTag>;
using Cancel = EmptyStruct<struct CancelTag>;
using Search = string;
using RetiredChoice = variant<Confirm, Cancel, Search>;
}

using RetiredChoices::RetiredChoice;

struct ModAction {
  int index;
  int actionId;
};

class View {
  public:
  View();
  virtual ~View();

  /** Does all the library specific init.*/
  virtual void initialize(unique_ptr<fx::FXRenderer>, unique_ptr<FXViewManager>) = 0;

  /** Resets the view before a new game.*/
  virtual void reset() = 0;

  /** Displays a splash screen in an active loop until \paramname{ready} is set to true in another thread.*/
  virtual void displaySplash(const ProgressMeter*, const string& text,
      function<void()> cancelFun = nullptr) = 0;

  virtual void clearSplash() = 0;

  virtual void playVideo(const string& path) = 0;

  void doWithSplash(const string& text, int totalProgress,
      function<void(ProgressMeter&)> fun, function<void()> cancelFun = nullptr);

  /** Shutdown routine.*/
  virtual void close() = 0;

  /** Reads the game state from \paramname{creatureView} and refreshes the display.*/
  virtual void refreshView() = 0;

  /** Returns real-time game mode speed measured in turns per millisecond. **/
  virtual double getGameSpeed() = 0;

  /** Reads the game state from \paramname{creatureView}. If \paramname{noRefresh} is set,
      won't trigger screen to refresh.*/
  virtual void updateView(CreatureView*, bool noRefresh) = 0;

  virtual void setScrollPos(Position) = 0;

  /** Scrolls back to the center of the view on next refresh.*/
  virtual void resetCenter() = 0;

  /** Reads input in a non-blocking manner.*/
  virtual UserInput getAction() = 0;

  /** Returns whether a travel interrupt key is pressed at a given moment.*/
  virtual bool travelInterrupt() = 0;

  /** Lets the player choose a direction from the main 8. Returns none if the player cancelled the choice.*/
  virtual optional<Vec2> chooseDirection(Vec2 playerPos, const string& message) = 0;

  using TargetResult = variant<none_t, Vec2, Keybinding>;
  /** Lets the player choose a target position. Returns none if the player cancelled the choice.*/
  virtual TargetResult chooseTarget(Vec2 playerPos, TargetType, Table<PassableInfo> passable,
      const string& message, optional<Keybinding> cycleKey) = 0;

  /** Asks the player a yer-or-no question.*/
  bool yesOrNoPrompt(const string& message, optional<ViewIdList> = none, bool defaultNo = false,
      ScriptedUIId = "yes_or_no");
  optional<int> multiChoice(const string& message, const vector<string>&);

  void windowedMessage(ViewIdList, const string& message);

  /** Draws a window with some text. The text is formatted to fit the window.*/
  void presentText(const string& title, const string& text);
  void presentTextBelow(const string& title, const string& text);

  virtual void scriptedUI(ScriptedUIId, const ScriptedUIData&, ScriptedUIState&) = 0;
  void scriptedUI(ScriptedUIId, const ScriptedUIData&);

  /** Lets the player choose a number. Returns none if the player cancelled the choice.*/
  virtual optional<int> getNumber(const string& title, Range range, int initial) = 0;

  /** Lets the player input a string. Returns none if the player cancelled the choice.*/
  virtual optional<string> getText(const string& title, const string& value, int maxLength) = 0;

  virtual optional<int> chooseAtMouse(const vector<string>& elems) = 0;

  virtual void dungeonScreenshot(Vec2 size) = 0;

  using BugReportSaveCallback = function<void(FilePath)>;

  bool confirmConflictingItems(const ContentFactory*, const vector<Item*>&);

  virtual void setBugReportSaveCallback(BugReportSaveCallback) = 0;

  struct CampaignMenuState {
    bool helpText;
    CampaignMenuIndex index;
  };
  struct CampaignOptions {
    const Campaign& campaign;
    optional<RetiredGames&> retired;
    vector<OptionId> options;
    string introText;
    string currentBiome;
    vector<string> worldMapNames;
    int currentWorldMap;
  };

  virtual CampaignAction prepareCampaign(CampaignOptions, CampaignMenuState&) = 0;
  virtual vector<int> prepareWarlordGame(RetiredGames&, const vector<PlayerInfo>&, int maxTeam, int maxDungeons) = 0;

  virtual optional<UniqueEntity<Creature>::Id> chooseCreature(const string& title, const vector<PlayerInfo>&,
      const string& cancelText) = 0;

  //virtual vector<UniqueEntity<Creature>::Id> chooseTeamLeader(const string& title, const vector<CreatureInfo>&) = 0;

  virtual bool creatureInfo(const string& title, bool prompt, const vector<PlayerInfo>&) = 0;

  virtual optional<Vec2> chooseSite(const string& message, const Campaign&, Vec2 current) = 0;

  virtual void presentWorldmap(const Campaign&, Vec2 current) = 0;

  /** Draws an animation of an object between two locations on a map.*/
  virtual void animateObject(Vec2 begin, Vec2 end, optional<ViewId> object, optional<FXInfo> fx) = 0;

  /** Draws an special animation on the map.*/
  virtual void animation(Vec2 pos, AnimationId, Dir orientation = Dir::N) = 0;
  virtual void animation(const FXSpawnInfo&) = 0;

  /** Returns the current real time in milliseconds. The clock is stopped on blocking keyboard input,
      so it can be used to sync game time in real-time mode.*/
  virtual milliseconds getTimeMilli() = 0;

  /** Returns the absolute time, doesn't take pausing into account.*/
  virtual milliseconds getTimeMilliAbsolute() = 0;

  /** Stops the real time clock.*/
  virtual void stopClock() = 0;

  /** Continues the real time clock after it had been stopped.*/
  virtual void continueClock() = 0;

  /** Returns whether the real time clock is currently stopped.*/
  virtual bool isClockStopped() = 0;

  virtual void addSound(const Sound&) = 0;

  virtual void logMessage(const string&) = 0;

  virtual bool zoomUIAvailable() const = 0;
};
