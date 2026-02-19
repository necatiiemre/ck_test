#include "ReportManager.h"
#include <iostream>
#include <algorithm>

// Global singleton
ReportManager g_ReportManager;

ReportManager::ReportManager()
{
}

ReportManager::~ReportManager()
{
}

bool ReportManager::containsTurkishCharacter(const std::string &input) const
{
    // UTF-8 encoded Turkish characters:
    // ç (c3 a7), Ç (c3 87)
    // ş (c5 9f), Ş (c5 9e)
    // ğ (c4 9f), Ğ (c4 9e)
    // ü (c3 bc), Ü (c3 9c)
    // ö (c3 b6), Ö (c3 96)
    // ı (c4 b1), İ (c4 b0)
    const std::string turkishChars[] = {
        "\xc3\xa7", "\xc3\x87",  // ç, Ç
        "\xc5\x9f", "\xc5\x9e",  // ş, Ş
        "\xc4\x9f", "\xc4\x9e",  // ğ, Ğ
        "\xc3\xbc", "\xc3\x9c",  // ü, Ü
        "\xc3\xb6", "\xc3\x96",  // ö, Ö
        "\xc4\xb1", "\xc4\xb0"   // ı, İ
    };

    for (const auto &tc : turkishChars)
    {
        if (input.find(tc) != std::string::npos)
        {
            return true;
        }
    }

    return false;
}

bool ReportManager::collectTestInfo()
{
    std::cout << "========================================" << std::endl;
    std::cout << "         REPORT MANAGER" << std::endl;
    std::cout << "========================================" << std::endl;

    while (true)
    {
        std::cout << "Test adini giriniz: ";
        std::getline(std::cin, m_testName);

        if (m_testName.empty())
        {
            std::cout << "Test adi bos olamaz! Tekrar giriniz." << std::endl;
            continue;
        }

        if (!containsTurkishCharacter(m_testName))
        {
            std::cout << "Hatali giris! Test adi Turkce karakter icermelidir (ornek: ç, ş, ğ, ü, ö, ı)." << std::endl;
            std::cout << "Lutfen dogru test adini tekrar giriniz." << std::endl;
            continue;
        }

        break;
    }

    std::cout << "Test adi: " << m_testName << std::endl;
    std::cout << "========================================" << std::endl;

    return true;
}

std::string ReportManager::getTestName() const
{
    return m_testName;
}
