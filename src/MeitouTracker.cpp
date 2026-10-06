#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/Character.h>
#include <kenshi/GameData.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/Gear.h>
#include <kenshi/InputHandler.h>
#include <kenshi/Inventory.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/RootObject.h>
#include <ois/OISKeyboard.h>
#include <windows.h>

#include <sstream>
#include <string>
#include <vector>

namespace MeitouTracker
{
    struct Record
    {
        hand handle;
        std::string name;
        std::string sid;
        std::string status;
        std::string holder;
        Ogre::Vector3 position;
        bool seenThisScan;
    };

    static std::vector<Record> records;
    static DWORD lastScanMs = 0;
    static bool autoScan = false;
    static bool scanWasDown = false;
    static bool autoWasDown = false;

    static const float kScanRadius = 400.0f;
    static const int kMaxWorldObjects = 2048;
    static const int kMaxCharacters = 1024;
    static const int kMaxBuildings = 2048;
    static const int kMaxInventoryDepth = 3;
    static const DWORD kAutoScanIntervalMs = 1500;

    static std::string formatPosition(const Ogre::Vector3& position)
    {
        std::ostringstream out;
        out << position.x << "," << position.y << "," << position.z;
        return out.str();
    }

    static bool isMeitou(Item* item)
    {
        Weapon* weapon = item ? item->isWeapon() : 0;
        // Kenshi weapon quality/model value: Meitou is the 100 tier.
        return weapon != 0 && weapon->getLevel() == 100;
    }

    static int findRecord(const hand& handle)
    {
        for (size_t i = 0; i < records.size(); ++i)
            if (records[i].handle == handle)
                return static_cast<int>(i);
        return -1;
    }

    static void markFound(
        Item* item,
        const std::string& status,
        const std::string& holder,
        const Ogre::Vector3& position)
    {
        if (!isMeitou(item))
            return;

        const hand handle = item->getHandle();
        const int oldIndex = findRecord(handle);
        const bool known = oldIndex >= 0;

        const std::string name = item->getName();
        const std::string sid = item->data ? item->data->stringID : "";

        if (!known)
        {
            Record record;
            record.handle = handle;
            record.name = name;
            record.sid = sid;
            record.status = status;
            record.holder = holder;
            record.position = position;
            record.seenThisScan = true;
            records.push_back(record);

            DebugLog(
                "Meitou Tracker: discovered " + record.name +
                " [" + record.sid + "] at " + status +
                " holder=" + record.holder +
                " pos=(" + formatPosition(record.position) + ")");
            return;
        }

        Record& record = records[static_cast<size_t>(oldIndex)];
        const bool changed =
            record.status != status ||
            record.holder != holder ||
            record.position.x != position.x ||
            record.position.y != position.y ||
            record.position.z != position.z;

        record.name = name;
        record.sid = sid;
        record.status = status;
        record.holder = holder;
        record.position = position;
        record.seenThisScan = true;

        if (changed)
        {
            DebugLog(
                "Meitou Tracker: update " + record.name +
                " [" + record.sid + "] -> " + record.status +
                " holder=" + record.holder +
                " pos=(" + formatPosition(record.position) + ")");
        }
    }

    static void scanInventory(
        Inventory* inventory,
        const std::string& owner,
        const Ogre::Vector3& ownerPosition,
        int depth)
    {
        if (!inventory || depth > kMaxInventoryDepth)
            return;

        const lektor<InventorySection*>& sections = inventory->getAllSections();

        for (int sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex)
        {
            InventorySection* section = sections[sectionIndex];
            if (!section)
                continue;

            const Ogre::vector<InventorySection::SectionItem>::type& items = section->getItems();
            for (size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex)
            {
                Item* item = items[itemIndex].item;
                if (!item)
                    continue;

                if (isMeitou(item))
                {
                    markFound(
                        item,
                        depth == 0 ? "inventory" : "nested inventory",
                        owner,
                        ownerPosition);
                }

                Inventory* nested = item->getInventory();
                if (nested && nested != inventory)
                {
                    scanInventory(
                        nested,
                        owner + " / container",
                        ownerPosition,
                        depth + 1);
                }
            }
        }
    }

    static void scanNearbyGroundWeapons(const Ogre::Vector3& center)
    {
        if (!ou)
            return;

        lektor<RootObject*> weapons;
        ou->getObjectsWithinSphere(
            weapons,
            center,
            kScanRadius,
            WEAPON,
            kMaxWorldObjects,
            0);

        for (int i = 0; i < weapons.size(); ++i)
        {
            Item* item = weapons[i] ? dynamic_cast<Item*>(weapons[i]) : 0;
            if (!item || !item->onGround())
                continue;

            markFound(
                item,
                "ground",
                "world",
                item->getPosition());
        }
    }

    static void scanNearbyCharacters(const Ogre::Vector3& center)
    {
        if (!ou)
            return;

        lektor<RootObject*> characters;
        ou->getCharactersWithinSphere(
            characters,
            center,
            kScanRadius,
            kScanRadius,
            0.0f,
            kMaxCharacters,
            kMaxCharacters,
            0);

        for (int i = 0; i < characters.size(); ++i)
        {
            Character* character = characters[i]
                ? dynamic_cast<Character*>(characters[i])
                : 0;
            if (!character)
                continue;

            scanInventory(
                character->getInventory(),
                "character: " + character->getName(),
                character->getPosition(),
                0);
        }
    }

    static void scanNearbyBuildings(const Ogre::Vector3& center)
    {
        if (!ou)
            return;

        lektor<RootObject*> buildings;
        ou->getObjectsWithinSphere(
            buildings,
            center,
            kScanRadius,
            BUILDING,
            kMaxBuildings,
            0);

        for (int i = 0; i < buildings.size(); ++i)
        {
            RootObject* building = buildings[i];
            if (!building)
                continue;

            scanInventory(
                building->getInventory(),
                "building: " + building->getName(),
                building->getPosition(),
                0);
        }
    }

    static void scanPlayerCharacters()
    {
        if (!ou || !ou->player)
            return;

        const lektor<Character*>& characters = ou->player->getAllPlayerCharacters();
        for (int i = 0; i < characters.size(); ++i)
        {
            Character* character = characters[i];
            if (!character)
                continue;

            scanInventory(
                character->getInventory(),
                "player: " + character->getName(),
                character->getPosition(),
                0);
        }
    }

    static void report()
    {
        if (records.empty())
        {
            if (ou)
            {
                ou->showPlayerAMessage(
                    "Meitou Tracker: no Meitou weapons have been discovered yet.",
                    false);
            }
            DebugLog("Meitou Tracker: no tracked Meitou weapons.");
            return;
        }

        std::ostringstream summary;
        summary << "Meitou Tracker: " << records.size()
                << " tracked. See the Kenshi log for details.";
        if (ou)
            ou->showPlayerAMessage(summary.str(), false);

        DebugLog("========== Meitou Tracker ==========");

        for (size_t i = 0; i < records.size(); ++i)
        {
            const Record& record = records[i];

            std::ostringstream line;
            line << "#" << (i + 1)
                 << " " << record.name
                 << " [" << record.sid << "]"
                 << " handle=" << record.handle.toString()
                 << " status="
                 << (record.seenThisScan
                        ? record.status
                        : "last known " + record.status)
                 << " holder=" << record.holder
                 << " pos=(" << formatPosition(record.position) << ")";

            DebugLog(line.str());
        }

        DebugLog("====================================");
    }

    static void scan(bool announce)
    {
        if (!ou || ou->isLoadingFromASaveGame())
            return;

        for (size_t i = 0; i < records.size(); ++i)
            records[i].seenThisScan = false;

        Ogre::Vector3 center = ou->getCameraCenter();
        if (ou->player)
        {
            Character* playerCharacter = ou->player->getAnyPlayerCharacter();
            if (playerCharacter)
                center = playerCharacter->getPosition();
        }

        scanNearbyGroundWeapons(center);
        scanNearbyCharacters(center);
        scanNearbyBuildings(center);
        scanPlayerCharacters();

        if (announce)
            report();
    }

    static void toggleAutoScan()
    {
        autoScan = !autoScan;
        lastScanMs = 0;

        if (ou)
        {
            ou->showPlayerAMessage(
                autoScan
                    ? "Meitou Tracker: automatic tracking enabled."
                    : "Meitou Tracker: automatic tracking disabled.",
                false);
        }

        DebugLog(
            autoScan
                ? "Meitou Tracker: automatic tracking enabled."
                : "Meitou Tracker: automatic tracking disabled.");
    }

    static void updateInput()
    {
        if (!key)
            return;

        const bool scanDown = key->isKeyState("MeitouTracker_Scan");
        const bool autoDown = key->isKeyState("MeitouTracker_Auto");

        if (scanDown && !scanWasDown)
            scan(true);

        if (autoDown && !autoWasDown)
            toggleAutoScan();

        scanWasDown = scanDown;
        autoWasDown = autoDown;
    }

    static void updateAuto()
    {
        if (!autoScan)
            return;

        const DWORD now = GetTickCount();
        if (lastScanMs != 0 && now - lastScanMs < kAutoScanIntervalMs)
            return;

        lastScanMs = now;
        scan(false);
    }
}

static void (*gMainLoopOriginal)(GameWorld*, float) = 0;
static void (*gLoadConfigOriginal)(InputHandler*) = 0;
static bool gScanCommand = false;
static bool gAutoCommand = false;

static void registerCommands(InputHandler* handler)
{
    if (!handler)
        return;

    if (handler->commands.find("MeitouTracker_Scan") == handler->commands.end())
    {
        handler->addCommand(
            "MeitouTracker_Scan",
            gScanCommand,
            OIS::KeyCode::KC_F9,
            OIS::KeyCode::KC_UNASSIGNED,
            InputHandler::NONE_MASK,
            InputHandler::GLOBAL);
    }

    if (handler->commands.find("MeitouTracker_Auto") == handler->commands.end())
    {
        handler->addCommand(
            "MeitouTracker_Auto",
            gAutoCommand,
            OIS::KeyCode::KC_F10,
            OIS::KeyCode::KC_UNASSIGNED,
            InputHandler::NONE_MASK,
            InputHandler::GLOBAL);
    }
}

static void loadConfigHook(InputHandler* handler)
{
    registerCommands(handler);
    if (gLoadConfigOriginal)
        gLoadConfigOriginal(handler);
}

static void mainLoopHook(GameWorld* world, float time)
{
    if (gMainLoopOriginal)
        gMainLoopOriginal(world, time);

    MeitouTracker::updateInput();
    MeitouTracker::updateAuto();
}

__declspec(dllexport) void startPlugin()
{
    DebugLog("Meitou Tracker: startPlugin()");

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
        KenshiLib::GetRealAddress(&InputHandler::loadConfig),
        &loadConfigHook,
        &gLoadConfigOriginal))
    {
        DebugLog("Meitou Tracker: could not hook InputHandler::loadConfig");
    }

    if (key)
        registerCommands(key);

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
        KenshiLib::GetRealAddress(&GameWorld::_NV_mainLoop_GPUSensitiveStuff),
        &mainLoopHook,
        &gMainLoopOriginal))
    {
        DebugLog("Meitou Tracker: could not hook GameWorld main loop");
        return;
    }

    DebugLog("Meitou Tracker: initialized. F9=scan, F10=toggle automatic tracking.");
}
