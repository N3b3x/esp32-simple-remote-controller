/**
 * @file menu_system.cpp
 * @brief Menu system builder implementation
 */

#include "menu_system.hpp"
#include "../devices/device_base.hpp"
#include "menu_items.hpp"
#include <memory>
#include <vector>

std::unique_ptr<MenuItemBase> MenuBuilder::BuildDeviceMenu(DeviceBase* device) noexcept
{
    if (!device) {
        return nullptr;
    }
    
    device->BuildSettingsMenu(*this);
    return nullptr; // TODO: Return root menu item
}

void MenuBuilder::AddValueItem(MenuItemBase* parent, const char* label, 
                               uint32_t* value_ptr, uint32_t min_val, 
                               uint32_t max_val, uint32_t step) noexcept
{
    auto item = std::make_unique<ValueMenuItem>(label, value_ptr, min_val, max_val, step);
    addItemToParent(parent, item.get());
    menu_items_.push_back(std::move(item));
}

void MenuBuilder::AddFloatItem(MenuItemBase* parent, const char* label, 
                               float* value_ptr, float min_val, 
                               float max_val, float step) noexcept
{
    auto item = std::make_unique<FloatMenuItem>(label, value_ptr, min_val, max_val, step);
    addItemToParent(parent, item.get());
    menu_items_.push_back(std::move(item));
}

void MenuBuilder::AddChoiceItem(MenuItemBase* parent, const char* label, bool* value_ptr) noexcept
{
    auto item = std::make_unique<ChoiceMenuItem>(label, value_ptr);
    addItemToParent(parent, item.get());
    menu_items_.push_back(std::move(item));
}

void MenuBuilder::AddActionItem(MenuItemBase* parent, const char* label, 
                                 bool(*callback)()) noexcept
{
    auto item = std::make_unique<ActionMenuItem>(label, callback);
    addItemToParent(parent, item.get());
    menu_items_.push_back(std::move(item));
}

void MenuBuilder::addItemToParent(MenuItemBase* parent, MenuItemBase* item) noexcept
{
    (void)parent;
    (void)item;
    // TODO: Implement parent-child relationship
}

