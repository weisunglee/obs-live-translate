#include <catch2/catch_test_macros.hpp>
#include "live-protocol.hpp"
#include <nlohmann/json.hpp>

using namespace lt;
using nlohmann::json;

TEST_CASE("setup message has model and translation config")
{
    std::string msg = build_setup_message("zh-TW", true);
    json j = json::parse(msg);
    REQUIRE(j["setup"]["model"] == "models/gemini-3.5-live-translate-preview");
    REQUIRE(j["setup"]["generationConfig"]["responseModalities"][0] == "AUDIO");
    REQUIRE(j["setup"]["generationConfig"]["translationConfig"]["targetLanguageCode"] == "zh-TW");
    REQUIRE(j["setup"]["generationConfig"]["translationConfig"]["echoTargetLanguage"] == true);
    REQUIRE(j["setup"]["generationConfig"]["inputAudioTranscription"].is_object());
    REQUIRE(j["setup"]["generationConfig"]["outputAudioTranscription"].is_object());
}

TEST_CASE("setup message can disable target-language echo")
{
    std::string msg = build_setup_message("en", false);
    json j = json::parse(msg);
    REQUIRE(j["setup"]["generationConfig"]["translationConfig"]["targetLanguageCode"] == "en");
    REQUIRE(j["setup"]["generationConfig"]["translationConfig"]["echoTargetLanguage"] == false);
}

TEST_CASE("realtime input message carries base64 pcm with correct mime")
{
    std::vector<uint8_t> pcm{0x01, 0x02, 0x03, 0x04};
    std::string msg = build_realtime_input_message(pcm.data(), pcm.size());
    json j = json::parse(msg);
    auto chunk = j["realtimeInput"]["audio"];
    REQUIRE(chunk["mimeType"] == "audio/pcm;rate=16000");
    REQUIRE(chunk["data"].get<std::string>() == "AQIDBA==");
}

TEST_CASE("parse extracts pcm audio from serverContent")
{
    std::string server = R"({
      "serverContent": { "modelTurn": { "parts": [
        { "inlineData": { "mimeType": "audio/pcm;rate=24000", "data": "AQIDBA==" } }
      ] } }
    })";
    ServerMessage m = parse_server_message(server);
    REQUIRE(m.kind == ServerMessage::Kind::Audio);
    REQUIRE(m.audio == std::vector<uint8_t>{1, 2, 3, 4});
}

TEST_CASE("parse concatenates multiple pcm audio parts from one server message")
{
    std::string server = R"({
      "serverContent": { "modelTurn": { "parts": [
        { "inlineData": { "mimeType": "audio/pcm;rate=24000", "data": "AQI=" } },
        { "inlineData": { "mimeType": "audio/pcm;rate=24000", "data": "AwQ=" } }
      ] } }
    })";
    ServerMessage m = parse_server_message(server);
    REQUIRE(m.kind == ServerMessage::Kind::Audio);
    REQUIRE(m.audio == std::vector<uint8_t>{1, 2, 3, 4});
}

TEST_CASE("parse flags setup-rejected / error messages")
{
    std::string err = R"({"error": {"code": 400, "message": "API key not valid"}})";
    ServerMessage m = parse_server_message(err);
    REQUIRE(m.kind == ServerMessage::Kind::Error);
    REQUIRE(m.error_message.find("API key") != std::string::npos);
}

TEST_CASE("parse ignores unrelated messages gracefully")
{
    ServerMessage m = parse_server_message(R"({"setupComplete": {}})");
    REQUIRE(m.kind == ServerMessage::Kind::SetupComplete);
}

TEST_CASE("parse flags turnComplete and generationComplete on serverContent")
{
    std::string s =
        R"({"serverContent": {"generationComplete": true, "turnComplete": true}})";
    ServerMessage m = parse_server_message(s);
    REQUIRE(m.turn_complete == true);
    REQUIRE(m.generation_complete == true);
    REQUIRE(m.interrupted == false);
}

TEST_CASE("parse flags interrupted on serverContent")
{
    ServerMessage m = parse_server_message(R"({"serverContent": {"interrupted": true}})");
    REQUIRE(m.interrupted == true);
    REQUIRE(m.kind == ServerMessage::Kind::Other);
}

TEST_CASE("parse keeps audio and turnComplete in one message")
{
    std::string s = R"({
      "serverContent": { "turnComplete": true, "modelTurn": { "parts": [
        { "inlineData": { "mimeType": "audio/pcm;rate=24000", "data": "AQIDBA==" } }
      ] } }
    })";
    ServerMessage m = parse_server_message(s);
    REQUIRE(m.kind == ServerMessage::Kind::Audio);
    REQUIRE(m.audio == std::vector<uint8_t>{1, 2, 3, 4});
    REQUIRE(m.turn_complete == true);
}

TEST_CASE("parse leaves turn flags false when absent")
{
    std::string s = R"({
      "serverContent": { "modelTurn": { "parts": [
        { "inlineData": { "mimeType": "audio/pcm;rate=24000", "data": "AQIDBA==" } }
      ] } }
    })";
    ServerMessage m = parse_server_message(s);
    REQUIRE(m.kind == ServerMessage::Kind::Audio);
    REQUIRE(m.turn_complete == false);
    REQUIRE(m.generation_complete == false);
    REQUIRE(m.interrupted == false);
}
