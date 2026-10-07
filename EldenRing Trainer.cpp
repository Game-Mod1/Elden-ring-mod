#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <vector>
#include <cstring>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "psapi.lib")

enum {
    IDC_ATTACH=1001, IDC_SCAN, IDC_STATUS,
    IDC_GOD, IDC_HP, IDC_FP, IDC_STAMINA,
    IDC_ONEHIT, IDC_DMG, IDC_DMGEDIT,
    IDC_SOULS, IDC_SOULSEDIT, IDC_NOWEIGHT,
    IDC_NOCOST, IDC_ITEMS, IDC_HORSE,
    IDC_NODROP, IDC_NOTIME, IDC_STEALTH
};

HWND hMainWnd=nullptr, hStatus=nullptr;
HWND btnAttach=nullptr, btnScan=nullptr;
HWND hDmgEdit=nullptr, hSoulsEdit=nullptr;

HANDLE hProcess=nullptr;
DWORD processId=0;
uintptr_t moduleBase=0;
size_t moduleSize=0;
bool isAttached=false, patternsFound=false;

bool fGod=false, fHp=false, fFp=false, fStamina=false;
bool fOneHit=false, fDmg=false, fSouls=false, fNoWeight=false;
bool fNoCost=false, fItems=false, fHorse=false;
bool fNoDrop=false, fNoTime=false, fStealth=false;

float dmgMul=5.0f;
int soulsVal=999999999;

uintptr_t aPlayer=0, aGod=0, aDmg=0, aSouls=0, aWeight=0;
uintptr_t aStaminaCost=0, aItem=0, aHorse=0, aDrop=0, aDaytime=0, aStealth=0;

void SetStatus(const wchar_t* t){ if(hStatus) SetWindowTextW(hStatus,t); }

bool WriteMem(uintptr_t addr, const void* data, size_t size){
    if(!hProcess||!addr) return false;
    DWORD old=0;
    VirtualProtectEx(hProcess,(LPVOID)addr,size,PAGE_EXECUTE_READWRITE,&old);
    SIZE_T w=0;
    bool ok=WriteProcessMemory(hProcess,(LPVOID)addr,data,size,&w)&&w==size;
    VirtualProtectEx(hProcess,(LPVOID)addr,size,old,&old);
    return ok;
}

bool GetModuleInfo(DWORD pid, const wchar_t* name, uintptr_t& base, size_t& size){
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(snap==INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{sizeof(me)};
    bool found=false;
    if(Module32FirstW(snap,&me)){
        do{
            if(_wcsicmp(me.szModule,name)==0){
                base=(uintptr_t)me.modBaseAddr;
                size=me.modBaseSize;
                found=true;
                break;
            }
        }while(Module32NextW(snap,&me));
    }
    CloseHandle(snap);
    return found;
}

DWORD FindProcessId(const wchar_t* name){
    PROCESSENTRY32W pe{sizeof(pe)};
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap==INVALID_HANDLE_VALUE) return 0;
    DWORD pid=0;
    if(Process32FirstW(snap,&pe)){
        do{
            if(_wcsicmp(pe.szExeFile,name)==0){ pid=pe.th32ProcessID; break; }
        }while(Process32NextW(snap,&pe));
    }
    CloseHandle(snap);
    return pid;
}

uintptr_t FindPattern(uintptr_t start, size_t size, const BYTE* pat, const char* mask){
    size_t plen=strlen(mask);
    const size_t chunk=0x100000;
    std::vector<BYTE> buf(chunk+plen);
    for(size_t off=0; off<size; off+=chunk){
        size_t toRead=(std::min)(chunk+plen, size-off);
        SIZE_T bytes=0;
        if(!ReadProcessMemory(hProcess,(LPCVOID)(start+off),buf.data(),toRead,&bytes)) continue;
        for(size_t i=0; i+plen<=bytes; ++i){
            bool match=true;
            for(size_t j=0; j<plen; ++j){
                if(mask[j]!='?' && buf[i+j]!=pat[j]){ match=false; break; }
            }
            if(match) return start+off+i;
        }
    }
    return 0;
}

bool ScanAllPatterns(){
    if(!moduleBase||!moduleSize) return false;
    SetStatus(L"Scanning accurate patterns...");

    static const BYTE pPlayer[] = {0x8B,0x88,0x00,0x00,0x00,0x00,0x89};
    static const char mPlayer[] = "xx????x";
    static const BYTE pGod[] = {0x66,0xC1,0xE9,0x08,0x84,0xC9,0x75};
    static const char mGod[] = "xxxxxxx";
    static const BYTE pDmg[] = {0x41,0x8B,0x96,0x00,0x00,0x00,0x00};
    static const char mDmg[] = "xxx????";
    static const BYTE pSouls[] = {0x44,0x8B,0x49,0x6C,0x45,0x33,0xDB};
    static const char mSouls[] = "xxxxxxx";
    static const BYTE pWeight[] = {0xFF,0xC3,0x83,0xFB,0x05,0x7C};
    static const char mWeight[] = "xxxxxx";
    static const BYTE pStamina[] = {0x03,0x91,0x54,0x01,0x00,0x00,0xE9};
    static const char mStamina[] = "xxxxxxx";
    static const BYTE pItem[] = {0x0F,0x42,0xE8,0x3B,0xEB,0x8B,0xD5};
    static const char mItem[] = "xxxxxxx";
    static const BYTE pHorse[] = {0x8B,0x8A,0x00,0x00,0x00,0x00,0x89};
    static const char mHorse[] = "xx????x";
    static const BYTE pDrop[] = {0x41,0x0F,0x28,0xF8,0x48,0x85,0xD2,0x74};
    static const char mDrop[] = "xxxxxxxx";
    static const BYTE pDay[] = {0xF3,0x0F,0x2C,0xD0,0x85,0xD2,0x7E};
    static const char mDay[] = "xxxxxxx";
    static const BYTE pStealth[] = {0x74,0x0C,0x00,0x88,0x00,0x00,0x00,0x00,0xE9};
    static const char mStealth[] = "xx?x????x";

    aPlayer      = FindPattern(moduleBase,moduleSize,pPlayer,mPlayer);
    aGod         = FindPattern(moduleBase,moduleSize,pGod,mGod);
    aDmg         = FindPattern(moduleBase,moduleSize,pDmg,mDmg);
    aSouls       = FindPattern(moduleBase,moduleSize,pSouls,mSouls);
    aWeight      = FindPattern(moduleBase,moduleSize,pWeight,mWeight);
    aStaminaCost = FindPattern(moduleBase,moduleSize,pStamina,mStamina);
    aItem        = FindPattern(moduleBase,moduleSize,pItem,mItem);
    aHorse       = FindPattern(moduleBase,moduleSize,pHorse,mHorse);
    aDrop        = FindPattern(moduleBase,moduleSize,pDrop,mDrop);
    aDaytime     = FindPattern(moduleBase,moduleSize,pDay,mDay);
    aStealth     = FindPattern(moduleBase,moduleSize,pStealth,mStealth);

    int found=0;
    if(aPlayer) found++;
    if(aGod) found++;
    if(aDmg) found++;
    if(aSouls) found++;
    if(aWeight) found++;
    if(aStaminaCost) found++;
    if(aItem) found++;
    if(aHorse) found++;
    if(aDrop) found++;
    if(aDaytime) found++;
    if(aStealth) found++;

    wchar_t buf[80];
    wsprintfW(buf,L"Accurate patterns: %d/11",found);
    SetStatus(buf);
    patternsFound = found >= 4;
    return patternsFound;
}

bool Attach(){
    processId = FindProcessId(L"eldenring.exe");
    if(!processId){ SetStatus(L"Game not running"); return false; }
    hProcess = OpenProcess(PROCESS_ALL_ACCESS,FALSE,processId);
    if(!hProcess){ SetStatus(L"OpenProcess failed - run as Admin"); return false; }
    if(!GetModuleInfo(processId,L"eldenring.exe",moduleBase,moduleSize)){
        SetStatus(L"Module info failed");
        CloseHandle(hProcess); hProcess=nullptr;
        return false;
    }
    isAttached=true;
    SetStatus(L"Attached to eldenring.exe");
    return true;
}

void ApplyAll(){
    if(!isAttached||!hProcess||!patternsFound) return;

    if(fHp && aPlayer){
        int maxhp = 99999;
        WriteMem(aPlayer, &maxhp, 4);
    }
    if(fFp && aPlayer){
        int maxfp = 9999;
        WriteMem(aPlayer + 0x10, &maxfp, 4);
    }
    if(fStamina && aPlayer){
        int maxstm = 9999;
        WriteMem(aPlayer + 0x1C, &maxstm, 4);
    }
    if(fGod && aGod){
        BYTE patch[] = {0xB1, 0x01};
        WriteMem(aGod, patch, 2);
    }
    if(fOneHit && aDmg){
        float v = 99999.0f;
        WriteMem(aDmg, &v, 4);
    }
    if(fDmg && aDmg){
        WriteMem(aDmg, &dmgMul, 4);
    }
    if(fSouls && aSouls){
        WriteMem(aSouls, &soulsVal, 4);
    }
    if(fNoWeight && aWeight){
        int v = 0;
        WriteMem(aWeight, &v, 4);
    }
    if(fNoCost && aStaminaCost){
        BYTE nop5[] = {0x90,0x90,0x90,0x90,0x90};
        WriteMem(aStaminaCost, nop5, 5);
    }
    if(fItems && aItem){
        int v = 999;
        WriteMem(aItem, &v, 4);
    }
    if(fHorse && aHorse){
        int v = 99999;
        WriteMem(aHorse, &v, 4);
    }
    if(fNoDrop && aDrop){
        BYTE patch[] = {0x31,0xC0,0x90,0x90};
        WriteMem(aDrop, patch, 4);
    }
    if(fNoTime && aDaytime){
        BYTE patch[] = {0x31,0xD2,0x90};
        WriteMem(aDaytime, patch, 3);
    }
    if(fStealth && aStealth){
        BYTE patch[] = {0xEB,0x0C};
        WriteMem(aStealth, patch, 2);
    }
}

void CALLBACK TimerProc(HWND,UINT,UINT_PTR,DWORD){ ApplyAll(); }

LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{
        int y=8;
        CreateWindowW(L"STATIC",L"Elden ring",
            WS_CHILD|WS_VISIBLE|SS_CENTER,10,y,420,20,hwnd,0,0,0);
        y+=28;

        btnAttach=CreateWindowW(L"BUTTON",L"1. Attach",WS_CHILD|WS_VISIBLE,
            15,y,100,26,hwnd,(HMENU)IDC_ATTACH,0,0);
        btnScan=CreateWindowW(L"BUTTON",L"2. Scan",WS_CHILD|WS_VISIBLE,
            125,y,100,26,hwnd,(HMENU)IDC_SCAN,0,0);
        y+=36;

        CreateWindowW(L"BUTTON",L"God Mode",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            15,y,140,22,hwnd,(HMENU)IDC_GOD,0,0);
        CreateWindowW(L"BUTTON",L"Infinite HP",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            160,y,130,22,hwnd,(HMENU)IDC_HP,0,0);
        CreateWindowW(L"BUTTON",L"Infinite FP",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            300,y,120,22,hwnd,(HMENU)IDC_FP,0,0);
        y+=26;

        CreateWindowW(L"BUTTON",L"Infinite Stamina",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            15,y,140,22,hwnd,(HMENU)IDC_STAMINA,0,0);
        CreateWindowW(L"BUTTON",L"One Hit Kill",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            160,y,130,22,hwnd,(HMENU)IDC_ONEHIT,0,0);
        CreateWindowW(L"BUTTON",L"No Weight",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            300,y,120,22,hwnd,(HMENU)IDC_NOWEIGHT,0,0);
        y+=26;

        CreateWindowW(L"BUTTON",L"No Stamina Cost",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            15,y,140,22,hwnd,(HMENU)IDC_NOCOST,0,0);
        CreateWindowW(L"BUTTON",L"Infinite Items",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            160,y,130,22,hwnd,(HMENU)IDC_ITEMS,0,0);
        CreateWindowW(L"BUTTON",L"Horse God",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            300,y,120,22,hwnd,(HMENU)IDC_HORSE,0,0);
        y+=26;

        CreateWindowW(L"BUTTON",L"No Item Drop",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            15,y,140,22,hwnd,(HMENU)IDC_NODROP,0,0);
        CreateWindowW(L"BUTTON",L"Freeze Time",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            160,y,130,22,hwnd,(HMENU)IDC_NOTIME,0,0);
        CreateWindowW(L"BUTTON",L"Stealth",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            300,y,120,22,hwnd,(HMENU)IDC_STEALTH,0,0);
        y+=30;

        CreateWindowW(L"BUTTON",L"Damage x",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            15,y,90,22,hwnd,(HMENU)IDC_DMG,0,0);
        hDmgEdit=CreateWindowW(L"EDIT",L"5.0",WS_CHILD|WS_VISIBLE|WS_BORDER,
            110,y,55,22,hwnd,(HMENU)IDC_DMGEDIT,0,0);

        CreateWindowW(L"BUTTON",L"Edit Runes",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            180,y,100,22,hwnd,(HMENU)IDC_SOULS,0,0);
        hSoulsEdit=CreateWindowW(L"EDIT",L"999999999",WS_CHILD|WS_VISIBLE|WS_BORDER,
            285,y,100,22,hwnd,(HMENU)IDC_SOULSEDIT,0,0);
        y+=36;

        hStatus=CreateWindowW(L"STATIC",L"Start game → Attach → Scan → Enable",
            WS_CHILD|WS_VISIBLE|SS_LEFT,15,y,400,20,hwnd,(HMENU)IDC_STATUS,0,0);

        SetTimer(hwnd,1,60,TimerProc);
        break;
    }
    case WM_COMMAND:{
        #define ISCHK(h) (SendMessage(h,BM_GETCHECK,0,0)==BST_CHECKED)
        switch(LOWORD(wp)){
        case IDC_ATTACH:
            if(Attach()) EnableWindow(btnAttach,FALSE);
            break;
        case IDC_SCAN:
            if(!isAttached){ SetStatus(L"Attach first"); break; }
            ScanAllPatterns();
            break;
        case IDC_GOD:     fGod=ISCHK((HWND)lp); break;
        case IDC_HP:      fHp=ISCHK((HWND)lp); break;
        case IDC_FP:      fFp=ISCHK((HWND)lp); break;
        case IDC_STAMINA: fStamina=ISCHK((HWND)lp); break;
        case IDC_ONEHIT:  fOneHit=ISCHK((HWND)lp); break;
        case IDC_DMG:     fDmg=ISCHK((HWND)lp); break;
        case IDC_SOULS:   fSouls=ISCHK((HWND)lp); break;
        case IDC_NOWEIGHT:fNoWeight=ISCHK((HWND)lp); break;
        case IDC_NOCOST:  fNoCost=ISCHK((HWND)lp); break;
        case IDC_ITEMS:   fItems=ISCHK((HWND)lp); break;
        case IDC_HORSE:   fHorse=ISCHK((HWND)lp); break;
        case IDC_NODROP:  fNoDrop=ISCHK((HWND)lp); break;
        case IDC_NOTIME:  fNoTime=ISCHK((HWND)lp); break;
        case IDC_STEALTH: fStealth=ISCHK((HWND)lp); break;
        case IDC_DMGEDIT:
            if(HIWORD(wp)==EN_CHANGE){
                wchar_t b[32]{}; GetWindowTextW(hDmgEdit,b,32);
                dmgMul=(float)_wtof(b);
                if(dmgMul<0.1f) dmgMul=0.1f;
                if(dmgMul>9999.f) dmgMul=9999.f;
            }
            break;
        case IDC_SOULSEDIT:
            if(HIWORD(wp)==EN_CHANGE){
                wchar_t b[32]{}; GetWindowTextW(hSoulsEdit,b,32);
                soulsVal=_wtoi(b);
            }
            break;
        }
        #undef ISCHK
        break;
    }
    case WM_DESTROY:
        KillTimer(hwnd,1);
        if(hProcess){ CloseHandle(hProcess); hProcess=nullptr; }
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst,HINSTANCE,LPWSTR,int show){
    INITCOMMONCONTROLSEX icc{sizeof(icc),ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc=WndProc;
    wc.hInstance=hInst;
    wc.lpszClassName=L"EldenRingTrainer";
    wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    RegisterClassExW(&wc);

    hMainWnd=CreateWindowExW(0,L"EldenRingTrainer",
        L"Elden ring",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,460,360,
        nullptr,nullptr,hInst,nullptr);

    ShowWindow(hMainWnd,show);
    UpdateWindow(hMainWnd);

    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)){
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
