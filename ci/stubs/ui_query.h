/**
 * UI Query - wrapper for testing LVGL UI via KDL
 * 
 * Convention:
 *   - Uppercase name = UI Object (Button, Label, Screen)
 *   - lowercase name = property (width, text, x)
 */
#pragma once
#include <kdlpp.h>
#include <string>
#include <vector>
#include <cctype>

class UIQuery {
    kdl::Document m_doc;
    
    static bool is_object(const std::u8string& name) {
        return !name.empty() && std::isupper(static_cast<unsigned char>(name[0]));
    }
    
    static std::string to_str(const std::u8string& s) {
        return std::string(s.begin(), s.end());
    }
    
    void collect_objects(const kdl::Node& node, std::vector<const kdl::Node*>& out) const {
        if (is_object(node.name())) {
            out.push_back(&node);
        }
        for (const auto& child : node.children()) {
            collect_objects(child, out);
        }
    }
    
    // Get string value from a Value
    static std::string get_string(const kdl::Value& val) {
        if (val.type() == kdl::Type::String) {
            return to_str(val.as<std::u8string>());
        }
        return "";
    }
    
    // Get int value from a Value
    static int get_int(const kdl::Value& val) {
        if (val.type() == kdl::Type::Number) {
            return static_cast<int>(val.as<kdl::Number>().as<long long>());
        }
        return 0;
    }
    
public:
    UIQuery(kdl::Document doc) : m_doc(std::move(doc)) {}
    
    static UIQuery parse(const char* kdl) {
        return UIQuery(kdl::parse(reinterpret_cast<const char8_t*>(kdl)));
    }
    
    static UIQuery parse(const std::string& kdl) {
        return parse(kdl.c_str());
    }
    
    std::vector<const kdl::Node*> objects() const {
        std::vector<const kdl::Node*> result;
        for (const auto& node : m_doc.nodes()) {
            collect_objects(node, result);
        }
        return result;
    }
    
    int count(const std::string& type) const {
        int c = 0;
        for (auto* obj : objects()) {
            if (to_str(obj->name()) == type) c++;
        }
        return c;
    }
    
    // Find property value in node's children (string)
    std::string get_prop(const kdl::Node& node, const std::string& prop) const {
        for (const auto& child : node.children()) {
            if (to_str(child.name()) == prop && !child.args().empty()) {
                return get_string(child.args()[0]);
            }
        }
        return "";
    }
    
    // Find property value in node's children (int)
    int get_prop_int(const kdl::Node& node, const std::string& prop) const {
        for (const auto& child : node.children()) {
            if (to_str(child.name()) == prop && !child.args().empty()) {
                return get_int(child.args()[0]);
            }
        }
        return 0;
    }
    
    // Find object by id
    const kdl::Node* find_by_id(const std::string& id) const {
        for (auto* obj : objects()) {
            if (get_prop(*obj, "id") == id) {
                return obj;
            }
        }
        return nullptr;
    }
    
    // Find object by type and text
    const kdl::Node* find(const std::string& type, const std::string& text) const {
        for (auto* obj : objects()) {
            if (to_str(obj->name()) == type && get_prop(*obj, "text") == text) {
                return obj;
            }
        }
        return nullptr;
    }
    
    bool has_button(const std::string& text) const {
        return find("Button", text) != nullptr;
    }
    
    bool has_label(const std::string& id) const {
        return find_by_id(id) != nullptr;
    }
    
    bool has_text(const std::string& text) const {
        for (auto* obj : objects()) {
            if (get_prop(*obj, "text") == text) {
                return true;
            }
        }
        return false;
    }
    
    // === Alignment checks ===
    
    // Check text horizontal alignment: "left", "center", "right"
    std::string text_align_h(const std::string& id) const {
        if (auto* obj = find_by_id(id)) {
            return get_prop(*obj, "text-align-h");
        }
        return "";
    }
    
    // Check pad-top (used for vertical text alignment)
    int pad_top(const std::string& id) const {
        if (auto* obj = find_by_id(id)) {
            return get_prop_int(*obj, "pad-top");
        }
        return 0;
    }
    
    // Check element alignment type (LV_ALIGN_*)
    int align_type(const std::string& id) const {
        if (auto* obj = find_by_id(id)) {
            return get_prop_int(*obj, "align");
        }
        return 0;
    }
    
    // === Position/size ===
    
    int x(const std::string& id) const {
        if (auto* obj = find_by_id(id)) return get_prop_int(*obj, "x");
        return 0;
    }
    
    int y(const std::string& id) const {
        if (auto* obj = find_by_id(id)) return get_prop_int(*obj, "y");
        return 0;
    }
    
    int width(const std::string& id) const {
        if (auto* obj = find_by_id(id)) return get_prop_int(*obj, "width");
        return 0;
    }
    
    int height(const std::string& id) const {
        if (auto* obj = find_by_id(id)) return get_prop_int(*obj, "height");
        return 0;
    }
    
    // === Style ===
    
    int radius(const std::string& id) const {
        if (auto* obj = find_by_id(id)) return get_prop_int(*obj, "radius");
        return 0;
    }
    
    const kdl::Document& doc() const { return m_doc; }
};
