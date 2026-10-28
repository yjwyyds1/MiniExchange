#pragma once

#include <string>
#include <string_view>

namespace exchange {

class MatchingEngine;

// 文本协议命令处理：N/C/S/Q，返回多行响应。
// 单线程 TCP 服务器与 epoll 网关共用，避免协议逻辑重复。
std::string apply_gateway_command(MatchingEngine& engine, std::string_view payload);

}  // namespace exchange
