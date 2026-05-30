// CPPAudioMixer.cpp : définit le point d'entrée de l'application.

#include "CPPAudioMixer.h"
#include <alsa/asoundlib.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#include <csignal>

namespace fs = std::filesystem;

struct UsbFilter
{
    std::string vendor;
    std::string product;

    bool empty() const
    {
        return vendor.empty() && product.empty();
    }
};

static const UsbFilter inputUsbFilter = {"194f", "0101"};
static const UsbFilter outputUsbFilter = {"194f", "0302"};

struct CardInfo
{
    int card;
    std::string shortName;
    std::string longName;
    std::string driver;
    std::string vendor;
    std::string product;
    std::string serial;
};

static void printUsage(const char *programName);
static bool parseArguments(int argc, char *argv[], bool &listOnly);
static std::vector<CardInfo> listAlsaCards();
static std::string cardType(const std::string &name, const std::string &longname, const std::string &driver);
static std::string readSysfsAttribute(const fs::path &path);
static bool findUsbDeviceSysfs(const fs::path &devicePath, fs::path &usbDevicePath);
static std::optional<int> findCardByUsbFilter(const UsbFilter &filter, const std::vector<CardInfo> &cards);
static bool recoverPcm(snd_pcm_t *handle, int err);
static snd_pcm_t *openPcmDevice(int card, snd_pcm_stream_t stream, unsigned int sampleRate, unsigned int channels, snd_pcm_uframes_t periodSize);
static void runAudioLoop(snd_pcm_t *capture, snd_pcm_t *playback, unsigned int channels, snd_pcm_uframes_t frames);
static volatile std::sig_atomic_t keepRunning = 1;

static void signalHandler(int)
{
    keepRunning = 0;
}

static bool recoverPcm(snd_pcm_t *handle, int err)
{
    if (err >= 0)
        return true;

    err = snd_pcm_recover(handle, err, 1);
    if (err < 0)
    {
        std::cerr << "Erreur ALSA recover : " << snd_strerror(err) << std::endl;
        return false;
    }
    return true;
}

int main(int argc, char *argv[])
{
    bool listOnly = false;

    if (!parseArguments(argc, argv, listOnly))
    {
        printUsage(argv[0]);
        return 1;
    }

    auto cards = listAlsaCards();
    if (listOnly)
    {
        return 0;
    }

    if (inputUsbFilter.empty() || outputUsbFilter.empty())
    {
        std::cerr << "Veuillez définir les ID USB d'entrée et de sortie dans les variables globales inputUsbFilter et outputUsbFilter." << std::endl;
        return 1;
    }

    auto inputCard = findCardByUsbFilter(inputUsbFilter, cards);
    auto outputCard = findCardByUsbFilter(outputUsbFilter, cards);

    if (!inputCard)
    {
        std::cerr << "Périphérique d'entrée USB introuvable pour :"
                  << " ID_VENDOR_ID=" << inputUsbFilter.vendor
                  << " ID_MODEL_ID=" << inputUsbFilter.product << std::endl;
        return 1;
    }

    if (!outputCard)
    {
        std::cerr << "Périphérique de sortie USB introuvable pour :"
                  << " ID_VENDOR_ID=" << outputUsbFilter.vendor
                  << " ID_MODEL_ID=" << outputUsbFilter.product << std::endl;
        return 1;
    }

    std::cout << "Ouverture du flux audio :" << std::endl;
    std::cout << "  entrée -> hw:" << *inputCard << std::endl;
    std::cout << "  sortie -> hw:" << *outputCard << std::endl;

    unsigned int sampleRate = 44100;
    unsigned int channels = 2;
    snd_pcm_uframes_t periodSize = 1024;

    snd_pcm_t *capture = openPcmDevice(*inputCard, SND_PCM_STREAM_CAPTURE, sampleRate, channels, periodSize);
    if (!capture)
    {
        return 1;
    }

    snd_pcm_t *playback = openPcmDevice(*outputCard, SND_PCM_STREAM_PLAYBACK, sampleRate, channels, periodSize);
    if (!playback)
    {
        snd_pcm_close(capture);
        return 1;
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    runAudioLoop(capture, playback, channels, periodSize);

    snd_pcm_close(capture);
    snd_pcm_close(playback);

    return 0;
}

static void printUsage(const char *programName)
{
    std::cout << "Usage: " << programName << " [--list]" << std::endl;
    std::cout << "  --list : liste les cartes ALSA disponibles" << std::endl;
    std::cout << "Les ID USB d'entrée et de sortie sont définis directement dans les variables globales du code." << std::endl;
}

static bool parseArguments(int argc, char *argv[], bool &listOnly)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--list")
        {
            listOnly = true;
            continue;
        }
        return false;
    }
    return true;
}

static std::vector<CardInfo> listAlsaCards()
{
    std::vector<CardInfo> cards;
    std::cout << "Liste des cartes ALSA disponibles" << std::endl;
    int card = -1;
    if (snd_card_next(&card) < 0)
    {
        std::cout << "Impossible de lister les cartes ALSA." << std::endl;
        return cards;
    }

    if (card < 0)
    {
        std::cout << "Aucune carte ALSA trouvée." << std::endl;
        return cards;
    }

    while (card >= 0)
    {
        snd_ctl_t *ctl = nullptr;
        char deviceName[32];
        snprintf(deviceName, sizeof(deviceName), "hw:%d", card);
        if (snd_ctl_open(&ctl, deviceName, 0) >= 0)
        {
            snd_ctl_card_info_t *info;
            snd_ctl_card_info_alloca(&info);
            if (snd_ctl_card_info(ctl, info) >= 0)
            {
                CardInfo cardInfo;
                cardInfo.card = card;
                cardInfo.shortName = snd_ctl_card_info_get_name(info);
                cardInfo.longName = snd_ctl_card_info_get_longname(info);
                cardInfo.driver = snd_ctl_card_info_get_driver(info);
                std::string type = cardType(cardInfo.shortName, cardInfo.longName, cardInfo.driver);

                std::cout << "- hw:" << card << " " << cardInfo.shortName;
                if (!cardInfo.longName.empty() && cardInfo.longName != cardInfo.shortName)
                {
                    std::cout << " [" << cardInfo.longName << "]";
                }
                std::cout << " type=" << type
                          << " driver=" << cardInfo.driver;

                fs::path soundDevice = fs::path("/sys/class/sound/card" + std::to_string(card)) / "device";
                if (fs::exists(soundDevice))
                {
                    fs::path realDevice = fs::canonical(soundDevice);
                    fs::path usbDevice;
                    if (findUsbDeviceSysfs(realDevice, usbDevice))
                    {
                        cardInfo.vendor = readSysfsAttribute(usbDevice / "idVendor");
                        cardInfo.product = readSysfsAttribute(usbDevice / "idProduct");
                        cardInfo.serial = readSysfsAttribute(usbDevice / "serial");
                        if (!cardInfo.vendor.empty())
                            std::cout << " ID_VENDOR_ID=" << cardInfo.vendor;
                        if (!cardInfo.product.empty())
                            std::cout << " ID_MODEL_ID=" << cardInfo.product;
                        if (!cardInfo.serial.empty())
                            std::cout << " ID_SERIAL_SHORT=" << cardInfo.serial;
                    }
                }

                cards.push_back(std::move(cardInfo));
                std::cout << std::endl;
            }
            snd_ctl_close(ctl);
        }

        if (snd_card_next(&card) < 0)
        {
            break;
        }
    }
    std::cout << std::endl;
    return cards;
}

static std::string cardType(const std::string &name, const std::string &longname, const std::string &driver)
{
    std::string lower = name + " " + longname + " " + driver;
    for (auto &c : lower)
        c = static_cast<char>(std::tolower(c));
    if (lower.find("usb") != std::string::npos)
    {
        return "USB";
    }
    return "system";
}

static std::string readSysfsAttribute(const fs::path &path)
{
    std::ifstream file(path);
    if (!file)
        return {};

    std::string value;
    std::getline(file, value);
    if (!value.empty() && value.back() == '\r')
        value.pop_back();
    return value;
}

static bool findUsbDeviceSysfs(const fs::path &devicePath, fs::path &usbDevicePath)
{
    fs::path current = devicePath;
    while (!current.empty())
    {
        if (fs::exists(current / "idVendor") && fs::exists(current / "idProduct"))
        {
            usbDevicePath = current;
            return true;
        }
        fs::path parent = current.parent_path();
        if (parent == current)
            break;
        current = parent;
    }
    return false;
}

static bool filterMatches(const UsbFilter &filter, const CardInfo &card)
{
    if (!filter.vendor.empty() && card.vendor != filter.vendor)
        return false;
    if (!filter.product.empty() && card.product != filter.product)
        return false;
    return true;
}

static std::optional<int> findCardByUsbFilter(const UsbFilter &filter, const std::vector<CardInfo> &cards)
{
    for (const auto &card : cards)
    {
        if (filterMatches(filter, card))
        {
            return card.card;
        }
    }
    return std::nullopt;
}

static snd_pcm_t *openPcmDevice(int card, snd_pcm_stream_t stream, unsigned int sampleRate, unsigned int channels, snd_pcm_uframes_t periodSize)
{
    std::string deviceName = "plughw:" + std::to_string(card) + ",0";
    snd_pcm_t *handle = nullptr;
    int err = snd_pcm_open(&handle, deviceName.c_str(), stream, 0);
    if (err < 0)
    {
        std::cerr << "Impossible d'ouvrir " << deviceName << " : " << snd_strerror(err) << std::endl;
        return nullptr;
    }

    snd_pcm_hw_params_t *hwParams = nullptr;
    snd_pcm_hw_params_malloc(&hwParams);
    snd_pcm_hw_params_any(handle, hwParams);
    snd_pcm_hw_params_set_access(handle, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, hwParams, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(handle, hwParams, channels);
    err = snd_pcm_hw_params_set_rate_near(handle, hwParams, &sampleRate, 0);
    if (err < 0)
    {
        std::cerr << "Erreur configuration taux echantillonnage : " << snd_strerror(err) << std::endl;
        snd_pcm_hw_params_free(hwParams);
        snd_pcm_close(handle);
        return nullptr;
    }

    err = snd_pcm_hw_params_set_period_size_near(handle, hwParams, &periodSize, 0);
    if (err < 0)
    {
        std::cerr << "Erreur configuration period size : " << snd_strerror(err) << std::endl;
        snd_pcm_hw_params_free(hwParams);
        snd_pcm_close(handle);
        return nullptr;
    }

    snd_pcm_uframes_t bufferSize = periodSize * 4;
    err = snd_pcm_hw_params_set_buffer_size_near(handle, hwParams, &bufferSize);
    if (err < 0)
    {
        std::cerr << "Erreur configuration buffer size : " << snd_strerror(err) << std::endl;
        snd_pcm_hw_params_free(hwParams);
        snd_pcm_close(handle);
        return nullptr;
    }

    err = snd_pcm_hw_params(handle, hwParams);
    if (err < 0)
    {
        std::cerr << "Impossible de configurer le périphérique ALSA : " << snd_strerror(err) << std::endl;
        snd_pcm_hw_params_free(hwParams);
        snd_pcm_close(handle);
        return nullptr;
    }

    unsigned int actualRate = 0;
    unsigned int actualChannels = 0;
    snd_pcm_uframes_t actualPeriod = 0;
    snd_pcm_hw_params_get_rate(hwParams, &actualRate, 0);
    snd_pcm_hw_params_get_channels(hwParams, &actualChannels);
    snd_pcm_hw_params_get_period_size(hwParams, &actualPeriod, 0);
    std::cout << "  " << deviceName << " config: rate=" << actualRate
              << " channels=" << actualChannels
              << " period=" << actualPeriod
              << " buffer=" << bufferSize << std::endl;

    snd_pcm_hw_params_free(hwParams);

    err = snd_pcm_prepare(handle);
    if (err < 0)
    {
        std::cerr << "Impossible de préparer le périphérique ALSA : " << snd_strerror(err) << std::endl;
        snd_pcm_close(handle);
        return nullptr;
    }

    return handle;
}

static void runAudioLoop(snd_pcm_t *capture, snd_pcm_t *playback, unsigned int channels, snd_pcm_uframes_t frames)
{
    std::vector<int16_t> buffer(frames * channels);
    unsigned long iteration = 0;
    unsigned long totalRead = 0;
    unsigned long totalWritten = 0;
    int maxSample = 0;

    while (keepRunning)
    {
        int err = snd_pcm_readi(capture, buffer.data(), frames);
        if (err == -EPIPE || err == -ESTRPIPE)
        {
            if (!recoverPcm(capture, err))
                break;
            continue;
        }
        if (err < 0)
        {
            std::cerr << "Erreur capture ALSA : " << snd_strerror(err) << std::endl;
            if (!recoverPcm(capture, err))
                break;
            continue;
        }
        if (err == 0)
        {
            continue;
        }

        totalRead += err;
        int absMax = 0;
        for (int i = 0; i < err * static_cast<int>(channels); ++i)
        {
            absMax = std::max(absMax, std::abs(buffer[i]));
        }
        maxSample = std::max(maxSample, absMax);

        snd_pcm_uframes_t framesToWrite = static_cast<snd_pcm_uframes_t>(err);
        int16_t *writePtr = buffer.data();
        while (framesToWrite > 0)
        {
            int written = snd_pcm_writei(playback, writePtr, framesToWrite);
            if (written == -EPIPE || written == -ESTRPIPE)
            {
                if (!recoverPcm(playback, written))
                    return;
                continue;
            }
            if (written < 0)
            {
                std::cerr << "Erreur lecture ALSA : " << snd_strerror(written) << std::endl;
                if (!recoverPcm(playback, written))
                    return;
                continue;
            }
            writePtr += static_cast<size_t>(written) * channels;
            framesToWrite -= static_cast<snd_pcm_uframes_t>(written);
            totalWritten += written;
        }

        ++iteration;
        if ((iteration % 100) == 0)
        {
            std::cout << "Flux actif : read=" << totalRead << " frames, written=" << totalWritten
                      << " frames, maxSample=" << maxSample << std::endl;
            maxSample = 0;
        }
    }

    std::cout << "Arrêt du flux audio." << std::endl;
}
