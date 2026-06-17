#pragma once

namespace lt {
struct LangEntry {
    const char *code;
    const char *name;
};

// Target languages supported by gemini-3.5-live-translate-preview, per the
// official Gemini Live Translate docs (78 languages):
//   https://ai.google.dev/gemini-api/docs/live-api/live-translate
// `code` is the BCP-47 value sent as translationConfig.targetLanguageCode.
// Names are shown in Traditional Chinese. Common languages are listed first;
// the rest follow in English-name alphabetical order (matching the doc).
// Names use plain narrow string literals; the Windows build compiles with
// /utf-8 (cmake/windows/compilerconfig.cmake) so these are UTF-8 bytes, which
// is what obs_property_list_add_string expects.
static const LangEntry kLanguages[] = {
    // Common (pinned to the top of the dropdown)
    {"en", "英文"},
    // Speech-to-speech output has no script, so a single script-less "zh" is
    // used: zh-Hant / zh-Hans sound identical and break same-language echo
    // (the model doesn't treat spoken Mandarin as an exact match for a
    // script-tagged target), whereas "zh" echoes and translates correctly.
    {"zh", "中文"},
    {"ja", "日文"},
    {"ko", "韓文"},
    {"es", "西班牙文"},
    {"fr", "法文"},
    {"de", "德文"},
    {"pt-BR", "葡萄牙文（巴西）"},
    {"it", "義大利文"},
    {"ru", "俄文"},
    {"id", "印尼文"},
    {"th", "泰文"},
    {"vi", "越南文"},

    // Remaining languages, by English name (A→Z)
    {"af", "阿非利卡文"},
    {"ak", "阿坎文"},
    {"sq", "阿爾巴尼亞文"},
    {"am", "阿姆哈拉文"},
    {"ar", "阿拉伯文"},
    {"hy", "亞美尼亞文"},
    {"az", "亞塞拜然文"},
    {"eu", "巴斯克文"},
    {"be", "白俄羅斯文"},
    {"bn", "孟加拉文"},
    {"bg", "保加利亞文"},
    {"my", "緬甸文"},
    {"ca", "加泰隆尼亞文"},
    {"hr", "克羅埃西亞文"},
    {"cs", "捷克文"},
    {"da", "丹麥文"},
    {"nl", "荷蘭文"},
    {"et", "愛沙尼亞文"},
    {"fil", "菲律賓文"},
    {"fi", "芬蘭文"},
    {"gl", "加利西亞文"},
    {"ka", "喬治亞文"},
    {"el", "希臘文"},
    {"gu", "古吉拉特文"},
    {"ha", "豪薩文"},
    {"he", "希伯來文"},
    {"hi", "印地文"},
    {"hu", "匈牙利文"},
    {"is", "冰島文"},
    {"jv", "爪哇文"},
    {"kn", "坎那達文"},
    {"kk", "哈薩克文"},
    {"km", "高棉文"},
    {"rw", "盧安達文"},
    {"lo", "寮文"},
    {"lv", "拉脫維亞文"},
    {"lt", "立陶宛文"},
    {"mk", "馬其頓文"},
    {"ms", "馬來文"},
    {"ml", "馬拉雅拉姆文"},
    {"mr", "馬拉地文"},
    {"mn", "蒙古文"},
    {"ne", "尼泊爾文"},
    {"no", "挪威文"},
    {"fa", "波斯文"},
    {"pl", "波蘭文"},
    {"pt-PT", "葡萄牙文（葡萄牙）"},
    {"pa", "旁遮普文"},
    {"ro", "羅馬尼亞文"},
    {"sr", "塞爾維亞文"},
    {"sd", "信德文"},
    {"si", "僧伽羅文"},
    {"sk", "斯洛伐克文"},
    {"sl", "斯洛維尼亞文"},
    {"su", "巽他文"},
    {"sw", "史瓦希里文"},
    {"sv", "瑞典文"},
    {"ta", "坦米爾文"},
    {"te", "泰盧固文"},
    {"tr", "土耳其文"},
    {"uk", "烏克蘭文"},
    {"ur", "烏爾都文"},
    {"uz", "烏茲別克文"},
    {"zu", "祖魯文"},
};

static const int kLanguagesCount = sizeof(kLanguages) / sizeof(kLanguages[0]);
}
