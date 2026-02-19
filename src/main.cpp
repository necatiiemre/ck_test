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

    if (!g_UnitManager.configureDeviceForUnit(unit))
    {
        std::cout << "Cihaz konfigurasyon hatasi!" << std::endl;
        return -1;
    }

    return 0;
}
