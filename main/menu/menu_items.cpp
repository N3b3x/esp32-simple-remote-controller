/**
 * @file menu_items.cpp
 * @brief Menu item implementations
 */

#include "menu_items.hpp"
#include "../components/Adafruit_SH1106_ESPIDF/Adafruit_SH1106.h"
#include "../components/EC11_Encoder/inc/ec11_encoder.hpp"
#include <cstdio>
#include <cstdint>

MenuItemBase::MenuItemBase(const char* label, MenuItemBase* parent) noexcept
    : label_(label)
    , parent_(parent)
{
}

const char* MenuItemBase::GetLabel() const noexcept
{
    return label_;
}

ValueMenuItem::ValueMenuItem(const char* label, uint32_t* value_ptr, 
                             uint32_t min_val, uint32_t max_val, uint32_t step) noexcept
    : MenuItemBase(label, nullptr)
    , value_ptr_(value_ptr)
    , min_val_(min_val)
    , max_val_(max_val)
    , step_(step)
    , editing_(false)
{
}

const char* ValueMenuItem::GetLabel() const noexcept
{
    return label_;
}

void ValueMenuItem::Render(int y_position, bool is_selected) noexcept
{
    (void)y_position;
    (void)is_selected;
    (void)label_;
    (void)value_ptr_;
    (void)editing_;
    // TODO: Implement rendering
}

bool ValueMenuItem::HandleEnter() noexcept
{
    editing_ = !editing_;
    return true;
}

bool ValueMenuItem::HandleRotation(EC11Encoder::Direction direction) noexcept
{
    if (editing_) {
        adjustValue(direction == EC11Encoder::Direction::CW ? static_cast<int32_t>(step_) : -static_cast<int32_t>(step_));
        return true;
    }
    return false;
}

void ValueMenuItem::adjustValue(int32_t delta) noexcept
{
    if (!value_ptr_) return;
    
    int32_t new_value = static_cast<int32_t>(*value_ptr_) + delta;
    if (new_value < static_cast<int32_t>(min_val_)) {
        new_value = static_cast<int32_t>(min_val_);
    } else if (new_value > static_cast<int32_t>(max_val_)) {
        new_value = static_cast<int32_t>(max_val_);
    }
    *value_ptr_ = static_cast<uint32_t>(new_value);
}

void ValueMenuItem::saveValue() noexcept
{
    editing_ = false;
}

ChoiceMenuItem::ChoiceMenuItem(const char* label, bool* value_ptr) noexcept
    : MenuItemBase(label, nullptr)
    , value_ptr_(value_ptr)
    , editing_(false)
{
}

const char* ChoiceMenuItem::GetLabel() const noexcept
{
    return label_;
}

void ChoiceMenuItem::Render(int y_position, bool is_selected) noexcept
{
    (void)y_position;
    (void)is_selected;
    (void)label_;
    (void)value_ptr_;
    (void)editing_;
    // TODO: Implement rendering
}

bool ChoiceMenuItem::HandleEnter() noexcept
{
    if (editing_ && value_ptr_) {
        *value_ptr_ = !*value_ptr_;
        editing_ = false;
        return true;
    }
    editing_ = true;
    return true;
}

bool ChoiceMenuItem::HandleRotation(EC11Encoder::Direction direction) noexcept
{
    if (editing_ && value_ptr_) {
        *value_ptr_ = (direction == EC11Encoder::Direction::CW);
        return true;
    }
    return false;
}

ActionMenuItem::ActionMenuItem(const char* label, ActionCallback callback) noexcept
    : MenuItemBase(label, nullptr)
    , callback_(callback)
{
}

const char* ActionMenuItem::GetLabel() const noexcept
{
    return label_;
}

bool ActionMenuItem::HandleEnter() noexcept
{
    if (callback_) {
        return callback_();
    }
    return false;
}

void ActionMenuItem::Render(int y_position, bool is_selected) noexcept
{
    (void)y_position;
    (void)is_selected;
    (void)label_;
    // TODO: Implement rendering
}

bool ActionMenuItem::HandleRotation(EC11Encoder::Direction direction) noexcept
{
    (void)direction;
    return false;
}

