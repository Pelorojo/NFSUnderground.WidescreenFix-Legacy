#include "stdafx.h"
#include "GTA\CFileMgr.h"
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <shlobj.h>

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

void DrawRect(LPDIRECT3DDEVICE9 pDevice, int32_t x, int32_t y, int32_t w, int32_t h, D3DCOLOR color = D3DCOLOR_XRGB(0, 0, 0))
{
    D3DRECT BarRect = { x, y, x + w, y + h };
    pDevice->Clear(1, &BarRect, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, color, 0, 0);
}

void*(*CreateResourceFile)(const char* ResourceFileName, int32_t ResourceFileType, int, int, int);
void(__fastcall *ResourceFileBeginLoading)(int a1, void* rsc, int a2);
void LoadResourceFile(const char* ResourceFileName, int32_t ResourceFileType, int32_t nUnk1 = 0, int32_t nUnk2 = 0, int32_t nUnk3 = 0, int32_t nUnk4 = 0, int32_t nUnk5 = 0)
{
    auto r = CreateResourceFile(ResourceFileName, ResourceFileType, nUnk1, nUnk2, nUnk3);
    _asm
    {
        mov     edx, r
        mov     ecx, nUnk5
        mov     eax, nUnk4
        call    ResourceFileBeginLoading
    }
};

uint32_t* dword_73645C;
uint8_t* byte_735768;
void* off_71AA4C;
void __declspec(naked) CmpShouldClearSurfaceDuring3DRender()
{
    _asm
    {
        mov eax, dword_73645C
        cmp dword ptr [eax], 1
        jne resume

        // byte_735768 is set if the magazine view screen is open.
        mov eax, byte_735768
        cmp byte ptr [eax], 0

        resume:
        ret
    }
}

void __declspec(naked) MaybeClearSurfaceDuringBackgroundRender()
{
    _asm
    {
        call CmpShouldClearSurfaceDuring3DRender
        jz resume

        // In case 3D rendering won't clear the render surface, it must
        // be done during the 2D background rendering.
        mov dword ptr [esp+4], 1

        resume:
        mov esi, off_71AA4C
        ret
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
    int nHideDebugObjects = ReadIniInteger(iniReader, "MISC", "HideDebugObjects", 0);
    bool bClipFrontend = ReadIniInteger(iniReader, "MISC", "ClipFrontend", 1) != 0;
    if (!bFixHUD)
    {
        // HUD and frontend stretched over the whole screen: nothing sits in a 4:3 area, so neither moving the HUD
        // to the edges nor clipping to 4:3 makes sense
        bHUDWidescreenMode = false;
        bClipFrontend = false;
    }
    bool bBlackMagazineFix = ReadIniInteger(iniReader, "MISC", "BlackMagazineFix", 0) != 0;
    bool bSaveKeyboardBindings = ReadIniInteger(iniReader, "MISC", "SaveKeyboardBindings", 1) != 0;
    if (szCustomUserFilesDirectoryInGameDir.empty() || szCustomUserFilesDirectoryInGameDir == "0")
        szCustomUserFilesDirectoryInGameDir.clear();
    int nWindowedMode = ReadIniInteger(iniReader, "MISC", "WindowedMode", 0);

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
        pattern = hook::pattern("6A FF 68 ? ? ? ? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 51 A1 ? ? ? ? 57");
        CreateResourceFile = (void*(*)(const char* ResourceFileName, int32_t ResourceFileType, int, int, int)) pattern.get_first(0); //0x004482F0
        pattern = hook::pattern("56 8B F2 89 8E 98 00 00 00 8B 0D ? ? ? ? 89 86 94 00 00 00 8B 86 9C 00 00 00");
        ResourceFileBeginLoading = (void(__fastcall *)(int a1, void* rsc, int a2)) pattern.get_first(0); //0x00448110;

        if (nImproveGamepadSupport < 3)
        {
            static auto TPKPath = GetThisModulePath<std::string>().substr(GetExeModulePath<std::string>().length());

            if (nImproveGamepadSupport == 1)
                TPKPath += "buttons-xbox.tpk";
            else if (nImproveGamepadSupport == 2)
                TPKPath += "buttons-playstation.tpk";

            static injector::hook_back<void(__cdecl*)()> hb_448600;
            auto LoadTPK = []()
            {
                if (std::filesystem::exists(TPKPath))
                {
                    LoadResourceFile(TPKPath.c_str(), 1);
                }
                return hb_448600.fun();
            };

            pattern = hook::pattern("E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? 6A 00 6A 00 6A 00 68 F0 59 00 00"); //0x447280
            hb_448600.fun = injector::MakeCALL(pattern.get_first(0), static_cast<void(__cdecl*)()>(LoadTPK), true).get();
        }

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
        static uint8_t* pOrbitMouseHeld = nullptr; //0x7064B0
        static int32_t* pOrbitDelta = nullptr;     //0x7064A4 (x), +4 (y)
        static bool bOrbitStick = false;
        static uintptr_t OrbitNotHeld = 0;
        if (auto orbit = hook::pattern("38 1D ? ? ? ? 74 76 8B 0D"); !orbit.empty()) //0x4B0CB9: cmp [mouse button], bl / je
        {
            pOrbitMouseHeld = *orbit.get_first<uint8_t*>(2);
            pOrbitDelta = *orbit.get_first<int32_t*>(10);
            OrbitNotHeld = reinterpret_cast<uintptr_t>(orbit.get_first(8)) + *orbit.get_first<int8_t>(7); //0x4B0D37
            struct OrbitHeldHook
            {
                void operator()(injector::reg_pack& regs)
                {
                    const bool held = *pOrbitMouseHeld != static_cast<uint8_t>(regs.ebx) || bOrbitStick;
                    if (!held)
                        *(uint32_t*)(regs.esp - 4) = OrbitNotHeld;
                }
            }; injector::MakeInline<OrbitHeldHook>(orbit.get_first(0), orbit.get_first(8));
        }

        static bool Zstate, Pstate, Tstate, Dstate, Qstate, Cstate;
        pattern = hook::pattern("7C ? 5F 5D 5E 33 C0 5B C2 08 00"); //0x41989E
        injector::WriteMemory(pattern.count(1).get(0).get<uint32_t>(8 + 5), 0x900008C2, true);
        //static uintptr_t ButtonsState = (uintptr_t)*hook::pattern("").count(1).get(0).get<uint32_t*>(8); //0x
        struct CatchPad
        {
            void operator()(injector::reg_pack& regs)
            {
                PadState* PadKeyPresses = *(PadState**)(regs.esp + 0x4);
                //Keyboard
                //006F9358 backspace
                //006F931C enter
                if (PadKeyPresses != nullptr && PadKeyPresses != (PadState*)0x1 && nGameState == 3)
                {
                    if (PadKeyPresses->LSClick && PadKeyPresses->RSClick)
                    {
                        if (!Qstate)
                        {
                            keybd_event(BYTE(VkKeyScan('Q')), 0, KEYEVENTF_EXTENDEDKEY, 0);
                            keybd_event(BYTE(VkKeyScan('Q')), 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
                        }
                        Qstate = true;
                    }
                    else
                        Qstate = false;

                    if (PadKeyPresses->Y)
                    {
                        if (!Zstate)
                        {
                            keybd_event(BYTE(VkKeyScan('Z')), 0, KEYEVENTF_EXTENDEDKEY, 0);
                            keybd_event(BYTE(VkKeyScan('Z')), 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
                        }
                        Zstate = true;
                    }
                    else
                        Zstate = false;

                    if (PadKeyPresses->X)
                    {
                        if (!Pstate)
                        {
                            keybd_event(BYTE(VkKeyScan('P')), 0, KEYEVENTF_EXTENDEDKEY, 0);
                            keybd_event(BYTE(VkKeyScan('P')), 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
                        }
                        Pstate = true;
                    }
                    else
                        Pstate = false;

                    if (PadKeyPresses->LSClick && !PadKeyPresses->RSClick)
                    {
                        if (!Tstate)
                        {
                            keybd_event(BYTE(VkKeyScan('T')), 0, KEYEVENTF_EXTENDEDKEY, 0);
                            keybd_event(BYTE(VkKeyScan('T')), 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
                        }
                        Tstate = true;
                    }
                    else
                        Tstate = false;

                    if (PadKeyPresses->RSClick && !PadKeyPresses->LSClick)
                    {
                        if (!Dstate)
                        {
                            keybd_event(BYTE(VkKeyScan('D')), 0, KEYEVENTF_EXTENDEDKEY, 0);
                            keybd_event(BYTE(VkKeyScan('D')), 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
                        }
                        Dstate = true;
                    }
                    else
                        Dstate = false;

                    if (PadKeyPresses->Select)
                    {
                        if (!Cstate)
                        {
                            keybd_event(BYTE(VkKeyScan('C')), 0, KEYEVENTF_EXTENDEDKEY, 0);
                            keybd_event(BYTE(VkKeyScan('C')), 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
                            VinylColor::Send(); // The C above may come too short to be seen by VinylColor::PollKeyboard
                        }
                        Cstate = true;
                    }
                    else
                        Cstate = false;

                    // Right stick: [-10000, 10000], dead zone 20 %, full deflection 300 frontend pixels (480 high) a second
                    if (pOrbitDelta)
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
                        const float x = shape(PadKeyPresses->RSAxis1), y = shape(PadKeyPresses->RSAxis2);
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
                    }
                }
                else if (nGameState != 3 && bOrbitStick)
                {
                    pOrbitDelta[0] = pOrbitDelta[1] = 0;
                    bOrbitStick = false;
                }
            }
        }; injector::MakeInline<CatchPad>(pattern.get_first(8));

        const wchar_t* ControlsTexts[] = { L" 0", L" 1", L" 2", L" 3", L" 4", L" 5", L" 6", L" 7", L" 8", L" 9", L" Up", L" Down", L" Left", L" Right", L"X Rotation", L"Y Rotation", L"X Axis", L"Y Axis", L"Z Axis", L"Hat Switch" };
        const wchar_t* ControlsTextsXBOX[] = { L"A", L"B", L"X", L"Y", L"LB", L"RB", L"View (Select)", L"Menu (Start)", L"Left stick", L"Right stick", L"D-pad Up", L"D-pad Down", L"D-pad Left", L"D-pad Right", L"Right stick Left/Right", L"Right stick Up/Down", L"Left stick Left/Right", L"Left stick Up/Down", L"LT / RT", L"D-pad" };
        const wchar_t* ControlsTextsPS[] = { L"Cross", L"Circle", L"Square", L"Triangle", L"L1", L"R1", L"Select", L"Start", L"L3", L"R3", L"D-pad Up", L"D-pad Down", L"D-pad Left", L"D-pad Right", L"Right stick Left/Right", L"Right stick Up/Down", L"Left stick Left/Right", L"Left stick Up/Down", L"L2 / R2", L"D-pad" };

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
            const auto& names = (nImproveGamepadSupport != 2) ? TextsXBOX : TextsPS;

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
                            return L"Share";
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
                    return L"Share";
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

                auto pszStr = (wchar_t*)(regs.esp + 0x10);
                std::wstring name = GamepadName(pszStr);
                if (!name.empty())
                    wcscpy(pszStr, name.c_str());
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

    // Pillarboxing needs the EndScene spot, which isn't there in the Russian SoftClub exe; remove the objects instead
    if (nHideDebugObjects == 2 && hook::pattern("A1 ? ? ? ? 8B 08 83 C4 04 50").empty())
        nHideDebugObjects = 1;

    {
        if (nHideDebugObjects == 2)
        {
            //D3D9 EndScene Hook
            pattern = hook::pattern("A1 ? ? ? ? 8B 08 83 C4 04 50");
            static LPDIRECT3DDEVICE9* pDevice = *pattern.get_first<LPDIRECT3DDEVICE9*>(1);
            struct EndSceneHook
            {
                void operator()(injector::reg_pack& regs)
                {
                    regs.eax = *(uint32_t*)pDevice;
                    regs.ecx = *(uint32_t*)(regs.eax);

                    if (nGameState == 3)
                    {
                        DrawRect(*pDevice, 0, 0, static_cast<int32_t>(Screen.fHudOffsetReal), Screen.nHeight);
                        DrawRect(*pDevice, static_cast<int32_t>(Screen.fWidth43 + Screen.fHudOffsetReal), 0, static_cast<int32_t>(Screen.fWidth43 + Screen.fHudOffsetReal + Screen.fHudOffsetReal), Screen.nHeight);
                    }
                }
            }; injector::MakeInline<EndSceneHook>(pattern.get_first(0), pattern.get_first(7));
        }
        // The FE message hook also hides the fixed resolution's arrows, so it's always installed; the dev objects only
        // with HideDebugObjects = 1
        {
            using namespace DebugTags;
            bDevObjects = (nHideDebugObjects == 1);
            
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

    // Not available in the Russian SoftClub exe (the background render spot differs)
    if (bBlackMagazineFix && (hook::pattern("8B F8 A0 ? ? ? ? 84 C0 0F 85").empty() || hook::pattern("83 3D ? ? ? ? 01 8B 77 58").empty() || hook::pattern("6A 00 BE ? ? ? ? 74 ? BE").empty()))
        bBlackMagazineFix = false;

    if (bBlackMagazineFix)
    {
        pattern = hook::pattern("8B F8 A0 ? ? ? ? 84 C0 0F 85");
        byte_735768 = *pattern.get_first<uint8_t*>(3);

        pattern = hook::pattern("83 3D ? ? ? ? 01 8B 77 58");
        dword_73645C = *pattern.get_first<uint32_t*>(2);
        injector::MakeRangedNOP(pattern.get_first(0), pattern.get_first(7));
        injector::MakeCALL(pattern.get_first(0), CmpShouldClearSurfaceDuring3DRender); // 4098A2

        pattern = hook::pattern("6A 00 BE ? ? ? ? 74 ? BE");
        off_71AA4C = *pattern.get_first<void*>(10);
        injector::MakeCALL(pattern.get_first(9), MaybeClearSurfaceDuringBackgroundRender); // 409CF2
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