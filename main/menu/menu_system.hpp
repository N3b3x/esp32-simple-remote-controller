/**
 * @file menu_system.hpp
 * @brief Dynamic menu system builder
 */

#pragma once

#include <memory>
#include <vector>
#include "menu_items.hpp"

class DeviceBase;

class MenuBuilder {
public:
    // Public functions: PascalCase
    std::unique_ptr<MenuItemBase> BuildDeviceMenu(DeviceBase* device) noexcept;
    void AddValueItem(MenuItemBase* parent, const char* label, 
                      uint32_t* value_ptr, uint32_t min_val, 
                      uint32_t max_val, uint32_t step) noexcept;
    void AddFloatItem(MenuItemBase* parent, const char* label, 
                      float* value_ptr, float min_val, 
                      float max_val, float step) noexcept;
    void AddChoiceItem(MenuItemBase* parent, const char* label, bool* value_ptr) noexcept;
    void AddActionItem(MenuItemBase* parent, const char* label, 
                       bool(*callback)()) noexcept;
    
private:
    // Private functions: camelCase
    void addItemToParent(MenuItemBase* parent, MenuItemBase* item) noexcept;
    
    // Member variables: snake_case + trailing underscore
    std::vector<std::unique_ptr<MenuItemBase>> menu_items_;
};

