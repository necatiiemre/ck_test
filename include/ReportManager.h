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

private:
    // Turkce karakter icerip icermedigini kontrol eder
    bool containsTurkishCharacter(const std::string &input) const;

    std::string m_testName;
};

// Global singleton declaration
extern ReportManager g_ReportManager;

#endif // REPORT_MANAGER_H
