#include "live-protocol.hpp"
#include "base64.hpp"
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace lt {

std::string build_setup_message(const std::string &target, bool echo)
{
    json j;
    j["setup"]["model"] = "models/gemini-3.5-live-translate-preview";
    j["setup"]["generationConfig"]["responseModalities"] = json::array({"AUDIO"});
    j["setup"]["generationConfig"]["translationConfig"]["targetLanguageCode"] = target;
    j["setup"]["generationConfig"]["translationConfig"]["echoTargetLanguage"] = echo;
    return j.dump();
}

std::string build_realtime_input_message(const uint8_t *pcm, size_t len)
{
    json chunk;
    chunk["mimeType"] = "audio/pcm;rate=16000";
    chunk["data"] = base64_encode(pcm, len);
    json j;
    j["realtimeInput"]["audio"] = chunk;
    return j.dump();
}

ServerMessage parse_server_message(const std::string &text)
{
    ServerMessage m;
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        m.kind = ServerMessage::Kind::Other;
        return m;
    }

    if (j.contains("error")) {
        m.kind = ServerMessage::Kind::Error;
        if (j["error"].contains("message"))
            m.error_message = j["error"]["message"].get<std::string>();
        return m;
    }

    if (j.contains("setupComplete")) {
        m.kind = ServerMessage::Kind::SetupComplete;
        return m;
    }

    if (j.contains("serverContent")) {
        const auto &sc = j["serverContent"];
        if (sc.contains("modelTurn") && sc["modelTurn"].contains("parts")) {
            for (const auto &part : sc["modelTurn"]["parts"]) {
                if (part.contains("inlineData") &&
                    part["inlineData"].contains("data")) {
                    auto mime = part["inlineData"].value("mimeType", "");
                    if (mime.rfind("audio/pcm", 0) == 0) {
                        m.kind = ServerMessage::Kind::Audio;
                        auto audio = base64_decode(
                            part["inlineData"]["data"].get<std::string>());
                        m.audio.insert(m.audio.end(), audio.begin(), audio.end());
                    }
                }
            }
            if (m.kind == ServerMessage::Kind::Audio) return m;
        }
    }

    m.kind = ServerMessage::Kind::Other;
    return m;
}

}
