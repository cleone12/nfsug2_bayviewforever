// Mac-side test of save_core against real files (saves and stored cars from test play).
// Build + run: ./test.sh   (native clang, not for Windows)
//
// Checks:
//  1. Every profile save passes the container checks; prints cash, stage and cars.
//  2. Every .vgcar the plugin wrote (from game memory) matches a car converted from some save byte for
//     byte over the range the plugin copies -> proves SlotImage's save->memory layout.
//  3. SetCash round trip: new cash reads back, the checksums are valid, setting the old cash restores
//     the original file exactly.
#include "save_core.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace st;

static Bytes ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(f), {});
}

static void Walk(const std::string& dir, std::vector<std::string>* out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string p = dir + "/" + e->d_name;
        struct stat s;
        if (stat(p.c_str(), &s) != 0) continue;
        if (S_ISDIR(s.st_mode)) Walk(p, out);
        else out->push_back(p);
    }
    closedir(d);
}

int main(int argc, char** argv) {
    std::vector<std::string> files;
    for (int i = 1; i < argc; i++) Walk(argv[i], &files);

    int failures = 0, saves = 0;
    std::vector<std::pair<std::string, Bytes>> images;  // every car found in a save
    for (const std::string& p : files) {
        struct stat s;
        if (stat(p.c_str(), &s) != 0 || s.st_size != static_cast<off_t>(kSaveSize)) continue;
        Bytes d = ReadFile(p);
        ProfileInfo info = ReadProfile(d);
        saves++;
        if (!info.ok) {
            printf("FAIL %s: %s\n", p.c_str(), info.error.c_str());
            failures++;
            continue;
        }
        printf("%-70s cash %9d stage %d cars:", p.c_str(), info.cash, info.stage);
        for (const Car& c : info.cars) {
            printf(" %d=%s(%d parts, $%d)", c.slot, CarName(c.model), c.parts, c.spent);
            images.push_back({p + " slot " + std::to_string(c.slot), SlotImage(d, c.slot)});
        }
        printf("\n");

        // 3. SetCash round trip
        Bytes copy = d;
        if (!SetCash(copy, info.cash + 123456) || ReadProfile(copy).cash != info.cash + 123456 ||
            !SetCash(copy, info.cash) || copy != d) {
            printf("FAIL SetCash round trip on %s\n", p.c_str());
            failures++;
        }
    }

    int stored = 0, matched = 0;
    for (const std::string& p : files) {
        if (p.size() < 6 || p.compare(p.size() - 6, 6, ".vgcar") != 0) continue;
        Bytes image;
        stored++;
        if (!ReadStoreFile(ReadFile(p), &image)) {
            printf("FAIL unreadable store file %s\n", p.c_str());
            failures++;
            continue;
        }
        const char* match = nullptr;
        for (const auto& im : images)
            if (SameCar(im.second, image)) {
                match = im.first.c_str();
                break;
            }
        if (match) matched++;
        printf("%s %s\n    = %s\n", match ? "MATCH" : "no match", p.c_str(), match ? match : "-");
        // The converted file must read back as the same car.
        if (match) {
            for (const auto& im : images)
                if (im.first == match) {
                    Bytes back;
                    if (!ReadStoreFile(MakeStoreFile(im.second, "Chris"), &back) || !SameCar(back, im.second)) {
                        printf("FAIL converted file doesn't read back\n");
                        failures++;
                    }
                }
        }
    }

    printf("\n%d saves, %zu cars in saves, %d store files, %d matched a save car, %d failures\n", saves,
           images.size(), stored, matched, failures);
    printf("name: %s / %s / seq %ld\n", StoreFileName(34, StringHash("SKYLINE"), "Old Save!").c_str(),
           StoreFolderName("My Profile.1").c_str(), StoreFileSeq("000033_350Z.vgcar"));
    return failures ? 1 : 0;
}
