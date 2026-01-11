/**
 * @file menu_items.hpp
 * @brief Menu item base classes for dynamic menu system
 */

#pragma once

#include <cstdint>
#include "../button.hpp"
#include "../components/Adafruit_SH1106_ESPIDF/Adafruit_SH1106.h"
#include "../components/EC11_Encoder/inc/ec11_encoder.hpp"

class MenuItemBase {
public:
    virtual ~MenuItemBase() = default;
    
    // Public functions: PascalCase
    virtual const char* GetLabel() const noexcept = 0;
    virtual void Render(int y_position, bool is_selected) noexcept = 0;
    virtual bool HandleEnter() noexcept = 0;
    virtual bool HandleRotation(EC11Encoder::Direction direction) noexcept = 0;
    
protected:
    // Protected constructor
    MenuItemBase(const char* label, MenuItemBase* parent) noexcept;
    
    // Member variables: snake_case + trailing underscore
    const char* label_;
    MenuItemBase* parent_;
};

// Derived classes
class ValueMenuItem : public MenuItemBase {
public:
    ValueMenuItem(const char* label, uint32_t* value_ptr, 
                  uint32_t min_val, uint32_t max_val, uint32_t step) noexcept;
    const char* GetLabel() const noexcept override;
    void Render(int y_position, bool is_selected) noexcept override;
    bool HandleEnter() noexcept override;
    bool HandleRotation(EC11Encoder::Direction direction) noexcept override;
    
private:
    // Private functions: camelCase
    void adjustValue(int32_t delta) noexcept;
    void saveValue() noexcept;
    
    // Member variables: snake_case + trailing underscore
    uint32_t* value_ptr_;
    uint32_t min_val_;
    uint32_t max_val_;
    uint32_t step_;
    bool editing_;
};

class ChoiceMenuItem : public MenuItemBase {
public:
    ChoiceMenuItem(const char* label, bool* value_ptr) noexcept;
    const char* GetLabel() const noexcept override;
    void Render(int y_position, bool is_selected) noexcept override;
    bool HandleEnter() noexcept override;
    bool HandleRotation(EC11Encoder::Direction direction) noexcept override;
    
private:
    // Member variables: snake_case + trailing underscore
    bool* value_ptr_;
    bool editing_;
};

class ActionMenuItem : public MenuItemBase {
public:
    using ActionCallback = bool(*)();
    
    ActionMenuItem(const char* label, ActionCallback callback) noexcept;
    const char* GetLabel() const noexcept override;
    bool HandleEnter() noexcept override;
    void Render(int y_position, bool is_selected) noexcept override;
    bool HandleRotation(EC11Encoder::Direction direction) noexcept override;
    
private:
    // Member variables: snake_case + trailing underscore
    ActionCallback callback_;
};

class FloatMenuItem : public MenuItemBase {
public:
    FloatMenuItem(const char* label, float* value_ptr, 
                  float min_val, float max_val, float step) noexcept;
    const char* GetLabel() const noexcept override;
    void Render(int y_position, bool is_selected) noexcept override;
    bool HandleEnter() noexcept override;
    bool HandleRotation(EC11Encoder::Direction direction) noexcept override;
    
private:
    // Private functions: camelCase
    void adjustValue(float delta) noexcept;
    void saveValue() noexcept;
    
    // Member variables: snake_case + trailing underscore
    float* value_ptr_;
    float min_val_;
    float max_val_;
    float step_;
    bool editing_;
};

