#include "stdafx.h"
#include "GTA\CFileMgr.h"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <xinput.h>
#include <shlobj.h>
#include <fstream>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
#include "miniz.h"

struct Screen
{
    int32_t nWidth;
    int32_t nHeight;
    int32_t nWidth43;
    float fWidth43;
    float fWidth;
    float fHeight;
    float fAspectRatio;
    float fHudScaleX;
    float fHudPosX;
    float fHudOffsetReal;
} Screen;

union HudPos
{
    uint32_t dwPos;
    float fPos;
    HudPos(uint32_t pos) : dwPos(pos) {}
    HudPos(float pos) : fPos(pos) {}
};

class CDatEntry
{
public:
    int32_t dwPosX;
    int32_t dwPosY;
    float fOffsetX;
    float fOffsetY;

    CDatEntry(uint32_t posx, uint32_t posy, float fOffsetX, float fOffsetY)
        : dwPosX(posx), dwPosY(posy), fOffsetX(fOffsetX), fOffsetY(fOffsetY)
    {}
};

void LoadDatFile(std::string_view str, std::function<void(std::string_view line)>&& cb)
{
    if (FILE* hFile = CFileMgr::OpenFile(str.data(), "r"))
    {
        while (const char* pLine = CFileMgr::LoadLine(hFile))
        {
            if (pLine[0] && pLine[0] != '#')
            {
                cb(pLine);
            }
        }
        CFileMgr::CloseFile(hFile);
    }
}

// BlackMagazineFix: with motion blur or light trails the game draws the scene into a texture, and the magazine view's
// background ends up black. The effects are switched off while a magazine is shown and back on when it closes: the
// game rebuilds its render targets for that by itself at the end of the frame (0x40B3F0), as after changing them in
// the options.
uint8_t* byte_735768; // Set while the magazine view is open
namespace MagazineEffects
{
    uint32_t* pMotionBlur = nullptr;  //0x700E98
    uint32_t* pLightTrails = nullptr; //0x700E9C
    uint32_t savedMotionBlur = 0, savedLightTrails = 0;
    bool bSaved = false;

    void Update()
    {
        const bool magazine = *byte_735768 != 0;
        if (magazine && !bSaved && (*pMotionBlur || *pLightTrails))
        {
            savedMotionBlur = *pMotionBlur;
            savedLightTrails = *pLightTrails;
            *pMotionBlur = *pLightTrails = 0;
            bSaved = true;
        }
        else if (!magazine && bSaved)
        {
            *pMotionBlur = savedMotionBlur;
            *pLightTrails = savedLightTrails;
            bSaved = false;
        }
    }
}

// The vinyl layer screen's "vinyl color" (the layer's color picker) answers a console button the PC version has no key
// for there, so it was mouse only. C (keyboard) or the pad's Select (which ImproveGamepadSupport turns into C) sends
// that button's message to the active screen.
// Tells other plugins that C / Select open the vinyl color (the LapRecorder shows the key's icon there only then)
extern "C" __declspec(dllexport) const int VinylColorKey = 1;

namespace VinylColor
{
    const char* pLoadedFng = nullptr;           // 0x72CDD0 (RU 0x72D330), name of the active frontend package
    uintptr_t FEngSendMessageToActive_Addr = 0; // 0x4C96C0, message pushed, package name in eax

    void Send()
    {
        static DWORD lastSent = 0;
        if (!pLoadedFng || !FEngSendMessageToActive_Addr || strcmp(pLoadedFng, "MU_GarageVinylLayerV2.fng") != 0)
            return;
        const DWORD now = GetTickCount();
        if (lastSent && now - lastSent < 300) // Select also sends a C: only once
            return;
        lastSent = now;
        const char* package = pLoadedFng;
        uint32_t message = 0xC519BFC0; // Handled by the vinyl layer screen (0x4E3320)
        uintptr_t send = FEngSendMessageToActive_Addr;
        _asm
        {
            mov eax, package
            push message
            call send
            add esp, 4
        }
    }

    // Called with every frontend message (the screens get one each frame): C pressed while the game is in front
    void PollKeyboard()
    {
        static bool wasDown = false;
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        const bool down = pid == GetCurrentProcessId() && (GetAsyncKeyState('C') & 0x8000);
        const bool pressed = down && !wasDown;
        wasDown = down; // Before Send: its message comes through here again
        if (pressed)
            Send();
    }
}

namespace IconThemes { void OnPackageMessage(const char* pkgname); }

namespace DebugTags
{
    //by xan1242
    //https://github.com/ThirteenAG/WidescreenFixesPack/issues/643#issuecomment-1083564196
    unsigned int FEngSetInvisible_Addr = 0x000495F70;
    void __stdcall FEngSetInvisible(const char* pkgname, unsigned int hash)
    {
        _asm
        {
            mov edi, hash
            mov esi, pkgname
            call FEngSetInvisible_Addr
        }
    }

    unsigned int FEHashUpper_Addr = 0x004FD230;
    unsigned int __stdcall FEHashUpper(const char* instr)
    {
        unsigned int result = 0;
        _asm
        {
            mov edx, instr
            call FEHashUpper_Addr
            mov result, eax
        }
        return result;
    }

    unsigned int FEngFindObject_Addr = 0x004FFB70;
    unsigned int __stdcall FEngFindObject(const char* pkg, int hash)
    {
        unsigned int result = 0;
        _asm
        {
            mov ecx, hash
            mov edx, pkg
            call FEngFindObject_Addr
            mov result, eax
        }
        return result;
    }

    unsigned int FEPkgMgr_FindPackage_Addr = 0x004F65D0;
    unsigned int __stdcall FEPkgMgr_FindPackage(const char* pkgname)
    {
        unsigned int result = 0;
        _asm
        {
            mov eax, pkgname
            call FEPkgMgr_FindPackage_Addr
            mov result, eax
        }
        return result;
    }

    bool bDevObjects = false; // HideDebugObjects = 1

    // Gives a text object a new string, like the game does it (NA/EU 0x41F060, RU 0x41F110; converts the text, then
    // flags the object as changed)
    unsigned int FEStringSetString_Addr = 0x0041F060;
    void FEngSetString(const char* pkgname, unsigned int hash, const char* text)
    {
        unsigned int pkg = FEPkgMgr_FindPackage(pkgname);
        unsigned int obj = pkg ? FEngFindObject((const char*)pkg, hash) : 0;
        if (!obj || *(int*)(obj + 0x18) != 2) // 2 = text
            return;
        reinterpret_cast<void(__stdcall*)(unsigned int, const char*)>(FEStringSetString_Addr)(obj, text);
        *(unsigned int*)(obj + 0x1C) |= 2;
    }

    // The display options: the resolution setting is fixed (see ResolutionHook), so its arrows (and their mouse areas)
    // go, and the 800x600 it always shows becomes the actual resolution
    void FixResolutionOption(const char* pkgname)
    {
        if (FEHashUpper(pkgname) != FEHashUpper("PC_graphics.fng"))
            return;
        FEngSetInvisible(pkgname, FEHashUpper("resolution_arrow_L"));
        FEngSetInvisible(pkgname, FEHashUpper("resolution_arrow_R"));
        FEngSetInvisible(pkgname, FEHashUpper("resolution_L_button"));
        FEngSetInvisible(pkgname, FEHashUpper("resolution_R_button"));
        if (FEStringSetString_Addr)
        {
            char text[32];
            sprintf_s(text, "%d x %d", Screen.nWidth, Screen.nHeight);
            FEngSetString(pkgname, FEHashUpper("Res_02"), text);
        }
    }

    void __stdcall CorruptAllFEDevObjects(char* pkgname)
    {
        FixResolutionOption(pkgname);
        if (!bDevObjects)
            return;

        // so in order to hide the silly little side-tags used during FEng development, an attack at its FEObjects is necessary
        // all of them, and I mean each and every one of the tags' text object hashes and name is "ConduitMdITC_TT21i"
        // because of this, the game only references the first one in memory (which is usually next_tag), so FEngSetInvisible doesn't work
        // ...unless we invalidate/corrupt the NameHash on purpose and that's exactly what this function will do

        unsigned int TestObj = 0;

        // this may be a lot and potentially taxing but it's the best we can do without editing all FNGs themselves
        // hide ALL of those red blocks
        FEngSetInvisible(pkgname, 0x8BDC3C7D);	// QUIT_TAG
        FEngSetInvisible(pkgname, 0x3A8A5C3E);	// QUITTAG
        FEngSetInvisible(pkgname, 0xA1D233EB);	// BACK_TAG
        FEngSetInvisible(pkgname, 0x88C81DEC);	// BACKTAG
        FEngSetInvisible(pkgname, 0x1A705339);	// NEXT_TAG
        FEngSetInvisible(pkgname, 0x2F588B7A);	// NEXTTAG
        FEngSetInvisible(pkgname, 0xFF4BB93F);	// DISPLAYMENUBAR
        FEngSetInvisible(pkgname, 0xD0257F7D);	// DISPLAY_MENUBAR_
        FEngSetInvisible(pkgname, 0x3AE43C3E);	// DeleteProfile_tag
        FEngSetInvisible(pkgname, 0x28894A9D);	// CUSTOMIZE_TAG
        FEngSetInvisible(pkgname, 0x10BE265E);	// CUSTOMIZETAG
        FEngSetInvisible(pkgname, 0xA8ED017E);	// STOCK_TAG
        FEngSetInvisible(pkgname, 0x2426051F);	// STOCKTAG
        FEngSetInvisible(pkgname, 0x7BD9401F);	// changecolor_tag
        FEngSetInvisible(pkgname, 0x134473A0);	// changecolortag
        FEngSetInvisible(pkgname, 0x5B393372);	// DecalColor_tag
        FEngSetInvisible(pkgname, 0xDBF9C893);	// DecalColortag
        FEngSetInvisible(pkgname, 0x502AFE6C);	// performance_tag
        FEngSetInvisible(pkgname, 0x50011C4D);	// performancetag
        FEngSetInvisible(pkgname, 0x677392C4);  // defaultcontsettings_tag

        // hide all text
        // there are many duplicates... eww...
        FEngSetInvisible(pkgname, 0x8C4C21FA); // ConduitMdITC_TT21i(88)
        FEngSetInvisible(pkgname, 0x8C4C2533); // ConduitMdITC_TT21i(90)
        FEngSetInvisible(pkgname, 0x8C4C2554); // ConduitMdITC_TT21i(91)
        FEngSetInvisible(pkgname, 0x8C4C2575); // ConduitMdITC_TT21i(92)
        FEngSetInvisible(pkgname, 0x8C4C2596); // ConduitMdITC_TT21i(93)
        FEngSetInvisible(pkgname, 0x8C4C25B7); // ConduitMdITC_TT21i(94)
        FEngSetInvisible(pkgname, 0x15CC69FE); // ConduitMdITC_TT21i(103)
        FEngSetInvisible(pkgname, 0x15CC6A82); // ConduitMdITC_TT21i(107)
        FEngSetInvisible(pkgname, 0x15CC6AA3); // ConduitMdITC_TT21i(108)
        FEngSetInvisible(pkgname, 0x15CC6AC4); // ConduitMdITC_TT21i(109)
        FEngSetInvisible(pkgname, 0x15CC6DDC); // ConduitMdITC_TT21i(110)
        FEngSetInvisible(pkgname, 0x15CC6EC3); // ConduitMdITC_TT21i(117)
        FEngSetInvisible(pkgname, 0x15CC6EE4); // ConduitMdITC_TT21i(118)

        // there are exceptions we have to watch out for (we're just gonna use the game's hashing function to quickly compare strings)
        // anything that breaks goes here
        // hash can be calclulated simply by using bStringHash or this FEHashUpper ripped from the game
        if (FEHashUpper(pkgname) == 0xAB447B38) // PC_Loading.fng -- removes the loading text (if it would exist, e.g. the beta loading screen)
            return;
        if (FEHashUpper(pkgname) == 0x2729D8C3) // LS_Splash_PC.fng -- removes the copyright text
            return;
        if (FEHashUpper(pkgname) == 0x41613BD0) // MU_Online_NetworkLogin.fng -- removes some buttons
            return;
        if (FEHashUpper(pkgname) == 0x630135FF) // MU_QuickRaceOptions_PC.fng -- removes some buttons
            return;

        // we search for every single test object until it's done (FEngFindObject returns 0 if it can't find any)
        while (FEngFindObject((char*)FEPkgMgr_FindPackage(pkgname), FEHashUpper("ConduitMdITC_TT21i")))
        {
            // once found, catch it in a variable
            TestObj = FEngFindObject((char*)FEPkgMgr_FindPackage(pkgname), FEHashUpper("ConduitMdITC_TT21i"));
            // before corrupting, hide it
            FEngSetInvisible(pkgname, FEHashUpper("ConduitMdITC_TT21i"));
            // after hiding, we can corrupt the hash, this is simply done by changing the NameHash at 0x10 of the FEObject (as per its class)
            *(int*)(TestObj + 0x10) = 0xDEADBEEF;
            // rip and tear... until it's done
        }
    }

    // FEPkgMgr_SendMessageToPackage talks to ALL of FrontEnd, so it's an ideal place to globally affect it
    unsigned int FEPkgMgr_SendMessageToPackage_Cave_Exit = 0x004F7D18;
    unsigned int FEPkgMgr_SendMessageToPackage_EAX = 0;
    unsigned int FEPkgMgr_SendMessageToPackage_ECX = 0;
    unsigned int FEPkgMgr_SendMessageToPackage_ESI = 0;
    void __declspec(naked) FEPkgMgr_SendMessageToPackage_Cave()
    {
        _asm
        {
            mov eax, [esp + 0x50]
            mov ecx, [esp + 0x4C]
            mov FEPkgMgr_SendMessageToPackage_EAX, eax
            mov FEPkgMgr_SendMessageToPackage_ECX, ecx
            mov FEPkgMgr_SendMessageToPackage_ESI, esi
        }
        CorruptAllFEDevObjects(*(char**)(FEPkgMgr_SendMessageToPackage_ESI + 0xC));
        IconThemes::OnPackageMessage(*(char**)(FEPkgMgr_SendMessageToPackage_ESI + 0xC));
        VinylColor::PollKeyboard();
        _asm
        {
            mov eax, FEPkgMgr_SendMessageToPackage_EAX
            mov ecx, FEPkgMgr_SendMessageToPackage_ECX
            mov esi, FEPkgMgr_SendMessageToPackage_ESI
            jmp FEPkgMgr_SendMessageToPackage_Cave_Exit
        }
    }
}

// Clips the frontend to the centered 4:3 area while it draws (like UIScissor in the newer WidescreenFix): what the
// game puts outside of it, e.g. leftover dev tags on the online login or the neighbor cars' names in the car select,
// isn't drawn. Only on screens wider than 4:3, not while a movie plays.
namespace FEClip
{
    LPDIRECT3DDEVICE9* pDevice = nullptr;
    int32_t* pGameState = nullptr;
    void** pMoviePlayer = nullptr;
    uint32_t* pStartValue = nullptr;   // What the start spot loads into EAX
    uint32_t* pRestoreValue = nullptr; // What the second end spot loads into EAX
    uintptr_t RestoreJmpTarget = 0;    // Where the first end spot's JMP goes
    uintptr_t DrawCursorTarget = 0;    // The function that draws the mouse cursor (its position from 0x70649C/0x7064A0)
    const char* pLoadedPackage = nullptr; // Name of the last loaded package (NA/EU 0x72CDD0, RU 0x72D330), nullptr = not found
    bool bActive = false;

    void StartClip()
    {
        if (*pMoviePlayer || !*pDevice)
            return;
        // The menus (game state 3) to 4:3. The race results run in the race (game state 6) and are wider than 4:3:
        // 16:10 there, which still hides the scores the game parks outside of them. In the race the name stays
        // that of the last menu, so it only says PostRaceResults while they are shown.
        float fClipWidth;
        if (*pGameState == 3)
            fClipWidth = Screen.fWidth43;
        else if (*pGameState == 6 && pLoadedPackage && strncmp(pLoadedPackage, "PostRaceResults", 15) == 0)
            fClipWidth = Screen.fHeight * 1.6f;
        else
            return;
        const float fLeft = (Screen.fWidth - fClipWidth) / 2.0f;
        if (fLeft < 1.0f)
            return;
        RECT rect = { static_cast<LONG>(fLeft), 0, static_cast<LONG>(fLeft + fClipWidth), static_cast<LONG>(Screen.nHeight) };
        (*pDevice)->SetScissorRect(&rect);
        (*pDevice)->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        bActive = true;
    }

    void __cdecl StopClip()
    {
        if (!bActive)
            return;
        bActive = false;
        if (*pDevice)
            (*pDevice)->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    }

    // The first end spot is a JMP: end the clipping, then take that JMP
    void __declspec(naked) RestoreJmpCave()
    {
        _asm
        {
            pushfd
            pushad
            call StopClip
            popad
            popfd
            jmp RestoreJmpTarget
        }
    }

    // The mouse cursor is drawn right after the packages: end the clipping before it, so it shows on the whole
    // screen. Called instead of the cursor function, the stack and all registers are passed on unchanged.
    void __declspec(naked) DrawCursorCave()
    {
        _asm
        {
            pushfd
            pushad
            call StopClip
            popad
            popfd
            jmp DrawCursorTarget
        }
    }
}

// The game keeps its keyboard bindings with DirectInput's action mapping (BuildActionMap/SetActionMap on the keyboard
// only, NA/EU/RU 0x405C60/0x407760), which saves them through dimap.dll. Windows doesn't ship dimap.dll anymore, and
// with it the game freezes on some PCs, so without it the keyboard was back to the defaults on every start. These two
// calls are taken over on the device vtable (shared by every IDirectInputDevice8A, but only the keyboard uses them):
// a SetActionMap with DIDSAM_FORCESAVE also writes the format's actions to Keyboard.map next to the game's .cfg files,
// and after BuildActionMap they are put back (same index, uAppData and dwSemantic, so a changed format is ignored)
namespace KeyboardBindings
{
    typedef HRESULT(__stdcall* ActionMapFn)(void* pDevice, LPDIACTIONFORMATA lpdiaf, LPCSTR lpszUserName, DWORD dwFlags);
    ActionMapFn pBuildActionMap = nullptr;
    ActionMapFn pSetActionMap = nullptr;
    std::string Folder; // ...\NFS Underground\ (as the game's .cfg), set in Init

    std::string FilePath() { return Folder + "Keyboard.map"; }

    // Lines: <format name>\t<index>\t<uAppData>\t<dwSemantic>\t<dwObjID>\t<dwHow>
    std::vector<std::string> ReadLines()
    {
        std::vector<std::string> lines;
        if (FILE* f = fopen(FilePath().c_str(), "r"))
        {
            char line[512];
            while (fgets(line, sizeof(line), f))
            {
                std::string text(line);
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                    text.pop_back();
                if (!text.empty())
                    lines.push_back(text);
            }
            fclose(f);
        }
        return lines;
    }

    void Save(LPDIACTIONFORMATA lpdiaf)
    {
        std::string name = lpdiaf->tszActionMap;
        std::vector<std::string> lines;
        for (auto& line : ReadLines())
        {
            if (line.compare(0, name.size() + 1, name + "\t") != 0)
                lines.push_back(line);
        }
        for (DWORD i = 0; i < lpdiaf->dwNumActions; i++)
        {
            const DIACTIONA& action = lpdiaf->rgoAction[i];
            char line[400];
            sprintf_s(line, "%s\t%u\t%u\t%u\t%u\t%u", name.c_str(), i, (unsigned)action.uAppData, action.dwSemantic, action.dwObjID, action.dwHow);
            lines.push_back(line);
        }
        CreateDirectoryA(Folder.c_str(), NULL);
        std::string tmp = FilePath() + ".tmp";
        if (FILE* f = fopen(tmp.c_str(), "w"))
        {
            for (auto& line : lines)
                fprintf(f, "%s\n", line.c_str());
            fclose(f);
            MoveFileExA(tmp.c_str(), FilePath().c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }

    void Restore(LPDIACTIONFORMATA lpdiaf)
    {
        std::string name = lpdiaf->tszActionMap;
        for (auto& line : ReadLines())
        {
            if (line.compare(0, name.size() + 1, name + "\t") != 0)
                continue;
            unsigned index, appData, semantic, objID, how;
            if (sscanf_s(line.c_str() + name.size() + 1, "%u\t%u\t%u\t%u\t%u", &index, &appData, &semantic, &objID, &how) != 5)
                continue;
            if (index >= lpdiaf->dwNumActions)
                continue;
            DIACTIONA& action = lpdiaf->rgoAction[index];
            if (action.uAppData != appData || action.dwSemantic != semantic)
                continue;
            action.dwObjID = objID;
            action.dwHow = how;
        }
    }

    HRESULT __stdcall BuildActionMapHook(void* pDevice, LPDIACTIONFORMATA lpdiaf, LPCSTR lpszUserName, DWORD dwFlags)
    {
        HRESULT hr = pBuildActionMap(pDevice, lpdiaf, lpszUserName, dwFlags);
        if (SUCCEEDED(hr) && lpdiaf && lpdiaf->rgoAction)
            Restore(lpdiaf);
        return hr;
    }

    HRESULT __stdcall SetActionMapHook(void* pDevice, LPDIACTIONFORMATA lpdiaf, LPCSTR lpszUserName, DWORD dwFlags)
    {
        if ((dwFlags & DIDSAM_FORCESAVE) && lpdiaf && lpdiaf->rgoAction)
            Save(lpdiaf);
        return pSetActionMap(pDevice, lpdiaf, lpszUserName, dwFlags);
    }

    // A keyboard device of our own, only to get to the vtable all devices share
    void Install(const std::string& folder)
    {
        Folder = folder;
        HMODULE dinput8 = LoadLibraryA("dinput8.dll");
        auto pDirectInput8Create = dinput8 ? (decltype(&DirectInput8Create))GetProcAddress(dinput8, "DirectInput8Create") : nullptr;
        if (!pDirectInput8Create)
            return;
        static const GUID IID_DirectInput8A = { 0xBF798030, 0x483A, 0x4DA2, { 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00 } };
        static const GUID GUID_Keyboard = { 0x6F1D2B61, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
        IDirectInput8A* pDI = nullptr;
        if (FAILED(pDirectInput8Create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION, IID_DirectInput8A, (void**)&pDI, NULL)) || !pDI)
            return;
        IDirectInputDevice8A* pKeyboard = nullptr;
        if (SUCCEEDED(pDI->CreateDevice(GUID_Keyboard, &pKeyboard, NULL)) && pKeyboard)
        {
            void** vtable = *(void***)pKeyboard;
            pBuildActionMap = (ActionMapFn)vtable[29]; // 0x74
            pSetActionMap = (ActionMapFn)vtable[30];   // 0x78
            injector::WriteMemory(&vtable[29], (void*)BuildActionMapHook, true);
            injector::WriteMemory(&vtable[30], (void*)SetActionMapHook, true);
            pKeyboard->Release();
        }
        pDI->Release();
    }
}

namespace IntroFMVScreen
{
#pragma runtime_checks( "", off )
    bool bOnceFlag = false;
    uintptr_t NotificationMessageFuncAddr = 0x004DE9F0;
    uintptr_t FEngPopPackageAddr = 0x004DE4A0;

    void __stdcall hkNotificationMessage(uint32_t msg, void* FEObject, uint32_t unk1, uint32_t unk2)
    {
        uintptr_t that;
        _asm mov that, ecx

        // pop the package once for the intro skip
        if (!bOnceFlag)
        {
            bOnceFlag = true;
            reinterpret_cast<void(__cdecl*)(char* pkgname)>(FEngPopPackageAddr)(*(char**)(that + 0xC));
            return;
        }

        return reinterpret_cast<void(__thiscall*)(uintptr_t, uint32_t, void*, uint32_t, uint32_t)>(NotificationMessageFuncAddr)(that, msg, FEObject, unk1, unk2);
    }
#pragma runtime_checks( "", restore )
}
HWND WINAPI CreateWindowExA_Hook_NFSU(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    HWND retval = WindowedModeWrapper::CreateWindowExA_Hook(dwExStyle, lpClassName, lpWindowName, 0, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);

    // icon fix
    SendMessage(retval, WM_SETICON, ICON_BIG, (LPARAM)CreateIconFromResourceICO(IDR_NFSUICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)));
    SendMessage(retval, WM_SETICON, ICON_SMALL, (LPARAM)CreateIconFromResourceICO(IDR_NFSUICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)));

    return retval;
}

// MSAA ([GRAPHICS]): the back buffer gets multisampling when the game creates its Direct3D 9 device. The game's
// Direct3DCreate9 import is redirected and CreateDevice is hooked in the vtable of the Direct3D object it gets; the
// device's own methods aren't hooked (the system's runtime keeps the device's vtable in the device and replaces it).
// The game keeps the parameters it passed (with the multisampling now) and uses them again for every reset.
namespace MSAA
{
    int nLevel = 0;

    using Direct3DCreate9_t = IDirect3D9* (WINAPI*)(UINT);
    using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
    Direct3DCreate9_t Direct3DCreate9_Orig = nullptr;
    CreateDevice_t CreateDevice_Orig = nullptr;
    constexpr int CreateDevice_Index = 16; // IDirect3D9 vtable (d3d9.h order)

    // With motion blur or light trails the game draws the scene into a texture (no multisampling in Direct3D 9) with
    // the back buffer's depth buffer; with a multisampled one that breaks the depth test (buildings and cars could be
    // seen through). Right after creating the device the game decides whether the hardware can do both effects: the
    // jump to "yes" is removed, so they're off and greyed out in the options like on hardware without them.
    uint8_t* pEffectsSupportedJump = nullptr; // jne in the check, 0x410C2C (NA)

    // The highest level up to nLevel the card can do for the back buffer and the depth buffer; MSAA needs a discarded
    // swap chain without a lockable back buffer, otherwise the game's parameters stay as they are
    void Apply(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, D3DPRESENT_PARAMETERS* pp)
    {
        if (pp->SwapEffect != D3DSWAPEFFECT_DISCARD || (pp->Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER))
            return;

        D3DFORMAT format = pp->BackBufferFormat;
        if (format == D3DFMT_UNKNOWN) // Windowed: the desktop's format
        {
            D3DDISPLAYMODE mode = {};
            if (FAILED(d3d->GetAdapterDisplayMode(adapter, &mode)))
                return;
            format = mode.Format;
        }

        for (int samples = nLevel; samples >= 2; --samples)
        {
            auto msType = static_cast<D3DMULTISAMPLE_TYPE>(samples);
            if (FAILED(d3d->CheckDeviceMultiSampleType(adapter, type, format, pp->Windowed, msType, nullptr)))
                continue;
            if (pp->EnableAutoDepthStencil && FAILED(d3d->CheckDeviceMultiSampleType(adapter, type, pp->AutoDepthStencilFormat, pp->Windowed, msType, nullptr)))
                continue;
            pp->MultiSampleType = msType;
            pp->MultiSampleQuality = 0;
            return;
        }
    }

    HRESULT STDMETHODCALLTYPE CreateDevice_Hook(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** device)
    {
        if (!pp)
            return CreateDevice_Orig(d3d, adapter, type, window, flags, pp, device);

        D3DPRESENT_PARAMETERS original = *pp;
        Apply(d3d, adapter, type, pp);
        HRESULT hr = CreateDevice_Orig(d3d, adapter, type, window, flags, pp, device);
        if (FAILED(hr) && pp->MultiSampleType != original.MultiSampleType)
        {
            // Not possible after all: as the game wanted it
            *pp = original;
            hr = CreateDevice_Orig(d3d, adapter, type, window, flags, pp, device);
        }
        if (SUCCEEDED(hr) && pp->MultiSampleType != D3DMULTISAMPLE_NONE)
            injector::MakeNOP(pEffectsSupportedJump, 2, true);
        return hr;
    }

    IDirect3D9* WINAPI Direct3DCreate9_Hook(UINT sdkVersion)
    {
        IDirect3D9* d3d = Direct3DCreate9_Orig(sdkVersion);
        if (d3d)
        {
            auto entry = &(*reinterpret_cast<void***>(d3d))[CreateDevice_Index];
            if (*entry != reinterpret_cast<void*>(&CreateDevice_Hook))
            {
                CreateDevice_Orig = reinterpret_cast<CreateDevice_t>(*entry);
                injector::WriteMemory(entry, reinterpret_cast<void*>(&CreateDevice_Hook), true);
            }
        }
        return d3d;
    }

    // The game's import of Direct3DCreate9 (in the import table of every exe version)
    void** FindImport()
    {
        auto base = reinterpret_cast<uint8_t*>(GetModuleHandle(NULL));
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dir.VirtualAddress)
            return nullptr;
        HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
        auto loaded = d3d9 ? reinterpret_cast<uintptr_t>(GetProcAddress(d3d9, "Direct3DCreate9")) : 0;
        for (auto desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc)
        {
            if (_stricmp(reinterpret_cast<char*>(base + desc->Name), "d3d9.dll") != 0)
                continue;
            auto funcs = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
            if (!desc->OriginalFirstThunk)
            {
                // No name list: the entry that holds the loaded function
                for (; funcs->u1.Function; ++funcs)
                    if (loaded && funcs->u1.Function == loaded)
                        return reinterpret_cast<void**>(&funcs->u1.Function);
                continue;
            }
            auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
            for (; names->u1.AddressOfData; ++names, ++funcs)
            {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                    continue;
                auto byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                if (strcmp(reinterpret_cast<char*>(byName->Name), "Direct3DCreate9") == 0)
                    return reinterpret_cast<void**>(&funcs->u1.Function);
            }
        }
        return nullptr;
    }

    void Install(int level)
    {
        nLevel = std::clamp(level, 0, 16);
        if (nLevel < 2)
            return;

        // Without a way to switch the effects off, no MSAA (the picture would break with them)
        auto pattern = hook::pattern("39 15 ? ? ? ? 89 0D ? ? ? ? 75 0F 5F 89 15 ? ? ? ? 89 15 ? ? ? ? 5E C3 5F 89 35");
        if (pattern.size() != 1)
            return;
        pEffectsSupportedJump = pattern.get_first<uint8_t>(12);

        void** import = FindImport();
        if (!import || !*import)
            return;
        Direct3DCreate9_Orig = reinterpret_cast<Direct3DCreate9_t>(*import);
        injector::WriteMemory(import, reinterpret_cast<void*>(&Direct3DCreate9_Hook), true);
    }
}

// The gamepad's right stick, read directly: the game hands over the axes of the device it takes for the frontend,
// which with a second one plugged in (e.g. a joystick) isn't always the gamepad. Of all XInput controllers that report
// themselves as gamepads the one whose right stick is pushed furthest (another idle one would read 0);
// XInput9_1_0.dll is part of Windows since Vista, loaded on first use.
namespace XInputRightStick
{
    using XInputGetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    using XInputGetCapabilities_t = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
    XInputGetState_t pGetState = nullptr;
    XInputGetCapabilities_t pGetCapabilities = nullptr;

    // Axes as the game's: [-10000, 10000], y down. False without such a gamepad (the caller keeps the game's values).
    bool Read(int32_t& x, int32_t& y)
    {
        static bool loaded = false;
        static bool gamepad[XUSER_MAX_COUNT] = {};
        static DWORD nextSearch = 0;
        if (!loaded)
        {
            loaded = true;
            if (HMODULE xinput = LoadLibraryA("XInput9_1_0.dll"))
            {
                pGetState = reinterpret_cast<XInputGetState_t>(GetProcAddress(xinput, "XInputGetState"));
                pGetCapabilities = reinterpret_cast<XInputGetCapabilities_t>(GetProcAddress(xinput, "XInputGetCapabilities"));
            }
        }
        if (!pGetState || !pGetCapabilities)
            return false;

        // Asking unplugged slots is slow: which slots hold a gamepad is looked up again at most every 2 s
        const DWORD now = GetTickCount();
        if (now >= nextSearch)
        {
            nextSearch = now + 2000;
            for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
            {
                XINPUT_CAPABILITIES caps = {};
                gamepad[i] = pGetCapabilities(i, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS && caps.SubType == XINPUT_DEVSUBTYPE_GAMEPAD;
            }
        }

        bool found = false;
        int best = -1;
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
        {
            XINPUT_STATE state = {};
            if (!gamepad[i])
                continue;
            if (pGetState(i, &state) != ERROR_SUCCESS)
            {
                gamepad[i] = false; // Unplugged
                continue;
            }
            const int deflection = (std::max)(std::abs(static_cast<int>(state.Gamepad.sThumbRX)), std::abs(static_cast<int>(state.Gamepad.sThumbRY)));
            if (deflection > best)
            {
                best = deflection;
                x = state.Gamepad.sThumbRX * 10000 / 32767;
                y = -state.Gamepad.sThumbRY * 10000 / 32767;
                found = true;
            }
        }
        return found;
    }
}

// Icon themes ([MISC] GamepadTheme / KeyboardTheme): the game shows 11 key icons (textures PC_*), 8 of them have a pad
// button. A theme is a folder or zip in scripts\buttons\gamepad|keyboard\ with a theme.ini ([Icons]: position or key =
// PNG file; [Names] and [Names.<language code>]: button names in the controls menu) and its PNGs. Its icons go into a
// texture pack of their own built in memory (names of their own: a pack with the game's names doesn't win against the
// game's packs) and loaded with the game's chunk loader; the menus' image objects that show a PC_* texture are then
// pointed to them. Missing icons stay the original ones, missing names the Xbox ones.
namespace IconThemes
{
    // bStringHash, the hash the game uses for texture names
    uint32_t Hash(const char* str)
    {
        uint32_t result = 0xFFFFFFFF;
        for (; *str; ++str)
            result = static_cast<uint8_t>(*str) + 33 * result;
        return result;
    }

    // The game's key icons: texture, key in a keyboard theme, position in a gamepad theme (nullptr = keyboard only)
    struct KeyIcon { const char* texture; const char* key; const char* pad; };
    constexpr KeyIcon KeyIcons[] =
    {
        { "PC_X",           "Enter",     "A"    },
        { "PC_BACK",        "Backspace", "B"    },
        { "PC_PERFORMANCE", "P",         "X"    },
        { "PC_CREATEROOM",  "Z",         "Y"    },
        { "PC_CUSTOM",      "C",         "View" },
        { "PC_TUTORIAL",    "T",         "LS"   },
        { "PC_DELETE",      "D",         "RS"   },
        { "PC_QUIT",        "Q",         "Quit" },
        { "PC_AGREEMENT",   "A",         nullptr },
        { "PC_HELP",        "H",         nullptr },
        { "PC_RACEOP",      "R",         nullptr },
    };

    // Controls menu names, in the order of the game's button numbers 0-9, then d-pad, axes, triggers, POV, Share
    constexpr const char* NameKeys[] = { "A", "B", "X", "Y", "LB", "RB", "View", "Menu", "LS", "RS", "DPadUp", "DPadDown",
        "DPadLeft", "DPadRight", "RStickX", "RStickY", "LStickX", "LStickY", "Triggers", "DPad", "Share" };

    struct Image { int size = 0; std::vector<uint8_t> rgba; };
    using Section = std::map<std::string, std::string>;

    struct Theme
    {
        std::map<std::string, Section> ini;         // section (lower case) -> key (lower case) -> value
        std::map<std::string, std::vector<uint8_t>> files; // file name (lower case) -> contents
    };

    std::string Lower(std::string s)
    {
        for (auto& c : s)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        return s;
    }

    std::string Trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? "" : s.substr(a, b - a + 1);
    }

    void ParseIni(const std::vector<uint8_t>& data, Theme& theme)
    {
        std::string text(data.begin(), data.end());
        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) // UTF-8 BOM
            text.erase(0, 3);
        std::string section;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            std::string line = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            pos = end == std::string::npos ? text.size() : end + 1;
            if (size_t comment = line.find(';'); comment != std::string::npos)
                line.erase(comment);
            line = Trim(line);
            if (line.empty())
                continue;
            if (line.front() == '[' && line.back() == ']')
                section = Lower(Trim(line.substr(1, line.size() - 2)));
            else if (size_t eq = line.find('='); eq != std::string::npos)
                theme.ini[section][Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
        }
    }

    bool ReadFile(const std::wstring& path, std::vector<uint8_t>& data)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;
        data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
    }

    // scripts\buttons\<kind>\<name>\ (wins) or <name>.zip, whose files may also sit in one folder inside it
    bool LoadTheme(const std::string& kind, const std::string& name, Theme& theme)
    {
        if (name.empty() || Lower(name) == "original")
            return false;
        std::wstring base = GetThisModulePath<std::wstring>() + L"buttons\\" + std::wstring(kind.begin(), kind.end()) + L"\\";
        std::wstring wname(name.begin(), name.end());

        std::error_code ec;
        std::filesystem::path folder = base + wname;
        if (std::filesystem::is_directory(folder, ec))
        {
            for (auto& entry : std::filesystem::directory_iterator(folder, ec))
            {
                std::vector<uint8_t> data;
                if (entry.is_regular_file(ec) && ReadFile(entry.path().wstring(), data))
                    theme.files[Lower(entry.path().filename().string())] = std::move(data);
            }
        }
        else
        {
            std::vector<uint8_t> zipData;
            if (!ReadFile(base + wname + L".zip", zipData))
                return false;
            mz_zip_archive zip = {};
            if (!mz_zip_reader_init_mem(&zip, zipData.data(), zipData.size(), 0))
                return false;
            for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); i++)
            {
                mz_zip_archive_file_stat stat;
                if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_is_directory)
                    continue;
                std::string path = stat.m_filename;
                std::string file = path.substr(path.find_last_of("/\\") + 1);
                size_t size = 0;
                if (void* p = mz_zip_reader_extract_to_heap(&zip, i, &size, 0))
                {
                    theme.files[Lower(file)].assign(static_cast<uint8_t*>(p), static_cast<uint8_t*>(p) + size);
                    mz_free(p);
                }
            }
            mz_zip_reader_end(&zip);
        }

        auto ini = theme.files.find("theme.ini");
        if (ini == theme.files.end())
            return false;
        ParseIni(ini->second, theme);
        return true;
    }

    // The theme's [Theme] Zoom: > 1 makes its icons bigger (cuts the margin around them), < 1 smaller
    double Zoom(const Theme& theme)
    {
        auto section = theme.ini.find("theme");
        if (section == theme.ini.end())
            return 1.0;
        auto zoom = section->second.find("zoom");
        if (zoom == section->second.end())
            return 1.0;
        double value = strtod(zoom->second.c_str(), nullptr);
        return (value >= 0.25 && value <= 4.0) ? value : 1.0;
    }

    // Averages pixels by the share of them that's covered, colors also weighted by their alpha, so transparent pixels
    // don't darken the edges
    struct Average
    {
        double color[3] = {}, alpha = 0, weight = 0;
        void Add(const uint8_t* p, double share = 1.0)
        {
            for (int c = 0; c < 3; c++)
                color[c] += p[c] * p[3] * share;
            alpha += p[3] * share;
            weight += share;
        }
        void AddTransparent(double share) { weight += share; }
        void Store(uint8_t* p) const
        {
            for (int c = 0; c < 3; c++)
                p[c] = alpha > 0 ? uint8_t((std::min)(255.0, color[c] / alpha + 0.5)) : 0;
            p[3] = weight > 0 ? uint8_t((std::min)(255.0, alpha / weight + 0.5)) : 0;
        }
    };

    // Scales the area (sx, sy, sw, sh) of an RGBA image (w x h; outside of it transparent) to (dx, dy, dw, dh) of a
    // square one: each target pixel averages the source pixels it covers, by the share of them it covers
    void Resample(const uint8_t* src, int w, int h, double sx, double sy, double sw, double sh, Image& dst, int dx, int dy, int dw, int dh)
    {
        for (int y = 0; y < dh; y++)
        {
            double fy0 = sy + y * sh / dh, fy1 = sy + (y + 1) * sh / dh;
            for (int x = 0; x < dw; x++)
            {
                double fx0 = sx + x * sw / dw, fx1 = sx + (x + 1) * sw / dw;
                Average average;
                for (int py = int(floor(fy0)); py < fy1; py++)
                {
                    double shareY = (std::min)(fy1, py + 1.0) - (std::max)(fy0, double(py));
                    for (int px = int(floor(fx0)); px < fx1; px++)
                    {
                        double share = shareY * ((std::min)(fx1, px + 1.0) - (std::max)(fx0, double(px)));
                        if (px >= 0 && px < w && py >= 0 && py < h)
                            average.Add(&src[(size_t(py) * w + px) * 4], share);
                        else
                            average.AddTransparent(share);
                    }
                }
                average.Store(&dst.rgba[(size_t(dy + y) * dst.size + dx + x) * 4]);
            }
        }
    }

    // Square, a power of two from 16 to 128, RGBA. Only scaled down (by the area each pixel covers), so the part of
    // the image that is shown decides the size: a 64x64 icon with Zoom 1.15 becomes 32x32, a 128x128 one 64x64
    bool DecodeImage(const Theme& theme, const std::string& file, Image& image)
    {
        auto it = theme.files.find(Lower(file));
        if (it == theme.files.end())
            return false;
        int w = 0, h = 0, n = 0;
        stbi_uc* pixels = stbi_load_from_memory(it->second.data(), int(it->second.size()), &w, &h, &n, 4);
        if (!pixels)
            return false;
        // The centered part of the image that is shown (bigger than the image with a zoom below 1, transparent around)
        double zoom = Zoom(theme);
        double sw = w / zoom, sh = h / zoom, ox = (w - sw) / 2, oy = (h - sh) / 2;
        int size = 16;
        while (size * 2 <= (std::min)(sw, sh) && size < 128)
            size *= 2;
        image.size = size;
        image.rgba.resize(size_t(size) * size * 4);
        Resample(pixels, w, h, ox, oy, sw, sh, image, 0, 0, size, size);
        stbi_image_free(pixels);
        return true;
    }

    Image Half(const Image& image)
    {
        Image half;
        half.size = image.size / 2;
        half.rgba.resize(size_t(half.size) * half.size * 4);
        for (int y = 0; y < half.size; y++)
            for (int x = 0; x < half.size; x++)
            {
                Average average;
                for (int dy = 0; dy < 2; dy++)
                    for (int dx = 0; dx < 2; dx++)
                        average.Add(&image.rgba[(size_t(y * 2 + dy) * image.size + x * 2 + dx) * 4]);
                average.Store(&half.rgba[(size_t(y) * half.size + x) * 4]);
            }
        return half;
    }

    // Quit without an icon of its own: the LS and RS icons side by side, without the empty margins around them, as
    // big as fits with the margin of the icons
    Image SideBySide(const Image& left, const Image& right)
    {
        struct Box { int x0, y0, x1, y1; };
        auto bounds = [](const Image& image) // of what isn't transparent
        {
            Box box = { image.size, image.size, 0, 0 };
            for (int y = 0; y < image.size; y++)
                for (int x = 0; x < image.size; x++)
                    if (image.rgba[(size_t(y) * image.size + x) * 4 + 3])
                        box = { (std::min)(box.x0, x), (std::min)(box.y0, y), (std::max)(box.x1, x + 1), (std::max)(box.y1, y + 1) };
            if (box.x1 <= box.x0)
                box = { 0, 0, image.size, image.size };
            return box;
        };
        Box l = bounds(left), r = bounds(right);
        int size = left.size;
        int margin = (std::min)({ l.x0, l.y0, size - l.x1, size - l.y1, r.x0, r.y0, size - r.x1, size - r.y1 });
        double gap = size / 16.0;
        double width = (l.x1 - l.x0) + gap + (r.x1 - r.x0), height = (std::max)(l.y1 - l.y0, r.y1 - r.y0);
        double scale = (std::min)((size - 2 * margin) / width, (size - 2 * margin) / height);

        Image image;
        image.size = size;
        image.rgba.assign(size_t(size) * size * 4, 0);
        int lw = int(lround((l.x1 - l.x0) * scale)), rw = int(lround((r.x1 - r.x0) * scale));
        int lh = int(lround((l.y1 - l.y0) * scale)), rh = int(lround((r.y1 - r.y0) * scale));
        int x = (size - (lw + int(lround(gap * scale)) + rw)) / 2;
        Resample(left.rgba.data(), size, size, l.x0, l.y0, l.x1 - l.x0, l.y1 - l.y0, image, x, (size - lh) / 2, lw, lh);
        Resample(right.rgba.data(), size, size, r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0, image, size - x - rw, (size - rh) / 2, rw, rh);
        return image;
    }

    // Fully transparent pixels get the color of their visible neighbors (spreading outwards), not black: the game
    // filters between pixels when drawing, and their color would show as a dark edge around the icon
    void Bleed(Image& image)
    {
        int size = image.size;
        std::vector<uint8_t> known(size_t(size) * size);
        for (size_t i = 0; i < known.size(); i++)
            known[i] = image.rgba[i * 4 + 3] != 0;
        for (bool changed = true; changed;)
        {
            changed = false;
            std::vector<uint8_t> next = known;
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++)
                {
                    size_t i = size_t(y) * size + x;
                    if (known[i])
                        continue;
                    uint32_t sum[3] = {}, count = 0;
                    for (int ny = (std::max)(0, y - 1); ny <= (std::min)(size - 1, y + 1); ny++)
                        for (int nx = (std::max)(0, x - 1); nx <= (std::min)(size - 1, x + 1); nx++)
                            if (size_t n = size_t(ny) * size + nx; known[n])
                            {
                                for (int c = 0; c < 3; c++)
                                    sum[c] += image.rgba[n * 4 + c];
                                count++;
                            }
                    if (count)
                    {
                        for (int c = 0; c < 3; c++)
                            image.rgba[i * 4 + c] = uint8_t(sum[c] / count);
                        next[i] = 1;
                        changed = true;
                    }
                }
            known = std::move(next);
        }
    }

    // A8R8G8B8 (BGRA in memory) with all mip levels
    std::vector<uint8_t> Pixels(const Image& image, int& mips)
    {
        std::vector<uint8_t> data;
        Image level = image;
        mips = 0;
        while (true)
        {
            Bleed(level);
            for (size_t i = 0; i < level.rgba.size(); i += 4)
            {
                data.push_back(level.rgba[i + 2]);
                data.push_back(level.rgba[i + 1]);
                data.push_back(level.rgba[i + 0]);
                data.push_back(level.rgba[i + 3]);
            }
            mips++;
            if (level.size == 1)
                break;
            level = Half(level);
        }
        return data;
    }

    struct Texture { std::string name; uint32_t hash; int size; int mips; std::vector<uint8_t> pixels; };

    // A texture pack (TPK version 4) laid out like the game's own
    std::vector<uint8_t> BuildPack(std::vector<Texture> textures, const char* packName)
    {
        // The game looks textures up with a binary search over the hash list
        std::sort(textures.begin(), textures.end(), [](const Texture& a, const Texture& b) { return a.hash < b.hash; });

        std::vector<uint8_t> w;
        auto put = [&](const void* p, size_t n) { w.insert(w.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n); };
        auto put32 = [&](uint32_t v) { put(&v, 4); };
        auto begin = [&](uint32_t id) { put32(id); put32(0); return w.size(); };
        auto end = [&](size_t start) { *reinterpret_cast<uint32_t*>(&w[start - 4]) = uint32_t(w.size() - start); };
        auto align = [&](size_t alignment) // an empty chunk, so that the next chunk starts at a multiple of alignment
        {
            size_t padding = (alignment - (w.size() + 8) % alignment) % alignment;
            put32(0); put32(uint32_t(padding)); w.insert(w.end(), padding, 0);
        };
        auto putString = [&](const char* s, size_t size) { std::vector<char> f(size, 0); strncpy_s(f.data(), size, s, _TRUNCATE); put(f.data(), size); };
        uint32_t packHash = Hash(packName);

        auto capsule = begin(0xB3300000);
        align(0x40);
        auto info = begin(0xB3310000);
        {
            auto header = begin(0x33310001);
            put32(4); // version
            putString(packName, 0x1C);
            putString(packName, 0x40);
            put32(packHash);
            for (int i = 0; i < 6; i++)
                put32(0);
            end(header);

            auto hashes = begin(0x33310002);
            for (auto& t : textures)
            {
                put32(t.hash);
                put32(0);
            }
            end(hashes);

            auto infos = begin(0x33310004);
            int32_t placement = 0;
            for (auto& t : textures)
            {
                uint8_t e[0x7C] = {};
                strncpy_s(reinterpret_cast<char*>(e + 0x0C), 24, t.name.c_str(), _TRUNCATE);
                *reinterpret_cast<uint32_t*>(e + 0x24) = t.hash;
                *reinterpret_cast<uint32_t*>(e + 0x28) = 0x1A93CF;              // class name hash
                *reinterpret_cast<int32_t*>(e + 0x30) = placement;
                *reinterpret_cast<int32_t*>(e + 0x38) = int32_t(t.pixels.size());
                *reinterpret_cast<int32_t*>(e + 0x40) = t.size * t.size * 4;     // size of the top level
                *reinterpret_cast<int16_t*>(e + 0x44) = int16_t(t.size);
                *reinterpret_cast<int16_t*>(e + 0x46) = int16_t(t.size);
                uint8_t shift = 0;
                while ((1 << shift) < t.size)
                    shift++;
                e[0x48] = e[0x49] = shift;                                       // log2 width/height
                e[0x4A] = 0x20;                                                  // uncompressed
                e[0x4E] = uint8_t(t.mips);
                e[0x51] = 5;                                                     // rendering order
                e[0x55] = 2;                                                     // alpha usage
                e[0x56] = 1;                                                     // alpha blend
                *reinterpret_cast<int16_t*>(e + 0x62) = 0x100;                   // offset T
                *reinterpret_cast<int16_t*>(e + 0x64) = 0x100;                   // scale S
                put(e, sizeof(e));
                placement += int32_t(t.pixels.size());
            }
            end(infos);

            auto formats = begin(0x33310005);
            for (size_t i = 0; i < textures.size(); i++)
            {
                uint32_t f[8] = { 0, 0, 0, 5, 6, 0x15 /* D3DFMT_A8R8G8B8 */, 0, 0 };
                put(f, sizeof(f));
            }
            end(formats);
        }
        end(info);

        align(0x80);
        auto data = begin(0xB3320000);
        {
            auto link = begin(0x33320001);
            put32(0); put32(0); put32(1); put32(packHash); put32(0); put32(0);
            end(link);
            align(0x80);
            auto images = begin(0x33320002);
            w.insert(w.end(), 0x78, 0x11); // the images start on a 0x80 boundary
            for (auto& t : textures)
                put(t.pixels.data(), t.pixels.size());
            end(images);
        }
        end(data);
        end(capsule);
        return w;
    }

    Theme Pad, Keyboard;
    bool bPadIcons = false;
    std::vector<uint8_t> Pack;                       // built in Init, handed to the game with the first front end message
    std::map<uint32_t, uint32_t> Redirect;           // PC_* texture hash -> theme texture hash
    std::map<std::string, std::string> RedirectNames; // PC_* texture name -> theme texture name (for ButtonTexture)
    char* (__cdecl* bMalloc)(int size, int debugName, int debugLine, int flags) = nullptr;
    bool (__cdecl* LoadChunksFromMemory)(char** buffer, int* size, int allocFlags, const char* debugName) = nullptr;

    void Install(const std::string& padTheme, bool padIcons, const std::string& keyboardTheme)
    {
        auto alloc = hook::pattern("57 57 50 E8 ? ? ? ? 8B 4C 24 ? 68 00 20 00 00"); //0x447DFE
        auto loader = hook::pattern("57 E8 ? ? ? ? 8A 46 0C 83 C4 10 A8 04");         //0x4482A6
        if (alloc.empty() || loader.empty())
            return;
        bMalloc = reinterpret_cast<decltype(bMalloc)>(injector::GetBranchDestination(alloc.get_first(3)).as_int());
        LoadChunksFromMemory = reinterpret_cast<decltype(LoadChunksFromMemory)>(injector::GetBranchDestination(loader.get_first(1)).as_int());

        bPadIcons = padIcons && LoadTheme("gamepad", padTheme, Pad);
        bool keyboard = LoadTheme("keyboard", keyboardTheme, Keyboard);

        std::vector<Texture> textures;
        auto add = [&](const std::string& name, const Image& image)
        {
            Texture t;
            t.name = name;
            for (auto& c : t.name)
                c = static_cast<char>(toupper(static_cast<unsigned char>(c))); // like the game's texture names
            t.hash = Hash(t.name.c_str());
            t.size = image.size;
            t.pixels = Pixels(image, t.mips);
            textures.push_back(std::move(t));
            return textures.size() - 1;
        };
        auto icon = [](const Theme& theme, const char* key, Image& image)
        {
            auto section = theme.ini.find("icons");
            if (section == theme.ini.end())
                return false;
            auto file = section->second.find(Lower(key));
            return file != section->second.end() && DecodeImage(theme, file->second, image);
        };

        for (const auto& k : KeyIcons)
        {
            Image image;
            size_t target = SIZE_MAX;
            if (bPadIcons && k.pad)
            {
                if (icon(Pad, k.pad, image))
                    target = add(std::string("WSF_PAD_") + k.pad, image);
                else if (!strcmp(k.pad, "Quit"))
                {
                    Image ls, rs;
                    if (icon(Pad, "LS", ls) && icon(Pad, "RS", rs) && ls.size == rs.size)
                        target = add("WSF_PAD_QUIT", SideBySide(ls, rs));
                }
            }
            if (target == SIZE_MAX && keyboard && icon(Keyboard, k.key, image))
                target = add(std::string("WSF_KEY_") + k.key, image);
            if (target != SIZE_MAX)
            {
                Redirect[Hash(k.texture)] = textures[target].hash;
                RedirectNames[k.texture] = textures[target].name;
            }
        }
        if (!textures.empty())
            Pack = BuildPack(textures, "WSF_ICON_THEMES");
    }

    // The objects of a front end package (list at +0x40, a group's children at +0x60, next at +4, type at +0x18,
    // texture hash at +0x24, flags at +0x1C) whose texture is one of the game's key icons get the theme's
    void RedirectObjects(uintptr_t object)
    {
        for (; object; object = *reinterpret_cast<uintptr_t*>(object + 0x04))
        {
            uint32_t type = *reinterpret_cast<uint32_t*>(object + 0x18);
            if (type == 5) // group
                RedirectObjects(*reinterpret_cast<uintptr_t*>(object + 0x60));
            else if (type == 1 || (type >= 9 && type <= 0xC)) // image kinds
            {
                auto& handle = *reinterpret_cast<uint32_t*>(object + 0x24);
                if (auto it = Redirect.find(handle); it != Redirect.end())
                {
                    handle = it->second;
                    *reinterpret_cast<uint32_t*>(object + 0x1C) |= 0x2400000;
                }
            }
        }
    }

    // With every front end message: loads the pack once, then points the package's icons to it
    void OnPackageMessage(const char* pkgname)
    {
        if (Redirect.empty() || !pkgname)
            return;
        if (!Pack.empty())
        {
            int size = int(Pack.size());
            char* buffer = bMalloc(size, 0, 0, 0);
            if (!buffer)
                return;
            memcpy(buffer, Pack.data(), Pack.size());
            Pack.clear();
            Pack.shrink_to_fit();
            LoadChunksFromMemory(&buffer, &size, 0, "WSF_ICON_THEMES"); // the loader frees the buffer
        }
        if (uintptr_t pkg = DebugTags::FEPkgMgr_FindPackage(pkgname))
            RedirectObjects(*reinterpret_cast<uintptr_t*>(pkg + 0x40));
    }

    // Language code of the game's active language (for [Names.<code>]), from its language table. The language the
    // game set last (NA/EU 0x6EE6F0, RU 0x6EE778; the language menu sets it too), else the one from the registry
    uint32_t* pLanguage = nullptr;
    int32_t* pActiveLanguage = nullptr;
    const uint32_t* pLanguageTable = nullptr; // name, language, region
    std::string LanguageCode()
    {
        if (!pActiveLanguage)
            if (auto set = hook::pattern("8B 0D ? ? ? ? 57 8B F8 3B F9 0F 84 ? ? ? ? 53 33 DB 83 F9 FF"); !set.empty()) //0x59FA20
                pActiveLanguage = *set.get_first<int32_t*>(2);
        uint32_t language = (pActiveLanguage && *pActiveLanguage != -1) ? uint32_t(*pActiveLanguage) : pLanguage ? *pLanguage : uint32_t(-1);
        if (language == uint32_t(-1) || !pLanguageTable)
            return "";
        static const std::map<std::string, std::string> codes = { { "french", "fr" }, { "german", "de" }, { "italian", "it" },
            { "spanish", "es" }, { "dutch", "nl" }, { "swedish", "sv" }, { "russian", "ru" }, { "korean", "ko" },
            { "chinese (traditional)", "zh" }, { "japanese", "ja" } };
        for (auto entry = pLanguageTable; entry[0]; entry += 3)
        {
            if (entry[1] == language)
            {
                auto it = codes.find(Lower(reinterpret_cast<const char*>(entry[0])));
                return it != codes.end() ? it->second : "";
            }
        }
        return "";
    }

    // The controls menu's button names: [Names.<code>], then [Names], then the given defaults
    std::vector<std::wstring> Names(const std::vector<std::wstring>& defaults)
    {
        std::vector<std::wstring> names = defaults;
        names.resize(std::size(NameKeys));
        if (!bPadIcons)
            return names;
        std::string code = LanguageCode();
        for (size_t i = 0; i < std::size(NameKeys); i++)
        {
            for (const std::string& section : { std::string("names.") + code, std::string("names") })
            {
                auto s = Pad.ini.find(section);
                if (section == "names." || s == Pad.ini.end())
                    continue;
                auto value = s->second.find(Lower(NameKeys[i]));
                if (value == s->second.end())
                    continue;
                wchar_t text[64] = {};
                MultiByteToWideChar(CP_UTF8, 0, value->second.c_str(), -1, text, 64);
                names[i] = text;
                break;
            }
        }
        return names;
    }
}

// Tells other plugins which texture the menus show instead of one of the game's key icons (e.g. PC_CUSTOM ->
// WSF_PAD_VIEW; the LapRecorder draws it at the vinyl color), the name itself when the theme has none for it
extern "C" __declspec(dllexport) const char* ButtonTexture(const char* name)
{
    if (!name)
        return name;
    auto it = IconThemes::RedirectNames.find(name);
    return it != IconThemes::RedirectNames.end() ? it->second.c_str() : name;
}

// The ini reader's ReadInteger throws on an empty or non-number value (std::stoi), which crashed the game at start
// ("FixHUD =" without a value): such a value counts as missing
static int ReadIniInteger(CIniReader& ini, const char* section, const char* key, int defaultValue)
{
    try
    {
        return ini.ReadInteger(section, key, defaultValue);
    }
    catch (...)
    {
        return defaultValue;
    }
}

void Init()
{
    CIniReader iniReader("");
    Screen.nWidth = ReadIniInteger(iniReader, "MAIN", "ResX", 0);
    Screen.nHeight = ReadIniInteger(iniReader, "MAIN", "ResY", 0);
    bool bFixHUD = ReadIniInteger(iniReader, "MAIN", "FixHUD", 1) != 0;
    bool bFixFOV = ReadIniInteger(iniReader, "MAIN", "FixFOV", 1) != 0;
    bool bScaling = ReadIniInteger(iniReader, "MAIN", "Scaling", 0);
    bool bHUDWidescreenMode = ReadIniInteger(iniReader, "MAIN", "HUDWidescreenMode", 1) != 0;
    int nFMVWidescreenMode = ReadIniInteger(iniReader, "MAIN", "FMVWidescreenMode", 1);
    bool bSkipIntro = ReadIniInteger(iniReader, "MISC", "SkipIntro", 0) != 0;
    bool bShowLangSelect = ReadIniInteger(iniReader, "MISC", "ShowLangSelect", 0) != 0;
    static auto szCustomUserFilesDirectoryInGameDir = iniReader.ReadString("MISC", "CustomUserFilesDirectoryInGameDir", "0");
    static int nImproveGamepadSupport = ReadIniInteger(iniReader, "MISC", "ImproveGamepadSupport", 0);
    static float fLeftStickDeadzone = iniReader.ReadFloat("MISC", "LeftStickDeadzone", 0.0f);
    static int nFPSLimit = ReadIniInteger(iniReader, "MISC", "FPSLimit", 0);
    int nHideDebugObjects = ReadIniInteger(iniReader, "MISC", "HideDebugObjects", 1);
    bool bClipFrontend = ReadIniInteger(iniReader, "MISC", "ClipFrontend", 1) != 0;
    if (!bFixHUD)
    {
        // HUD and frontend stretched over the whole screen: nothing sits in a 4:3 area, so neither moving the HUD
        // to the edges nor clipping to 4:3 makes sense
        bHUDWidescreenMode = false;
        bClipFrontend = false;
    }
    bool bBlackMagazineFix = ReadIniInteger(iniReader, "MISC", "BlackMagazineFix", 1) != 0;
    bool bSaveKeyboardBindings = ReadIniInteger(iniReader, "MISC", "SaveKeyboardBindings", 1) != 0;
    if (szCustomUserFilesDirectoryInGameDir.empty() || szCustomUserFilesDirectoryInGameDir == "0")
        szCustomUserFilesDirectoryInGameDir.clear();
    int nWindowedMode = ReadIniInteger(iniReader, "MISC", "WindowedMode", 0);
    std::string szLanguage = iniReader.ReadString("MISC", "Language", "");
    if (szLanguage == "0")
        szLanguage.clear();
    // Icon themes; the former ImproveGamepadSupport = 2 / 3 are PlayStation icons / the original ones
    std::string szGamepadTheme = iniReader.ReadString("MISC", "GamepadTheme", "");
    if (szGamepadTheme.empty() || szGamepadTheme == "0")
        szGamepadTheme = (nImproveGamepadSupport == 2) ? "playstation" : (nImproveGamepadSupport == 3) ? "original" : "xbox";
    std::string szKeyboardTheme = iniReader.ReadString("MISC", "KeyboardTheme", "original");
    static bool bPlayStationNames = (IconThemes::Lower(szGamepadTheme) == "playstation");
    MSAA::Install(ReadIniInteger(iniReader, "GRAPHICS", "MSAA", 0));

    if (!Screen.nWidth || !Screen.nHeight)
        std::tie(Screen.nWidth, Screen.nHeight) = GetDesktopRes();

    // clamp the size because below 32x32 the game crashes!
    if (Screen.nWidth < 32)
        Screen.nWidth = 32;

    if (Screen.nHeight < 32)
        Screen.nHeight = 32;

    Screen.fWidth = static_cast<float>(Screen.nWidth);
    Screen.fHeight = static_cast<float>(Screen.nHeight);
    Screen.nWidth43 = static_cast<uint32_t>(Screen.fHeight * (4.0f / 3.0f));
    Screen.fWidth43 = static_cast<float>(Screen.nWidth43);
    Screen.fAspectRatio = (Screen.fWidth / Screen.fHeight);
    Screen.fHudScaleX = (1.0f / Screen.fWidth * (Screen.fHeight / 480.0f)) * 2.0f;
    Screen.fHudPosX = 640.0f / (640.0f * Screen.fHudScaleX);
    Screen.fHudOffsetReal = (Screen.fWidth - Screen.fHeight * (4.0f / 3.0f)) / 2.0f;

    //Game state
    static auto nGameState = std::ref(**hook::get_pattern<int32_t*>("83 3D ? ? ? ? 06 ? ? A1", 2)); //0x77A920

    //Resolution
    //menu
    auto pattern = hook::pattern("68 20 03 00 00 BE 58 02 00 00");
    injector::WriteMemory(pattern.get_first(1), Screen.nWidth, true);
    injector::WriteMemory(pattern.get_first(6), Screen.nHeight, true);

    pattern = hook::pattern("68 20 03 00 00 BE 58 02 00 00");
    injector::WriteMemory(pattern.get_first(1), Screen.nWidth, true);
    injector::WriteMemory(pattern.get_first(6), Screen.nHeight, true);

    //game
    pattern = hook::pattern("B8 20 03 00 00 BE 58 02 00 00 77 75");
    injector::WriteMemory(pattern.get_first(1), Screen.nWidth, true);
    injector::WriteMemory(pattern.get_first(6), Screen.nHeight, true);
    injector::MakeNOP(pattern.get_first(10), 9, true);
    pattern = hook::pattern("B8 80 02 00 00 BE E0 01 00 00 50");
    injector::WriteMemory(pattern.get_first(1), Screen.nWidth, true);
    injector::WriteMemory(pattern.get_first(6), Screen.nHeight, true);
    pattern = hook::pattern("B8 20 03 00 00 BE 58 02 00 00 50");
    injector::WriteMemory(pattern.get_first(1), Screen.nWidth, true);
    injector::WriteMemory(pattern.get_first(6), Screen.nHeight, true);
    pattern = hook::pattern("B8 00 04 00 00 BE 00 03 00 00 50");
    injector::WriteMemory(pattern.get_first(1), Screen.nWidth, true);
    injector::WriteMemory(pattern.get_first(6), Screen.nHeight, true);
    pattern = hook::pattern("B8 00 05 00 00 BE C0 03 00 00 50");
    injector::WriteMemory(pattern.get_first(1), Screen.nWidth, true);
    injector::WriteMemory(pattern.get_first(6), Screen.nHeight, true);
    // initial ResX and ResY
    uint32_t* dword_701034 = *hook::pattern("83 EB 00 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ?").count(1).get(0).get<uint32_t*>(6); // ResX
    uint32_t* dword_701038 = *hook::pattern("83 EB 00 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ?").count(1).get(0).get<uint32_t*>(13); // ResY
    *dword_701034 = Screen.nWidth;
    *dword_701038 = Screen.nHeight;

    //HUD
    if (bFixHUD)
    {
        pattern = hook::pattern("D8 0D ? ? ? ? 57 0F B6 B8 81 00 00 00 D8"); //6CC914
        injector::WriteMemory<float>(*pattern.get_first<float*>(2), Screen.fHudScaleX, true);

        for (size_t i = 0; i < 4; i++)
        {
            auto pattern = hook::pattern("C7 ? ? ? ? 00 00 00 00 A0 43 C7 ? ? ? ? 00 00 00 00 70 43");
            if (!pattern.empty())
            {
                injector::WriteMemory<float>(pattern.get(0).get<void>(7), Screen.fHudPosX, true);
            }
            else
            {
                pattern = hook::pattern("C7 ? ? ? 00 00 A0 43 C7 ? ? ? ? 00 00 00 00 70 43");
                if (!pattern.empty())
                {
                    injector::WriteMemory<float>(pattern.get(0).get<void>(4), Screen.fHudPosX, true);
                }
            }
        }

        pattern = hook::pattern("C7 ? ? ? 00 00 A0 43 C7 ? ? ? 00 00 70 43");
        injector::WriteMemory<float>(pattern.get_first(4), Screen.fHudPosX, true);
        pattern = hook::pattern("C7 ? ? ? 00 00 A0 43 C7 ? ? ? 00 00 70 43");
        injector::WriteMemory<float>(pattern.get_first(4), Screen.fHudPosX, true);

        pattern = hook::pattern("D8 1D ? ? ? ? DF E0 F6 C4 41 75 0F D9 45 FC");
        injector::WriteMemory<float>(*pattern.get_first<float*>(2), Screen.fHudPosX, true);

        pattern = hook::pattern("C7 44 24 ? 00 00 DC 43 C7 44 24 ? 00 00 70 41");
        if (!pattern.empty())
        {
            injector::WriteMemory<float>(pattern.get_first(20), (Screen.fHudPosX - 320.0f) + 200.0f, true);
            injector::WriteMemory<float>(pattern.get_first(42), (Screen.fHudPosX - 320.0f) + 200.0f, true);
            injector::WriteMemory<float>(pattern.get_first(4), (Screen.fHudPosX - 320.0f) + 440.0f, true);
            injector::WriteMemory<float>(pattern.get_first(64), (Screen.fHudPosX - 320.0f) + 440.0f, true);
        }
        else
        {
            // Russian SoftClub exe: same values, but stored with 32-bit stack offsets, so the immediates are further apart
            pattern = hook::pattern("C7 44 24 ? 00 00 DC 43 C7 84 24 ? 00 00 00 00 00 70 41");
            if (!pattern.empty())
            {
                injector::WriteMemory<float>(pattern.get_first(26), (Screen.fHudPosX - 320.0f) + 200.0f, true);
                injector::WriteMemory<float>(pattern.get_first(48), (Screen.fHudPosX - 320.0f) + 200.0f, true);
                injector::WriteMemory<float>(pattern.get_first(4), (Screen.fHudPosX - 320.0f) + 440.0f, true);
                injector::WriteMemory<float>(pattern.get_first(70), (Screen.fHudPosX - 320.0f) + 440.0f, true);
            }
        }

        //pattern = hook::pattern("8D 72 04 83 C0 60 E8 ? ? ? ? 5F 5E C3"); //menu (#179)
        //if (pattern.size() > 0)
        //  injector::MakeNOP(pattern.get_first(), 11, true);

        //mouse cursor fix
        pattern = hook::pattern("81 C2 C0 FE FF FF");
        for (size_t i = 0; i < pattern.count_hint(26).size(); ++i)
        {
            injector::WriteMemory(pattern.get(i).get<void>(2), static_cast<int32_t>(-Screen.fHudPosX), true);
        }

        pattern = hook::pattern("05 C0 FE FF FF");
        for (size_t i = 0; i < pattern.count_hint(3).size(); ++i)
        {
            injector::WriteMemory(pattern.get(i).get<void>(1), static_cast<int32_t>(-Screen.fHudPosX), true);
        }

        pattern = hook::pattern("8D 88 C0 FE FF FF");
        injector::WriteMemory(pattern.get_first(2), static_cast<int32_t>(-Screen.fHudPosX), true);

        pattern = hook::pattern("8B 06 8B 4C 24 08");
        struct ClientRectHook1
        {
            void operator()(injector::reg_pack& regs)
            {
                regs.eax = *(uint32_t*)(regs.esi + 0x00);
                regs.ecx = static_cast<uint32_t>(Screen.fHudPosX + Screen.fHudPosX);
            }
        }; injector::MakeInline<ClientRectHook1>(pattern.get_first(0), pattern.get_first(6));

        pattern = hook::pattern("8B 07 8B 4C 24 0C");
        struct ClientRectHook2
        {
            void operator()(injector::reg_pack& regs)
            {
                regs.eax = *(uint32_t*)(regs.edi + 0x00);
                regs.ecx = 480;
            }
        }; injector::MakeInline<ClientRectHook2>(pattern.get_first(0), pattern.get_first(6));
    }

    if (bFixFOV)
    {
        static float hor3DScale = 1.0f / (Screen.fAspectRatio / (4.0f / 3.0f));
        static float ver3DScale = 1.0f; // don't touch this
        static float mirrorScale = 0.45f;
        static float f129 = 1.28f;
        static float f04525 = 0.4525f;
        static float f1 = 1.0f; // horizontal for vehicle reflection
        static float flt1 = 0.0f;
        static float flt2 = 0.0f;
        static float flt3 = 0.0f;

        if (bScaling)
        {
            hor3DScale /= 1.0252904893f;
        }

        pattern = hook::pattern("DB 40 18 C7 44 24");
        struct FOVHook
        {
            void operator()(injector::reg_pack& regs)
            {
                *(float*)(regs.esp + 0x20) = 1.0f;
                regs.eax = *(uint32_t*)(regs.ebp + 4);

                if (regs.eax == 1 || regs.eax == 4) //Headlights stretching, reflections etc
                {
                    flt1 = hor3DScale;
                    flt2 = f04525;
                    flt3 = f129;
                }
                else
                {
                    if (regs.eax > 9)
                    {
                        flt1 = f1;
                        flt2 = 0.5f;
                        flt3 = 1.0f;
                        _asm fld ds : f1
                        return;
                    }
                    else
                    {
                        flt1 = 1.0f;
                        flt2 = 0.5f;
                        flt3 = 1.0f;
                    }
                }

                if (regs.eax == 3) //if rearview mirror
                    _asm fld ds : mirrorScale
                    else
                        _asm fld ds : ver3DScale
                    }
        }; injector::MakeInline<FOVHook>(pattern.get_first(0), pattern.get_first(14));

        pattern = hook::pattern("D8 3D ? ? ? ? D9 1F E8 ? ? ? ? D9");
        injector::WriteMemory(pattern.get_first(2), &flt1, true);

        // FOV being different in menus and in-game fix
        pattern = hook::pattern("D8 0D ? ? ? ? ? ? E8 ? ? ? ? DD D8");
        injector::WriteMemory(pattern.get_first(2), &flt2, true);

        pattern = hook::pattern("D8 3D ? ? ? ? D9 E0 D9 5E 54 D9 44 24 20");
        injector::WriteMemory(pattern.get_first(2), &flt3, true);
    }

    if (nFMVWidescreenMode)
    {
        pattern = hook::pattern("68 00 00 80 3F 68 00 00 00 3F 68 00 00 00 3F 68 00 00 00 BF 68 00 00 00 BF");
        injector::WriteMemory<float>(pattern.get_first(6 + 0), (0.5f / ((4.0f / 3.0f) / (16.0f / 9.0f))), true); // Height (Bottom)
        injector::WriteMemory<float>(pattern.get_first(6 + 5), (0.5f / ((4.0f / 3.0f) / (16.0f / 9.0f))), true); // Width (Right)
        injector::WriteMemory<float>(pattern.get_first(6 + 10), -(0.5f / ((4.0f / 3.0f) / (16.0f / 9.0f))), true); // Height (Top)
        injector::WriteMemory<float>(pattern.get_first(6 + 15), -(0.5f / ((4.0f / 3.0f) / (16.0f / 9.0f))), true); // Width (Left)

        if (nFMVWidescreenMode > 1)
        {
            injector::WriteMemory<float>(pattern.get_first(6 + 0), (0.5f / ((4.0f / 3.0f) / (4.0f / 3.0f))), true); // Height (Bottom)
            injector::WriteMemory<float>(pattern.get_first(6 + 5), (0.5f / ((4.0f / 3.0f) / (16.0f / 9.0f))), true); // Width (Right)
            injector::WriteMemory<float>(pattern.get_first(6 + 10), -(0.5f / ((4.0f / 3.0f) / (4.0f / 3.0f))), true); // Height (Top)
            injector::WriteMemory<float>(pattern.get_first(6 + 15), -(0.5f / ((4.0f / 3.0f) / (16.0f / 9.0f))), true); // Width (Left)
        }
    }

    // With HUDWidescreenMode = 0 only the .dat's removal entries (offset -9999) are used: the HUD stays in the
    // 4:3 area, but objects the game parks just outside it (e.g. the drift scores after a race) would show in
    // the pillarbox area
    if (bFixHUD && (Screen.fAspectRatio > (4.0f / 3.0f)))
    {
        static std::vector<CDatEntry> HudCoords;

        CIniReader iniReader("");
        auto DataFilePath = iniReader.GetIniPath();
        auto pos = DataFilePath.rfind('.');
        if (pos != std::string::npos)
            DataFilePath.replace(pos, DataFilePath.length() - pos, ".dat");
        else
            DataFilePath.append(".dat");

        LoadDatFile(DataFilePath, [bHUDWidescreenMode](std::string_view line)
        {
            int32_t PosX, PosY;
            float fOffsetX, fOffsetY;
            sscanf_s(line.data(), "%*s %x %x %f %f %*f %*f", &PosX, &PosY, &fOffsetX, &fOffsetY);

            if (!bHUDWidescreenMode && fOffsetX > -9000.0f)
                return;

            if (Screen.fAspectRatio < (16.0f / 9.0f))
                fOffsetX /= (((16.0f / 9.0f) / Screen.fAspectRatio) * 1.5f);

            HudCoords.emplace_back(PosX, PosY, fOffsetX, fOffsetY);
        });

        static float fMinimapPosX = 320.0f;
        static float fMinimapPosY = 240.0f;

        static auto WidescreenHud = [](HudPos& HudPosX, HudPos& HudPosY, bool bAddHudOffset = false)
        {
            if (nGameState != 3) // Disables WidescreenHUD in FrontEnd
            {
                auto it = std::find_if(HudCoords.begin(), HudCoords.end(),
                                       [cc = CDatEntry(HudPosX.dwPos, HudPosY.dwPos, 0.0f, 0.0f)]
                                       (const CDatEntry& cde) -> bool { return (cc.dwPosX == cde.dwPosX && cc.dwPosY == cde.dwPosY); });

                if (it != HudCoords.end())
                {
                    HudPosX.fPos += it->fOffsetX;
                    HudPosY.fPos += it->fOffsetY;

                    if (bAddHudOffset)
                    {
                        HudPosX.fPos -= fMinimapPosX;
                        HudPosX.fPos += Screen.fHudPosX;
                    }
                }
            }
        };

        pattern = hook::pattern("8B 47 1C 8B 4F 20 89 84 24 10 01 00 00 8B 47 24 89 84 24 18 01 00 00 39 5C 24 04");
        if (!pattern.empty())
        {
            struct HudHook
            {
                void operator()(injector::reg_pack& regs)
                {
                    regs.eax = *(uint32_t*)(regs.edi + 0x1C);
                    regs.ecx = *(uint32_t*)(regs.edi + 0x20);
                    HudPos HudPosX = regs.eax;
                    HudPos HudPosY = regs.ecx;
                    WidescreenHud(HudPosX, HudPosY);
                    regs.eax = HudPosX.dwPos;
                    regs.ecx = HudPosY.dwPos;
                }
            }; injector::MakeInline<HudHook>(pattern.get_first(0), pattern.get_first(6));

            if (bHUDWidescreenMode)
            {
                pattern = hook::pattern("D8 05 ? ? ? ? D9 5B 68 D8 05 ? ? ? ? D9 5B 6C D9 40 3C");
                injector::WriteMemory(pattern.get_first(2), &fMinimapPosX, true);
                injector::WriteMemory(pattern.get_first(11), &fMinimapPosY, true);
            }

            pattern = hook::pattern("8B 4D 08 D9 41 68 53 57 8B 5D 0C 8B F8");
            struct MinimapHook1
            {
                void operator()(injector::reg_pack& regs)
                {
                    regs.ecx = *(uint32_t*)(regs.ebp + 0x08);

                    HudPos HudPosX = *(uint32_t*)(regs.ecx + 0x68);
                    HudPos HudPosY = *(uint32_t*)(regs.ecx + 0x6C);
                    WidescreenHud(HudPosX, HudPosY, true);
                    *(uint32_t*)(regs.ecx + 0x68) = HudPosX.dwPos;
                    *(uint32_t*)(regs.ecx + 0x6C) = HudPosY.dwPos;

                    _asm fld dword ptr[HudPosX.fPos]
                }
            }; injector::MakeInline<MinimapHook1>(pattern.get_first(0), pattern.get_first(6));

            pattern = hook::pattern("D9 40 68 8B 4D 0C D8 60 70 53 56 8D 91 83 00 00 00 D9 54 24 34");
            struct MinimapHook2
            {
                void operator()(injector::reg_pack& regs)
                {
                    regs.ecx = *(uint32_t*)(regs.ebp + 0x0C);

                    HudPos HudPosX = *(uint32_t*)(regs.eax + 0x68);
                    HudPos HudPosY = *(uint32_t*)(regs.eax + 0x6C);
                    WidescreenHud(HudPosX, HudPosY, true);
                    *(uint32_t*)(regs.eax + 0x68) = HudPosX.dwPos;
                    *(uint32_t*)(regs.eax + 0x6C) = HudPosY.dwPos;
                    _asm fld dword ptr[HudPosX.fPos]
                }
            }; injector::MakeInline<MinimapHook2>(pattern.get_first(0), pattern.get_first(6));

            pattern = hook::pattern("D9 40 1C 8B 4D 08 D9 40 20");
            struct MinimapHook3
            {
                void operator()(injector::reg_pack& regs)
                {
                    HudPos HudPosX = *(uint32_t*)(regs.eax + 0x1C);
                    HudPos HudPosY = *(uint32_t*)(regs.eax + 0x20);
                    WidescreenHud(HudPosX, HudPosY, true);
                    *(uint32_t*)(regs.eax + 0x1C) = HudPosX.dwPos;
                    *(uint32_t*)(regs.eax + 0x20) = HudPosY.dwPos;
                    regs.ecx = *(uint32_t*)(regs.ebp + 0x8);
                    _asm {fld dword ptr[HudPosX.fPos]}
                    _asm {fld dword ptr[HudPosY.fPos]}
                }
            }; injector::MakeInline<MinimapHook3>(pattern.get_first(0), pattern.get_first(9));

            pattern = hook::pattern("D8 43 1C 8B 54 24 18 8B 44 24 10 D9 1A 8B 4E 68 50 51 57");
            struct LapsHook
            {
                void operator()(injector::reg_pack& regs)
                {
                    regs.edx = *(uint32_t*)(regs.esp + 0x18);

                    HudPos HudPosX = *(uint32_t*)(regs.ebx + 0x1C);
                    HudPos HudPosY = *(uint32_t*)(regs.ebx + 0x20);
                    WidescreenHud(HudPosX, HudPosY);
                    *(uint32_t*)(regs.ebx + 0x1C) = HudPosX.dwPos;
                    *(uint32_t*)(regs.ebx + 0x20) = HudPosY.dwPos;
                    _asm fadd dword ptr[HudPosX.fPos]
                }
            }; injector::MakeInline<LapsHook>(pattern.get_first(0), pattern.get_first(7));
        }
    }

    // The Russian SoftClub release can't use the language selection (empty language names, garbled text, crash when
    // choosing); recognized by its "LANGUAGES\RUSSIAN.BIN" string
    if (bShowLangSelect && !hook::pattern("4C 41 4E 47 55 41 47 45 53 5C 52 55 53 53 49 41 4E 2E 42 49 4E").empty())
        bShowLangSelect = false;

    // The game takes its language from the registry (Language under HKLM\Software\EA Games\Need For Speed Underground
    // or ...\NFS Underground, 0x401190), without it
    // English. Language in the ini (a name from the game's language table) is used instead of the registry; without
    // it the Russian SoftClub release (its table has "Russian") starts in Russian when the registry has none.
    if (auto language = hook::pattern("8B 0C 85 ? ? ? ? 89 0D ? ? ? ? EB 06 89 3D ? ? ? ? 8D 54 24 14"); !language.empty()) //0x401386 (RU 0x400796)
    {
        auto table = reinterpret_cast<const uint32_t*>(*language.get_first<uintptr_t>(3) - 4); // name, language, region
        auto pLanguage = *language.get_first<uint32_t*>(9);
        IconThemes::pLanguage = pLanguage;
        IconThemes::pLanguageTable = table;
        auto Find = [table](const char* name) -> const uint32_t*
        {
            for (auto entry = table; entry[0]; entry += 3)
                if (_stricmp(reinterpret_cast<const char*>(entry[0]), name) == 0)
                    return entry;
            return nullptr;
        };
        if (auto entry = szLanguage.empty() ? nullptr : Find(szLanguage.c_str()))
        {
            *pLanguage = entry[1];
            injector::MakeNOP(language.get_first(7), 6, true);  // mov [language], ecx: the registry's
            injector::MakeNOP(language.get_first(15), 6, true); // mov [language], edi: 0 without a Language value
        }
        else if (auto russian = Find("Russian"))
        {
            *pLanguage = russian[1];
            injector::MakeNOP(language.get_first(15), 6, true);
        }
    }

    IconThemes::Install(szGamepadTheme, nImproveGamepadSupport != 0, szKeyboardTheme);

    if (bSkipIntro)
    {
        // patch bootflow

        // LS_IntroFMV.fng NEEDS to be there in order to hear the splash screen music!
        // This means that the intro video technically still plays, which means e3_title.mad still needs to be in the movies folder
        static const char* BootFlowScreens[] = { "LS_IntroFMV.fng", "LS_Splash_PC.fng", "MU_UG_NEwOrLoad_PC2.fng" , "BootFlowEnd" };
        static const char* BootFlowScreensLangSelect[] = { "LS_LangSelect.fng", "LS_IntroFMV.fng", "LS_Splash_PC.fng", "MU_UG_NEwOrLoad_PC2.fng" , "BootFlowEnd"};

        const char** outScreens = BootFlowScreens;
        const char** outLastScreen = &BootFlowScreens[_countof(BootFlowScreens) - 1];
        if (bShowLangSelect)
        {
            outScreens = BootFlowScreensLangSelect;
            outLastScreen = &BootFlowScreensLangSelect[_countof(BootFlowScreensLangSelect) - 1];
        }

        uintptr_t loc_4DE21A = reinterpret_cast<uintptr_t>(hook::pattern("89 36 89 76 04 A1 ? ? ? ? 83 F8 05").get_first(0)) + 0x15;
        uintptr_t loc_4DE273 = loc_4DE21A + 2 + *reinterpret_cast<int8_t*>(loc_4DE21A + 1); // target of the je there (+0x59 in NA/EU, +0x58 in the Russian SoftClub exe)
        uintptr_t loc_4DE2A0 = reinterpret_cast<uintptr_t>(hook::pattern("8B 4E 04 89 01 83 C7 04 81 FF ? ? ? ? 89 46 04 89 48 04 89 30 7C C8 8B 36").get_first(0)) + 8;

        // force US Retail bootflow
        injector::MakeJMP(loc_4DE21A, loc_4DE273);

        // list start
        injector::WriteMemory<const char**>(loc_4DE273 + 1, outScreens, true);

        // list end
        injector::WriteMemory<const char**>(loc_4DE2A0 + 2, outLastScreen, true);

        // IntroFMV NotificationMessage Hook -- for skipping the video for the first time it plays
        // dereference instruction at 0x004DE985 (IntroFMVScreen constructor) for the vtable
        uintptr_t loc_4DE985 = reinterpret_cast<uintptr_t>(hook::pattern("A1 ? ? ? ? 85 C0 C7 44 24 10 00 00 00 00 C7 07 ? ? ? ? 74 49 68 ? ? ? ? B8").get_first(0)) + 0xF;
        uintptr_t IntroFMVScreenVTable = *reinterpret_cast<uintptr_t*>(loc_4DE985 + 2);
        IntroFMVScreen::NotificationMessageFuncAddr = *reinterpret_cast<uintptr_t*>(IntroFMVScreenVTable + 8);
        // dereference instruction at 0x004DEA5A
        uintptr_t loc_4DEA5A = reinterpret_cast<uintptr_t>(hook::pattern("5F 5E C2 10 00 8B 49 0C 51 E8 ? ? ? ? 83 C4 04 5E C2 10 00").get_first(0)) + 9;
        IntroFMVScreen::FEngPopPackageAddr = static_cast<uintptr_t>(injector::GetBranchDestination(loc_4DEA5A));

        injector::WriteMemory<uintptr_t>(IntroFMVScreenVTable + 8, (uintptr_t)&IntroFMVScreen::hkNotificationMessage, true);
    }
    else if (bShowLangSelect)
    {
        static const char* BootFlowScreensLangSelect[] = { "LS_LangSelect.fng", "LS_EAlogo.fng", "LS_PSAMovie.fng", "LS_IntroFMV.fng", "LS_Splash_PC.fng", "MU_UG_NEwOrLoad_PC2.fng" , "BootFlowEnd" };
        
        uintptr_t loc_4DE21A = reinterpret_cast<uintptr_t>(hook::pattern("89 36 89 76 04 A1 ? ? ? ? 83 F8 05").get_first(0)) + 0x15;
        uintptr_t loc_4DE273 = loc_4DE21A + 2 + *reinterpret_cast<int8_t*>(loc_4DE21A + 1); // target of the je there (+0x59 in NA/EU, +0x58 in the Russian SoftClub exe)
        uintptr_t loc_4DE2A0 = reinterpret_cast<uintptr_t>(hook::pattern("8B 4E 04 89 01 83 C7 04 81 FF ? ? ? ? 89 46 04 89 48 04 89 30 7C C8 8B 36").get_first(0)) + 8;

        // force US Retail bootflow
        injector::MakeJMP(loc_4DE21A, loc_4DE273);

        // list start
        injector::WriteMemory<const char**>(loc_4DE273 + 1, BootFlowScreensLangSelect, true);

        // list end
        injector::WriteMemory<const char**>(loc_4DE2A0 + 2, &BootFlowScreensLangSelect[_countof(BootFlowScreensLangSelect) - 1], true);
    }

    if (!szCustomUserFilesDirectoryInGameDir.empty())
    {
        szCustomUserFilesDirectoryInGameDir = GetExeModulePath<std::string>() + szCustomUserFilesDirectoryInGameDir;

        auto SHGetFolderPathAHook = [](HWND /*hwnd*/, int /*csidl*/, HANDLE /*hToken*/, DWORD /*dwFlags*/, LPSTR pszPath) -> HRESULT
        {
            CreateDirectoryA(szCustomUserFilesDirectoryInGameDir.c_str(), NULL);
            strcpy(pszPath, szCustomUserFilesDirectoryInGameDir.c_str());
            return S_OK;
        };

        auto dword_41C481 = *hook::pattern("68 ? ? ? ? 6A 00 6A 00 68 23 80 00 00 6A 00 FF 15").count(1).get(0).get<uint32_t*>(16 + 2);
        auto pattern = hook::pattern(pattern_str(0xFF, 0x15, to_bytes(dword_41C481)));
        for (size_t i = 0; i < pattern.size(); i++)
        {
            injector::MakeCALL(pattern.get(i).get<void*>(0), static_cast<HRESULT(WINAPI*)(HWND, int, HANDLE, DWORD, LPSTR)>(SHGetFolderPathAHook), true);
            injector::MakeNOP(pattern.get(i).get<void*>(5), 1, true);
        }
    }

    if (bSaveKeyboardBindings)
    {
        // Where the game keeps its .cfg files: CSIDL_COMMON_APPDATA (or the CustomUserFilesDirectoryInGameDir) + "\NFS Underground\"
        char folder[MAX_PATH] = {};
        if (!szCustomUserFilesDirectoryInGameDir.empty())
            strcpy_s(folder, szCustomUserFilesDirectoryInGameDir.c_str());
        else
            SHGetFolderPathA(NULL, CSIDL_COMMON_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, folder);
        KeyboardBindings::Install(std::string(folder) + "\\NFS Underground\\");
    }

    if (nImproveGamepadSupport)
    {

        struct PadState
        {
            int32_t LSAxis1;
            int32_t LSAxis2;
            int32_t LTRT;
            int32_t RSAxis1;
            int32_t RSAxis2;
            uint8_t unk[28];
            int8_t A;
            int8_t B;
            int8_t X;
            int8_t Y;
            int8_t LB;
            int8_t RB;
            int8_t Select;
            int8_t Start;
            int8_t LSClick;
            int8_t RSClick;
        };

        // Right stick turns the car in the menus like dragging with the mouse: a movement per frame in the orbit values
        // while the orbit is "held". The car view checks the mouse button for that, but the frontend reads the same
        // button as a click (sound), so the car view's check also takes the stick (the button itself isn't touched).
        // Worked out there, once a frame: the pad callback below runs once per device, and with a second one plugged
        // in its call came right after the gamepad's and overwrote the movement with none.
        static uint8_t* pOrbitMouseHeld = nullptr; //0x7064B0
        static int32_t* pOrbitDelta = nullptr;     //0x7064A4 (x), +4 (y)
        static bool bOrbitStick = false;
        static uintptr_t OrbitNotHeld = 0;
        static int32_t nGameStickX = 0, nGameStickY = 0; // The game's right stick axes, of the device pushed furthest

        // Right stick: [-10000, 10000], dead zone 20 %, full deflection 300 frontend pixels (480 high) a second
        static auto OrbitStick = []()
        {
            static DWORD lastTick = 0;
            static float restX = 0.0f, restY = 0.0f;
            const DWORD now = GetTickCount();
            const float seconds = (lastTick && now > lastTick) ? (std::min)((now - lastTick) / 1000.0f, 0.1f) : 0.0f;
            lastTick = now;
            auto shape = [](int32_t axis) {
                const float v = std::clamp(axis / 10000.0f, -1.0f, 1.0f);
                const float a = std::fabs(v);
                return (a <= 0.2f) ? 0.0f : std::copysign((a - 0.2f) / 0.8f, v);
            };
            int32_t axisX = nGameStickX, axisY = nGameStickY;
            nGameStickX = nGameStickY = 0;
            XInputRightStick::Read(axisX, axisY);
            const float x = shape(axisX), y = shape(axisY);
            if (x != 0.0f || y != 0.0f)
            {
                restX += x * 300.0f * seconds;
                restY += y * 300.0f * seconds;
                const int32_t dx = static_cast<int32_t>(restX), dy = static_cast<int32_t>(restY);
                restX -= dx;
                restY -= dy;
                pOrbitDelta[0] = dx;
                pOrbitDelta[1] = dy;
                bOrbitStick = true;
            }
            else if (bOrbitStick)
            {
                pOrbitDelta[0] = pOrbitDelta[1] = 0;
                restX = restY = 0.0f;
                bOrbitStick = false;
            }
        };
        if (auto orbit = hook::pattern("38 1D ? ? ? ? 74 76 8B 0D"); !orbit.empty()) //0x4B0CB9: cmp [mouse button], bl / je
        {
            pOrbitMouseHeld = *orbit.get_first<uint8_t*>(2);
            pOrbitDelta = *orbit.get_first<int32_t*>(10);
            OrbitNotHeld = reinterpret_cast<uintptr_t>(orbit.get_first(8)) + *orbit.get_first<int8_t>(7); //0x4B0D37
            struct OrbitHeldHook
            {
                void operator()(injector::reg_pack& regs)
                {
                    OrbitStick();
                    const bool held = *pOrbitMouseHeld != static_cast<uint8_t>(regs.ebx) || bOrbitStick;
                    if (!held)
                        *(uint32_t*)(regs.esp - 4) = OrbitNotHeld;
                }
            }; injector::MakeInline<OrbitHeldHook>(orbit.get_first(0), orbit.get_first(8));
        }

        // Pad buttons as frontend keys (Y = Z, X = P, Select = C, left stick click = T, right one = D, both = Q), sent
        // when pressed. The callback runs once per device and frame, so the state is kept per device: a second one
        // (e.g. a joystick) reported nothing pressed and a held button sent its key again every frame. A stick click
        // alone is sent when it's let go, if the other stick wasn't pressed meanwhile: both sticks are pressed one
        // after the other, and the first one sent its key before Q.
        struct PadKeys
        {
            bool Y = false, X = false, Select = false, LS = false, RS = false, Q = false;
            bool both = false; // Both sticks were pressed since they were last let go
        };
        static std::map<const void*, PadKeys> DevicePadKeys;
        static auto SendKey = [](char key)
        {
            keybd_event(BYTE(VkKeyScan(key)), 0, KEYEVENTF_EXTENDEDKEY, 0);
            keybd_event(BYTE(VkKeyScan(key)), 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
        };

        pattern = hook::pattern("7C ? 5F 5D 5E 33 C0 5B C2 08 00"); //0x41989E
        injector::WriteMemory(pattern.count(1).get(0).get<uint32_t>(8 + 5), 0x900008C2, true);
        struct CatchPad
        {
            void operator()(injector::reg_pack& regs)
            {
                PadState* PadKeyPresses = *(PadState**)(regs.esp + 0x4);
                if (PadKeyPresses != nullptr && PadKeyPresses != (PadState*)0x1 && nGameState == 3)
                {
                    PadKeys& keys = DevicePadKeys[PadKeyPresses];

                    const bool Y = PadKeyPresses->Y != 0, X = PadKeyPresses->X != 0, Select = PadKeyPresses->Select != 0;
                    if (Y && !keys.Y)
                        SendKey('Z');
                    if (X && !keys.X)
                        SendKey('P');
                    if (Select && !keys.Select)
                    {
                        SendKey('C');
                        VinylColor::Send(); // The C above may come too short to be seen by VinylColor::PollKeyboard
                    }
                    keys.Y = Y;
                    keys.X = X;
                    keys.Select = Select;

                    const bool LS = PadKeyPresses->LSClick != 0, RS = PadKeyPresses->RSClick != 0;
                    if (LS && RS)
                    {
                        if (!keys.Q)
                            SendKey('Q');
                        keys.both = true;
                    }
                    keys.Q = LS && RS;
                    if (!LS && keys.LS && !keys.both)
                        SendKey('T');
                    if (!RS && keys.RS && !keys.both)
                        SendKey('D');
                    if (!LS && !RS)
                        keys.both = false;
                    keys.LS = LS;
                    keys.RS = RS;

                    // For OrbitStick: the furthest pushed of all devices this frame
                    if (std::abs(PadKeyPresses->RSAxis1) + std::abs(PadKeyPresses->RSAxis2) > std::abs(nGameStickX) + std::abs(nGameStickY))
                    {
                        nGameStickX = PadKeyPresses->RSAxis1;
                        nGameStickY = PadKeyPresses->RSAxis2;
                    }
                }
                else if (nGameState != 3)
                {
                    DevicePadKeys.clear();
                    if (bOrbitStick)
                    {
                        pOrbitDelta[0] = pOrbitDelta[1] = 0;
                        bOrbitStick = false;
                    }
                }
            }
        }; injector::MakeInline<CatchPad>(pattern.get_first(8));

        const wchar_t* ControlsTexts[] = { L" 0", L" 1", L" 2", L" 3", L" 4", L" 5", L" 6", L" 7", L" 8", L" 9", L" Up", L" Down", L" Left", L" Right", L"X Rotation", L"Y Rotation", L"X Axis", L"Y Axis", L"Z Axis", L"Hat Switch" };
        const wchar_t* ControlsTextsXBOX[] = { L"A", L"B", L"X", L"Y", L"LB", L"RB", L"Back (View)", L"Start (Menu)", L"Left stick", L"Right stick", L"D-pad Up", L"D-pad Down", L"D-pad Left", L"D-pad Right", L"Right stick Left/Right", L"Right stick Up/Down", L"Left stick Left/Right", L"Left stick Up/Down", L"LT / RT", L"D-pad", L"Share" };
        const wchar_t* ControlsTextsPS[] = { L"Cross", L"Circle", L"Square", L"Triangle", L"L1", L"R1", L"Select (Share)", L"Start (Options)", L"L3", L"R3", L"D-pad Up", L"D-pad Down", L"D-pad Left", L"D-pad Right", L"Right stick Left/Right", L"Right stick Up/Down", L"Left stick Left/Right", L"Left stick Up/Down", L"L2 / R2", L"D-pad", L"" };

        static std::vector<std::wstring> Texts(ControlsTexts, std::end(ControlsTexts));
        static std::vector<std::wstring> TextsXBOX(ControlsTextsXBOX, std::end(ControlsTextsXBOX));
        static std::vector<std::wstring> TextsPS(ControlsTextsPS, std::end(ControlsTextsPS));

        // The device the controls screen shows (its type at +0x134, 0x13 = keyboard): NA/EU index 0x736344/0x73633C,
        // table 0x71B808, base 0x78DAA8/0x78DA98; RU 0x73689C, 0x71BD68, 0x78DFF8
        static uint32_t* pDeviceIndex = nullptr;
        static uint32_t* pDeviceTable = nullptr;
        static uintptr_t DeviceBase = 0;
        if (auto device = hook::pattern("A1 ? ? ? ? 8D 0C 40 8B 04 8D ? ? ? ? 69 C0 70 02 00 00 05 ? ? ? ? 83 B8 34 01 00 00 13"); !device.empty())
        {
            pDeviceIndex = *device.get_first<uint32_t*>(1);
            pDeviceTable = *device.get_first<uint32_t*>(11);
            DeviceBase = *device.get_first<uintptr_t>(22);
        }
        else if (auto deviceRU = hook::pattern("A1 ? ? ? ? 53 8D 0C 40 8B 1C 8D ? ? ? ? 69 DB 70 02 00 00 81 C3 ? ? ? ? 83 BB 34 01 00 00 13"); !deviceRU.empty())
        {
            pDeviceIndex = *deviceRU.get_first<uint32_t*>(1);
            pDeviceTable = *deviceRU.get_first<uint32_t*>(12);
            DeviceBase = *deviceRU.get_first<uintptr_t>(24);
        }

        // The game asks DirectInput for the object's name with GetObjectInfo into a DIDEVICEOBJECTINSTANCEA (NA 0x78ECE8,
        // EU 0x78ECD8, RU 0x78F250): its type and GUID say what it is in every language, the name is at +0x20
        static uint8_t* pObjectInfo = nullptr;
        if (auto info = hook::pattern("B9 4F 00 00 00 BF ? ? ? ? F3 AB C7 05"); !info.empty())
            pObjectInfo = *info.get_first<uint8_t*>(6);

        // The gamepad name for the game's text of a button/axis, empty = leave it. Never on the keyboard page: its key
        // names come from Windows in its language and "Num 4" became LB. The controller's names may be localized too
        // ("<axis> X" on a Russian Windows, the font has no Cyrillic), so it goes by the object DirectInput describes:
        // buttons by their exact number ("Button 11", the Xbox controllers' Share button, showed as B before)
        static auto GamepadName = [](const std::wstring& text) -> std::wstring
        {
            if (pDeviceIndex && *(int32_t*)(DeviceBase + 0x270 * pDeviceTable[3 * *pDeviceIndex] + 0x134) == 0x13)
                return L"";
            // From the gamepad theme, read when first needed (the game's language is known by then)
            static std::vector<std::wstring> names;
            if (names.empty())
                names = IconThemes::Names(bPlayStationNames ? TextsPS : TextsXBOX);

            // The text shown is (the start of) the name the game just got for this object
            if (pObjectInfo && !text.empty())
            {
                const char* pszName = (const char*)(pObjectInfo + 0x20);
                wchar_t wszName[260] = {};
                MultiByteToWideChar(CP_ACP, 0, pszName, -1, wszName, 260);
                if (std::wstring(wszName).compare(0, text.size(), text) == 0)
                {
                    // dwType (+0x18) and guidType (+0x04) mean the same with every data format (the game's own
                    // isn't c_dfDIJoystick, so dwOfs doesn't): button/POV by type, axis by its GUID (Data1)
                    uint32_t type = *(uint32_t*)(pObjectInfo + 0x18);
                    uint32_t instance = (type >> 8) & 0xFFFF;
                    if (type & 0x0C) // DIDFT_BUTTON
                    {
                        if (instance <= 9)
                            return names[instance];
                        if (instance == 11)
                            return names[20];
                        return L"";
                    }
                    if (type & 0x10) // DIDFT_POV
                        return names[19];
                    if (type & 0x03) // DIDFT_AXIS
                    {
                        switch (*(uint32_t*)(pObjectInfo + 0x04))
                        {
                        case 0xA36D02E0: return names[16]; // GUID_XAxis
                        case 0xA36D02E1: return names[17]; // GUID_YAxis
                        case 0xA36D02E2: return names[18]; // GUID_ZAxis (both triggers on Xbox controllers)
                        case 0xA36D02F4: return names[14]; // GUID_RxAxis
                        case 0xA36D02F5: return names[15]; // GUID_RyAxis
                        }
                    }
                    return L"";
                }
            }

            size_t space = text.find_last_of(L' ');
            if (space != std::wstring::npos && space + 1 < text.size()
                && text.find_first_not_of(L"0123456789", space + 1) == std::wstring::npos)
            {
                int number = _wtoi(text.c_str() + space + 1);
                if (number >= 0 && number <= 9)
                    return names[number];
                if (number == 11)
                    return names[20];
                return L"";
            }
            for (size_t i = 10; i < Texts.size(); i++)
            {
                if (text.find(Texts[i]) != std::wstring::npos)
                    return names[i];
            }
            return L"";
        };

        pattern = hook::pattern("8D 43 60 8D 74 24 10 E8 ? ? ? ? 8B"); //0x4148F6
        struct Buttons
        {
            void operator()(injector::reg_pack& regs)
            {
                regs.eax = regs.ebx + 0x60;
                regs.esi = regs.esp + 0x10;

                // The game's buffer holds 23 characters (NA 0x4148C4: MultiByteToWideChar with 0x17), 22 and the end:
                // longer names (a theme's) are cut, they would overwrite the stack
                auto pszStr = (wchar_t*)(regs.esp + 0x10);
                std::wstring name = GamepadName(pszStr);
                if (!name.empty())
                    wcsncpy_s(pszStr, 23, name.c_str(), _TRUNCATE);
            }
        }; if (pattern.size() > 0) { injector::MakeInline<Buttons>(pattern.get_first(0), pattern.get_first(7)); }
        else
        {
            // Russian SoftClub release: builds the text as char itself (no MultiByteToWideChar) in [esp+0Ch], 22 characters
            // at most, and ends it at [esp+22h] (RU 0x4149E5); replaced right after that, before its length is counted
            pattern = hook::pattern("8D 44 24 0C C6 44 24 22 00 8D 50 01");
            struct ButtonsRU
            {
                void operator()(injector::reg_pack& regs)
                {
                    regs.eax = regs.esp + 0x0C;
                    *(char*)(regs.esp + 0x22) = '\0';

                    auto pszStr = (char*)(regs.esp + 0x0C);
                    wchar_t wszText[32] = {};
                    MultiByteToWideChar(CP_ACP, 0, pszStr, -1, wszText, 32); // Like the name it's compared with
                    std::wstring name = GamepadName(wszText);
                    if (!name.empty())
                    {
                        size_t i = 0;
                        for (; i < name.size() && i < 22; i++)
                            pszStr[i] = static_cast<char>(name[i]);
                        pszStr[i] = '\0';
                    }
                }
            }; if (pattern.size() > 0) { injector::MakeInline<ButtonsRU>(pattern.get_first(0), pattern.get_first(9)); }
        }

        // FrontEnd button remap (through game code, not key emulation)
        auto pattern = hook::pattern("8B 86 ? ? ? ? 8B 0C ? ? ? ? ? 85 C9 0F"); // 004071C5
        struct FrontEndRemap
        {
            void operator()(injector::reg_pack& regs)
            {
                regs.eax = *(uintptr_t*)(regs.esi + 0x0130);

                int LB = (regs.esi + 0x0130) - (0x88);
                *(uintptr_t*)LB = 0x1A; // FE Action "Comma"
                int RB = (regs.esi + 0x0130) - (0x84);
                *(uintptr_t*)RB = 0x19; // FE Action "Period"
            }
        };
        injector::MakeInline<FrontEndRemap>(pattern.count(1).get(0).get<uint32_t>(0), pattern.count(1).get(0).get<uint32_t>(6));
    }

    if (fLeftStickDeadzone)
    {
        // [ -10000 | 10000 ]
        static int32_t nLeftStickDeadzone = static_cast<int32_t>(fLeftStickDeadzone * 100.0f);
        pattern = hook::pattern("85 F6 7D 3D 8B 7C 24 18 57"); //0x4193B6
        static auto loc_4193F7 = (uint32_t)hook::get_pattern("83 7C 24 18 01 75 ? B9 04 00 00 00");
        static auto dword_71D8A8 = *hook::get_pattern<int32_t*>("8D 95 ? ? ? ? 8B F2 52 B9", 2);
        struct DeadzoneHook
        {
            void operator()(injector::reg_pack& regs)
            {
                regs.esi = regs.eax;
                if (*(int32_t*)&regs.esi >= 0)
                {
                    int32_t dStickStateX = *(int32_t*)(dword_71D8A8 + 0);
                    int32_t dStickStateY = *(int32_t*)(dword_71D8A8 + 1);

                    *(int32_t*)(dword_71D8A8 + 0) = (std::abs(dStickStateX) <= nLeftStickDeadzone) ? 0 : dStickStateX;
                    *(int32_t*)(dword_71D8A8 + 1) = (std::abs(dStickStateY) <= nLeftStickDeadzone) ? 0 : dStickStateY;

                    *(uint32_t*)(regs.esp - 4) = loc_4193F7;
                }
            }
        }; injector::MakeInline<DeadzoneHook>(pattern.get_first(-2), pattern.get_first(4));
    }

    {
        // The FE message hook also hides the fixed resolution's arrows, so it's always installed; the dev objects only
        // with HideDebugObjects (2, the former pillarboxing, counts as 1)
        {
            using namespace DebugTags;
            bDevObjects = (nHideDebugObjects != 0);
            
            FEngSetInvisible_Addr = static_cast<decltype(FEngSetInvisible_Addr)>(injector::GetBranchDestination(hook::get_pattern("E8 ? ? ? ? 8A 43 61"))); //0x000495F70;
            FEHashUpper_Addr = static_cast<decltype(FEHashUpper_Addr)>(injector::GetBranchDestination(hook::get_pattern("E8 ? ? ? ? 8B 75 6C"))); //0x004FD230;
            FEngFindObject_Addr = static_cast<decltype(FEngFindObject_Addr)>(injector::GetBranchDestination(hook::get_pattern("E8 ? ? ? ? 3B C6 75 16"))); //0x004FFB70;
            FEPkgMgr_FindPackage_Addr = static_cast<decltype(FEPkgMgr_FindPackage_Addr)>(injector::GetBranchDestination(hook::get_pattern("E8 ? ? ? ? 8B 50 1C"))); //0x004F65D0;
            auto setString = hook::pattern("81 EC 00 08 00 00 53 8B 9C 24 08 08 00 00 56 57 8B 3D ? ? ? ? 68 00 08 00 00");
            FEStringSetString_Addr = setString.empty() ? 0 : reinterpret_cast<decltype(FEStringSetString_Addr)>(setString.get_first(0)); //0x0041F060;

            auto pattern = hook::pattern("8B 4C 24 4C 8B 16");
            FEPkgMgr_SendMessageToPackage_Cave_Exit = reinterpret_cast<decltype(FEPkgMgr_SendMessageToPackage_Cave_Exit)>(pattern.get_first(4)); //0x004F7D18;
            injector::MakeJMP(pattern.get_first(-4), FEPkgMgr_SendMessageToPackage_Cave, true);

            // Vinyl color with C / Select (see VinylColor)
            if (auto fng = hook::pattern("8A 02 84 C0 A2 ? ? ? ? 74 16 B8"); !fng.empty()) //0x4F7AA3
                VinylColor::pLoadedFng = *fng.get_first<const char*>(5);
            if (auto send = hook::pattern("68 ? ? ? 00 E8 ? ? ? ? 85 C0 74 21 8B 48 18 85 C9 74 1A 8B 40 14 85 C0 74 13 56 51"); !send.empty())
                VinylColor::FEngSendMessageToActive_Addr = reinterpret_cast<uintptr_t>(send.get_first(0));
        }
    }

    // The game's own resolution setting (Options > Display, 640x480 .. 1280x1024) does nothing here, the resolution
    // comes from ResX/ResY. Where the game checks the saved setting against the modes the monitor offers (NA/EU 0x410E12,
    // RU likewise; RES at 0x700E70/RU 0x700F80, the modes at 0x71B7B0/RU 0x71BD10): only 800x600 (index 1, the default)
    // is offered from then on, and the setting is always that one, so the menu can't change it either (left/right only
    // step through offered modes). Also avoids the game's search for a replacement there, which runs past the end of
    // the five modes until the index overflows
    if (auto res = hook::pattern("A1 ? ? ? ? 8B 0C 85 ? ? ? ? 85 C9 75 21 85 C0 7C 1D"); !res.empty())
    {
        static int32_t* pResolution = *res.get_first<int32_t*>(1);
        static int32_t* pModeOffered = *res.get_first<int32_t*>(8);
        struct ResolutionHook
        {
            void operator()(injector::reg_pack& regs)
            {
                for (int i = 0; i < 5; i++)
                    pModeOffered[i] = (i == 1) ? 1 : 0;
                *pResolution = 1;
                regs.eax = 1; // mov eax, [RES]
            }
        }; injector::MakeInline<ResolutionHook>(res.get_first(0));
    }

    if (bClipFrontend)
    {
        // Around the frontend's drawing (NA/EU 0x409E68 .. 0x409E8A / 0x409EC2, RU 0x409F5E .. 0x409FC2 / 0x409FFB). The
        // start pattern has a second match in RU (0x4E3161), the first one is the right one there too.
        auto start = hook::pattern("A1 ? ? ? ? 74 ? 85 C0");
        auto restoreJmp = hook::pattern("E9 ? ? ? ? 85 C0 74 ? A1");
        auto restore = hook::pattern("A1 ? ? ? ? 33 F6 85 C0 7E ? 8D 49");
        if (restore.empty())
            restore = hook::pattern("A1 ? ? ? ? 33 F6 85 C0 7E ? EB ? 8D A4 24 ? ? ? ? 90 8B 04 B5 ? ? ? ? 8B 80"); // RU
        auto cursor = hook::pattern("E8 ? ? ? ? 83 C4 08 A1 ? ? ? ? 33 F6"); // Call that draws the mouse cursor (NA/EU 0x409EBA, RU 0x409FF3)
        auto device = hook::pattern("A1 ? ? ? ? 8B 08 83 C4 04 57 6A 0E");
        auto movie = hook::pattern("A1 ? ? ? ? 85 C0 74 ? BB");

        if (!start.empty() && !restoreJmp.empty() && !restore.empty() && !cursor.empty() && !device.empty() && !movie.empty())
        {
            FEClip::pDevice = *device.get_first<LPDIRECT3DDEVICE9*>(1);
            FEClip::pGameState = &nGameState.get();
            FEClip::pMoviePlayer = *movie.get_first<void**>(1);
            FEClip::pStartValue = *start.get_first<uint32_t*>(1);
            FEClip::pRestoreValue = *restore.get_first<uint32_t*>(1);
            FEClip::RestoreJmpTarget = injector::GetBranchDestination(restoreJmp.get_first(0)).as_int();
            FEClip::DrawCursorTarget = injector::GetBranchDestination(cursor.get_first(0)).as_int();

            // Where the frontend copies the name of the package it loads (NA/EU 0x4F7A9F, RU: same code); without it
            // only the menus are clipped. Only with HUDWidescreenMode = 0: otherwise the .dat already moves away what
            // the game parks outside the race results
            auto package = hook::pattern("8A 02 84 C0 A2 ? ? ? ? 74 16 B8");
            if (!bHUDWidescreenMode && !package.empty())
                FEClip::pLoadedPackage = *package.get_first<const char*>(5);

            struct FEClipStartHook
            {
                void operator()(injector::reg_pack& regs)
                {
                    regs.eax = *FEClip::pStartValue; // mov eax, [...] (the flags of the test before it are kept)
                    FEClip::StartClip();
                }
            }; injector::MakeInline<FEClipStartHook>(start.get_first(0));

            injector::MakeJMP(restoreJmp.get_first(0), FEClip::RestoreJmpCave, true);
            injector::MakeCALL(cursor.get_first(0), FEClip::DrawCursorCave, true);

            struct FEClipRestoreHook
            {
                void operator()(injector::reg_pack& regs)
                {
                    regs.eax = *FEClip::pRestoreValue; // mov eax, [...]
                    FEClip::StopClip();
                }
            }; injector::MakeInline<FEClipRestoreHook>(restore.get_first(0));
        }
    }

    auto magazineOpen = hook::pattern("8B F8 A0 ? ? ? ? 84 C0 0F 85");
    auto optionsCheck = hook::pattern("8B 0D ? ? ? ? 56 33 F6 33 C0 3B CE 57 BF 01 00 00 00 75 26 39 35 ? ? ? ? 75 1E");
    if (bBlackMagazineFix && !magazineOpen.empty() && !optionsCheck.empty())
    {
        byte_735768 = *magazineOpen.get_first<uint8_t*>(3);
        MagazineEffects::pMotionBlur = *optionsCheck.get_first<uint32_t*>(2);
        MagazineEffects::pLightTrails = *optionsCheck.get_first<uint32_t*>(23);
        struct OptionsCheckHook
        {
            void operator()(injector::reg_pack& regs)
            {
                MagazineEffects::Update();
                regs.ecx = *MagazineEffects::pMotionBlur; // mov ecx, [motion blur]
            }
        }; injector::MakeInline<OptionsCheckHook>(optionsCheck.get_first(0), optionsCheck.get_first(6)); // 40B3F0
    }

    //__mbclen to strlen, "---" bug fix
    pattern = hook::pattern("E8 ? ? ? ? 83 C4 04 85 C0 76 21 8D 43 60");
    if (!pattern.empty())
    {
        injector::MakeCALL(pattern.get_first(0), strlen, true);
    }

    if (nFPSLimit)
    {
        if (nFPSLimit < 0)
        {
            if (nFPSLimit == -1)
                nFPSLimit = GetDesktopRefreshRate();
            else if (nFPSLimit == -2)
                nFPSLimit = GetDesktopRefreshRate() * 2;
            else
                nFPSLimit = 60;
        }

        // the game limits FPS 2x over the frametime (or just reports it that way to D3D) -- this is the real game framerate here!
        static float FrameTime = 1.0f / nFPSLimit;
        // Video mode frametime
        uint32_t* dword_6CC7B0 = *hook::pattern("83 EC 10 A1 ? ? ? ? 89 44 24 04").count(1).get(0).get<uint32_t*>(31);
        injector::WriteMemory(dword_6CC7B0, FrameTime, true);
        // RealTimestep frametime
        uint32_t* dword_6F0890 = *hook::pattern("99 D9 05 ? ? ? ? B9 64 00 00 00").count(1).get(0).get<uint32_t*>(3);
        injector::WriteMemory(dword_6F0890, FrameTime, true);
        uint32_t* dword_6CCDEC = *hook::pattern("99 D9 05 ? ? ? ? B9 64 00 00 00").count(1).get(0).get<uint32_t*>(63);
        injector::WriteMemory(dword_6CCDEC, FrameTime * 2.0f, true);
        // a function in eDisplayFrame (particle effects?) frametime
        uint32_t* dword_40A744 = hook::pattern("68 89 88 88 3C").count(1).get(0).get<uint32_t>(1);
        injector::WriteMemory(dword_40A744, FrameTime, true);
        // something related to framerate and/or seconds. This value has to be an integer multiple of 60, otherwise the game can freeze. This affects some gameplay features such as NOS and menus.
        uint32_t* dword_6CC8B0 = *hook::pattern("83 E1 01 0B F9 D9 44 24 28 D8 1D ? ? ? ?").count(1).get(0).get<uint32_t*>(11);
        static float FrameSeconds = nFPSLimit;

        if (nFPSLimit % 60)
            FrameSeconds = nFPSLimit - (nFPSLimit % 60);

        // needed to avoid crashes, anything above 120 seems to be problematic at the moment...
        if (FrameSeconds > 120.0f)
            FrameSeconds = 120.0f;

        if (FrameSeconds < 60.0)
            FrameSeconds = 60.0;
        injector::WriteMemory(dword_6CC8B0, FrameSeconds, true);
        // another frametime -- seems to affect some gameplay elements...
        uint32_t* dword_6B5C08 = *hook::pattern("DF E0 F6 C4 41 7A ? 8A 44 24 12 D9 05 ? ? ? ?").count(1).get(0).get<uint32_t*>(13);
        injector::WriteMemory(dword_6B5C08, FrameTime, true);

        // GAME BUGFIX: fix sticky steering (especially on high FPS)
        // kill the autocentering!
        uint32_t* dword_460A3D = hook::pattern("D9 54 24 14 D9 E1 D8 1D ? ? ? ? DF E0 F6 C4 05 7A 08").count(1).get(0).get<uint32_t>(0x11); //0x460A2C anchor
        injector::WriteMemory<uint8_t>(dword_460A3D, 0xEB, true);

        // explanation:
        // this is a native game bug which can sadly happen on a bone-stock game, it's just been exaggerated by the higher FPS and faster input
        // what happens is this: if the steering curve isn't finished with the steering (if the wheels aren't centered), it will outright ignore the other direction you're pressing
        // to exaggerate the issue even further: nop out the instruction at 00460DFA which will make the steering extremely slow, then try steering all the way to the left, then quickly go and hold right. Notice how it *won't* continue to go right.
        // so to summarize in a chain of events:
        // 1. game controller sets steering value (-1.0 to 1.0)
        // 2. World::DoTimestep updates the player info and somewhere
        // 3. Somewhere in that chain it calls PlayerSteering::DoUndergroundSteering with the steering value as argument
        // 4. Steering curves get processed at PlayerSteering::CalculateSteeringSpeed                                          <-- THIS IS WHERE THE BUG HAPPENS (or shortly thereafter)
        // 5. Steering value gets passed to the car
        // Skipping 4. is possible by enabling the bool at 00736514, but that is intended exclusively for wheel input and not gamepad/keyboard input.
    }

    // windowed mode
    if (nWindowedMode)
    {
        uint32_t* dword_408AA1 = hook::pattern("8B 0D ? ? ? ? 68 00 00 C0 10").count(1).get(0).get<uint32_t>(0);
        uint32_t* dword_408AB5 = (uint32_t*)((uint32_t)dword_408AA1 + 0x14);
        
        uint32_t* dword_4089B1 = hook::pattern("68 00 00 08 90 68 ? ? ? ? 68 ? ? ? ? 53 FF 15").count(1).get(0).get<uint32_t>(16);
        uint32_t* dword_408975 = hook::pattern("68 00 00 08 90 8D 44 24 14 50 89 4C 24 20 89 54 24 24 FF 15").count(1).get(0).get<uint32_t>(18);
        uint32_t* WindowedMode_73637C = *hook::pattern("B9 0E 00 00 00 BF ? ? ? ? F3 AB A1 ? ? ? ?").count(1).get(0).get<uint32_t*>(13);

        // skip SetWindowLong because it messes things up
        injector::MakeJMP(dword_408AA1, dword_408AB5, true);
        // hook the offending functions
        injector::MakeNOP(dword_4089B1, 6, true);
        injector::MakeCALL(dword_4089B1, CreateWindowExA_Hook_NFSU, true);
        injector::MakeNOP(dword_408975, 6, true);
        injector::MakeCALL(dword_408975, WindowedModeWrapper::AdjustWindowRect_Hook, true);
        // enable windowed mode variable
        *WindowedMode_73637C = 1;

        // The Russian SoftClub exe moved ResX/ResY, but its window creation still reads them from the old place
        // (garbage size there); point it to the ones the rest of the game uses
        pattern = hook::pattern("8B 0D ? ? ? ? 8B 15 ? ? ? ? B8 64 00 00 00 53 03 C8 03 D0");
        if (!pattern.empty() && *pattern.get_first<uint32_t*>(2) != dword_701034)
        {
            injector::WriteMemory(pattern.get_first(2), dword_701034, true);
            injector::WriteMemory(pattern.get_first(8), dword_701038, true);
        }

        switch (nWindowedMode)
        {
        case 5:
            WindowedModeWrapper::bStretchWindow = true;
            break;
        case 4:
            WindowedModeWrapper::bScaleWindow = true;
            break;
        case 3:  // TODO: implement dynamic resizing (like in MW)
            WindowedModeWrapper::bEnableWindowResize = true;
        case 2:
            WindowedModeWrapper::bBorderlessWindowed = false;
            break;
        default:
            break;
        }
    }
    else
    {
        //icon fix
        auto SetWindowLongHook = [](HWND hWndl, int nIndex, LONG dwNewLong)
        {
            SetWindowLongA(hWndl, nIndex, dwNewLong);
            SendMessage(hWndl, WM_SETICON, ICON_BIG, (LPARAM)CreateIconFromResourceICO(IDR_NFSUICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON)));
            SendMessage(hWndl, WM_SETICON, ICON_SMALL, (LPARAM)CreateIconFromResourceICO(IDR_NFSUICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON)));
        };

        pattern = hook::pattern("FF 15 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? 8B 0D ? ? ? ? 33 D2 3B CB 0F 95 C2");
        injector::MakeNOP(pattern.get_first(0), 6, true);
        injector::MakeCALL(pattern.get_first(0), static_cast<void(WINAPI*)(HWND, int, LONG)>(SetWindowLongHook), true);
    }
}

CEXP void InitializeASI()
{
    std::call_once(CallbackHandler::flag, []()
    {
        CallbackHandler::RegisterCallback(Init, hook::pattern("68 20 03 00 00 BE 58 02 00 00").count_hint(1).empty(), 0x1100);
    });
}

BOOL APIENTRY DllMain(HMODULE hModule, uint32_t reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {

    }
    return TRUE;
}