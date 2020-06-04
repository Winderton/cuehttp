/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef CUEHTTP_ROUTER_HPP_
#define CUEHTTP_ROUTER_HPP_

#include <functional>
#include <unordered_map>

#include "cuehttp/context.hpp"
#include "cuehttp/detail/noncopyable.hpp"
#include "cuehttp/detail/common.hpp"

namespace cue {
namespace http {

class router final : safe_noncopyable {
public:
    router() noexcept = default;

    template <typename Prefix, typename = std::enable_if_t<!std::is_same<std::decay_t<Prefix>, router>::value>>
    explicit router(Prefix&& prefix) noexcept : prefix_{std::forward<Prefix>(prefix)} {
    }

    std::function<void(context&)> routes() const noexcept {
        return make_routes();
    }

    template <typename Prefix>
    router& prefix(Prefix&& prefix) {
        prefix_ = std::forward<Prefix>(prefix);
        return *this;
    }

    template <typename... Args>
    router& del(std::string_view path, Args&&... args) {
        register_impl("DEL", path, std::forward<Args>(args)...);
        return *this;
    }

    template <typename... Args>
    router& get(std::string_view path, Args&&... args) {
        register_impl("GET", path, std::forward<Args>(args)...);
        return *this;
    }

    template <typename... Args>
    router& head(std::string_view path, Args&&... args) {
        register_impl("HEAD", path, std::forward<Args>(args)...);
        return *this;
    }

    template <typename... Args>
    router& post(std::string_view path, Args&&... args) {
        register_impl("POST", path, std::forward<Args>(args)...);
        return *this;
    }

    template <typename... Args>
    router& put(std::string_view path, Args&&... args) {
        register_impl("PUT", path, std::forward<Args>(args)...);
        return *this;
    }

    template <typename... Args>
    router& all(std::string_view path, Args&&... args) {
        static const std::vector<std::string> methods{"DEL", "GET", "HEAD", "POST", "PUT"};
        for (std::string method : methods) {
            register_impl(std::move(method), path, std::forward<Args>(args)...);
        }
        return *this;
    }

    template <typename... Args>
    router& redirect(Args&&... args) {
        redirect_impl(std::forward<Args>(args)...);
        return *this;
    }

    operator auto() const noexcept {
        return make_routes();
    }

private:
    template <typename Func, typename = std::enable_if_t<!std::is_member_function_pointer<Func>::value>,
              typename... Args>
    void register_impl(std::string&& method, std::string_view path, Func&& func, Args&&... args) {
        std::vector<std::function<void(context&, std::function<void()>)>> handlers;
        register_multiple(handlers, std::forward<Func>(func), std::forward<Args>(args)...);
        compose(std::move(method), path, std::move(handlers));
    }

    template <typename T, typename Func, typename Self, typename = std::enable_if_t<std::is_same<T*, Self>::value>,
              typename... Args>
    void register_impl(std::string&& method, std::string_view path, Func (T::*func)(context&, std::function<void()>),
                       Self self, Args&&... args) {
        std::vector<std::function<void(context&, std::function<void()>)>> handlers;
        register_multiple(handlers, func, self, std::forward<Args>(args)...);
        compose(std::move(method), path, std::move(handlers));
    }

    template <typename T, typename Func, typename... Args>
    void register_impl(std::string&& method, std::string_view path, Func (T::*func)(context&, std::function<void()>),
                       Args&&... args) {
        std::vector<std::function<void(context&, std::function<void()>)>> handlers;
        register_multiple(handlers, func, std::forward<Args>(args)...);
        compose(std::move(method), path, std::move(handlers));
    }

    template <typename T, typename Func, typename Self, typename = std::enable_if_t<std::is_same<T*, Self>::value>,
              typename... Args>
    void register_impl(std::string&& method, std::string_view path, Func (T::*func)(context&), Self self,
                       Args&&... args) {
        std::vector<std::function<void(context&, std::function<void()>)>> handlers;
        register_multiple(handlers, func, self, std::forward<Args>(args)...);
        compose(std::move(method), path, std::move(handlers));
    }

    template <typename T, typename Func, typename... Args>
    void register_impl(std::string&& method, std::string_view path, Func (T::*func)(context&), Args&&... args) {
        std::vector<std::function<void(context&, std::function<void()>)>> handlers;
        register_multiple(handlers, func, std::forward<Args>(args)...);
        compose(std::move(method), path, std::move(handlers));
    }

    template <typename Func, typename... Args>
    std::enable_if_t<detail::is_middleware<Func>::value, std::true_type> register_multiple(
        std::vector<std::function<void(context&, std::function<void()>)>>& handlers, Func&& func, Args&&... args) {
        handlers.emplace_back(std::forward<Func>(func));
        register_multiple(handlers, std::forward<Args>(args)...);
        return std::true_type{};
    }

    template <typename Func, typename... Args>
    std::enable_if_t<detail::is_middleware<Func>::value, std::true_type> register_multiple(
        std::vector<std::function<void(context&, std::function<void()>)>>& handlers, Func&& func) {
        handlers.emplace_back(std::forward<Func>(func));
        return std::true_type{};
    }

    template <typename Func, typename... Args>
    std::enable_if_t<detail::is_middleware_without_next<Func>::value, std::false_type> register_multiple(
        std::vector<std::function<void(context&, std::function<void()>)>>& handlers, Func&& func, Args&&... args) {
        handlers.emplace_back([func = std::forward<Func>(func)](context& ctx, std::function<void()> next) {
            func(ctx);
            next();
        });
        register_multiple(handlers, std::forward<Args>(args)...);
        return std::false_type{};
    }

    template <typename Func, typename... Args>
    std::enable_if_t<detail::is_middleware_without_next<Func>::value, std::false_type> register_multiple(
        std::vector<std::function<void(context&, std::function<void()>)>>& handlers, Func&& func) {
        handlers.emplace_back([func = std::forward<Func>(func)](context& ctx, std::function<void()> next) {
            func(ctx);
            next();
        });
        return std::false_type{};
    }

    template <typename T, typename Func, typename Self, typename = std::enable_if_t<std::is_same<T*, Self>::value>,
              typename... Args>
    void register_multiple(std::vector<std::function<void(context&, std::function<void()>)>>& handlers,
                           Func (T::*func)(context&, std::function<void()>), Self self, Args&&... args) {
        handlers.emplace_back([func, self](context& ctx, std::function<void()> next) {
            if (self) {
                (self->*func)(ctx, std::move(next));
            }
        });
        register_multiple(handlers, std::forward<Args>(args)...);
    }

    template <typename T, typename Func, typename... Args>
    void register_multiple(std::vector<std::function<void(context&, std::function<void()>)>>& handlers,
                           Func (T::*func)(context&, std::function<void()>), Args&&... args) {
        handlers.emplace_back([func](context& ctx, std::function<void()> next) { (T{}.*func)(ctx, std::move(next)); });
        register_multiple(handlers, std::forward<Args>(args)...);
    }

    template <typename T, typename Func, typename Self, typename = std::enable_if_t<std::is_same<T*, Self>::value>,
              typename... Args>
    void register_multiple(std::vector<std::function<void(context&, std::function<void()>)>>& handlers,
                           Func (T::*func)(context&), Self self, Args&&... args) {
        handlers.emplace_back([func, self](context& ctx, std::function<void()> next) {
            if (self) {
                (self->*func)(ctx);
            }
            next();
        });
        register_multiple(handlers, std::forward<Args>(args)...);
    }

    template <typename T, typename Func, typename... Args>
    void register_multiple(std::vector<std::function<void(context&, std::function<void()>)>>& handlers,
                           Func (T::*func)(context&), Args&&... args) {
        handlers.emplace_back([func](context& ctx, std::function<void()> next) {
            (T{}.*func)(ctx);
            next();
        });
        register_multiple(handlers, std::forward<Args>(args)...);
    }

    template <typename T, typename Func, typename Self, typename = std::enable_if_t<std::is_same<T*, Self>::value>>
    void register_multiple(std::vector<std::function<void(context&, std::function<void()>)>>& handlers,
                           Func (T::*func)(context&, std::function<void()>), Self self) {
        handlers.emplace_back([func, self](context& ctx, std::function<void()> next) {
            if (self) {
                (self->*func)(ctx, std::move(next));
            }
        });
    }

    template <typename T, typename Func>
    void register_multiple(std::vector<std::function<void(context&, std::function<void()>)>>& handlers,
                           Func (T::*func)(context&, std::function<void()>)) {
        handlers.emplace_back([func](context& ctx, std::function<void()> next) { (T{}.*func)(ctx, std::move(next)); });
    }

    template <typename T, typename Func, typename Self, typename = std::enable_if_t<std::is_same<T*, Self>::value>>
    void register_multiple(std::vector<std::function<void(context&, std::function<void()>)>>& handlers,
                           Func (T::*func)(context&), Self self) {
        handlers.emplace_back([func, self](context& ctx, std::function<void()> next) {
            if (self) {
                (self->*func)(ctx);
            }
        });
    }

    template <typename T, typename Func>
    void register_multiple(std::vector<std::function<void(context&, std::function<void()>)>>& handlers,
                           Func (T::*func)(context&)) {
        handlers.emplace_back([func](context& ctx, std::function<void()> next) { (T{}.*func)(ctx); });
    }

    template <typename Func, typename = std::enable_if_t<!std::is_member_function_pointer<Func>::value>>
    std::enable_if_t<detail::is_middleware<Func>::value, std::true_type> register_impl(std::string&& method,
                                                                                       std::string_view path,
                                                                                       Func&& func) {
        register_with_next(std::move(method), path, std::forward<Func>(func));
        return std::true_type{};
    }

    template <typename T, typename Func, typename Self, typename = std::enable_if_t<std::is_same<T*, Self>::value>>
    void register_impl(std::string&& method, std::string_view path, Func (T::*func)(context&, std::function<void()>),
                       Self self) {
        register_with_next(std::move(method), path, func, self);
    }

    template <typename T, typename Func>
    void register_impl(std::string&& method, std::string_view path, Func (T::*func)(context&, std::function<void()>)) {
        register_with_next(std::move(method), path, func, (T*)nullptr);
    }

    template <typename Func, typename = std::enable_if_t<!std::is_member_function_pointer<Func>::value>>
    std::enable_if_t<detail::is_middleware_without_next<Func>::value, std::false_type> register_impl(
        std::string&& method, std::string_view path, Func&& func) {
        register_without_next(std::move(method), path, std::forward<Func>(func));
        return std::false_type{};
    }

    template <typename T, typename Func, typename Self, typename = std::enable_if_t<std::is_same<T*, Self>::value>>
    void register_impl(std::string&& method, std::string_view path, Func (T::*func)(context&), Self self) {
        register_without_next(std::move(method), path, func, self);
    }

    template <typename T, typename Func>
    void register_impl(std::string&& method, std::string_view path, Func (T::*func)(context&)) {
        register_without_next(std::move(method), path, func, (T*)nullptr);
    }

    template <typename Func>
    void register_with_next(std::string&& method, std::string_view path, Func&& func) {
        handlers_.emplace(std::move(method + "+" + prefix_ + std::string{path}),
                          [func = std::forward<Func>(func)](context& ctx) {
                              const auto next = []() {};
                              func(ctx, std::move(next));
                          });
    }

    template <typename T, typename Func, typename Self>
    void register_with_next(std::string&& method, std::string_view path, Func T::*func, Self self) {
        handlers_.emplace(std::move(method + "+" + prefix_ + std::string{path}), [func, self](context& ctx) {
            const auto next = []() {};
            if (self) {
                (self->*func)(ctx, std::move(next));
            } else {
                (T{}.*func)(ctx, std::move(next));
            }
        });
    }

    template <typename Func>
    void register_without_next(std::string&& method, std::string_view path, Func&& func) {
        handlers_.emplace(std::move(method + "+" + prefix_ + std::string{path}), std::forward<Func>(func));
    }

    template <typename T, typename Func, typename Self>
    void register_without_next(std::string&& method, std::string_view path, Func T::*func, Self self) {
        handlers_.emplace(std::move(method + "+" + prefix_ + std::string{path}), [func, self](context& ctx) {
            if (self) {
                (self->*func)(ctx);
            } else {
                (T{}.*func)(ctx);
            }
        });
    }

    template <typename Destination>
    void redirect_impl(std::string_view path, Destination&& destination) {
        redirect_impl(path, std::forward<Destination>(destination), 301);
    }

    template <typename Destination>
    void redirect_impl(std::string_view path, Destination&& destination, unsigned status) {
        all(path, [destination = std::forward<Destination>(destination), status](context& ctx) {
            ctx.redirect(std::move(destination));
            ctx.status(status);
        });
    }

    void compose(std::string&& method, std::string_view path,
                 std::vector<std::function<void(context&, std::function<void()>)>>&& handlers) {
        const auto handler = [handlers = std::move(handlers)](context& ctx) {
            if (handlers.empty()) {
                return;
            }

            if (handlers.size() == 1) {
                handlers[0](ctx, []() {});
            } else {
                std::size_t index{0};
                std::function<void()> next;
                next = [&handlers, &next, &index, &ctx]() {
                    if (++index == handlers.size()) {
                        return;
                    }
                    handlers[index](ctx, next);
                };

                handlers[0](ctx, next);
            }
        };
        handlers_.emplace(method + "+" + prefix_ + std::string{path}, std::move(handler));
    }

    std::function<void(context&)> make_routes() const noexcept {
        return [this](context& ctx) {
            if (ctx.status() != 404) {
                return;
            }
            std::string key{ctx.method()};
            key.append("+");
            key.append(prefix_);
            key.append(std::string{ctx.path()});
            const auto it = handlers_.find(key);
            if (it != handlers_.end()) {
                it->second(ctx);
            }
        };
    }

    std::string prefix_;
    std::unordered_map<std::string, std::function<void(context&)>> handlers_;
};

} // namespace http
} // namespace cue

#endif // CUEHTTP_ROUTER_HPP_
