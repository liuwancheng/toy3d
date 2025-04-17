#pragma once

#include "core/misc/pch.h"

namespace toy3d {

class FileSystem {
public:
    // 单例模式
    static FileSystem& get_instance();

    // 初始化文件系统
    void initialize();
    
    // 解析虚拟路径为物理路径
    std::string get_absolution_path(const std::string& virtual_path) const;
    
    // 检查文件是否存在
    bool file_exists(const std::string& virtual_path) const;
    
    // 读取文件内容到字符串
    std::string read_file_as_string(const std::string& virtual_path) const;
    
    // 读取二进制文件内容
    std::vector<uint8_t> read_file(const std::string& virtual_path) const;
    
    std::string get_engine_asset() {return m_root_path+"/asset";}
private:
    // 添加资源路径映射
    void register_path(const std::string& virtual_path, const std::string& physical_path);

    // 辅助函数，处理路径分隔符
    std::string normalize_path(const std::string& path) const;
private:
    FileSystem(){};
    ~FileSystem() = default;
    
    FileSystem(const FileSystem&) = delete;
    FileSystem& operator=(const FileSystem&) = delete;

private:
    bool b_init = false;
    std::string m_root_path;  // 项目根目录
    std::unordered_map<std::string, std::string> m_path_mappings;
};
}  // namespace toy3d