#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <dlfcn.h>
#include <link.h>
#include "logging.h"
#include "elf_util.h"
#include "solist.h"
#include "enc_str.h"

namespace Solist {
    namespace {
        std::string GetLinkerPath() {
#if __LP64__
            if (android_get_device_api_level() >= 29) {
                return "/apex/com.android.runtime/bin/linker64"_ienc .c_str();
            } else {
                return "/system/bin/linker64"_ienc .c_str();
            }
#else
            if (android_get_device_api_level() >= 29) {
                return "/apex/com.android.runtime/bin/linker"_ienc .c_str();
            } else {
                return "/system/bin/linker"_ienc .c_str();
            }
#endif
        }

        struct soinfo;

        soinfo *solist = nullptr;
        soinfo *somain = nullptr;

        struct soinfo {
            soinfo *next() {
                return *(soinfo **) ((uintptr_t) this + solist_next_offset);
            }

            const char *get_realpath() {
                return get_realpath_sym ? get_realpath_sym(this) : ((std::string *) (
                        (uintptr_t) this +
                        solist_realpath_offset))->c_str();
            }

            const char *get_soname() {
                return get_soname_sym ? get_soname_sym(this) : *((const char**) (
                        (uintptr_t) this +
                        solist_realpath_offset - sizeof(void*)));
            }

            static bool setup(const SandHook::ElfImg &linker) {
                get_realpath_sym = reinterpret_cast<decltype(get_realpath_sym)>(linker.getSymbAddress(
                        "__dl__ZNK6soinfo12get_realpathEv"_ienc .c_str()));
                get_soname_sym = reinterpret_cast<decltype(get_soname_sym)>(linker.getSymbAddress(
                        "__dl__ZNK6soinfo10get_sonameEv"_ienc .c_str()));
                for (size_t i = 0; i < 1024 / sizeof(void *); i++) {
                    if (*(void **) ((uintptr_t) solist + i * sizeof(void *)) == somain) {
                        solist_next_offset = i * sizeof(void *);
                        return android_get_device_api_level() < 26 || (get_realpath_sym != nullptr && get_soname_sym !=
                                                                                                              nullptr);
                    }
                }
                LOGW("%s", "failed to search next offset"_ienc .c_str());
                // shortcut
                return android_get_device_api_level() < 26 || (get_realpath_sym != nullptr && get_soname_sym != nullptr);
            }

            static size_t solist_next_offset;
#ifdef __LP64__
            constexpr static size_t solist_realpath_offset = 0x1a8;
#else
            constexpr static size_t solist_realpath_offset = 0x174;
#endif

            // since Android 8
            inline static const char *(*get_realpath_sym)(soinfo *) = nullptr;
            inline static const char *(*get_soname_sym)(soinfo *) = nullptr;
        };

#ifdef __LP64__
        size_t soinfo::solist_next_offset = 0x30;
#else
        size_t soinfo::solist_next_offset = 0xa4;
#endif

        const auto initialized = []() {
            SandHook::ElfImg linker(GetLinkerPath().c_str());
            return (solist = *reinterpret_cast<soinfo **>(linker.getSymbAddress(
                    "__dl__ZL6solist"_ienc .c_str()))) != nullptr &&
                   (somain = *reinterpret_cast<soinfo **>(linker.getSymbAddress(
                           "__dl__ZL6somain"_ienc .c_str()))) != nullptr &&
                   soinfo::setup(linker);
        }();

        std::vector<soinfo *> linker_get_solist() {
            std::vector<soinfo *> linker_solist{};
            for (auto *iter = solist; iter; iter = iter->next()) {
                linker_solist.push_back(iter);
            }
            return linker_solist;
        }
    }

    std::set<std::string_view> FindPathsFromSolist(std::string_view keyword) {
        std::set<std::string_view> paths{};
        if (!initialized) {
            LOGW("%s", "not initialized"_ienc .c_str());
            return paths;
        }
        auto list = linker_get_solist();
        for (const auto &soinfo : linker_get_solist()) {
            if (const auto &real_path = soinfo->get_realpath(); real_path && std::string_view(real_path).find(keyword) != std::string::npos) {
                paths.emplace(real_path);
                LOGE("%s%s", "Found Riru:"_ienc .c_str(), real_path);
            } else if(const auto &soname = soinfo->get_soname(); soname && std::string_view(soname).find(keyword) != std::string::npos) {
                paths.emplace(soname);
                LOGE("%s%s", "Found Riru:"_ienc .c_str(), soname);
            }
        }
        return paths;
    }
}
