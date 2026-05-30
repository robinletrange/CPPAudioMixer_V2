// CPPAudioMixer.cpp : définit le point d'entrée de l'application.
//

#include "CPPAudioMixer.h"
#include <alsa/asoundlib.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <jack/jack.h>

namespace fs = std::filesystem;

static void listAlsaCards();
static std::string cardType(const std::string &name, const std::string &longname, const std::string &driver);
static std::string readSysfsAttribute(const fs::path &path);
static bool findUsbDeviceSysfs(const fs::path &devicePath, fs::path &usbDevicePath);

int main()
{
    std::cout << "Liste des périphériques alsa disponibles" << std::endl;

    listAlsaCards();

    return 0;
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

static void listAlsaCards()
{
    std::cout << "Liste des cartes ALSA disponibles" << std::endl;
    int card = -1;
    if (snd_card_next(&card) < 0)
    {
        std::cout << "Impossible de lister les cartes ALSA." << std::endl;
        return;
    }

    if (card < 0)
    {
        std::cout << "Aucune carte ALSA trouvée." << std::endl;
        return;
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
                std::string shortName = snd_ctl_card_info_get_name(info);
                std::string longName = snd_ctl_card_info_get_longname(info);
                std::string driver = snd_ctl_card_info_get_driver(info);
                std::string type = cardType(shortName, longName, driver);

                std::cout << "- hw:" << card << " " << shortName;
                if (!longName.empty() && longName != shortName)
                {
                    std::cout << " [" << longName << "]";
                }
                std::cout << " type=" << type
                          << " driver=" << driver;

                fs::path soundDevice = fs::path("/sys/class/sound/card" + std::to_string(card)) / "device";
                if (fs::exists(soundDevice))
                {
                    fs::path realDevice = fs::canonical(soundDevice);
                    fs::path usbDevice;
                    if (findUsbDeviceSysfs(realDevice, usbDevice))
                    {
                        std::string vendor = readSysfsAttribute(usbDevice / "idVendor");
                        std::string model = readSysfsAttribute(usbDevice / "idProduct");
                        std::string serial = readSysfsAttribute(usbDevice / "serial");
                        if (!vendor.empty())
                            std::cout << " ID_VENDOR_ID=" << vendor;
                        if (!model.empty())
                            std::cout << " ID_MODEL_ID=" << model;
                        if (!serial.empty())
                            std::cout << " ID_SERIAL_SHORT=" << serial;
                    }
                }

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
}
