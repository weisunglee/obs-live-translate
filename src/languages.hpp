#pragma once

namespace lt {
struct LangEntry {
    const char *code;
    const char *name;
};

static const LangEntry kLanguages[] = {
    {"en", "English"},
    {"zh-TW", "Chinese (Traditional)"},
    {"zh-CN", "Chinese (Simplified)"},
    {"ja", "Japanese"},
    {"ko", "Korean"},
    {"es", "Spanish"},
    {"fr", "French"},
    {"de", "German"},
    {"pt", "Portuguese"},
    {"it", "Italian"},
    {"ru", "Russian"},
    {"id", "Indonesian"},
    {"th", "Thai"},
    {"vi", "Vietnamese"},
};

static const int kLanguagesCount = sizeof(kLanguages) / sizeof(kLanguages[0]);
}
