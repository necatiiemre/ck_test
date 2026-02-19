#include "ReportManager.h"
#include "Dtn.h"
#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <ctime>

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

bool ReportManager::containsOnlyDigits(const std::string &input) const
{
    for (const auto &c : input)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
        {
            return false;
        }
    }
    return true;
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

    while (true)
    {
        std::cout << "Enter serial number: ";
        std::getline(std::cin, m_serial_number);

        if (m_serial_number.empty())
        {
            std::cout << "Serial number can not be empty!" << std::endl;
            continue;
        }

        if (!containsOnlyDigits(m_serial_number))
        {
            std::cout << "Error! Serial number must contain only digits." << std::endl;
            std::cout << "Please enter again!" << std::endl;
            continue;
        }

        std::cout << "Enter serial number for correction: ";
        std::getline(std::cin, m_serial_number_correction);

        if (m_serial_number_correction.empty())
        {
            std::cout << "Serial number can not be empty!" << std::endl;
            continue;
        }

        std::cout << "Serial number: " << m_serial_number << std::endl;

        if (m_serial_number.compare(m_serial_number_correction) == 0)
        {
            break;
        }

        std::cout << "Invalid serial number. Please try again." << std::endl;
    }

    std::cout << "Serial number saved: " << m_serial_number << std::endl;
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

std::string ReportManager::getSerialNumber() const
{
    return m_serial_number;
}

void ReportManager::setUnitName(std::string name)
{
    m_unit_name = name;
    std::cout << "Unit name saved: " << m_unit_name << std::endl;
    std::cout << "========================================" << std::endl;
}

std::string ReportManager::getLogPathForUnit() const
{
    if (m_unit_name == "CMC") return LogPaths::CMC();
    if (m_unit_name == "VMC") return LogPaths::VMC();
    if (m_unit_name == "MMC") return LogPaths::MMC();
    if (m_unit_name == "DTN") return LogPaths::DTN();
    if (m_unit_name == "HSN") return LogPaths::HSN();
    return LogPaths::baseDir();
}

bool ReportManager::writeReportHeader()
{
    std::string logDir = getLogPathForUnit();
    std::string logFile = logDir + "/" + m_testName + ".log";

    // Mevcut icerik varsa oku
    std::string existingContent;
    {
        std::ifstream inFile(logFile);
        if (inFile.is_open())
        {
            std::stringstream ss;
            ss << inFile.rdbuf();
            existingContent = ss.str();
            inFile.close();
        }
    }

    // Dosyayi ac ve basina rapor bilgilerini yaz
    std::ofstream outFile(logFile);
    if (!outFile.is_open())
    {
        std::cerr << "Error: Could not open log file: " << logFile << std::endl;
        return false;
    }

    // Tarih ve saat bilgisini al
    std::time_t now = std::time(nullptr);
    char dateTimeBuf[64];
    std::strftime(dateTimeBuf, sizeof(dateTimeBuf), "%B %d, %Y %H:%M:%S", std::localtime(&now));

    outFile << "========================================" << std::endl;
    outFile << "         TEST REPORT" << std::endl;
    outFile << "========================================" << std::endl;
    outFile << "Date/Time       : " << dateTimeBuf << std::endl;
    outFile << "Test Name       : " << m_testName << std::endl;
    outFile << "Serial Number   : " << m_serial_number << std::endl;
    outFile << "Tester Name     : " << m_tester_name << std::endl;
    outFile << "Quality Checker : " << m_quality_checker_name << std::endl;
    outFile << "Unit Name       : " << m_unit_name << std::endl;
    outFile << "========================================" << std::endl;
    outFile << std::endl;

    // Mevcut icerigi ekle
    if (!existingContent.empty())
    {
        outFile << existingContent;
    }

    outFile.close();

    std::cout << "Report header written to: " << logFile << std::endl;

    return true;
}
