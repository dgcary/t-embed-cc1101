#ifndef __DASHBOARD_MENU_H__
#define __DASHBOARD_MENU_H__

#include <MenuItemInterface.h>

class DashboardMenu : public MenuItemInterface {
public:
    DashboardMenu() : MenuItemInterface("Dashboard") {}

    void optionsMenu(void) override;
    void drawIcon(float scale) override;
    bool hasTheme() override { return false; }
    const String &themePath() override {
        static const String empty;
        return empty;
    }
};

#endif
