#pragma once

#include <Tempest/Widget>
#include <Tempest/Texture2d>
#include <Tempest/Timer>
#include <Tempest/Color>

#include <daedalus/DaedalusVM.h>
#include <memory>

#include "game/savegameheader.h"
#include "game/questlog.h"
#include "utils/keycodec.h"

class Gothic;
class MenuRoot;
class Npc;
class GthFont;

class GameMenu : public Tempest::Widget {
  public:
    GameMenu(MenuRoot& owner, KeyCodec& keyCodec, Daedalus::DaedalusVM& vm, const char *menuSection, KeyCodec::Action keyClose);
    ~GameMenu() override;

    void setPlayer(const Npc& pl);

    void onMove (int dy);
    void onSlide(int dx);
    void onSelect();
    void onTick();
    void processMusicTheme();

    KeyCodec::Action keyClose() const { return kClose; }

  protected:
    void paintEvent (Tempest::PaintEvent& event) override;
    void resizeEvent(Tempest::SizeEvent&  event) override;

  private:
    enum class QuestStat : uint8_t {
      Current   = uint8_t(QuestLog::Status::Running),
      Old       = uint8_t(QuestLog::Status::Failed),
      Failed    = uint8_t(QuestLog::Status::Success),
      Log       = uint8_t(5),
      };

    struct ListContentDialog;
    struct ListViewDialog;
    struct KeyEditDialog;
    struct SavNameDialog;
    struct Item {
      std::string                           name;
      Daedalus::GEngineClasses::C_Menu_Item handle={};
      const Tempest::Texture2d*             img=nullptr;
      SaveGameHeader                        savHdr;
      Tempest::Pixmap                       savPriview;
      int32_t                               value   = 0;
      int32_t                               scroll  = 0;
      bool                                  visible = true;
      };

    MenuRoot&                             owner;
    KeyCodec&                             keyCodec;
    Daedalus::DaedalusVM&                 vm;
    Tempest::Timer                        timer;
    const Tempest::Texture2d*             up   = nullptr;
    const Tempest::Texture2d*             down = nullptr;

    Daedalus::GEngineClasses::C_Menu      menu={};
    const Tempest::Texture2d*             back=nullptr;
    const Tempest::Texture2d*             slider=nullptr;
    Tempest::Texture2d                    savThumb;
    std::vector<char>                     textBuf;

    Item                                  hItems[Daedalus::GEngineClasses::MenuConstants::MAX_ITEMS];
    Item*                                 ctrlInput = nullptr;
    uint32_t                              curItem=0;
    bool                                  exitFlag=false;
    bool                                  closeFlag=false;

    KeyCodec::Action                      kClose = KeyCodec::Escape;

    void                                  drawItem(Tempest::Painter& p, Item& it);
    void                                  drawSlider(Tempest::Painter& p, Item& it, int x, int y, int w, int h);
    void                                  drawQuestList(Tempest::Painter& p, Item& it, int x, int y, int w, int h,
                                                        const QuestLog& log, QuestStat st);

    Item*                                 selectedItem();
    Item*                                 selectedNextItem(Item* cur);
    Item*                                 selectedContentItem(Item* it);
    void                                  setSelection(int cur, int seek=1);
    void                                  initItems();
    void                                  getText(const Item &it, std::vector<char>& out);
    const GthFont&                        getTextFont(const Item &it);
    static bool                           isSelectable(const Daedalus::GEngineClasses::C_Menu_Item& item);
    static bool                           isEnabled(const Daedalus::GEngineClasses::C_Menu_Item& item);

    void                                  exec         (Item &item, int slideDx);
    void                                  execSingle   (Item &it,   int slideDx);
    void                                  execChgOption(Item &item, int slideDx);
    void                                  execSaveGame (const Item& item);
    void                                  execLoadGame (const Item& item);
    void                                  execCommands (const Daedalus::ZString str, bool isClick);

    bool                                  implUpdateSavThumb(Item& sel);
    size_t                                saveSlotId(const Item& sel);

    const char*                           strEnum(const char* en, int id, std::vector<char> &out);
    size_t                                strEnumSize(const char* en);

    void                                  updateValues();
    void                                  updateItem    (Item &item);
    void                                  updateSavTitle(Item& sel);
    void                                  updateSavThumb(Item& sel);
    void                                  updateVideo();
    void                                  setDefaultKeys(const char* preset);

    static bool                           isInGameAndAlive();
    static QuestStat                      toStatus(const Daedalus::ZString& str);
    static bool                           isCompatible(const QuestLog::Quest& q, QuestStat st);
    static int32_t                        numQuests(const QuestLog* q, QuestStat st);

    void                                  set(std::string_view item, const Tempest::Texture2d* value);
    void                                  set(std::string_view item, const uint32_t value);
    void                                  set(std::string_view item, const int32_t  value);
    void                                  set(std::string_view item, const int32_t  value,const char* post);
    void                                  set(std::string_view item, const int32_t  value, const int32_t max);
    void                                  set(std::string_view item, const char* value);
  };
