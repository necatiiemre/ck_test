#include "ReportManager.h"
#include "UnitManager.h"
#include "Server.h"
#include "SSHDeployer.h"
#include <iostream>

int main(int argc, char const *argv[])
{
    if (!g_ReportManager.collectTestInfo())
    {
        std::cout << "Failed to collect report information!" << std::endl;
        return -1;
    }

    Unit unit;
    unit = g_UnitManager.unitSelector();

    g_ReportManager.setUnitName(g_UnitManager.enumToString(unit));

    if (!g_UnitManager.configureDeviceForUnit(unit))
    {
        std::cout << "Device configuration error!" << std::endl;
        return -1;
    }

    g_ReportManager.writeReportHeader();

    // ... test operations take place here ...

    // Create PDF report after test
    if (!g_ReportManager.createPdfReport())
    {
        std::cout << "Failed to create PDF report!" << std::endl;
    }

    return 0;
}
