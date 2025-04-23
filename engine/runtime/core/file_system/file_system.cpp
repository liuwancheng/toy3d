#include "file_system.h"
#include "core/misc/logger.h"

namespace toy3d {

FileSystem& FileSystem::get_instance() 
{
    static FileSystem instance;
    return instance;
}

void FileSystem::initialize() 
{
    if (b_init) return;
    b_init = true;

    // 从CMake生成的宏中读取项目根路径
    m_root_path = normalize_path(ENGINE_ASSET_ROOT);
    
    // 注册默认路径映射，Engine、Editor、Game都作为顶级目录
    register_path("engine",  m_root_path + "/asset");
    register_path("engine/asset",  m_root_path + "/asset");
    register_path("shader", m_root_path + "/shader");
    register_path("engine/shader", m_root_path + "/shader");
    
    // 如果有editor目录，也注册
    if (std::filesystem::exists(m_root_path + "/editor")) 
    {
        register_path("editor", m_root_path + "/editor/asset");
        register_path("editor/asset", m_root_path + "/editor/asset");
    }
    
    // 如果有game目录，也注册
    // if (std::filesystem::exists(m_root_path + "/game")) 
    // {
    //     register_path("game", m_root_path + "/game/asset");
    //     register_path("game/asset", m_root_path + "/game/asset");
    // }
    
    TOY_LOG_INFO("FileSystem initialized with root path: {}", m_root_path);
    // 输出所有注册路径用于调试
    TOY_LOG_INFO("Registered paths:");
    for (const auto& [vpath, ppath] : m_path_mappings) 
    {
        TOY_LOG_DEBUG("  {} -> {}", vpath, ppath);
    }
}

void FileSystem::register_path(const std::string& virtual_path, const std::string& physical_path) 
{
    m_path_mappings[normalize_path(virtual_path)] = normalize_path(physical_path);
}

std::string FileSystem::get_absolution_path(const std::string& virtual_path) const 
{
    std::string normalized = normalize_path(virtual_path);
    
    // 情况1: 直接匹配完整的虚拟路径
    auto it = m_path_mappings.find(normalized);
    if (it != m_path_mappings.end()) {
        return it->second;
    }
    
    // 情况2: 尝试找到最长的前缀匹配
    // 这会处理所有多级路径，包括"engine/a.png"和"engine/asset/a.png"等
    std::string longest_prefix;
    std::string longest_mapping;
    size_t max_length = 0;
    
    size_t path_start = 0;
    size_t slash_pos;
    
    // 逐级检查路径前缀
    while ((slash_pos = normalized.find('/', path_start)) != std::string::npos) {
        std::string prefix = normalized.substr(0, slash_pos);
        
        it = m_path_mappings.find(prefix);
        if (it != m_path_mappings.end() && prefix.length() > max_length) {
            longest_prefix = prefix;
            longest_mapping = it->second;
            max_length = prefix.length();
        }
        
        path_start = slash_pos + 1;
    }
    
    if (!longest_prefix.empty()) {
        // 找到有效的前缀映射，提取后缀部分
        std::string suffix = normalized.substr(longest_prefix.length());
        if (!suffix.empty() && suffix[0] == '/') {
            suffix = suffix.substr(1); // 移除前导斜杠
        }
        
        return longest_mapping + "/" + suffix;
    }
    
    // 情况3: 处理单一文件名 (默认指向引擎资源目录)
    if (normalized.find('/') == std::string::npos) {
        it = m_path_mappings.find("engine");
        if (it != m_path_mappings.end()) {
            return it->second + "/" + normalized;
        }
    }
    
    // 未找到映射时的警告和处理
    std::cerr << "Warning: No mapping found for virtual path: " << virtual_path << std::endl;
    
    // 作为后备，尝试返回相对于根目录的路径
    if (!normalized.empty() && normalized[0] != '/') {
        return m_root_path + "/" + normalized;
    }
    
    return normalized;
}

bool FileSystem::file_exists(const std::string& virtual_path) const 
{
    std::string physical_path = get_absolution_path(virtual_path);
    return std::filesystem::exists(physical_path);
}

std::string FileSystem::read_file_as_string(const std::string& virtual_path) const 
{
    std::string physical_path = get_absolution_path(virtual_path);
    std::ifstream file(physical_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + physical_path);
    }
    
    return std::string( (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>() );
}

std::vector<uint8_t> FileSystem::read_file(const std::string& virtual_path) const 
{
    std::string physical_path = get_absolution_path(virtual_path);
    std::ifstream file(physical_path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + physical_path);
    }
    
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(file_size);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);
    
    return buffer;
}

std::string FileSystem::normalize_path(const std::string& path) const 
{
    // 将所有反斜杠转换为正斜杠，移除重复斜杠等
    std::string result = path;
    for (auto& c : result) {
        if (c == '\\') c = '/';
    }
    
    // 移除尾部斜杠
    while (!result.empty() && result.back() == '/') 
    {
        result.pop_back();
    }
    
    return result;
}

}  // namespace toy3d