#include "ReportManager.h"
#include "UnitManager.h"
#include "Server.h"
#include "SSHDeployer.h"
#include <iostream>

int main(int argc, char const *argv[])
{
    if (!g_ReportManager.collectTestInfo())
    {
        std::cout << "Rapor bilgileri alinamadi!" << std::endl;
        return -1;
    }

    Unit unit;
    unit = g_UnitManager.unitSelector();

    g_ReportManager.setUnitName(g_UnitManager.enumToString(unit));

    if (!g_UnitManager.configureDeviceForUnit(unit))
    {
        std::cout << "Cihaz konfigurasyon hatasi!" << std::endl;
        return -1;
    }

    g_ReportManager.writeReportHeader();

    // ... test islemleri burada gerceklesir ...

    // Test sonrasi PDF raporu olustur
    if (!g_ReportManager.createPdfReport())
    {
        std::cout << "PDF raporu olusturulamadi!" << std::endl;
    }

    return 0;
}
