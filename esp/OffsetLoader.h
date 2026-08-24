#pragma once

// CRT secure-api warnings are just noise for _wfopen (we control paths).
// _CRT_SECURE_NO_WARNINGS must be defined before any CRT header is pulled
// in (including transitively via Windows.h), so we ALSO pragma-disable
// C4996 around the one call site as a belt-and-suspenders measure for
// projects that enable /sdl and forced-include headers.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <Windows.h>
#include <cstdio>
#include <fstream>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <cctype>
#include <regex>
#include <algorithm>
#include <io.h>

// ---------------------------------------------------------------------------
// OffsetLoader
//
// Loads runtime offset overrides from dumper-generated .hpp files placed in
// the output\ folder next to the EXE. Supports BOTH:
//
//   1) Anroshka dota2-dumper format (https://github.com/Anroshka/dota2-dumper):
//        namespace dota2_dumper { namespace offsets { namespace client_dll {
//            constexpr std::ptrdiff_t dwEntityList = 0x...;
//        }}}
//      and per-module schema files like:
//        namespace dota2_dumper { namespace schemas { namespace client_dll {
//            namespace C_BaseEntity {
//                constexpr std::ptrdiff_t m_iHealth = 0x...;
//            }
//        }}}
//      We scan output\*.hpp (and *.hpp next to the EXE as a safety net) so
//      dropping the whole dump folder contents works as-is.
//
//   2) Generic "namespace offsets { ... }" dumper format (a2x/source2gen etc.)
//
//   3) Legacy INI form: client_dll::dwEntityList = 0x...
//
// The loader strips wrapper namespaces ("dota2_dumper", "offsets", "schemas",
// and the module segment e.g. "client_dll" for schemas) so keys normalize to
// the form the ESP expects:
//      client_dll::dwEntityList
//      c_baseentity::m_ihealth
//      cgamescenenode::m_vecabsorigin
//      ...
// ---------------------------------------------------------------------------

namespace OffsetLoader
{
    namespace detail {
        static std::unordered_map<std::string, std::ptrdiff_t*> ptrMap;
        static std::unordered_map<std::string, int*>         intMap;
        static int g_applied = 0;

        static std::string ToLower(std::string s) {
            for (auto& c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }

        static std::string Trim(const std::string& s) {
            size_t a = s.find_first_not_of(" \t\r\n");
            size_t b = s.find_last_not_of(" \t\r\n");
            if (a == std::string::npos) return "";
            return s.substr(a, b - a + 1);
        }

        static bool ParseHex(const std::string& s, uint64_t& out) {
            out = 0;
            if (s.empty()) return false;
            const char* p = s.c_str();
            if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) p += 2;
            while (*p) {
                char c = (char)std::tolower((unsigned char)*p);
                uint64_t d;
                if (c >= '0' && c <= '9')      d = (uint64_t)(c - '0');
                else if (c >= 'a' && c <= 'f') d = 10 + (uint64_t)(c - 'a');
                else return false;
                out = out * 16 + d;
                ++p;
            }
            return true;
        }

        static std::wstring GetExeDirW() {
            wchar_t exe[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            std::wstring p(exe);
            auto slash = p.find_last_of(L"\\/");
            if (slash != std::wstring::npos) p = p.substr(0, slash);
            return p;
        }

        // Strip dumper wrapper prefixes from namespace stack and build the
        // effective key prefix ("c_baseentity::" / "client_dll::" / "").
        //
        // Anroshka globals:  [dota2_dumper, offsets, client_dll]
        //                    -> strip dota2_dumper+offsets -> "client_dll::"
        // Anroshka schemas:  [dota2_dumper, schemas, client_dll, C_BaseEntity]
        //                    -> strip dota2_dumper+schemas+client_dll -> "c_baseentity::"
        // Generic dumper:    [offsets, client_dll] -> "client_dll::"
        //                    [offsets, C_BaseEntity] -> "c_baseentity::"
        static std::string BuildKeyPrefix(const std::vector<std::string>& nsStack) {
            // Normalize to lowercase for parsing.
            std::vector<std::string> ns;
            ns.reserve(nsStack.size());
            for (auto& n : nsStack) ns.push_back(ToLower(n));

            // Pop the root "dota2_dumper" namespace if present (Anroshka).
            if (!ns.empty() && ns[0] == "dota2_dumper")
                ns.erase(ns.begin());

            // Pop "offsets" or "schemas" at position 0 if present.
            if (!ns.empty() && (ns[0] == "offsets" || ns[0] == "schemas"))
                ns.erase(ns.begin());

            // For SCHEMA files, the next token is the module name (client_dll,
            // engine2_dll, ...). Netvars/fields live inside classes inside that
            // module; strip the module so the class name is the first segment.
            // For OFFSETS files the next token IS the module (e.g. client_dll)
            // and the leaf is a global like dwEntityList — keep the module.
            // We distinguish: if depth after stripping is >=2 AND the first
            // segment looks like a module name ("*_dll") AND we came from
            // schemas, drop the module.
            // Simpler heuristic that works for both: after stripping wrappers
            // we have [module, class, ...] OR [class, ...]. If the first
            // segment ends with "_dll" and there are further segments, drop
            // the module segment (so the class name becomes the prefix root).
            if (ns.size() >= 2) {
                const std::string& first = ns[0];
                if (first.size() >= 4 &&
                    first.compare(first.size() - 4, 4, "_dll") == 0) {
                    ns.erase(ns.begin());
                }
            }

            std::string s;
            for (size_t i = 0; i < ns.size(); ++i) {
                if (i) s += "::";
                s += ns[i];
            }
            if (!s.empty()) s += "::";
            return s;
        }

        static void ApplyKeyValue(std::string key, uint64_t v); // fwd decl

        // Parse one stream (hpp or ini style).
        //
        // Strategy: walk the stripped line character by character while
        // maintaining a brace depth counter. Namespace declarations are
        // detected by finding every "namespace <name> <optional ws> {"
        // occurrence and recording that the FOLLOWING '{' opens a namespace
        // scope at the new depth. That way enum/struct/union braces nested
        // inside namespaces don't pop the namespace stack. We apply constexpr
        // lines with the prefix built from the CURRENT namespace stack at
        // the moment we see the 'constexpr' token (correct even when a line
        // ends with "} }" that closes namespaces AFTER the declaration).
        static int ParseStream(std::istream& is) {
            int applied = 0;

            static const std::regex re_expr(
                R"(constexpr\s+(?:std::ptrdiff_t|int|unsigned\s+int|long|long\s+long|uint\d+_t|int\d+_t)\s+([A-Za-z_]\w*)\s*=\s*(0[xX][0-9A-Fa-f]+|-?[0-9]+)\s*;)"
            );
            static const std::regex re_ini(
                R"(\s*([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)+)\s*=\s*(0[xX][0-9A-Fa-f]+|-?[0-9]+)\s*)"
            );
            // Match a single "namespace <ident>" (we'll look for the '{' after
            // the match when scanning the line).
            static const std::regex re_ns_kw(R"(\bnamespace\s+([A-Za-z_]\w*))");

            struct NsFrame { std::string name; int openedAtDepth; };
            std::vector<NsFrame> nsStack;
            int depth = 0;
            // Queue of namespace names whose opening '{' we haven't seen yet.
            // When we see a '{' and this queue is non-empty, the front of the
            // queue is the namespace being opened at that new depth.
            std::vector<std::string> pendingNs;

            auto currentPrefix = [&]() -> std::string {
                std::vector<std::string> names;
                names.reserve(nsStack.size());
                for (auto& f : nsStack) names.push_back(f.name);
                return BuildKeyPrefix(names);
            };

            std::string line;
            while (std::getline(is, line)) {
                // Strip comments.
                {
                    auto c = line.find("//");
                    if (c != std::string::npos) line.erase(c);
                    c = line.find('#');
                    if (c != std::string::npos) line.erase(c);
                    if (Trim(line).empty()) continue;
                }

                // INI form — whole-line simple form, no brace processing needed.
                std::smatch m;
                if (std::regex_match(line, m, re_ini)) {
                    uint64_t v = 0;
                    if (ParseHex(m[2].str(), v)) { ApplyKeyValue(m[1].str(), v); ++applied; }
                    continue;
                }

                // Pre-scan: find every "namespace NAME" occurrence and queue
                // the names in source order so we know which '{' belongs to
                // a namespace (vs. an enum/struct/union).
                pendingNs.clear();
                {
                    auto b = std::sregex_iterator(line.begin(), line.end(), re_ns_kw);
                    auto e = std::sregex_iterator();
                    for (auto it = b; it != e; ++it) pendingNs.push_back((*it)[1].str());
                }

                // Now walk each character, handling '{' and '}' and applying
                // constexprs when they appear at current position.
                size_t pos = 0;
                const size_t len = line.size();
                while (pos < len) {
                    char ch = line[pos];

                    if (ch == '{') {
                        int newDepth = depth + 1;
                        // Does this '{' open a pending namespace?
                        if (!pendingNs.empty()) {
                            nsStack.push_back({ pendingNs.front(), newDepth });
                            pendingNs.erase(pendingNs.begin());
                        }
                        depth = newDepth;
                        ++pos;
                        continue;
                    }
                    if (ch == '}') {
                        int newDepth = depth - 1;
                        // Close namespaces that were opened at a depth
                        // greater than the post-close depth.
                        while (!nsStack.empty() && nsStack.back().openedAtDepth > newDepth) {
                            nsStack.pop_back();
                        }
                        depth = newDepth;
                        if (depth < 0) depth = 0; // safety
                        ++pos;
                        continue;
                    }

                    // Try matching constexpr at pos.
                    if (pos + 9 <= len && line.compare(pos, 9, "constexpr") == 0 &&
                        isspace((unsigned char)line[pos + 9])) {
                        std::string sub = line.substr(pos);
                        std::smatch em;
                        if (std::regex_search(sub, em, re_expr) && em.position(0) == 0) {
                            std::string name = em[1].str();
                            uint64_t v = 0;
                            if (ParseHex(em[2].str(), v)) {
                                std::string key = currentPrefix() + name;
                                ApplyKeyValue(key, v);
                                ++applied;
                            }
                            pos += em.length(0);
                            continue;
                        }
                    }

                    ++pos;
                }
            }
            return applied;
        }

        static void ApplyKeyValue(std::string key, uint64_t v) {
            key = ToLower(Trim(key));

            // Some dumpers expose the entity system via dwEntityList, others
            // via dwGameEntitySystem; alias both to our single slot.
            if (key == "client_dll::dwgameentitysystem")
                key = "client_dll::dwentitylist";

            auto itP = ptrMap.find(key);
            if (itP != ptrMap.end()) { *itP->second = (std::ptrdiff_t)v; return; }
            auto itI = intMap.find(key);
            if (itI != intMap.end()) { *itI->second = (int)v; return; }

            // Silently ignore globals from other modules (engine2, inputsystem, etc.)
            // (anything from a DLL we don't consume we just skip — no warning spam
            // for 3000+ fields the ESP doesn't use.)
            // plus fields from classes the ESP doesn't use.
            static const char* kSilentPrefixes[] = {
                "engine2_dll::", "engine_dll::", "inputsystem_dll::",
                "soundsystem_dll::", "animationsystem_dll::", "worldrenderer_dll::",
                "vphysics2_dll::", "pulse_system_dll::", "filesystem_stdio_dll::",
                "matchmaking_dll::", "server_dll::", "networksystem_dll::",
            };
            // Silently ignore specific extra globals in client_dll that the ESP
            // doesn't currently consume (present in Anroshka's dump).
            static const char* kSilentKeys[] = {
                "client_dll::dwgameentitysystem_highestentityindex",
                "client_dll::dwgamerules",
                "client_dll::dwglobalvars",
                "client_dll::dwviewrender",
            };
            for (auto sk : kSilentKeys) if (key == sk) return;
            for (auto pre : kSilentPrefixes) {
                std::string p(pre);
                if (key.compare(0, p.size(), p) == 0)
                    return;
            }
        }

        static void Register(const char* key, std::ptrdiff_t* var) {
            ptrMap[ToLower(key)] = var;
        }
        static void Register(const char* key, int* var) {
            intMap[ToLower(key)] = var;
        }

        static void RegisterDefaults() {
            using namespace offsets;
            // client.dll globals (offsets.hpp)
            Register("client_dll::dwentitylist",              &client_dll::dwEntityList);
            Register("client_dll::dwlocalplayerpawn",        &client_dll::dwLocalPlayerPawnBase);
            Register("client_dll::dwlocalplayerpawnbase",    &client_dll::dwLocalPlayerPawnBase);
            Register("client_dll::dwviewmatrix",             &client_dll::dwViewMatrix);

		// CGameEntitySystem layout (stable; not in Anroshka's dump but overridable)
		Register("cgameentitysystem::m_entityptrarray", &CGameEntitySystem::m_EntityPtrArray);
		Register("cgameentitysystem::identitystride",   &CGameEntitySystem::IdentityStride);
		Register("cgameentitysystem::m_pinstance",      &CGameEntitySystem::m_pInstance);
		// ChunkSize is intentionally NOT registered -- Source2 always uses 512
		// slots per entity-list chunk, and making it constexpr lets the compiler
		// constant-fold MaxScanIndex / division/modulo in the hot loop.

            // Schemas (client_dll.hpp)
            Register("c_baseentity::m_cbodycomponent",  &C_BaseEntity::m_CBodyComponent);
            Register("c_baseentity::m_imaxhealth",      &C_BaseEntity::m_iMaxHealth);
            Register("c_baseentity::m_ihealth",         &C_BaseEntity::m_iHealth);
            Register("c_baseentity::m_lifestate",       &C_BaseEntity::m_lifeState);
            Register("c_baseentity::m_iteamnum",        &C_BaseEntity::m_iTeamNum);
            Register("c_baseentity::m_pgamescenenode",  &C_BaseEntity::m_pGameSceneNode);
            Register("c_baseentity::m_hownerentity",    &C_BaseEntity::m_hOwnerEntity);

            Register("cgamescenenode::m_vecabsorigin", &CGameSceneNode::m_vecAbsOrigin);

            Register("c_dota_basenpc::m_icurrentlevel", &C_DOTA_BaseNPC::m_iCurrentLevel);
            Register("c_dota_basenpc::m_flmana",        &C_DOTA_BaseNPC::m_flMana);
            Register("c_dota_basenpc::m_flmaxmana",     &C_DOTA_BaseNPC::m_flMaxMana);
            Register("c_dota_basenpc::m_bisillusion",   &C_DOTA_BaseNPC::m_bIsIllusion);
            Register("c_dota_basenpc::m_vecabilities",  &C_DOTA_BaseNPC::m_vecAbilities);
            Register("c_dota_basenpc::m_idamagemin",    &C_DOTA_BaseNPC::m_iDamageMin);
            Register("c_dota_basenpc::m_idamagemax",    &C_DOTA_BaseNPC::m_iDamageMax);

            Register("cbaseplayercontroller::m_hpawn",                     &CBasePlayerController::m_hPawn);
            Register("cbaseplayercontroller::m_bislocalplayercontroller",  &CBasePlayerController::m_bIsLocalPlayerController);
        }

        // Read a whole file into a string; returns empty on failure.
        static std::string ReadFileUtf8(const std::wstring& path) {
            // _wfopen is the only portable way on MSVC to open a file via
            // a wide path; we know the path is safe (it's under our EXE dir).
            #pragma warning(push)
            #pragma warning(disable: 4996)
            FILE* f = _wfopen(path.c_str(), L"rb");
            #pragma warning(pop)
            if (!f) return {};
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz <= 0) { fclose(f); return {}; }
            std::string data((size_t)sz, '\0');
            size_t n = fread(&data[0], 1, (size_t)sz, f);
            fclose(f);
            data.resize(n);
            // Strip UTF-8 BOM if present.
            if (data.size() >= 3 &&
                (unsigned char)data[0] == 0xEF &&
                (unsigned char)data[1] == 0xBB &&
                (unsigned char)data[2] == 0xBF)
                data.erase(0, 3);
            return data;
        }

        static int ParseFile(const std::wstring& path) {
            std::string data = ReadFileUtf8(path);
            if (data.empty()) return 0;
            std::istringstream ss(data);
            return ParseStream(ss);
        }

        // Collect every .hpp file under a directory (non-recursive).
        static std::vector<std::wstring> ListHppFiles(const std::wstring& dir) {
            std::vector<std::wstring> out;
            std::wstring pattern = dir + L"\\*.hpp";
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return out;
            do {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                    out.push_back(dir + L"\\" + fd.cFileName);
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
            return out;
        }
    }

    static void Register(const char* key, std::ptrdiff_t* var) {
        detail::ptrMap[detail::ToLower(key)] = var;
    }
    static void Register(const char* key, int* var) {
        detail::intMap[detail::ToLower(key)] = var;
    }

    // Load all known offset sources. Returns the number of overrides applied.
    // 0 means "nothing loaded, using compiled-in defaults".
    static int LoadFromFile() {
        using namespace detail;
        RegisterDefaults();
        g_applied = 0;

        std::wstring exeDir  = GetExeDirW();
        std::wstring outDir   = exeDir + L"\\output";
        std::wstring rootDir  = exeDir;

        // Preferred order:
        //   1. output\offsets.hpp          (globals from Anroshka or generic dumper)
        //   2. offsets.hpp                 (if user drops it at exe root)
        //   3. output\offsets.ini          (legacy)
        //   4. Every other *.hpp in output\ (schemas / per-module netvars)
        //   5. Every *.hpp at exe root     (safety net)

        auto TryLoad = [&](const std::wstring& path, const wchar_t* label) -> bool {
            int n = ParseFile(path);
            if (n > 0) {
                wprintf(L"[+] Loaded %d offset override(s) from %ls\n", n, label);
                g_applied += n;
                return true;
            }
            return false;
        };

        bool loadedAny = false;
        loadedAny |= TryLoad(outDir + L"\\offsets.hpp",  L"output\\offsets.hpp");
        loadedAny |= TryLoad(rootDir + L"\\offsets.hpp", L"offsets.hpp");
        loadedAny |= TryLoad(outDir + L"\\offsets.ini",  L"output\\offsets.ini");

        // Load every remaining .hpp in output\ so per-module schema files
        // (client_dll.hpp etc.) are picked up automatically.
        for (auto& p : ListHppFiles(outDir)) {
            // Skip offsets.hpp if we already did it.
            std::wstring lower;
            for (auto c : p) lower += (wchar_t)towlower(c);
            if (lower.size() >= 12 &&
                lower.rfind(L"offsets.hpp") == lower.size() - 11)
                continue;
            std::wstring label = L"output\\" + p.substr(p.find_last_of(L"\\/") + 1);
            int n = ParseFile(p);
            if (n > 0) {
                wprintf(L"[+] Loaded %d schema/offset override(s) from %ls\n", n, label.c_str());
                g_applied += n;
                loadedAny = true;
            }
        }

        // Safety net: root-level .hpp files except offsets.hpp (done above).
        for (auto& p : ListHppFiles(rootDir)) {
            std::wstring lower;
            for (auto c : p) lower += (wchar_t)towlower(c);
            if (lower.size() >= 11 &&
                lower.rfind(L"offsets.hpp") == lower.size() - 11)
                continue;
            std::wstring label = p.substr(p.find_last_of(L"\\/") + 1);
            int n = ParseFile(p);
            if (n > 0) {
                wprintf(L"[+] Loaded %d schema/offset override(s) from %ls\n", n, label.c_str());
                g_applied += n;
                loadedAny = true;
            }
        }

        if (!loadedAny) {
            printf("[*] No offset override files found in output\\ -- using compiled-in defaults.\n");
            printf("    Drop dumper-generated .hpp files (e.g. from Anroshka/dota2-dumper) into output\\ to update.\n");
        }
        return g_applied;
    }
}
