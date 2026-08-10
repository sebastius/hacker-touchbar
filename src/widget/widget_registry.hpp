#pragma once
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>

class Widget;

class WidgetRegistry {
public:
    using Factory = std::function<std::unique_ptr<Widget>()>;

    static WidgetRegistry& instance();

    void registerType(const std::string& type, Factory factory);
    std::unique_ptr<Widget> create(const std::string& type) const;

private:
    std::unordered_map<std::string, Factory> factories_;
};
