// mini_audio_router.cpp
#include <alsa/asoundlib.h>
#include <iostream>
#include <vector>
#include <csignal>

const char *RASPBERRYPI = "plughw:0,0";
const char *AUDIOBOXVSL = "plughw:3,0";
const char *AUDIOBOXUSB = "plughw:4,0";

static bool running = true;
void stop(int) { running = false; }

static void check(int err, const char *msg)
{
    if (err < 0)
    {
        std::cerr << msg << " : " << snd_strerror(err) << "\n";
        exit(1);
    }
}

int main()
{
    signal(SIGINT, stop);

    snd_pcm_t *cap2 = nullptr;
    snd_pcm_t *cap3 = nullptr;

    snd_pcm_t *out1 = nullptr;
    snd_pcm_t *out2 = nullptr;
    snd_pcm_t *out3 = nullptr;

    check(snd_pcm_open(&cap2, AUDIOBOXVSL, SND_PCM_STREAM_CAPTURE, 0), "open capture");
    check(snd_pcm_open(&cap3, AUDIOBOXUSB, SND_PCM_STREAM_CAPTURE, 0), "open capture");

    check(snd_pcm_open(&out1, RASPBERRYPI, SND_PCM_STREAM_PLAYBACK, 0), "open playback");
    check(snd_pcm_open(&out2, AUDIOBOXVSL, SND_PCM_STREAM_PLAYBACK, 0), "open playback");
    check(snd_pcm_open(&out3, AUDIOBOXUSB, SND_PCM_STREAM_PLAYBACK, 0), "open playback");

    int rate = 48000;
    int channels = 2;
    snd_pcm_uframes_t frames = 128;

    int err;

    err = snd_pcm_set_params(cap2, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate, 1, 20000);
    check(err, "set capture params");
    err = snd_pcm_set_params(cap3, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate, 1, 20000);
    check(err, "set capture params");

    err = snd_pcm_set_params(out1, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate, 1, 20000);
    check(err, "set playback params");
    err = snd_pcm_set_params(out2, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate, 1, 20000);
    check(err, "set playback params");
    err = snd_pcm_set_params(out3, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate, 1, 20000);
    check(err, "set playback params");

    std::vector<int16_t> buf2(frames * channels);
    std::vector<int16_t> buf3(frames * channels);

    std::vector<int16_t> mix1(frames * channels);
    std::vector<int16_t> mix2(frames * channels);
    std::vector<int16_t> mix3(frames * channels);

    float gaini2 = 1.0f;
    float gaini3 = 1.0f;

    float gaino1 = 1.0f;
    float gaino2 = 1.0f;
    float gaino3 = 1.0f;

    std::cout << "Routing + gain...\n";

    while (running)
    {
        int n1 = snd_pcm_readi(cap2, buf2.data(), frames);
        int n2 = snd_pcm_readi(cap3, buf3.data(), frames);

        if (n1 < 0)
        {
            snd_pcm_prepare(cap1);
            continue;
        }
        if (n2 < 0)
        {
            snd_pcm_prepare(cap2);
            continue;
        }

        int n = std::min(n1, n2);

        for (int i = 0; i < n * channels; i++)
        {
            int v = (int)(buf1[i] * gain1) + (int)(buf2[i] * gain2);
            v = v * gain;

            if (v > 32767)
                v = 32767;
            if (v < -32768)
                v = -32768;

            mix[i] = (int16_t)v;
        }

        int w = snd_pcm_writei(out, mix.data(), n);
        if (w < 0)
        {
            snd_pcm_prepare(out);
            continue;
        }
    }

    snd_pcm_close(cap1);
    snd_pcm_close(cap2);
    snd_pcm_close(out);
}