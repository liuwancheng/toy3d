# Toy3d 中的 yaml-cpp

- 上游项目：https://github.com/jbeder/yaml-cpp
- 固定版本：`yaml-cpp-0.9.0`
- 源码归档：https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.9.0.zip
- 许可证：MIT，见 `LICENSE`。

上游源码未作修改。Toy3d 在 `engine/thirdparty/CMakeLists.txt` 中关闭上游工具、测试和安装规则。需要读取 YAML 的目标可链接 `yaml-cpp::yaml-cpp`。
