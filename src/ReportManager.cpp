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
        std::cout << "Enter test name: ";
        std::getline(std::cin, m_testName);

        if (m_testName.empty())
        {
            std::cout << "Test name can not be empty!" << std::endl;
            continue;
        }

        if (containsTurkishCharacter(m_testName))
        {
            std::cout << "Error! Test name must not include Turkish letters.(ç, ş, ğ, ü, ö, ı)." << std::endl;
            std::cout << "Please enter again!" << std::endl;
            continue;
        }

        std::cout << "Enter test name for correction: ";
        std::getline(std::cin, m_test_name_correction);

        if (m_test_name_correction.empty())
        {
            std::cout << "Test name can not be empty!" << std::endl;
            continue;
        }

        // Turkce karakter yok, teyit icin tekrar sor
        std::cout << "Test name: " << m_testName << std::endl;

        if (m_testName.compare(m_test_name_correction) == 0)
        {
            break;
        }

        std::cout << "Invalid test name. Plase try again." << std::endl;
    }

    std::cout << "Test name saved: " << m_testName << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "Enter tester name: ";
    std::getline(std::cin, m_tester_name);
    std::cout << "Tester name saved: " << m_tester_name << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "Enter quality checker name: ";
    std::getline(std::cin, m_quality_checker_name);
    std::cout << "Quality checker name saved: " << m_quality_checker_name << std::endl;
    std::cout << "========================================" << std::endl;

    return true;
}

std::string ReportManager::getTestName() const
{
    return m_testName;
}

std::string ReportManager::getTesterName() const
{
    return m_tester_name;
}

std::string ReportManager::getQualityCheckerName() const
{
    return m_quality_checker_name;
}

void ReportManager::setUnitName(std::string name)
{
    m_unit_name = name;
    std::cout << "Unit name saved: " << m_unit_name << std::endl;
    std::cout << "========================================" << std::endl;
}
