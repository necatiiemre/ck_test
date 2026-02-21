#ifndef REPORT_MANAGER_H
#define REPORT_MANAGER_H

#include <string>

class ReportManager
{
public:
    ReportManager();
    ~ReportManager();

    // Collects test information from user
    bool collectTestInfo();

    // Returns the test name
    std::string getTestName() const;
    std::string getTesterName() const;
    std::string getQualityCheckerName() const;
    std::string getSerialNumber() const;

    void setUnitName(std::string name);

    // Writes report header to the beginning of the log file
    bool writeReportHeader();

    // Creates PDF report from log file
    bool createPdfReport();

private:
    // Returns the Python script path
    std::string getPythonScriptPath() const;
    // Checks if input contains Turkish characters
    bool containsTurkishCharacter(const std::string &input) const;

    // Checks if input contains only digits
    bool containsOnlyDigits(const std::string &input) const;

    // Returns the log directory path for the given unit name
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
