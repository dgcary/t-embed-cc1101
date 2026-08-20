#ifndef __NETTOOLS_MENU_H__
#define __NETTOOLS_MENU_H__

#include <MenuItemInterface.h>

class NetToolsMenu : public MenuItemInterface {
public:
    NetToolsMenu() : MenuItemInterface("NetTools") {}

    void optionsMenu(void) override;
    void drawIcon(float scale) override;
    bool hasTheme() override { return false; }
    const String &themePath() override {
        static const String empty;
        return empty;
    }
};

#endif
