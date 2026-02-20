#ifndef REPORT_MANAGER_H
#define REPORT_MANAGER_H

#include <string>

class ReportManager
{
public:
    ReportManager();
    ~ReportManager();

    // Kullanicidan test bilgilerini toplar
    bool collectTestInfo();

    // Test adini dondurur
    std::string getTestName() const;
    std::string getTesterName() const;
    std::string getQualityCheckerName() const;
    std::string getSerialNumber() const;

    void setUnitName(std::string name);

    // Log dosyasinin basina rapor bilgilerini yazar
    bool writeReportHeader();

    // Log dosyasindan PDF raporu olusturur
    bool createPdfReport();

private:
    // Python script yolunu dondurur
    std::string getPythonScriptPath() const;
    // Turkce karakter icerip icermedigini kontrol eder
    bool containsTurkishCharacter(const std::string &input) const;

    // Sadece rakamlardan olusup olusmadigini kontrol eder
    bool containsOnlyDigits(const std::string &input) const;

    // Unit adina gore log dizin yolunu dondurur
    std::string getLogPathForUnit() const;

    std::string m_testName;
    std::string m_test_name_correction;
    std::string m_serial_number;
    std::string m_serial_number_correction;
    std::string m_tester_name;
    std::string m_quality_checker_name;
    std::string m_unit_name;
};

// Global singleton declaration
extern ReportManager g_ReportManager;

#endif // REPORT_MANAGER_H
