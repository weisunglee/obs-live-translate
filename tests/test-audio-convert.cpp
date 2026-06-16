#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "audio-convert.hpp"
#include <vector>

using namespace lt;

TEST_CASE("downmix averages channels into mono")
{
    float l[] = {1.0f, 0.0f, -1.0f};
    float r[] = {-1.0f, 0.0f, 1.0f};
    const float *planes[] = {l, r};
    std::vector<float> mono = downmix_to_mono(planes, 2, 3);
    REQUIRE(mono.size() == 3);
    REQUIRE(mono[0] == Catch::Approx(0.0f));
    REQUIRE(mono[1] == Catch::Approx(0.0f));
    REQUIRE(mono[2] == Catch::Approx(0.0f));
}

TEST_CASE("downmix with one channel is identity")
{
    float c[] = {0.5f, -0.5f};
    const float *planes[] = {c};
    std::vector<float> mono = downmix_to_mono(planes, 1, 2);
    REQUIRE(mono[0] == Catch::Approx(0.5f));
    REQUIRE(mono[1] == Catch::Approx(-0.5f));
}

TEST_CASE("float to s16le clamps and scales")
{
    std::vector<float> in{0.0f, 1.0f, -1.0f, 2.0f, -2.0f};
    std::vector<uint8_t> pcm = float_to_s16le(in.data(), in.size());
    REQUIRE(pcm.size() == in.size() * 2);
    auto sample = [&](size_t i) {
        return static_cast<int16_t>(pcm[i * 2] | (pcm[i * 2 + 1] << 8));
    };
    REQUIRE(sample(0) == 0);
    REQUIRE(sample(1) == 32767);
    REQUIRE(sample(2) == -32767);
    REQUIRE(sample(3) == 32767);
    REQUIRE(sample(4) == -32767);
}

TEST_CASE("chunker emits fixed-size blocks and carries remainder")
{
    Chunker chunker(3200);
    std::vector<uint8_t> a(2000, 0xAB);
    auto out1 = chunker.push(a.data(), a.size());
    REQUIRE(out1.empty());

    std::vector<uint8_t> b(2500, 0xCD);
    auto out2 = chunker.push(b.data(), b.size());
    REQUIRE(out2.size() == 1);
    REQUIRE(out2[0].size() == 3200);

    std::vector<uint8_t> c(2000, 0xEE);
    auto out3 = chunker.push(c.data(), c.size());
    REQUIRE(out3.size() == 1);
    REQUIRE(out3[0].size() == 3200);
}

TEST_CASE("s16le signal detector ignores silence and low noise")
{
    std::vector<uint8_t> silence(3200, 0);
    REQUIRE(s16le_rms(silence.data(), silence.size()) == Catch::Approx(0.0));
    REQUIRE_FALSE(s16le_has_signal(silence.data(), silence.size(), 500.0));

    std::vector<uint8_t> low_noise;
    for (int i = 0; i < 1600; ++i) {
        int16_t sample = (i % 2 == 0) ? 100 : -100;
        low_noise.push_back(static_cast<uint8_t>(sample & 0xFF));
        low_noise.push_back(static_cast<uint8_t>((sample >> 8) & 0xFF));
    }
    REQUIRE_FALSE(s16le_has_signal(low_noise.data(), low_noise.size(), 500.0));
}

TEST_CASE("s16le signal detector passes speech-level samples")
{
    std::vector<uint8_t> speech;
    for (int i = 0; i < 1600; ++i) {
        int16_t sample = (i % 2 == 0) ? 2000 : -2000;
        speech.push_back(static_cast<uint8_t>(sample & 0xFF));
        speech.push_back(static_cast<uint8_t>((sample >> 8) & 0xFF));
    }
    REQUIRE(s16le_has_signal(speech.data(), speech.size(), 500.0));
}
