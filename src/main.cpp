// NorsePayload - "at this moment in this animation, do that" (UE4SS v4.0.0 C++ mod for NORSE: Oath of Blood), 2026-11-17.
//
// Payload files name moments in animations; when a montage passes one, NorsePayload publishes a Payload event through
// NorseEvents (subject = the unit, asset = the montage, text = the payload, newValue = the time in ms). Works on the
// game's own animations too - nothing in the paks changes. Modelled on Payload Interpreter and Behavior Data Injector.
//
// How a montage start is seen: Montage_Play, Montage_PlayWithBlendIn and Montage_PlayWithBlendSettings (and so
// PlaySlotAnimationAsDynamicMontage) all end in one virtual on the AnimInstance - vtable slot 95 (+0x2f8), Unreal's
// Montage_PlayInternal(Montage, BlendSettings, PlayRate, ReturnType, StartAt, bStopAll) - read from the exe's thunks
// 2026-11-17. That slot is read from the live AnimInstance's own vtable and hooked once per distinct function.
// A montage with payloads is then followed until it stops: one Montage_GetPosition per frame, only while a moment is
// still to come. Files: <this mod>\payloads\*.ini in NorseManifest order:
//   [Drengr_Attack_Light_01]          ; montage or animation name (the montage's first animation), * wildcards
//   0.32 = HitStart                   ; seconds into the montage
//   60%  = CancelDodge                ; or a share of its length
// Console: pl.list (what is loaded), pl.log on|off (log every montage start, to find names), pl.reload.
#include "NorseReflect.hpp"
#include "NorseManifest.hpp"
#include "NorseFilters.hpp"
#include "NorseEvents.h"

#include <MinHook.h>

#include <map>
#include <set>

namespace fs = std::filesystem;
using namespace nrn;
using namespace nrn::ue;
using namespace norse;

namespace
{
    fs::path g_mod;
    bool g_logStarts = false;

    fs::path ModDir()
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ModDir), &self);
        wchar_t buf[MAX_PATH * 2]{};
        GetModuleFileNameW(self, buf, static_cast<DWORD>(std::size(buf)));
        return fs::path(buf).parent_path().parent_path();
    }

    // ------------------------------------------------------------------ payload tables
    struct Mark
    {
        double at = 0;     // seconds, or a share 0..1 when percent
        bool percent = false;
        std::string name;
        std::string where;
    };
    struct Table
    {
        std::wstring pattern; // lower-case, * wildcards
        std::vector<Mark> marks;
    };
    std::vector<Table> g_tables;

    void LoadTables()
    {
        g_tables.clear();
        fs::path dir = g_mod / "payloads";
        std::vector<fs::path> files;
        std::error_code ec;
        if (fs::is_directory(dir, ec))
            for (const auto& f : fs::directory_iterator(dir, ec))
                if (f.is_regular_file() && Lower(f.path().extension().wstring()) == L".ini") files.push_back(f.path());
        nm::Manifest man;
        man.Resolve(g_mod.parent_path().parent_path());
        files = man.Arrange(files, [](const std::string& s) { NRN_INFO("load order: %s", s.c_str()); });
        size_t marks = 0;
        for (const auto& f : files)
        {
            Table* cur = nullptr;
            int n = 0;
            for (const auto& raw : ReadLines(f))
            {
                ++n;
                std::wstring l = Trim(StripComment(raw));
                if (l.empty()) continue;
                std::string where = Narrow(f.filename().wstring()) + ":" + std::to_string(n);
                if (l.front() == L'[' && l.back() == L']')
                {
                    g_tables.push_back({Lower(Trim(l.substr(1, l.size() - 2))), {}});
                    cur = &g_tables.back();
                    continue;
                }
                size_t eq = l.find(L'=');
                if (!cur || eq == std::wstring::npos)
                {
                    NRN_WARN("%s: not an [animation] or 'time = payload' line - skipped", where.c_str());
                    continue;
                }
                std::wstring t = Trim(l.substr(0, eq));
                Mark m;
                m.where = where;
                m.name = Narrow(Trim(l.substr(eq + 1)));
                if (!t.empty() && t.back() == L'%')
                {
                    m.percent = true;
                    m.at = _wtof(t.substr(0, t.size() - 1).c_str()) / 100.0;
                }
                else m.at = _wtof(t.c_str());
                if (m.name.empty() || m.at < 0)
                {
                    NRN_WARN("%s: a payload needs a time >= 0 and a name - skipped", where.c_str());
                    continue;
                }
                cur->marks.push_back(m);
                ++marks;
            }
        }
        NRN_INFO("payloads: %zu animation(s), %zu moment(s) from %zu file(s)", g_tables.size(), marks, files.size());
    }

    // ------------------------------------------------------------------ following montages
    struct Track
    {
        UObject* anim = nullptr;    // the AnimInstance
        UObject* montage = nullptr;
        UObject* unit = nullptr;
        std::string label;          // montage (and animation) name
        std::vector<std::pair<double, std::string>> todo; // seconds, payload - sorted
        size_t next = 0;
        double last = -1;
    };
    std::vector<Track> g_tracks;

    // The montage's first animation, from its exported SlotAnimTracks text (a dynamic montage made by
    // PlaySlotAnimationAsDynamicMontage has a generated name; the animation inside it is what a payload file names).
    std::wstring FirstAnimation(UObject* montage)
    {
        auto* cls = reinterpret_cast<UStruct*>(Reflect::Get().ClassOf(montage));
        FProperty* p = cls ? FindProp(cls, L"SlotAnimTracks") : nullptr;
        if (!p) return {};
        std::wstring t = Export(p, reinterpret_cast<uint8_t*>(montage) + p->GetOffset_Internal(), montage);
        size_t a = t.find(L"AnimReference=");
        if (a == std::wstring::npos) return {};
        a += 14;
        size_t b = t.find_first_of(L",)", a);
        std::wstring ref = t.substr(a, b == std::wstring::npos ? std::wstring::npos : b - a);
        ref.erase(std::remove(ref.begin(), ref.end(), L'"'), ref.end());
        size_t q = ref.find(L'\'');
        if (q != std::wstring::npos) ref = ref.substr(q + 1, ref.rfind(L'\'') - q - 1);
        return ShortName(ref);
    }

    float MontageLength(UObject* montage)
    {
        float len = 0;
        GetFloat(montage, L"GetPlayLength", len);
        return len;
    }

    bool CallMontageFloat(UObject* anim, const wchar_t* fn, UObject* montage, float& out)
    {
        Buf p;
        std::memcpy(p.b, &montage, 8);
        uint16_t ps = Reflect::Get().ParmsSizeOf(anim, fn);
        if (ps < 12 || !Reflect::Get().Call(anim, fn, p.b, ps, sizeof(p.b))) return false;
        std::memcpy(&out, p.b + 8, 4);
        return true;
    }

    bool MontageIsPlaying(UObject* anim, UObject* montage)
    {
        Buf p;
        std::memcpy(p.b, &montage, 8);
        uint16_t ps = Reflect::Get().ParmsSizeOf(anim, L"Montage_IsPlaying");
        if (ps < 9 || !Reflect::Get().Call(anim, L"Montage_IsPlaying", p.b, ps, sizeof(p.b))) return false;
        return p.b[8] != 0;
    }

    void Publish(const Track& t, double at, const std::string& name)
    {
        if (const ne::ApiTable* api = ne::Api())
        {
            NE_Event e{};
            e.size = static_cast<uint32_t>(sizeof(NE_Event));
            e.name = "Payload";
            e.subject = t.unit;
            e.asset = t.montage;
            e.newValue = static_cast<int32_t>(at * 1000.0);
            e.text = name.c_str();
            api->Publish(&e);
        }
        else NRN_INFO("Payload %s at %.3f s on %s (NorseEvents not loaded - nobody told)", name.c_str(), at, t.label.c_str());
    }

    void OnMontageStart(UObject* anim, UObject* montage, float startAt)
    {
        if (!anim || !montage) return;
        std::wstring mname = ShortName(montage->GetFullName(nullptr));
        std::wstring aname = FirstAnimation(montage);
        if (g_logStarts)
            NRN_INFO("montage start: %s (animation %s) at %.2f s", Narrow(mname).c_str(), Narrow(aname).c_str(), startAt);
        if (g_tables.empty()) return;
        std::wstring lm = Lower(mname), la = Lower(aname);
        Track t;
        for (const Table& tb : g_tables)
            if (Glob(tb.pattern.c_str(), lm.c_str()) || (!la.empty() && Glob(tb.pattern.c_str(), la.c_str())))
            {
                float len = 0;
                for (const Mark& m : tb.marks)
                {
                    double at = m.at;
                    if (m.percent)
                    {
                        if (len <= 0) len = MontageLength(montage);
                        at = m.at * len;
                    }
                    if (at >= startAt) t.todo.push_back({at, m.name});
                }
            }
        if (t.todo.empty()) return;
        std::sort(t.todo.begin(), t.todo.end());
        t.anim = anim;
        t.montage = montage;
        t.unit = GetObjectRet(anim, L"GetOwningActor");
        t.label = Narrow(mname) + (aname.empty() ? "" : " / " + Narrow(aname));
        t.last = startAt - 0.0001;
        // a montage restarted on the same AnimInstance replaces its old track
        g_tracks.erase(std::remove_if(g_tracks.begin(), g_tracks.end(), [&](const Track& x) { return x.anim == anim && x.montage == montage; }),
                       g_tracks.end());
        NRN_DEBUG("following %s: %zu moment(s)", t.label.c_str(), t.todo.size());
        g_tracks.push_back(std::move(t));
    }

    void Tick()
    {
        for (size_t i = 0; i < g_tracks.size();)
        {
            Track& t = g_tracks[i];
            float pos = 0;
            bool playing = MontageIsPlaying(t.anim, t.montage) && CallMontageFloat(t.anim, L"Montage_GetPosition", t.montage, pos);
            if (playing)
            {
                while (t.next < t.todo.size() && t.todo[t.next].first <= pos)
                {
                    Publish(t, t.todo[t.next].first, t.todo[t.next].second);
                    ++t.next;
                }
                t.last = pos;
            }
            if (!playing || t.next >= t.todo.size())
            {
                if (!playing && t.next < t.todo.size())
                    NRN_DEBUG("%s stopped at %.3f s with %zu moment(s) not reached", t.label.c_str(), t.last, t.todo.size() - t.next);
                g_tracks.erase(g_tracks.begin() + static_cast<long long>(i));
                continue;
            }
            ++i;
        }
    }

    // ------------------------------------------------------------------ the hook (AnimInstance vtable slot 95)
    using PlayInternalFn = float(__fastcall*)(UObject* anim, UObject* montage, const void* blend, float rate, uint8_t retType, float startAt,
                                              bool stopAll);
    constexpr size_t kPlayInternalSlot = 0x2f8 / 8;
    std::map<void*, PlayInternalFn> g_originals; // hooked function -> its trampoline

    // MinHook gives one trampoline per hooked function; the detour finds the right one through the object's vtable.
    float __fastcall PlayInternalHook(UObject* anim, UObject* montage, const void* blend, float rate, uint8_t retType, float startAt, bool stopAll)
    {
        void* fn = (*reinterpret_cast<void***>(anim))[kPlayInternalSlot];
        auto it = g_originals.find(fn);
        float r = it != g_originals.end() ? it->second(anim, montage, blend, rate, retType, startAt, stopAll) : 0.f;
        try
        {
            if (r > 0.f)
            {
                // The montage that actually started: NorseAnimSwap (its hook runs inside this one) may have swapped the
                // montage asset passed in for another. Refinement 2026-11-18.
                UObject* playing = montage;
                Buf cm;
                if (Reflect::Get().ParmsSizeOf(anim, L"GetCurrentActiveMontage") == 8 && Reflect::Get().Call(anim, L"GetCurrentActiveMontage", cm.b, 8, sizeof(cm.b)))
                {
                    UObject* cur = nullptr;
                    std::memcpy(&cur, cm.b, 8);
                    if (cur) playing = cur;
                }
                OnMontageStart(anim, playing, startAt);
            }
        }
        catch (...)
        {
            NRN_ERROR("montage start handling threw");
        }
        return r;
    }

    void HookVtableOf(UObject* anim, const char* from)
    {
        if (!anim) return;
        void* fn = (*reinterpret_cast<void***>(anim))[kPlayInternalSlot];
        if (!fn || g_originals.count(fn)) return;
        PlayInternalFn orig = nullptr;
        if (MH_CreateHook(fn, reinterpret_cast<void*>(&PlayInternalHook), reinterpret_cast<void**>(&orig)) != MH_OK || MH_EnableHook(fn) != MH_OK)
        {
            NRN_ERROR("Montage_PlayInternal (vtable slot %zu of %s) at %p: hook failed", kPlayInternalSlot, from, fn);
            return;
        }
        g_originals[fn] = orig;
        uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        NRN_INFO("Montage_PlayInternal hooked: vtable slot %zu of %s -> base+0x%llx", kPlayInternalSlot, from,
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(fn) - base));
    }

    // every AnimInstance class in play: the engine's own default, then whatever the units use (checked each map load)
    void HookAll()
    {
        HookVtableOf(RC::Unreal::UObjectGlobals::StaticFindObject_InternalSlow(nullptr, nullptr, L"/Script/Engine.Default__AnimInstance", false),
                     "AnimInstance");
        std::vector<UObject*> all;
        try
        {
            RC::Unreal::UObjectGlobals::FindAllOf(L"AnimInstance", all);
        }
        catch (...)
        {
        }
        for (UObject* a : all) HookVtableOf(a, Narrow(ShortName(a->GetFullName(nullptr))).c_str());
    }

    bool RunCommand(const std::wstring& c)
    {
        std::wstring cmd = Lower(Trim(c));
        NRN_INFO("command: %s", Narrow(cmd).c_str());
        if (cmd == L"pl.log on" || cmd == L"pl.log off")
        {
            g_logStarts = cmd == L"pl.log on";
            NRN_INFO("montage starts %s logged", g_logStarts ? "are" : "are not");
        }
        else if (cmd == L"pl.reload") LoadTables();
        else if (cmd == L"pl.list")
            for (const Table& t : g_tables)
            {
                NRN_INFO("  [%s]", Narrow(t.pattern).c_str());
                for (const Mark& m : t.marks) NRN_INFO("    %.3f%s = %s", m.percent ? m.at * 100 : m.at, m.percent ? "%" : " s", m.name.c_str());
            }
        else NRN_WARN("unknown command - pl.log on|off | pl.list | pl.reload");
        return true;
    }
} // namespace

class NorsePayload : public RC::CppUserModBase
{
  public:
    NorsePayload()
    {
        ModName = L"NorsePayload";
        ModVersion = L"" NPL_VERSION;
        ModDescription = L"Timing tables on animations: at a moment in a montage, a Payload event (NorseEvents).";
        ModAuthors = L"ApocryphaRealm";
        for (auto& s : g_nopVtbl) s = reinterpret_cast<void*>(&NopFn);
        g_errOut.vtbl = g_nopVtbl;
        g_mod = ModDir();
        std::wstring dir;
        int logLevel = 2; // uLogLevel: 0 trace, 1 debug, 2 info, 3 warn, 4 error, 6 off
        for (const auto& raw : ReadLines(g_mod / "NorsePayload.ini"))
        {
            std::wstring l = Trim(raw);
            if (l.rfind(L"sLogDir=", 0) == 0) dir = Trim(l.substr(8));
            if (l.rfind(L"uLogLevel=", 0) == 0) logLevel = _wtoi(l.c_str() + 10);
            if (l.rfind(L"bLogMontageStarts=", 0) == 0) g_logStarts = _wtoi(l.c_str() + 18) != 0;
        }
        Log::Get().Open((dir.empty() ? g_mod : fs::path(dir)) / "NorsePayload.log", logLevel);
        Log::Get().Write(Level::Error, "NorsePayload v%s - log level %d", NPL_VERSION, logLevel);
        LoadTables();
    }

    auto on_unreal_init() -> void override
    {
        MH_STATUS s = MH_Initialize();
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) NRN_ERROR("MinHook init failed (%d)", static_cast<int>(s));
        try
        {
            RC::Unreal::Hook::RegisterInitGameStatePostCallback([](RC::Unreal::AGameModeBase*) {
                g_tracks.clear();
                HookAll();
            });
            RC::Unreal::Hook::RegisterEngineTickPreCallback([](RC::Unreal::UEngine*, float) {
                static int n = 0;
                if (++n % 300 == 0) HookAll(); // an AnimInstance class first seen mid-map (every ~5 s, cheap)
                if (!g_tracks.empty()) Tick();
            });
            RC::Unreal::Hook::RegisterProcessConsoleExecCallback(
                    [](UObject*, const wchar_t* cmd, RC::Unreal::FOutputDevice&, UObject*) -> bool {
                        if (!cmd || _wcsnicmp(cmd, L"pl.", 3) != 0) return false;
                        static ULONGLONG last = 0;
                        static std::wstring lastCmd;
                        ULONGLONG now = GetTickCount64();
                        if (lastCmd == cmd && now - last < 250) return true;
                        last = now;
                        lastCmd = cmd;
                        return RunCommand(cmd);
                    });
        }
        catch (const std::exception& e)
        {
            NRN_ERROR("callback registration failed: %s", e.what());
        }
    }
};

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    return new NorsePayload();
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
